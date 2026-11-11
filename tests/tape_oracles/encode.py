"""Independent encoder for tape log entries and venue-shaped frames, written from the byte layout in docs/tape/DECISIONS.md.

Entry layout (little-endian): u8 version=1, u8 type, then
  1 Observation:      header, u8 channel, u8 kind, symbol, u8 has_seqnum, u64 seqnum, 32B sha256(payload),
                      i64 utc_recv_ns, u64 mono_recv_ns, u32 clock_error_us, u32 len, payload
  2 ObservationPart:  header, u64 first_request_seq, u16 part_index, u16 part_count, u32 whole_size,
                      32B sha256(whole), u32 len, chunk
  3 RecorderEvent:    header, u8 kind, u8 channel, symbol, u64 expected, u64 received, i64 utc_ns
  4 Tick:             u64 number, u8 proposer, i64 proposer_utc_ns
  5 HeadSignature:    u64 tick, u64 tree_size, 32B root, u8 voter, 64B signature
header = u8 recorder, u64 session, u64 request_seq; symbol = u8 len, bytes.
"""
import hashlib
import json
import struct

CHANNEL = {"INSTRUMENT": 1, "EBBO": 2, "DEPTH": 3, "TRADE": 4, "HEARTBEAT": 5, "CONTROL": 6}
KIND = {"SNAPSHOT": 1, "UPDATE": 2, "SNAPSHOT_COMPLETE": 3, "HEARTBEAT": 4, "CONTROL": 5, "UNPARSED": 6}
EVENT = {"session_start": 1, "gap": 2, "regression": 3, "overflow": 4, "dead_connection": 5,
         "reconnect": 6, "resubscribe": 7, "oversize": 8}


def _header(recorder, session, request):
    return struct.pack("<BQQ", recorder, session, request)


def _symbol(symbol):
    b = symbol.encode()
    return struct.pack("<B", len(b)) + b


def observation_body(recorder, session, request, channel, kind, symbol, seqnum, payload, utc, mono=0,
                     clock=0xFFFFFFFF, has_seqnum=True, payload_hash=None):
    digest = payload_hash if payload_hash is not None else hashlib.sha256(payload).digest()
    return (bytes([1, 1]) + _header(recorder, session, request) + bytes([CHANNEL[channel], KIND[kind]]) +
            _symbol(symbol) + struct.pack("<BQ", 1 if has_seqnum else 0, seqnum if has_seqnum else 0) + digest +
            struct.pack("<qQII", utc, mono, clock, len(payload)) + payload)


def parts(recorder, session, first_request, whole, chunk):
    pieces = [whole[i:i + chunk] for i in range(0, len(whole), chunk)]
    digest = hashlib.sha256(whole).digest()
    out = []
    for i, piece in enumerate(pieces):
        out.append(bytes([1, 2]) + _header(recorder, session, first_request + i) +
                   struct.pack("<QHHI", first_request, i, len(pieces), len(whole)) + digest +
                   struct.pack("<I", len(piece)) + piece)
    return out


def event(recorder, session, request, kind, channel="DEPTH", symbol="", expected=0, received=0, utc=0):
    return (bytes([1, 3]) + _header(recorder, session, request) + bytes([EVENT[kind], CHANNEL[channel]]) +
            _symbol(symbol) + struct.pack("<QQq", expected, received, utc))


def tick(number, proposer=1, utc=0):
    return bytes([1, 4]) + struct.pack("<QBq", number, proposer, utc)


def head_signature(tick_number, tree_size, root, voter, signature=bytes(64)):
    return bytes([1, 5]) + struct.pack("<QQ", tick_number, tree_size) + root + bytes([voter]) + signature


def depth_frame(seqnum, kind, symbol, bids=(), asks=()):
    if kind == "SNAPSHOT_COMPLETE":
        return json.dumps({"seqnum": str(seqnum), "channel": "DEPTH", "update": kind, "item": symbol},
                          separators=(",", ":")).encode()
    def levels(xs):
        return [{"price": p, "qty": q, "order_count": c, "last_update": "1"} for p, q, c in xs]
    return json.dumps({"seqnum": str(seqnum), "channel": "DEPTH", "update": kind,
                       "data": {"symbol": symbol, "bids": levels(bids), "asks": levels(asks)}},
                      separators=(",", ":")).encode()


def ebbo_frame(seqnum, kind, symbol, bid=None, ask=None):
    info = {"last_update": "1"}
    if bid: info["best_bid"] = {"price": bid[0], "qty": bid[1], "order_count": bid[2], "last_update": "1"}
    if ask: info["best_ask"] = {"price": ask[0], "qty": ask[1], "order_count": ask[2], "last_update": "1"}
    return json.dumps({"seqnum": str(seqnum), "channel": "EBBO", "update": kind,
                       "data": {"id": "1", "symbol": symbol, "info": info}}, separators=(",", ":")).encode()


def trade_frame(seqnum, symbol, price, qty):
    return json.dumps({"seqnum": str(seqnum), "channel": "TRADE", "update": "UPDATE",
                       "data": {"match_id": "1", "taking_side": "BUY", "symbol": symbol,
                                "info": {"price": price, "qty": qty, "timestamp": "1"}}},
                      separators=(",", ":")).encode()


def instrument_frame(seqnum, symbol, band):
    return json.dumps({"seqnum": str(seqnum), "channel": "INSTRUMENT", "update": "SNAPSHOT",
                       "data": {"id": "1", "status": "ACTIVE", "info": {"symbol": symbol, "reference_price": "1",
                                                                         "price_bands_percent": band}}},
                      separators=(",", ":")).encode()


def write_log(path, entries):
    with open(path, "wb") as f:
        for e in entries:
            f.write(struct.pack("<I", len(e)) + e)
