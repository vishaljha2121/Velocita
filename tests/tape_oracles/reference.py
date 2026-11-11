"""Reference tape: an independent re-implementation of ADR 0030's attestation and book rules, used as the test oracle.

A snapshot is a reply to one subscription, so every snapshot message has its own verdict (DECISIONS D-25, D-26);
updates are broadcast and keyed across recorders.

Written from ADR 0030, GUARANTEES.md and DECISIONS.md, not from the C++ reducer. It returns, per log index, the
apply outcome, the affected stream, the symbol's DEPTH book state and hash, and every verdict close.
"""
import hashlib
import json
import struct
from collections import OrderedDict

CHANNELS = ["INSTRUMENT", "EBBO", "DEPTH", "TRADE"]
CHANNEL_CODE = {1: "INSTRUMENT", 2: "EBBO", 3: "DEPTH", 4: "TRADE", 5: "HEARTBEAT", 6: "CONTROL"}
KIND_CODE = {1: "SNAPSHOT", 2: "UPDATE", 3: "SNAPSHOT_COMPLETE", 4: "HEARTBEAT", 5: "CONTROL", 6: "UNPARSED"}
APPLIED, DUPLICATE, OUT_OF_ORDER, UNKNOWN_RECORDER, MALFORMED, MISMATCH, PART_STORED, PART_REFUSED, \
    TICK_REFUSED, HEAD_REFUSED, SYMBOL_CAPACITY = range(11)


def reply_owner(recorder, session, request):
    tag = (session ^ ((request * 0x9E3779B97F4A7C15) % 2**64) ^ (recorder << 56)) % 2**64
    return tag or 1


def atoms(text):
    if not text or text.count(".") > 1:
        raise ValueError("malformed")
    whole, _, frac = text.partition(".")
    if not whole.isdigit() or (_ and not frac.isdigit()):
        raise ValueError("malformed")
    if len(frac) > 9:
        raise ValueError("precision")
    value = int(whole) * 10**9 + int((frac + "0" * 9)[:9])
    if value > 2**63 - 1:
        raise ValueError("overflow")
    return value


class Book:
    def __init__(self):
        self.state, self.reason = "unknown", "not_started"
        self.position = None
        self.bids, self.asks = {}, {}
        self.staging = None
        self.held = {}
        self.hold_tick = 0

    def go_unknown(self, reason):
        self.state, self.reason = "unknown", reason
        self.staging, self.held, self.hold_tick = None, {}, 0


