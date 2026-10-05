"""Realized volatility over several horizons, from a bounded price history.

Bounded is the operative word. This runs for days on a Pi, so the history is a
ring buffer sized by the longest horizon, and every statistic is computed from
what is actually in the window - never extrapolated to fill it. A 5-minute
volatility computed from 8 seconds of data is reported as unavailable, because
publishing it would make a thin window look like a measurement.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from decimal import Decimal
from typing import Deque, Iterable

NS_PER_SECOND = 1_000_000_000


@dataclass(frozen=True, slots=True)
class PricePoint:
    at_ns: int
    price: Decimal


class RealizedVolatility:
    __slots__ = ("_window_ns", "_min_samples", "_points")

    def __init__(self, window_seconds: float, *, min_samples: int = 3) -> None:
        self._window_ns = int(window_seconds * NS_PER_SECOND)
        self._min_samples = min_samples
        self._points: Deque[PricePoint] = deque()

    @property
    def window_seconds(self) -> float:
        return self._window_ns / NS_PER_SECOND

    def observe(self, at_ns: int, price: Decimal) -> None:
        if price <= 0:
            return
        self._points.append(PricePoint(at_ns, price))
        cutoff = at_ns - self._window_ns
        while self._points and self._points[0].at_ns < cutoff:
            self._points.popleft()

    @property
    def sample_count(self) -> int:
        return len(self._points)

    def spans_full_window(self) -> bool:
        if len(self._points) < 2:
            return False
        return (self._points[-1].at_ns - self._points[0].at_ns) >= self._window_ns * 0.8

    def value_bps(self) -> Decimal | None:
        if len(self._points) < self._min_samples or not self.spans_full_window():
            return None
        squares = Decimal(0)
        count = 0
        previous = self._points[0].price
        for point in list(self._points)[1:]:
            if previous > 0:
                move = (point.price - previous) / previous * Decimal(10_000)
                squares += move * move
                count += 1
            previous = point.price
        if count == 0:
            return None
        return (squares / Decimal(count)).sqrt()


class VolatilitySet:
    __slots__ = ("_estimators",)

    def __init__(self, horizons_seconds: Iterable[float] = (1.0, 5.0, 30.0, 300.0)) -> None:
        self._estimators = {float(h): RealizedVolatility(h) for h in horizons_seconds}

    def observe(self, at_ns: int, price: Decimal) -> None:
        for estimator in self._estimators.values():
            estimator.observe(at_ns, price)

    def value_bps(self, horizon_seconds: float) -> Decimal | None:
        return self._estimators[float(horizon_seconds)].value_bps()

    def coverage(self) -> dict[float, int]:
        return {horizon: est.sample_count for horizon, est in self._estimators.items()}


def jump_count(points: Iterable[PricePoint], threshold_bps: Decimal) -> int:
    total = 0
    previous: Decimal | None = None
    for point in points:
        if previous is not None and previous > 0:
            move = abs(point.price - previous) / previous * Decimal(10_000)
            if move >= threshold_bps:
                total += 1
        previous = point.price
    return total
