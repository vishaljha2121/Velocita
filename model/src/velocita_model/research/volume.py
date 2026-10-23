"""Threshold rebalancer: the volume strategy candidate.

Hold TARGET_WEIGHT of equity in one asset and the rest in USDC. When price
moves the asset's share outside target +/- BAND, trade back to target with a
market order (the only order type the True Markets DeFi path offers). Each
trade sells after a rise or buys after a dip; volume is a by-product whose size
the band controls. On a swap venue the budget identity is
    volume  ~=  loss budget / cost per unit of volume,
so `volume_per_cost` is the number that decides leaderboard feasibility once
the venue's markup is known.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class BandParams:
    target_weight: float = 0.5
    band: float = 0.02
    cost_bps: float = 10.0
    start_in_cash: bool = False


@dataclass
class VolumeResult:
    returns: list[float]
    volume: float
    trades: int
    cost: float

    @property
    def volume_per_cost(self) -> float:
        return self.volume / self.cost if self.cost > 0 else float("inf")


def band_rebalance(prices: Sequence[float], p: BandParams) -> VolumeResult:
    cost_rate = p.cost_bps / 10_000
    value, cash = (0.0, 1.0) if p.start_in_cash else (p.target_weight, 1.0 - p.target_weight)
    result = VolumeResult(returns=[0.0] * len(prices), volume=0.0, trades=0, cost=0.0)
    previous = 1.0
    for i, price in enumerate(prices):
        if i > 0:
            value *= price / prices[i - 1]
        equity = value + cash
        weight = value / equity
        if abs(weight - p.target_weight) > p.band:
            target = p.target_weight * equity
            traded = abs(target - value)
            fee = traded * cost_rate
            result.volume += traded
            result.cost += fee
            result.trades += 1
            equity -= fee
            value = p.target_weight * equity
            cash = equity - value
        equity = value + cash
        result.returns[i] = equity / previous - 1
        previous = equity
    return result
