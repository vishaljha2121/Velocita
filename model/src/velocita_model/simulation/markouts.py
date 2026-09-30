"""Fill-conditioned markouts: what a fill was worth after the fact.

A markout answers the only question that matters for a passive strategy: after
you were filled, did fair value move toward you or away from you? Spread capture
is what you hoped to earn; the markout is what the counterparty knew.

Sign convention, fixed here once: **positive is good for us.** A bid that fills
at 100 when fair value is then 101 has a +100bps markout, because we bought
below where the market went. This is stated because the opposite convention is
equally common and silently flipping it inverts every conclusion in the report.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from statistics import median
from typing import Iterable, Sequence

from ..contracts.enums import Side


@dataclass(frozen=True, slots=True)
class Markout:
    horizon_ms: int
    fair_value_then: Decimal
    markout_bps: Decimal


@dataclass(frozen=True, slots=True)
class FillMarkouts:
    at_ns: int
    side: Side
    fill_price: Decimal
    fill_size_base: Decimal
    fair_value_at_fill: Decimal
    assumption: str
    markouts: tuple[Markout, ...]

    def at_horizon(self, horizon_ms: int) -> Decimal | None:
        for markout in self.markouts:
            if markout.horizon_ms == horizon_ms:
                return markout.markout_bps
        return None


def markout_bps(side: Side, fill_price: Decimal, fair_value_later: Decimal) -> Decimal:
    """Positive means the fill was favourable to us."""
    if fill_price <= 0:
        raise ValueError("fill price must be positive")
    direction = Decimal(1) if side == Side.buy else Decimal(-1)
    return direction * (fair_value_later - fill_price) / fill_price * Decimal(10_000)


class FairValueTrack:
    """Time series of fair value, queried at a horizon after a fill.

    Lookup returns None past the end of the series. A markout horizon that
    extends beyond the collected data is *missing*, not zero, and excluding it
    is the difference between "we measured no adverse selection" and "we stopped
    watching before it showed up".
    """

    __slots__ = ("_points",)

    def __init__(self) -> None:
        self._points: list[tuple[int, Decimal]] = []

    def observe(self, at_ns: int, fair_value: Decimal) -> None:
        if fair_value <= 0:
            return
        if self._points and at_ns < self._points[-1][0]:
            return  # out-of-order sample; the collector records the reorder
        self._points.append((at_ns, fair_value))

    def value_at_or_after(self, at_ns: int) -> Decimal | None:
        lo, hi = 0, len(self._points)
        while lo < hi:
            mid = (lo + hi) // 2
            if self._points[mid][0] < at_ns:
                lo = mid + 1
            else:
                hi = mid
        if lo >= len(self._points):
            return None
        return self._points[lo][1]

    def __len__(self) -> int:
        return len(self._points)


def compute_markouts(
    *,
    track: FairValueTrack,
    at_ns: int,
    side: Side,
    fill_price: Decimal,
    fill_size_base: Decimal,
    fair_value_at_fill: Decimal,
    horizons_ms: Sequence[int],
    assumption: str,
) -> FillMarkouts:
    results: list[Markout] = []
    for horizon_ms in horizons_ms:
        later = track.value_at_or_after(at_ns + horizon_ms * 1_000_000)
        if later is None:
            continue  # horizon not covered by collected data; omitted, not zeroed
        results.append(Markout(horizon_ms, later, markout_bps(side, fill_price, later)))
    return FillMarkouts(
        at_ns=at_ns,
        side=side,
        fill_price=fill_price,
        fill_size_base=fill_size_base,
        fair_value_at_fill=fair_value_at_fill,
        assumption=assumption,
        markouts=tuple(results),
    )


def percentile(values: Sequence[Decimal], fraction: Decimal) -> Decimal | None:
    """Conservative nearest-rank percentile on exact Decimals.

    The rank is `floor(fraction * n) + 1` rather than the more common
    `ceil(fraction * n)`. The two differ only when `fraction * n` lands on an
    integer, and there the conventional rank excludes the tail entirely: with 20
    samples, a conventional p95 returns the 19th value and never sees the single
    worst one, which is precisely the observation a risk report exists to show.

    This convention errs toward the tail by at most one sample. For a statistic
    whose entire purpose is to characterise bad outcomes, understating is the
    dangerous direction and overstating is not.
    """
    if not values:
        return None
    ordered = sorted(values)
    rank = int((fraction * Decimal(len(ordered))).to_integral_value(rounding="ROUND_FLOOR")) + 1
    return ordered[max(0, min(len(ordered) - 1, rank - 1))]


@dataclass(frozen=True, slots=True)
class MarkoutStatistics:
    horizon_ms: int
    sample_count: int
    mean_bps: Decimal | None
    median_bps: Decimal | None
    p75_bps: Decimal | None
    p90_bps: Decimal | None
    p95_bps: Decimal | None
    worst_bps: Decimal | None

    def as_row(self) -> dict[str, object]:
        def fmt(value: Decimal | None) -> str:
            return "n/a" if value is None else f"{value:+.2f}"

        return {
            "horizon_ms": self.horizon_ms,
            "n": self.sample_count,
            "mean": fmt(self.mean_bps),
            "median": fmt(self.median_bps),
            "p75": fmt(self.p75_bps),
            "p90": fmt(self.p90_bps),
            "p95": fmt(self.p95_bps),
            "worst": fmt(self.worst_bps),
        }


def summarize(fills: Iterable[FillMarkouts], horizon_ms: int) -> MarkoutStatistics:
    """Percentiles are reported on the *unfavourable* tail.

    p95 here means "the 95th-percentile worst outcome", so a strategy is judged
    by how bad its bad fills were, not by how good its average looked. Averaging
    adverse selection is how a strategy that loses on 5% of fills and wins
    slightly on 95% gets published as profitable.
    """
    values = [m for fill in fills if (m := fill.at_horizon(horizon_ms)) is not None]
    if not values:
        return MarkoutStatistics(horizon_ms, 0, None, None, None, None, None, None)
    total = sum(values, Decimal(0))
    # Percentiles taken on negated values so p95 is the 95th-worst, then negated
    # back, keeping the "positive is good" convention in the output.
    negated = [-value for value in values]
    return MarkoutStatistics(
        horizon_ms=horizon_ms,
        sample_count=len(values),
        mean_bps=total / Decimal(len(values)),
        median_bps=median(values),
        p75_bps=-percentile(negated, Decimal("0.75")),
        p90_bps=-percentile(negated, Decimal("0.90")),
        p95_bps=-percentile(negated, Decimal("0.95")),
        worst_bps=min(values),
    )


def split_by_side(fills: Iterable[FillMarkouts]) -> dict[Side, list[FillMarkouts]]:
    """Bid and ask fills must be reported separately. A pair can be perfectly
    safe to bid and unsafe to offer - trending markets do exactly that - and a
    combined number averages the two into a conclusion that is wrong for both."""
    split: dict[Side, list[FillMarkouts]] = {Side.buy: [], Side.sell: []}
    for fill in fills:
        split[fill.side].append(fill)
    return split
