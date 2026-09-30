"""Execution costs measured from True Markets quotes (tools/measure-markup.py).

All-in cost per side in basis points against the True Markets reference price
(mean of the reference read just before and just after the quote):
    buy:  pay QTY USDC, receive QTY_OUT base  -> (QTY/QTY_OUT) / ref - 1
    sell: pay QTY base, receive QTY_OUT USDC  -> 1 - (QTY_OUT/QTY) / ref
A side that was never quoted raises: a missing cost is not replaced by a guess.
"""

from __future__ import annotations

import json
from decimal import Decimal
from pathlib import Path

from .rules import Costs

EVIDENCE = (Path(__file__).resolve().parents[4] / "docs" / "evidence" / "truemarkets_markup_2026-09-28"
            / "quotes_buy_sell_1631utc.json")


def measured(path: Path | str = EVIDENCE) -> dict[str, dict[str, list[float]]]:
    out: dict[str, dict[str, list[float]]] = {}
    for row in json.loads(Path(path).read_text()):
        side = row["request"]["side"]
        before = Decimal(row["reference_30s_close"])
        after = Decimal(row.get("reference_30s_close_after") or row["reference_30s_close"])
        ref = (before + after) / 2
        qty, got = Decimal(row["quote"]["qty"]), Decimal(row["quote"]["qty_out"])
        bps = (qty / got / ref - 1) * 10_000 if side == "buy" else (1 - got / qty / ref) * 10_000
        out.setdefault(row["symbol"], {}).setdefault(side, []).append(float(bps))
    return out


def model(path: Path | str, symbol: str, statistic: str = "mean") -> Costs:
    sides = measured(path)[symbol]
    pick = (lambda xs: sum(xs) / len(xs)) if statistic == "mean" else max
    return Costs(buy_bps=pick(sides["buy"]), sell_bps=pick(sides["sell"]))