class ReferenceTape:
    def __init__(self, verdict_ticks=5, levels=64, held=8, changes=32, open_verdicts=256, recent=1024, symbols=16):
        self.N, self.L, self.H, self.C, self.V, self.R, self.S = verdict_ticks, levels, held, changes, open_verdicts, recent, symbols
        self.sessions = {}
        self.symbols = []
        self.books = {}
        self.open = OrderedDict()
        self.closed = []
        self.tick = 0
        self.assembly = {}

    def book_hash(self, symbol):
        b = self.books[symbol]
        if b.state != "valid":
            return ""
        out = b"velocita-tape/v1/book" + bytes([len(symbol)]) + symbol.encode() + struct.pack("<Q", b.position)
        for side, rev in ((b.bids, True), (b.asks, False)):
            out += struct.pack("<H", len(side))
            for price in sorted(side, reverse=rev):
                qty, count = side[price]
                out += struct.pack("<qqI", price, qty, count)
        return hashlib.sha256(out).hexdigest()

    def close(self, key, reason, index, closes):
        v = self.open.pop(key)
        hashes = list(v["copies"].values())
        distinct = len(set(hashes))
        outcome = "divergent" if distinct > 1 else {3: "3/3", 2: "2/3", 1: "1/3"}[len(hashes)]
        mask = sum(1 << (r - 1) for r in v["copies"])
        closes.append({"stream": key[0], "kind": key[1], "seqnum": key[2], "owner": key[3], "outcome": outcome, "reason": reason,
                       "mask": mask, "distinct": distinct, "first_index": v["first_index"], "close_index": index})
        self.closed.append((key, v["applied"]))
        if len(self.closed) > self.R:
            self.closed.pop(0)

    def levels_of(self, payload):
        m = json.loads(payload)
        out = []
        for side in ("bids", "asks"):
            for lv in m.get("data", {}).get(side, []):
                out.append((side == "bids", atoms(lv["price"]), atoms(lv["qty"]), int(lv.get("order_count", "0") or 0)))
        return out

    def set_level(self, book_side, price, qty, count):
        if qty == 0:
            book_side.pop(price, None)
            return True
        if price not in book_side and len(book_side) >= self.L:
            return False
        book_side[price] = (qty, count)
        return True

    def apply_levels(self, b, levels, into_bids, into_asks):
        for bid, price, qty, count in levels:
            if not self.set_level(into_bids if bid else into_asks, price, qty, count):
                return False
        return True

    def drain(self, b):
        progressed = True
        while progressed and b.held:
            progressed = False
            for seq in sorted(b.held):
                if seq < b.position:
                    del b.held[seq]
                    progressed = True
                    break
                if seq == b.position:
                    levels = b.held.pop(seq)
                    if not self.apply_levels(b, levels, b.bids, b.asks):
                        b.go_unknown("capacity")
                        return
                    b.position += 1
                    progressed = True
                    break
        if not b.held:
            b.hold_tick = 0

    def hold(self, b, seq, payload):
        if seq in b.held:
            return
        if len(b.held) == self.H:
            b.go_unknown("capacity")
            return
        try:
            levels = self.levels_of(payload)
        except ValueError as e:
            b.go_unknown(str(e))
            return
        if len(levels) > self.C:
            b.go_unknown("capacity")
            return
        if not b.held:
            b.hold_tick = self.tick
        b.held[seq] = levels

    def depth(self, symbol, kind, seq, payload):
        b = self.books[symbol]
        if kind == "SNAPSHOT":
            if b.position is not None and seq < b.position:
                return
            if b.staging is None or b.staging["seq"] != seq:
                b.staging = {"seq": seq, "tick": self.tick, "bids": {}, "asks": {}}
                b.state, b.reason = "building", "none"
            try:
                ok = self.apply_levels(b, self.levels_of(payload), b.staging["bids"], b.staging["asks"])
            except ValueError as e:
                b.go_unknown(str(e))
                return
            if not ok:
                b.go_unknown("capacity")
        elif kind == "SNAPSHOT_COMPLETE":
            if b.staging is None or b.staging["seq"] != seq:
                return
            b.bids, b.asks = b.staging["bids"], b.staging["asks"]
            b.staging, b.position, b.state, b.reason = None, seq, "valid", "none"
            self.drain(b)
        elif kind == "UPDATE":
            if b.staging is not None:
                if seq >= b.staging["seq"]:
                    self.hold(b, seq, payload)
                return
            if b.state != "valid" or seq < b.position:
                return
            if seq > b.position:
                self.hold(b, seq, payload)
                return
            try:
                ok = self.apply_levels(b, self.levels_of(payload), b.bids, b.asks)
            except ValueError as e:
                b.go_unknown(str(e))
                return
            if not ok:
                b.go_unknown("capacity")
                return
            b.position += 1
            self.drain(b)

    def session(self, recorder, session, request, start=False):
        if recorder not in (1, 2, 3):
            return UNKNOWN_RECORDER
        cur = self.sessions.get(recorder)
        if start:
            if request != 1:
                return OUT_OF_ORDER
            if cur and cur[0] == session:
                return DUPLICATE
            self.sessions[recorder] = (session, 1)
            self.assembly.pop(recorder, None)
            return APPLIED
        if not cur or cur[0] != session:
            return OUT_OF_ORDER
        if request <= cur[1]:
            return DUPLICATE
        if request != cur[1] + 1:
            return OUT_OF_ORDER
        self.sessions[recorder] = (session, request)
        return APPLIED

    def observation(self, index, o, closes):
        if hashlib.sha256(o["payload"]).digest() != o["hash"]:
            return MALFORMED, None
        if o["kind"] in ("HEARTBEAT", "CONTROL", "UNPARSED") or not o["has_seqnum"]:
            return APPLIED, None
        m = json.loads(o["payload"])
        sym = m.get("item") if o["kind"] == "SNAPSHOT_COMPLETE" else (m.get("data", {}).get("symbol") or m.get("data", {}).get("info", {}).get("symbol"))
        if m.get("channel") != o["channel"] or m.get("update") != o["kind"] or int(m["seqnum"]) != o["seqnum"] or sym != o["symbol"]:
            return MISMATCH, None
        if o["symbol"] not in self.symbols:
            if len(self.symbols) == self.S:
                return SYMBOL_CAPACITY, None
            self.symbols.append(o["symbol"])
            self.books[o["symbol"]] = Book()
        stream = CHANNELS.index(o["channel"]) * 16 + self.symbols.index(o["symbol"])
        owner = 0 if o["kind"] == "UPDATE" else reply_owner(o["recorder"], o["session"], o["request"])
        key = (stream, o["kind"], o["seqnum"], owner)
        h = o["hash"]
        if key in self.open:
            v = self.open[key]
            if o["recorder"] in v["copies"]:
                return APPLIED, stream
            v["copies"][o["recorder"]] = h
            if h != v["applied"] and o["channel"] == "DEPTH":
                self.books[o["symbol"]].go_unknown("divergence")
            if len(v["copies"]) == 3:
                self.close(key, "all_reported", index, closes)
            return APPLIED, stream
        for ckey, applied in reversed(self.closed):
            if ckey == key:
                if applied != h and o["channel"] == "DEPTH":
                    self.books[o["symbol"]].go_unknown("divergence")
                return APPLIED, stream
        if len(self.open) == self.V:
            oldest = min(self.open, key=lambda k: self.open[k]["first_index"])
            self.close(oldest, "capacity", index, closes)
        self.open[key] = {"first_index": index, "first_tick": self.tick, "copies": {o["recorder"]: h}, "applied": h}
        if o["channel"] == "DEPTH":
            self.depth(o["symbol"], o["kind"], o["seqnum"], o["payload"])
        return APPLIED, stream

    def apply(self, index, raw):
        closes = []
        code, stream = APPLIED, None
        try:
            entry = decode(raw)
        except Exception:
            entry = None
        if entry is None:
            code = MALFORMED
        elif entry["type"] == 1:
            code = MALFORMED if len(entry["payload"]) > 4096 else self.session(entry["recorder"], entry["session"], entry["request"])
            if code == APPLIED:
                code, stream = self.observation(index, entry, closes)
        elif entry["type"] == 2:
            code = self.session(entry["recorder"], entry["session"], entry["request"])
            if code == APPLIED:
                code, stream = self.part(index, entry, closes)
        elif entry["type"] == 3:
            code = self.session(entry["recorder"], entry["session"], entry["request"], entry["kind"] == 1)
        elif entry["type"] == 4:
            if entry["number"] == self.tick + 1:
                self.tick = entry["number"]
                for key in [k for k, v in self.open.items() if self.tick >= v["first_tick"] + self.N]:
                    self.close(key, "n_ticks", index, closes)
                for b in self.books.values():
                    if b.staging is not None and self.tick >= b.staging["tick"] + self.N:
                        b.go_unknown("snapshot_incomplete")
                    elif b.held and self.tick >= b.hold_tick + self.N:
                        b.go_unknown("gap")
            else:
                code = TICK_REFUSED
        else:
            code = HEAD_REFUSED
        state, digest = "unknown", ""
        if stream is not None:
            sym = self.symbols[stream % 16]
            state, digest = self.books[sym].state, self.book_hash(sym)
        return {"code": code, "stream": 255 if stream is None else stream, "state": state, "hash": digest, "closes": closes}

    def part(self, index, p, closes):
        r = p["recorder"]
        if p["part_index"] == 0:
            self.assembly[r] = {"session": p["session"], "first": p["request"], "count": p["part_count"],
                                "size": p["whole_size"], "hash": p["whole_hash"], "data": b"", "received": 0}
        a = self.assembly.get(r)
        if (not a or a["session"] != p["session"] or a["first"] != p["first_request"] or a["count"] != p["part_count"]
                or a["received"] != p["part_index"] or a["size"] != p["whole_size"] or a["hash"] != p["whole_hash"]
                or len(a["data"]) + len(p["chunk"]) > a["size"]):
            self.assembly.pop(r, None)
            return PART_REFUSED, None
        a["data"] += p["chunk"]
        a["received"] += 1
        if a["received"] < a["count"]:
            return PART_STORED, None
        whole = self.assembly.pop(r)
        if len(whole["data"]) != whole["size"] or hashlib.sha256(whole["data"]).digest() != whole["hash"]:
            return PART_REFUSED, None
        o = decode(whole["data"])
        if o is None or o["type"] != 1 or o["recorder"] != r or o["session"] != p["session"] or o["request"] != whole["first"]:
            return PART_REFUSED, None
        return self.observation(index, o, closes)


