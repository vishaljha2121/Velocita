"""Measured execution costs are derived from quote evidence, never typed in."""

from __future__ import annotations

import json
import unittest
from tempfile import TemporaryDirectory

from velocita_model.research import costs


def quote(symbol, side, qty, out, before, after):
    return {"symbol": symbol, "request": {"side": side}, "reference_30s_close": before,
            "reference_30s_close_after": after, "quote": {"qty": qty, "qty_out": out}}


class MeasuredCosts(unittest.TestCase):
    def test_all_in_cost_per_side(self) -> None:
        rows = [quote("X", "buy", "100", "0.99", "99", "101"),
                quote("X", "sell", "1", "98", "100", "100"),
                quote("X", "sell", "1", "99", "100", "100")]
        with TemporaryDirectory() as d:
            path = f"{d}/q.json"
            json.dump(rows, open(path, "w"))
            measured = costs.measured(path)
            self.assertAlmostEqual(measured["X"]["buy"][0], 101.0101, places=3)
            self.assertEqual(sorted(round(x, 6) for x in measured["X"]["sell"]), [100.0, 200.0])
            mean = costs.model(path, "X", "mean")
            worst = costs.model(path, "X", "worst")
            self.assertAlmostEqual(mean.sell_bps, 150.0)
            self.assertAlmostEqual(worst.sell_bps, 200.0)
            self.assertAlmostEqual(worst.buy_bps, mean.buy_bps)

    def test_missing_side_is_refused_not_guessed(self) -> None:
        with TemporaryDirectory() as d:
            path = f"{d}/q.json"
            json.dump([quote("X", "buy", "100", "0.99", "100", "100")], open(path, "w"))
            with self.assertRaises(KeyError):
                costs.model(path, "X", "mean")


if __name__ == "__main__":
    unittest.main()
