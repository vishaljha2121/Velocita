"""Pair scoring and the stage gates.

The scoring is deliberately hostile. A pair earns a status; it is never assigned
one by default. Three principles decide the outcome:

  1. **Unverified is disqualifying, not neutral.** A pair whose tick size, fee,
     or eligibility we inferred cannot be a RESEARCH-CANDIDATE, regardless of
     how attractive its spread looks. An attractive spread computed against a
     wrong fee is not an attractive spread.

  2. **Conservative assumptions decide.** A result that holds only under the
     optimistic queue assumption is a NO-GO. Reality is not obliged to give you
     front-of-queue.

  3. **Wide spread is not the signal.** The most common way to lose money making
     markets is to be delighted by a wide quoted spread on an illiquid pair and
     then be adversely selected on every fill. Markouts outrank spread here.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal
from enum import Enum


class PairStatus(str, Enum):
    reject = "REJECT"
    watchlist = "WATCHLIST"
    research_candidate = "RESEARCH-CANDIDATE"


class GateResult(str, Enum):
    go = "GO"
    no_go = "NO-GO"
    insufficient_data = "INSUFFICIENT-DATA"


@dataclass(frozen=True, slots=True)
class Gate:
    stage: str
    result: GateResult
    reason: str

    def __str__(self) -> str:
        return f"[{self.result.value}] {self.stage}: {self.reason}"


@dataclass(frozen=True, slots=True)
class PairEvidence:
    pair_id: str
    metadata_confidence: str
    venue_supports_resting_orders: bool | None
    collection_hours: Decimal
    observed_trade_count: int
    median_spread_bps: Decimal | None
    p25_spread_bps: Decimal | None
    top_of_book_depth_quote: Decimal | None
    near_book_depth_quote: Decimal | None
    realized_vol_30s_bps: Decimal | None
    data_quality: Decimal | None
    fill_opportunities_conservative: int
    fill_opportunities_optimistic: int
    markout_30s_bps_conservative: Decimal | None
    markout_30s_bps_p95: Decimal | None
    round_trip_cost_bps: Decimal | None
    completed_cycle_fraction: Decimal | None
    max_simulated_drawdown_quote: Decimal | None
    min_notional_quote: Decimal | None


@dataclass(frozen=True, slots=True)
class Scorecard:
    pair_id: str
    status: PairStatus
    reasons: tuple[str, ...]
    gates: tuple[Gate, ...]
    expected_gross_capture_bps: Decimal | None = None
    expected_net_bps: Decimal | None = None
    verified_facts: tuple[str, ...] = field(default_factory=tuple)
    unverified_facts: tuple[str, ...] = field(default_factory=tuple)


MIN_COLLECTION_HOURS = Decimal("48")
MIN_FILL_OPPORTUNITIES_STAGE2 = 100
MIN_FILL_OPPORTUNITIES_STAGE3 = 20
MIN_COMPLETED_CYCLES_STAGE5 = 50


def expected_net_bps(evidence: PairEvidence) -> Decimal | None:
    if evidence.median_spread_bps is None or evidence.round_trip_cost_bps is None:
        return None
    gross = evidence.median_spread_bps / Decimal(2)
    adverse = evidence.markout_30s_bps_conservative
    if adverse is None:
        return None
    adverse_cost = -adverse if adverse < 0 else Decimal(0)
    return gross - evidence.round_trip_cost_bps - adverse_cost


def evaluate_stage_1(evidence: PairEvidence) -> Gate:
    stage = "Stage 1 - market data collection"
    if evidence.venue_supports_resting_orders is not True:
        return Gate(
            stage,
            GateResult.no_go,
            "venue support for resting limit orders is not verified; a passive strategy "
            "cannot be expressed here regardless of pair quality",
        )
    if evidence.metadata_confidence != "verified":
        return Gate(stage, GateResult.no_go, f"pair metadata is {evidence.metadata_confidence}")
    if evidence.collection_hours < MIN_COLLECTION_HOURS:
        return Gate(
            stage,
            GateResult.insufficient_data,
            f"{evidence.collection_hours}h collected, {MIN_COLLECTION_HOURS}h required across UTC sessions",
        )
    if evidence.data_quality is None or evidence.data_quality < Decimal("0.95"):
        return Gate(stage, GateResult.no_go, f"feed quality {evidence.data_quality} below 0.95")
    if evidence.min_notional_quote is None:
        return Gate(stage, GateResult.no_go, "minimum notional is unknown; sizing cannot be validated")
    return Gate(stage, GateResult.go, "eligible, accessible, and feed quality is adequate")


def evaluate_stage_2(evidence: PairEvidence) -> Gate:
    stage = "Stage 2 - shadow quoting"
    if evidence.fill_opportunities_conservative < MIN_FILL_OPPORTUNITIES_STAGE2:
        return Gate(
            stage,
            GateResult.insufficient_data,
            f"{evidence.fill_opportunities_conservative} conservative fill opportunities, "
            f"{MIN_FILL_OPPORTUNITIES_STAGE2} required (these are modelled, not executed)",
        )
    net = expected_net_bps(evidence)
    if net is None:
        return Gate(stage, GateResult.insufficient_data, "cost or markout inputs are missing")
    if net <= 0:
        return Gate(
            stage,
            GateResult.no_go,
            f"conservative net edge {net:+.2f}bps does not exceed costs and adverse selection",
        )
    if evidence.fill_opportunities_optimistic > 0 and evidence.fill_opportunities_conservative == 0:
        return Gate(stage, GateResult.no_go, "result exists only under the optimistic queue assumption")
    return Gate(stage, GateResult.go, f"conservative net edge {net:+.2f}bps clears modelled costs")


def evaluate_stage_3(evidence: PairEvidence) -> Gate:
    stage = "Stage 3 - 1 USDC hypothetical canary"
    if evidence.fill_opportunities_conservative < MIN_FILL_OPPORTUNITIES_STAGE3:
        return Gate(
            stage,
            GateResult.insufficient_data,
            f"{evidence.fill_opportunities_conservative} modelled opportunities, "
            f"{MIN_FILL_OPPORTUNITIES_STAGE3} required",
        )
    if evidence.min_notional_quote is not None and evidence.min_notional_quote > Decimal(1):
        return Gate(
            stage,
            GateResult.no_go,
            f"venue minimum notional {evidence.min_notional_quote} exceeds the 1 USDC research size",
        )
    return Gate(stage, GateResult.go, "1 USDC size is expressible and has enough modelled evidence")


def evaluate_stage_5(evidence: PairEvidence) -> Gate:
    stage = "Stage 5 - two-sided hypothetical quoting"
    if evidence.completed_cycle_fraction is None:
        return Gate(stage, GateResult.insufficient_data, "cycle completion was not measured")
    if evidence.completed_cycle_fraction < Decimal("0.5"):
        return Gate(
            stage,
            GateResult.no_go,
            f"only {evidence.completed_cycle_fraction:.0%} of modelled cycles complete both sides; "
            "one-sided inventory risk dominates",
        )
    if evidence.markout_30s_bps_p95 is not None and evidence.median_spread_bps is not None:
        gross = evidence.median_spread_bps / Decimal(2)
        if -evidence.markout_30s_bps_p95 > gross:
            return Gate(
                stage,
                GateResult.no_go,
                f"p95 adverse selection {evidence.markout_30s_bps_p95:+.2f}bps exceeds "
                f"gross capture {gross:.2f}bps",
            )
    return Gate(stage, GateResult.go, "two-sided cycles complete and tail adverse selection is bounded")


def score_pair(evidence: PairEvidence) -> Scorecard:
    gates = (
        evaluate_stage_1(evidence),
        evaluate_stage_2(evidence),
        evaluate_stage_3(evidence),
        evaluate_stage_5(evidence),
    )
    reasons = [str(gate) for gate in gates]

    verified: list[str] = []
    unverified: list[str] = []
    (verified if evidence.metadata_confidence == "verified" else unverified).append(
        f"pair metadata ({evidence.metadata_confidence})"
    )
    (verified if evidence.venue_supports_resting_orders is True else unverified).append(
        "venue supports resting limit orders"
    )

    if any(gate.result == GateResult.no_go for gate in gates):
        status = PairStatus.reject
    elif any(gate.result == GateResult.insufficient_data for gate in gates):
        status = PairStatus.watchlist
    else:
        status = PairStatus.research_candidate

    net = expected_net_bps(evidence)
    gross = evidence.median_spread_bps / Decimal(2) if evidence.median_spread_bps is not None else None
    return Scorecard(
        pair_id=evidence.pair_id,
        status=status,
        reasons=tuple(reasons),
        gates=gates,
        expected_gross_capture_bps=gross,
        expected_net_bps=net,
        verified_facts=tuple(verified),
        unverified_facts=tuple(unverified),
    )


def rank(scorecards: list[Scorecard]) -> list[Scorecard]:
    order = {PairStatus.research_candidate: 0, PairStatus.watchlist: 1, PairStatus.reject: 2}
    return sorted(
        scorecards,
        key=lambda card: (
            order[card.status],
            -(card.expected_net_bps if card.expected_net_bps is not None else Decimal(-999)),
        ),
    )