def decode(raw):
    if len(raw) < 2 or raw[0] != 1:
        return None
    t = raw[1]
    at = 2

    def take(fmt):
        nonlocal at
        size = struct.calcsize(fmt)
        if at + size > len(raw):
            raise ValueError("short")
        v = struct.unpack_from(fmt, raw, at)
        at += size
        return v

    def sym():
        nonlocal at
        (n,) = take("<B")
        s = raw[at:at + n].decode()
        at += n
        return s

    out = {"type": t}
    if t in (1, 2, 3):
        out["recorder"], out["session"], out["request"] = take("<BQQ")
    if t == 1:
        ch, kind = take("<BB")
        out["channel"], out["kind"] = CHANNEL_CODE.get(ch, "NONE"), KIND_CODE.get(kind, "NONE")
        out["symbol"] = sym()
        has, out["seqnum"] = take("<BQ")
        out["has_seqnum"] = has == 1
        out["hash"] = raw[at:at + 32]
        at += 32
        out["utc"], out["mono"], out["clock"], n = take("<qQII")
        out["payload"] = raw[at:at + n]
        at += n
    elif t == 2:
        out["first_request"], out["part_index"], out["part_count"], out["whole_size"] = take("<QHHI")
        out["whole_hash"] = raw[at:at + 32]
        at += 32
        (n,) = take("<I")
        out["chunk"] = raw[at:at + n]
        at += n
    elif t == 3:
        out["kind"], ch = take("<BB")
        out["symbol"] = sym()
        out["expected"], out["received"], out["utc"] = take("<QQq")
    elif t == 4:
        out["number"], out["proposer"], out["utc"] = take("<QBq")
    elif t == 5:
        out["tick"], out["size"] = take("<QQ")
        at += 32 + 1 + 64
    else:
        return None
    if at != len(raw):
        return None
    return out
