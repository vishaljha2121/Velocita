"""Order-book shape metrics.

Everything here works on integer atoms and returns `Decimal`, so the conversion
to canonical `FeatureMicros` happens once, in one place, under one declared
rounding mode. Intermediate floats would reintroduce exactly the host-dependent
behaviour the canonical contract exists to eliminate.

A metric that cannot be computed returns None. It is never zero-filled: a zero
spread and an unknown spread are different facts, and only one of them means the
book is tight.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from typing import Sequence

from ..contracts.enums import MarketDataStatus


@dataclass(frozen=True, slots=True)
class Level:
    """One ladder level, in exact decimal price and size."""

    price: Decimal
    size: Decimal


@dataclass(frozen=True, slots=True)
class BookObservation:
    """A normalised book snapshot. `status` is decided by the collector, not by
    the metric functions - staleness is a property of when we received data, not
    of what the data says."""

    pair_id: str
    source_sequence: int
    venue_sequence: int | None
    event_time_ns: int | None
    receive_time_ns: int
    bids: tuple[Level, ...]
    asks: tuple[Level, ...]
    status: MarketDataStatus
    raw_digest: bytes

    @property
    def best_bid(self) -> Decimal | None:
        return self.bids[0].price if self.bids else None

    @property
    def best_ask(self) -> Decimal | None:
        return self.asks[0].price if self.asks else None

    @property
    def top_bid_size(self) -> Decimal | None:
        return self.bids[0].size if self.bids else None

    @property
    def top_ask_size(self) -> Decimal | None:
        return self.asks[0].size if self.asks else None

    @property
    def is_two_sided(self) -> bool:
        return bool(self.bids and self.asks)

    @property
    def is_crossed(self) -> bool:
        return self.is_two_sided and self.bids[0].price >= self.asks[0].price


def mid_price(book: BookObservation) -> Decimal | None:
    if not book.is_two_sided:
        return None
    return (book.bids[0].price + book.asks[0].price) / 2


def spread(book: BookObservation) -> Decimal | None:
    if not book.is_two_sided:
        return None
    return book.asks[0].price - book.bids[0].price


def spread_bps(book: BookObservation) -> Decimal | None:
    mid = mid_price(book)
    width = spread(book)
    if mid is None or width is None or mid <= 0:
        return None
    return width / mid * Decimal(10_000)


def microprice(book: BookObservation) -> Decimal | None:
    """Size-weighted mid: the best bid is weighted by the *ask* size and vice
    versa, because a large resting ask pushes the true clearing price down."""
    if not book.is_two_sided:
        return None
    bid_size = book.bids[0].size
    ask_size = book.asks[0].size
    total = bid_size + ask_size
    if total <= 0:
        return mid_price(book)
    return (book.bids[0].price * ask_size + book.asks[0].price * bid_size) / total


def book_imbalance(book: BookObservation, depth: int = 1) -> Decimal | None:
    """(bid - ask) / (bid + ask) over the top `depth` levels, in [-1, 1].
    Positive means bid-heavy."""
    if not book.is_two_sided:
        return None
    bid_size = sum((level.size for level in book.bids[:depth]), Decimal(0))
    ask_size = sum((level.size for level in book.asks[:depth]), Decimal(0))
    total = bid_size + ask_size
    if total <= 0:
        return None
    return (bid_size - ask_size) / total


def cumulative_depth_quote(levels: Sequence[Level], mid: Decimal, distance_bps: Decimal) -> Decimal:
    """Notional resting within `distance_bps` of `mid`, in quote units.

    Notional rather than base size, because a 20 USDC account cares about how
    much money sits in front of it, not how many tokens."""
    if mid <= 0:
        return Decimal(0)
    limit = mid * distance_bps / Decimal(10_000)
    total = Decimal(0)
    for level in levels:
        if abs(level.price - mid) > limit:
            break
        total += level.price * level.size
    return total


def classify_status(
    book: BookObservation,
    *,
    age_ns: int,
    max_age_ns: int,
    recovering: bool = False,
) -> MarketDataStatus:
    """Decide the frame's `MarketDataStatus`.

    Order matters and is deliberately pessimistic: a book that is both stale and
    crossed reports `crossed`, because a crossed book is a stronger statement
    about the feed being wrong than an old book is.
    """
    if recovering:
        return MarketDataStatus.recovering
    if not book.is_two_sided:
        return MarketDataStatus.incomplete
    if book.is_crossed:
        return MarketDataStatus.crossed
    if age_ns > max_age_ns:
        return MarketDataStatus.stale
    return MarketDataStatus.ready
