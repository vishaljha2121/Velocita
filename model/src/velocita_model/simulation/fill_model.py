"""Modelled fill opportunities under explicit queue-position assumptions.

The central discipline: **the market touching your price is not a fill.**

A resting limit order fills only when enough opposing volume trades through the
queue ahead of it. On a venue where queue position is not observable, that
quantity is unknown, so this module refuses to produce a single fill count.
It produces three, under named assumptions, and a report that quotes only the
optimistic one is misusing it.

    optimistic   - you are at the front of the queue; any print at or through
                   your price fills you. This is the number a naive backtest
                   produces, and it is an upper bound, not an estimate.
    base         - you are behind the size that was already displayed at your
                   price when you arrived. Volume must clear that first.
    conservative - as base, plus the size that joined at your price afterwards
                   is assumed to have been ahead of you too. This approximates
                   a venue with unfavourable or unobservable queue mechanics.

Where the spread is crossed entirely - the market trades *through* your price
rather than at it - all three assumptions agree that you filled, because queue
position stops mattering once the level is swept.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal
from enum import Enum

from ..contracts.enums import Side


class QueueAssumption(str, Enum):
    optimistic = "optimistic"
    base = "base"
    conservative = "conservative"


@dataclass(frozen=True, slots=True)
class FillOpportunity:
    """A modelled fill. `is_model_output` exists so no consumer can pretend
    otherwise, and the reports print it."""

    assumption: QueueAssumption
    at_ns: int
    side: Side
    price: Decimal
    filled_size_base: Decimal
    partial: bool
    queue_ahead_at_fill: Decimal
    swept: bool
    is_model_output: bool = True


@dataclass(slots=True)
class RestingShadowQuote:
    """State for one hypothetical resting quote under one assumption."""

    side: Side
    price: Decimal
    size_base: Decimal
    placed_at_ns: int
    queue_ahead_base: Decimal
    filled_base: Decimal = Decimal(0)
    withdrawn_at_ns: int | None = None
    _fills: list[FillOpportunity] = field(default_factory=list, repr=False)

    @property
    def remaining_base(self) -> Decimal:
        return self.size_base - self.filled_base

    @property
    def is_open(self) -> bool:
        return self.withdrawn_at_ns is None and self.remaining_base > 0

    @property
    def fills(self) -> tuple[FillOpportunity, ...]:
        return tuple(self._fills)


def initial_queue_ahead(
    assumption: QueueAssumption, displayed_size_at_level: Decimal
) -> Decimal:
    """Size assumed to be ahead of a newly placed quote."""
    if assumption == QueueAssumption.optimistic:
        return Decimal(0)
    if assumption == QueueAssumption.base:
        return displayed_size_at_level
    # Conservative doubles the displayed size as a stand-in for joiners we
    # cannot see. It is an admittedly crude proxy; its purpose is to answer
    # "does the conclusion survive a pessimistic queue?", not to be precise.
    return displayed_size_at_level * Decimal(2)


class FillModel:
    """Consumes external trade prints and advances hypothetical queues.

    Only prints observed on the venue's public feed are consumed. Nothing this
    simulator hypothesises is ever fed back as volume - that would be modelling
    your own orders trading with themselves.
    """

    __slots__ = ("_assumption", "_quotes")

    def __init__(self, assumption: QueueAssumption) -> None:
        self._assumption = assumption
        self._quotes: list[RestingShadowQuote] = []

    @property
    def assumption(self) -> QueueAssumption:
        return self._assumption

    @property
    def open_quotes(self) -> tuple[RestingShadowQuote, ...]:
        return tuple(quote for quote in self._quotes if quote.is_open)

    def place(
        self,
        *,
        side: Side,
        price: Decimal,
        size_base: Decimal,
        at_ns: int,
        displayed_size_at_level: Decimal,
    ) -> RestingShadowQuote:
        quote = RestingShadowQuote(
            side=side,
            price=price,
            size_base=size_base,
            placed_at_ns=at_ns,
            queue_ahead_base=initial_queue_ahead(self._assumption, displayed_size_at_level),
        )
        self._quotes.append(quote)
        return quote

    def withdraw_all(self, at_ns: int) -> None:
        for quote in self._quotes:
            if quote.is_open:
                quote.withdrawn_at_ns = at_ns

    def observe_trade(
        self,
        *,
        at_ns: int,
        price: Decimal,
        size_base: Decimal,
        direction: str | None,
    ) -> list[FillOpportunity]:
        """Route one external print against every open hypothetical quote.

        `direction` is used only to skip prints that cannot have interacted with
        this side. When the venue does not state a direction and it could not be
        inferred, the print is applied to both sides - which overstates fills
        rather than understating them, and the reports say so.
        """
        produced: list[FillOpportunity] = []
        for quote in self._quotes:
            if not quote.is_open:
                continue

            # A bid is hit by seller-initiated flow; an ask is lifted by buyers.
            if direction == "buy" and quote.side == Side.buy:
                continue
            if direction == "sell" and quote.side == Side.sell:
                continue

            touched = price <= quote.price if quote.side == Side.buy else price >= quote.price
            if not touched:
                continue

            swept = price < quote.price if quote.side == Side.buy else price > quote.price
            available = size_base

            if not swept:
                # Traded exactly at our level: the queue ahead absorbs first.
                consumed_by_queue = min(quote.queue_ahead_base, available)
                quote.queue_ahead_base -= consumed_by_queue
                available -= consumed_by_queue
                if available <= 0:
                    continue

            filled = min(available, quote.remaining_base)
            if filled <= 0:
                continue
            quote.filled_base += filled
            opportunity = FillOpportunity(
                assumption=self._assumption,
                at_ns=at_ns,
                side=quote.side,
                price=quote.price,
                filled_size_base=filled,
                partial=quote.remaining_base > 0,
                queue_ahead_at_fill=quote.queue_ahead_base,
                swept=swept,
            )
            quote._fills.append(opportunity)
            produced.append(opportunity)
        return produced

    def summary(self) -> dict[str, object]:
        fills = [fill for quote in self._quotes for fill in quote.fills]
        return {
            "assumption": self._assumption.value,
            "quotes_placed": len(self._quotes),
            "fill_opportunities": len(fills),
            "partial_fills": sum(1 for fill in fills if fill.partial),
            "swept_fills": sum(1 for fill in fills if fill.swept),
            "filled_base": sum((fill.filled_size_base for fill in fills), Decimal(0)),
        }


class FillModelSet:
    """Runs all three assumptions side by side over the same data.

    This is the intended entry point. Instantiating a single `FillModel` and
    reporting its result is how a shadow study accidentally becomes an
    optimistic one.
    """

    __slots__ = ("_models",)

    def __init__(self, assumptions: tuple[QueueAssumption, ...] = tuple(QueueAssumption)) -> None:
        self._models = {assumption: FillModel(assumption) for assumption in assumptions}

    def __getitem__(self, assumption: QueueAssumption) -> FillModel:
        return self._models[assumption]

    def place(self, **kwargs: object) -> None:
        for model in self._models.values():
            model.place(**kwargs)  # type: ignore[arg-type]

    def withdraw_all(self, at_ns: int) -> None:
        for model in self._models.values():
            model.withdraw_all(at_ns)

    def observe_trade(self, **kwargs: object) -> dict[QueueAssumption, list[FillOpportunity]]:
        return {a: m.observe_trade(**kwargs) for a, m in self._models.items()}  # type: ignore[arg-type]

    def summaries(self) -> list[dict[str, object]]:
        return [model.summary() for model in self._models.values()]

    def fill_probability_range(self) -> tuple[int, int]:
        """(conservative, optimistic) fill counts - a range, never a point.

        When queue position cannot be observed, this range *is* the answer. A
        study that collapses it to one number has thrown away the uncertainty
        that determines whether the strategy works.
        """
        counts = [int(s["fill_opportunities"]) for s in self.summaries()]
        return (min(counts), max(counts)) if counts else (0, 0)
