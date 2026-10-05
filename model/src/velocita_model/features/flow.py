"""External taker-flow measurement.

This module counts what other participants did. It never counts anything the
simulator hypothesised - a shadow fill is not flow, and mixing the two would
manufacture the exact volume statistic the whole system exists to measure
honestly.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from decimal import Decimal
from typing import Deque

NS_PER_MINUTE = 60_000_000_000


@dataclass(frozen=True, slots=True)
class TradePrint:
    at_ns: int
    price: Decimal
    size: Decimal
    direction: str | None = None
    direction_inferred: str | None = None


def infer_direction(price: Decimal, best_bid: Decimal | None, best_ask: Decimal | None) -> str | None:
    if best_bid is None or best_ask is None:
        return None
    if price >= best_ask:
        return "buy"
    if price <= best_bid:
        return "sell"
    return None


class FlowWindow:
    __slots__ = ("_window_ns", "_prints")

    def __init__(self, window_seconds: float = 60.0) -> None:
        self._window_ns = int(window_seconds * 1_000_000_000)
        self._prints: Deque[TradePrint] = deque()

    def observe(self, print_: TradePrint) -> None:
        self._prints.append(print_)
        self._trim(print_.at_ns)

    def _trim(self, now_ns: int) -> None:
        cutoff = now_ns - self._window_ns
        while self._prints and self._prints[0].at_ns < cutoff:
            self._prints.popleft()

    def advance(self, now_ns: int) -> None:
        self._trim(now_ns)

    def trades_per_minute(self) -> Decimal:
        scale = Decimal(NS_PER_MINUTE) / Decimal(self._window_ns)
        return Decimal(len(self._prints)) * scale

    def notional_per_minute(self) -> Decimal:
        total = sum((p.price * p.size for p in self._prints), Decimal(0))
        scale = Decimal(NS_PER_MINUTE) / Decimal(self._window_ns)
        return total * scale

    def directional_notional(self) -> tuple[Decimal, Decimal]:
        buys = sells = Decimal(0)
        for p in self._prints:
            direction = p.direction or p.direction_inferred
            if direction == "buy":
                buys += p.price * p.size
            elif direction == "sell":
                sells += p.price * p.size
        return buys, sells

    def __len__(self) -> int:
        return len(self._prints)
