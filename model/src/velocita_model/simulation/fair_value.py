"""Fair-value estimation with explicit component attribution.

The estimate is a weighted blend, and the weights are configuration rather than
a tuned constant buried in code. What matters more than the blend is the gating:
a component whose input is stale, missing, or divergent contributes nothing and
says so, instead of contributing its last known value.

Every result carries its components, because "the fair value was 1.0037" is not
reviewable but "mid 1.0035 (0.6), micro 1.0041 (0.3), reference withheld: stale
by 4.2s" is.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal

from ..features.book_metrics import BookObservation, microprice, mid_price


@dataclass(frozen=True, slots=True)
class Component:
    name: str
    value: Decimal | None
    weight: Decimal
    withheld_reason: str | None = None

    @property
    def contributes(self) -> bool:
        return self.value is not None and self.withheld_reason is None and self.weight > 0


@dataclass(frozen=True, slots=True)
class FairValue:
    value: Decimal | None
    components: tuple[Component, ...] = field(default_factory=tuple)
    reason: str = ""

    @property
    def is_usable(self) -> bool:
        return self.value is not None and self.value > 0

    def explain(self) -> str:
        parts = []
        for component in self.components:
            if component.contributes:
                parts.append(f"{component.name}={component.value} w={component.weight}")
            else:
                parts.append(f"{component.name} withheld: {component.withheld_reason}")
        return "; ".join(parts)


@dataclass(frozen=True, slots=True)
class FairValueWeights:
    mid: Decimal = Decimal("0.4")
    microprice: Decimal = Decimal("0.4")
    reference: Decimal = Decimal("0.2")


def estimate_fair_value(
    book: BookObservation,
    *,
    weights: FairValueWeights = FairValueWeights(),
    reference_price: Decimal | None = None,
    reference_age_ns: int | None = None,
    max_reference_age_ns: int = 5_000_000_000,
    max_reference_divergence_bps: Decimal = Decimal("200"),
    book_age_ns: int = 0,
    max_book_age_ns: int = 2_000_000_000,
) -> FairValue:
    """Blend available components, withholding anything that fails its gate."""
    components: list[Component] = []

    if book_age_ns > max_book_age_ns:
        return FairValue(
            None,
            (Component("book", None, Decimal(0), f"book age {book_age_ns/1e9:.2f}s exceeds gate"),),
            reason="stale_book",
        )
    if not book.is_two_sided:
        return FairValue(None, (Component("book", None, Decimal(0), "book is one-sided"),), reason="incomplete_book")
    if book.is_crossed:
        # A crossed book is not a tight book. Quoting against one means quoting
        # against a feed that is wrong, which is the fastest way to be picked off.
        return FairValue(None, (Component("book", None, Decimal(0), "book is crossed"),), reason="crossed_book")

    mid = mid_price(book)
    micro = microprice(book)
    components.append(Component("mid", mid, weights.mid))
    components.append(Component("microprice", micro, weights.microprice))

    if reference_price is None:
        components.append(Component("reference", None, weights.reference, "no reference price configured"))
    elif reference_age_ns is not None and reference_age_ns > max_reference_age_ns:
        components.append(
            Component("reference", reference_price, weights.reference, f"reference age {reference_age_ns/1e9:.2f}s")
        )
    elif mid is not None and mid > 0:
        divergence_bps = abs(reference_price - mid) / mid * Decimal(10_000)
        if divergence_bps > max_reference_divergence_bps:
            # Large divergence means one of the two feeds is wrong and we do not
            # know which. Averaging them would produce a number that is wrong in
            # a new way; withholding preserves the disagreement as a signal.
            components.append(
                Component(
                    "reference",
                    reference_price,
                    weights.reference,
                    f"diverges {divergence_bps:.1f}bps from local mid",
                )
            )
        else:
            components.append(Component("reference", reference_price, weights.reference))
    else:
        components.append(Component("reference", reference_price, weights.reference, "no local mid to compare"))

    contributing = [c for c in components if c.contributes]
    total_weight = sum((c.weight for c in contributing), Decimal(0))
    if not contributing or total_weight <= 0:
        return FairValue(None, tuple(components), reason="no_usable_component")

    blended = sum((c.value * c.weight for c in contributing), Decimal(0)) / total_weight
    return FairValue(blended, tuple(components), reason="ok")


def reference_divergence_bps(mid: Decimal | None, reference: Decimal | None) -> Decimal | None:
    if mid is None or reference is None or mid <= 0:
        return None
    return (reference - mid) / mid * Decimal(10_000)
