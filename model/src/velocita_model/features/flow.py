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
    #: 'buy'/'sell' when the venue states it, None otherwise. Never guessed here.
    direction: str | None = None
    #: Direction we inferred, kept separate so an analysis can exclude inference.
    direction_inferred: str | None = None


def infer_direction(price: Decimal, best_bid: Decimal | None, best_ask: Decimal | None) -> str | None:
    """Transparent tick-rule inference: at-or-above the ask is buyer-initiated,
    at-or-below the bid is seller-initiated, anything inside is unknown.

    Returns None rather than a coin flip. An inferred direction that is wrong
    half the time is worse than an absent one, because it looks like data.
    """
    if best_bid is None or best_ask is None:
        return None
    if price >= best_ask:
        return "buy"
    if price <= best_bid:
        return "sell"
    return None


class FlowWindow:
    """Rolling trade count and notional over a fixed window."""

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
        """Age the window even when nothing traded. Without this, a dead market
        keeps reporting its last busy minute forever."""
        self._trim(now_ns)

    def trades_per_minute(self) -> Decimal:
        scale = Decimal(NS_PER_MINUTE) / Decimal(self._window_ns)
        return Decimal(len(self._prints)) * scale

    def notional_per_minute(self) -> Decimal:
        total = sum((p.price * p.size for p in self._prints), Decimal(0))
        scale = Decimal(NS_PER_MINUTE) / Decimal(self._window_ns)
        return total * scale

    def directional_notional(self) -> tuple[Decimal, Decimal]:
        """(buy_initiated, sell_initiated) using stated direction first, then
        inference. Prints with neither are excluded from both totals rather than
        being split evenly."""
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
