#!/usr/bin/env python3
"""Deliberately broken order-book clients, one per known trap, for the conformance harness tests."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools/book-kit"))
import reference_book as R  # noqa: E402


def add_deltas(side, level):
    price, qty, count = R.atoms(level["price"]), R.atoms(level["qty"]), int(level["order_count"])
    if qty == 0:
        side.pop(price, None)
    else:
        side[price] = (side.get(price, (0, 0))[0] + qty, count)


def float_put(side, level):
    price = int(float(level["price"]) * 10**R.SCALE)
    qty = int(float(level["qty"]) * 10**R.SCALE)
    if qty == 0:
        side.pop(price, None)
    else:
        side[price] = (qty, int(level["order_count"]))


def depth_field_put(side, level):
    price, qty = R.atoms(level["price"]), R.atoms(level["qty"])
    if qty == 0:
        side.pop(price, None)
    else:
        side[price] = (qty, int(level.get("depth", 0)))


original_complete = R.Book.complete
original_answer = R.Book.answer
original_update = R.Book.update


def snapshot_is_last(self, seqnum):
    original_complete(self, seqnum)
    if self.state == "valid" and self.next_seqnum == seqnum:
        self.next_seqnum = seqnum + 1


def early_answer(self):
    if self.state == "building" and self.staging is not None:
        seqnum, bids, asks = self.staging
        held = (self.bids, self.asks)
        self.bids, self.asks = bids, asks
        out = {"symbol": self.symbol, "state": "valid", "next_seqnum": str(seqnum),
               "bids": self.levels(self.bids), "asks": self.levels(self.asks)}
        self.bids, self.asks = held
        return out
    return original_answer(self)


def ignore_gaps(self, seqnum, data):
    if self.state == "valid" and seqnum > self.next_seqnum:
        self.next_seqnum = seqnum
    original_update(self, seqnum, data)


TRAPS = {
    "add-deltas": lambda: setattr(R.Book, "put", staticmethod(add_deltas)),
    "snapshot-is-last": lambda: setattr(R.Book, "complete", snapshot_is_last),
    "floats": lambda: setattr(R.Book, "put", staticmethod(float_put)),
    "depth-field": lambda: setattr(R.Book, "put", staticmethod(depth_field_put)),
    "serve-before-complete": lambda: setattr(R.Book, "answer", early_answer),
    "ignore-gaps": lambda: setattr(R.Book, "update", ignore_gaps),
}

if __name__ == "__main__":
    TRAPS[sys.argv[1]]()
    R.main()
