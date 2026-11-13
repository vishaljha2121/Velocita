#!/usr/bin/env python3
"""Reference order-book builder for the True Markets public DEPTH channel, and a client for conform.py.

Rules, as observed on the live feed and recorded by the Verifiable Tape:
- A snapshot is one or more SNAPSHOT messages followed by SNAPSHOT_COMPLETE, all with the same seqnum. That seqnum
  is the seqnum of the next update, so after a snapshot the book expects update number seqnum.
- An UPDATE carries only changed levels, each with its new total; qty 0 removes the level.
- Updates for a book step by exactly one. A lower seqnum is a duplicate; a higher one is a gap, and the book is
  stale until a newer snapshot rebuilds it.
- Prices and sizes are decimal strings with up to 9 places, kept as integers in units of 10^-9.

Run as a client: reads {"frame": "..."} lines on stdin and answers each with one JSON line on stdout.
"""
import json
import sys

SCALE = 9
MAX_HELD = 4096


def atoms(text):
    if not isinstance(text, str) or not text:
        raise ValueError("not a decimal string")
    neg = text.startswith("-")
    digits = text[1:] if neg else text
    whole, _, frac = digits.partition(".")
    if not whole.isdigit() or (frac and not frac.isdigit()) or len(frac) > SCALE:
        raise ValueError("bad decimal " + text)
    value = int(whole) * 10**SCALE + int(frac.ljust(SCALE, "0") or "0")
    return -value if neg else value


def decimal(value):
    sign = "-" if value < 0 else ""
    whole, frac = divmod(abs(value), 10**SCALE)
    frac = str(frac).rjust(SCALE, "0").rstrip("0")
    return sign + str(whole) + ("." + frac if frac else "")


class Book:
    def __init__(self, symbol):
        self.symbol = symbol
        self.state = "empty"
        self.next_seqnum = None
        self.bids = {}
        self.asks = {}
        self.staging = None
        self.held = []

    def levels(self, side):
        prices = sorted(side, reverse=side is self.bids)
        return [[decimal(p), decimal(side[p][0]), str(side[p][1])] for p in prices]

    def answer(self):
        out = {"symbol": self.symbol, "state": self.state}
        if self.state == "valid":
            out.update(next_seqnum=str(self.next_seqnum), bids=self.levels(self.bids), asks=self.levels(self.asks))
        elif self.state == "stale":
            out["resubscribe"] = True
        return out

    @staticmethod
    def put(side, level):
        price, qty, count = atoms(level["price"]), atoms(level["qty"]), int(level["order_count"])
        if qty == 0:
            side.pop(price, None)
        else:
            side[price] = (qty, count)

    def snapshot(self, seqnum, data):
        if self.state == "valid" and seqnum < self.next_seqnum:
            return
        if self.staging is None or self.staging[0] != seqnum:
            self.staging = (seqnum, {}, {})
            self.state = "building"
        for level in data.get("bids", []):
            self.put(self.staging[1], level)
        for level in data.get("asks", []):
            self.put(self.staging[2], level)

    def complete(self, seqnum):
        if self.staging is None or self.staging[0] != seqnum:
            return
        _, self.bids, self.asks = self.staging
        self.staging = None
        self.next_seqnum = seqnum
        self.state = "valid"
        held, self.held = sorted(self.held, key=lambda h: h[0]), []
        for s, data in held:
            if self.state == "valid":
                self.update(s, data)

    def update(self, seqnum, data):
        if self.state == "building":
            if len(self.held) < MAX_HELD:
                self.held.append((seqnum, data))
            else:
                self.stale()
            return
        if self.state != "valid" or seqnum < self.next_seqnum:
            return
        if seqnum > self.next_seqnum:
            self.stale()
            return
        for level in data.get("bids", []):
            self.put(self.bids, level)
        for level in data.get("asks", []):
            self.put(self.asks, level)
        self.next_seqnum += 1

    def stale(self):
        self.state = "stale"
        self.staging = None
        self.held = []
        self.bids, self.asks = {}, {}


class BookBuilder:
    def __init__(self):
        self.books = {}

    def on_frame(self, text):
        try:
            m = json.loads(text)
            if m.get("channel") != "DEPTH":
                return {"symbol": None, "state": "ignored"}
            kind, seqnum = m["update"], int(m["seqnum"])
            data = m.get("data") or {}
            symbol = data.get("symbol") or m.get("item")
            if not symbol:
                return {"symbol": None, "state": "ignored"}
        except (ValueError, KeyError, TypeError, AttributeError):
            return {"symbol": None, "state": "ignored"}
        book = self.books.setdefault(symbol, Book(symbol))
        try:
            if kind == "SNAPSHOT":
                book.snapshot(seqnum, data)
            elif kind == "SNAPSHOT_COMPLETE":
                book.complete(seqnum)
            elif kind == "UPDATE":
                book.update(seqnum, data)
        except (ValueError, KeyError, TypeError):
            book.stale()
        return book.answer()


def main():
    builder = BookBuilder()
    for line in sys.stdin:
        try:
            frame = json.loads(line)["frame"]
        except (ValueError, KeyError, TypeError):
            answer = {"symbol": None, "state": "ignored"}
        else:
            answer = builder.on_frame(frame)
        sys.stdout.write(json.dumps(answer, separators=(",", ":")) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
