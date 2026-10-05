"""Threshold rebalancer (volume strategy). Expected values are hand-computed."""

from __future__ import annotations

import math
import random
import unittest

from velocita_model.research import volume


class BandRebalancer(unittest.TestCase):
    def test_trades_back_to_target_only_outside_the_band(self) -> None:
        p = volume.BandParams(target_weight=0.5, band=0.02, cost_bps=0.0)
        r = volume.band_rebalance([100.0, 101.0, 110.0], p)
        self.assertEqual(r.trades, 1)
        self.assertAlmostEqual(r.volume, 0.025)
        self.assertAlmostEqual(r.returns[2], 1.05 / 1.005 - 1)

    def test_cost_is_charged_on_traded_value(self) -> None:
        p = volume.BandParams(target_weight=0.5, band=0.02, cost_bps=100.0)
        r = volume.band_rebalance([100.0, 110.0], p)
        self.assertAlmostEqual(r.cost, 0.00025)
        self.assertAlmostEqual(r.returns[1], 1.05 - 0.00025 - 1)

    def test_initial_purchase_is_counted(self) -> None:
        p = volume.BandParams(target_weight=0.5, band=0.02, cost_bps=0.0, start_in_cash=True)
        r = volume.band_rebalance([100.0, 100.0], p)
        self.assertAlmostEqual(r.volume, 0.5)

    def test_rebalancing_premium_on_a_mean_reverting_path(self) -> None:
        prices = [100.0, 125.0] * 10 + [100.0]
        r = volume.band_rebalance(prices, volume.BandParams(target_weight=0.5, band=0.05, cost_bps=0.0))
        self.assertGreater(math.prod(1 + x for x in r.returns), 1.0)

    def test_volume_per_unit_cost(self) -> None:
        r = volume.VolumeResult(returns=[], volume=2.0, trades=4, cost=0.001)
        self.assertAlmostEqual(r.volume_per_cost, 2000.0)


if __name__ == "__main__":
    unittest.main()
