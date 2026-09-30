"""Hypothetical quote construction.

Nothing in this module sends anything anywhere. It computes where a quote would
sit and why, and the "why" is retained per quote so a wide spread in a report
can be traced to the term that widened it.

The half-spread is built additively from named terms rather than as one tuned
number, because each term answers a different question a reviewer will ask:

    half_spread = base
                + volatility term      (how fast can fair value move against me)
                + latency term         (how stale is my view while I am exposed)
                + adverse-selection    (what did fills actually cost me lately)
                + cost floor           (fees + amortised gas, so a fill can pay)
                + inventory skew       (asymmetric: push away from the cap)

The cost floor is the term that kills most DeFi market-making ideas at this
account size, and it is deliberately not optional.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal

from ..contracts.enums import Side
from .inventory import SimulatedInventory


@dataclass(frozen=True, slots=True)
class QuoteParameters:
    base_half_spread_bps: Decimal
    min_half_spread_bps: Decimal
    max_half_spread_bps: Decimal
    vol_spread_multiplier: Decimal
    inventory_skew_bps_at_limit: Decimal
    maker_fee_bps: Decimal
    gas_cost_bps: Decimal
    tick_size: Decimal
    lot_size: Decimal
    min_notional_quote: Decimal
    #: Safety margin required on top of modelled costs before a quote is
    #: considered economically sensible. A quote that only breaks even under
    #: perfect assumptions is not a strategy.
    cost_safety_multiplier: Decimal = Decimal("1.5")


@dataclass(frozen=True, slots=True)
class SpreadTerms:
    base_bps: Decimal
    volatility_bps: Decimal
    latency_bps: Decimal
    adverse_selection_bps: Decimal
    cost_floor_bps: Decimal
    inventory_skew_bps: Decimal
    clamped_from_bps: Decimal | None

    @property
    def total_bps(self) -> Decimal:
        return (
            self.base_bps
            + self.volatility_bps
            + self.latency_bps
            + self.adverse_selection_bps
            + self.inventory_skew_bps
        )

    def explain(self) -> str:
        parts = [
            f"base={self.base_bps:.2f}",
            f"vol={self.volatility_bps:.2f}",
            f"latency={self.latency_bps:.2f}",
            f"adverse={self.adverse_selection_bps:.2f}",
            f"skew={self.inventory_skew_bps:+.2f}",
            f"floor={self.cost_floor_bps:.2f}",
        ]
        if self.clamped_from_bps is not None:
            parts.append(f"clamped from {self.clamped_from_bps:.2f}")
        return " ".join(parts)


@dataclass(frozen=True, slots=True)
class ShadowQuote:
    """A quote that exists only in this process. The field names carry that."""

    side: Side
    hypothetical_price: Decimal
    hypothetical_size_base: Decimal
    fair_value: Decimal
    half_spread_bps: Decimal
    terms: SpreadTerms
    suppressed_reason: str | None = None

    @property
    def is_quotable(self) -> bool:
        return self.suppressed_reason is None

    @property
    def hypothetical_notional_quote(self) -> Decimal:
        return self.hypothetical_price * self.hypothetical_size_base


def round_to_tick(price: Decimal, tick: Decimal, side: Side) -> Decimal:
    """Round *away* from the mid: a bid rounds down, an ask rounds up.

    Rounding toward the mid would make a quote more aggressive than the model
    asked for, which is the one direction the rounding must never move it. This
    mirrors the reject-not-clamp principle in Phase 3D: the sign of an
    adjustment is a safety property, not a formatting detail.
    """
    if tick <= 0:
        raise ValueError("tick size must be positive")
    steps = price / tick
    floor = int(steps.to_integral_value(rounding="ROUND_FLOOR"))
    if steps == floor:
        return tick * floor
    return tick * (floor if side == Side.buy else floor + 1)


def round_to_lot(size: Decimal, lot: Decimal) -> Decimal:
    """Size always rounds down. Rounding up would show more risk than intended."""
    if lot <= 0:
        raise ValueError("lot size must be positive")
    steps = size / lot
    return lot * int(steps.to_integral_value(rounding="ROUND_FLOOR"))


def cost_floor_bps(params: QuoteParameters) -> Decimal:
    """The minimum half-spread at which a round trip can pay for itself.

    A round trip pays maker fees twice and amortised gas twice, and each side of
    the quote captures only half the spread. The safety multiplier then requires
    the edge to beat costs by a margin rather than tie them.
    """
    round_trip_cost = (params.maker_fee_bps + params.gas_cost_bps) * Decimal(2)
    return round_trip_cost * params.cost_safety_multiplier / Decimal(2)


def build_quote(
    *,
    side: Side,
    fair_value: Decimal,
    params: QuoteParameters,
    inventory: SimulatedInventory,
    volatility_bps: Decimal | None,
    data_age_ns: int,
    recent_adverse_selection_bps: Decimal | None,
    target_notional_quote: Decimal,
) -> ShadowQuote:
    """Construct one hypothetical quote, or a suppressed one with a reason."""
    if fair_value <= 0:
        return _suppressed(side, fair_value, params, "fair value unavailable")

    floor_bps = cost_floor_bps(params)

    # Volatility term: the wider fair value can wander while the quote rests,
    # the wider the quote must be. Unavailable volatility is not treated as
    # zero - an unmeasured market is not a calm one.
    if volatility_bps is None:
        return _suppressed(side, fair_value, params, "volatility unavailable for this horizon")
    volatility_term = volatility_bps * params.vol_spread_multiplier

    # Latency term: scale the short-horizon volatility by how stale our view is.
    # One second of staleness against a 20bps/s market is 20bps of blind risk.
    latency_seconds = Decimal(data_age_ns) / Decimal(1_000_000_000)
    latency_term = volatility_bps * latency_seconds

    # Adverse selection: what fills actually cost recently, not what we hoped.
    # Only a negative (unfavourable) markout widens the quote; a favourable one
    # does not earn a tighter quote, because that would be fitting to noise.
    adverse_term = Decimal(0)
    if recent_adverse_selection_bps is not None and recent_adverse_selection_bps < 0:
        adverse_term = abs(recent_adverse_selection_bps)

    # Inventory skew: signed, and applied so the risk-increasing side widens.
    skew = inventory.skew(fair_value)
    skew_magnitude = params.inventory_skew_bps_at_limit * abs(skew)
    if (side == Side.buy and skew > 0) or (side == Side.sell and skew < 0):
        inventory_term = skew_magnitude       # already leaning this way: back off
    else:
        inventory_term = -skew_magnitude      # reduces inventory: quote closer in

    raw_half_spread = (
        params.base_half_spread_bps + volatility_term + latency_term + adverse_term + inventory_term
    )
    # The cost floor is a floor, not a term: no combination of favourable
    # adjustments may take the quote inside the price at which a fill pays.
    raw_half_spread = max(raw_half_spread, floor_bps)

    clamped_from: Decimal | None = None
    half_spread = raw_half_spread
    if half_spread < params.min_half_spread_bps:
        clamped_from, half_spread = half_spread, params.min_half_spread_bps
    elif half_spread > params.max_half_spread_bps:
        clamped_from, half_spread = half_spread, params.max_half_spread_bps

    terms = SpreadTerms(
        base_bps=params.base_half_spread_bps,
        volatility_bps=volatility_term,
        latency_bps=latency_term,
        adverse_selection_bps=adverse_term,
        cost_floor_bps=floor_bps,
        inventory_skew_bps=inventory_term,
        clamped_from_bps=clamped_from,
    )

    if inventory.at_limit(fair_value, side):
        return ShadowQuote(
            side=side,
            hypothetical_price=Decimal(0),
            hypothetical_size_base=Decimal(0),
            fair_value=fair_value,
            half_spread_bps=half_spread,
            terms=terms,
            suppressed_reason=f"simulated inventory at cap for {side.name}",
        )

    offset = fair_value * half_spread / Decimal(10_000)
    raw_price = fair_value - offset if side == Side.buy else fair_value + offset
    price = round_to_tick(raw_price, params.tick_size, side)
    if price <= 0:
        return _suppressed(side, fair_value, params, "quote price rounds to non-positive", terms)

    size = round_to_lot(target_notional_quote / price, params.lot_size)
    if size <= 0:
        return _suppressed(side, fair_value, params, "target notional is below one lot", terms)
    if price * size < params.min_notional_quote:
        return _suppressed(
            side,
            fair_value,
            params,
            f"notional {price * size:.4f} below venue minimum {params.min_notional_quote}",
            terms,
        )

    return ShadowQuote(
        side=side,
        hypothetical_price=price,
        hypothetical_size_base=size,
        fair_value=fair_value,
        half_spread_bps=half_spread,
        terms=terms,
    )


def _suppressed(
    side: Side, fair_value: Decimal, params: QuoteParameters, reason: str, terms: SpreadTerms | None = None
) -> ShadowQuote:
    zero = Decimal(0)
    return ShadowQuote(
        side=side,
        hypothetical_price=zero,
        hypothetical_size_base=zero,
        fair_value=fair_value if fair_value > 0 else zero,
        half_spread_bps=zero,
        terms=terms
        or SpreadTerms(zero, zero, zero, zero, cost_floor_bps(params), zero, None),
        suppressed_reason=reason,
    )
