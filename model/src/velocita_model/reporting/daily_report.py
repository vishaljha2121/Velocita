"""Markdown and CSV reporting.

Two rules govern the output:

  * Every number carries its provenance and its assumption. A fill count without
    its queue assumption, or a spread without its confidence, is not printed.
  * The default conclusion is NO LIVE TRADING RECOMMENDED, and it is overturned
    only by gates that actually passed - never by the absence of a failure.
"""

from __future__ import annotations

import csv
import io
from dataclasses import dataclass
from datetime import datetime, timezone
from decimal import Decimal
from typing import Iterable, Sequence

from ..adapters.base import VenueCapabilities
from ..calibration.pair_ranker import GateResult, PairStatus, Scorecard
from ..simulation.markouts import MarkoutStatistics

DISCLAIMER = (
    "Every quote, fill, and profit figure in this report is a model output produced from "
    "recorded market data. No order was created, sent, signed, or broadcast, and this "
    "system has no code path that could do so. A favourable simulation is not evidence "
    "that a live strategy would be profitable."
)


def _utc(at_ns: int) -> str:
    return datetime.fromtimestamp(at_ns / 1e9, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")


def _fmt(value: Decimal | float | None, spec: str = ".2f") -> str:
    return "n/a" if value is None else format(value, spec)


@dataclass(frozen=True, slots=True)
class SystemHealthSummary:
    uptime_seconds: float
    feed_uptime_fraction: Decimal | None
    disconnect_count: int
    sequence_gap_count: int
    max_data_staleness_ms: float | None
    cpu_percent_max: float | None
    mem_percent_max: float | None
    disk_percent: float | None
    db_bytes: int
    clock_synced: bool
    throttled_observed: bool
    temp_celsius_max: float | None


def render_daily_report(
    *,
    generated_at_ns: int,
    session_window: tuple[int, int],
    config_digest: str,
    schema_revision: str,
    schema_digest: str,
    code_version: str,
    capabilities: VenueCapabilities,
    health: SystemHealthSummary,
    scorecards: Sequence[Scorecard],
    markouts: Sequence[tuple[str, MarkoutStatistics]],
    fill_ranges: Sequence[tuple[str, int, int]],
    risk_events: Sequence[tuple[str, str, str]],
) -> str:
    out: list[str] = []
    add = out.append

    add("# Velocita model - daily research report")
    add("")
    add(f"Generated {_utc(generated_at_ns)} covering {_utc(session_window[0])} to {_utc(session_window[1])}.")
    add("")
    add(f"> {DISCLAIMER}")
    add("")
    add("## Attribution")
    add("")
    add("| Item | Value |")
    add("| --- | --- |")
    add(f"| Config digest | `{config_digest}` |")
    add(f"| Feature schema | {schema_revision} (`{schema_digest[:16]}...`) |")
    add(f"| Code version | {code_version} |")
    add("")

    expressible, capability_reason = capabilities.passive_strategy_is_expressible()
    add("## Venue capability")
    add("")
    add(f"**Passive strategy expressible on {capabilities.venue}: {'YES' if expressible else 'NO'}** "
        f"- {capability_reason}")
    add("")
    add("| Capability | Value | Confidence | Source |")
    add("| --- | --- | --- | --- |")
    for item in capabilities.all_fields():
        source = f"[link]({item.source_url})" if item.source_url else "-"
        add(f"| {item.field} | {item.value} | {item.confidence} | {source} |")
    if capabilities.notes:
        add("")
        for note in capabilities.notes:
            add(f"- {note}")
    add("")

    add("## System health")
    add("")
    add("| Metric | Value |")
    add("| --- | --- |")
    add(f"| Uptime | {health.uptime_seconds / 3600:.1f} h |")
    add(f"| Feed uptime | {_fmt(health.feed_uptime_fraction * 100 if health.feed_uptime_fraction is not None else None)}% |")
    add(f"| Disconnects | {health.disconnect_count} |")
    add(f"| Sequence gaps | {health.sequence_gap_count} |")
    add(f"| Max data staleness | {_fmt(health.max_data_staleness_ms, '.0f')} ms |")
    add(f"| CPU peak | {_fmt(health.cpu_percent_max, '.0f')}% |")
    add(f"| Memory peak | {_fmt(health.mem_percent_max, '.0f')}% |")
    add(f"| Disk | {_fmt(health.disk_percent, '.0f')}% |")
    add(f"| Database size | {health.db_bytes / 1e6:.1f} MB |")
    add(f"| Clock synchronised | {'yes' if health.clock_synced else '**NO - measurements suspect**'} |")
    add(f"| Firmware throttling observed | {'**YES**' if health.throttled_observed else 'no'} |")
    add(f"| Peak temperature | {_fmt(health.temp_celsius_max, '.1f')} C |")
    add("")
    if not health.clock_synced or health.throttled_observed:
        add("> Latency, staleness, and markout horizons in this report are wall-clock "
            "differences. An unsynchronised clock or a throttled interval makes them "
            "unreliable, and conclusions drawn from this window should be discarded.")
        add("")

    add("## Pair scorecards")
    add("")
    add("| Pair | Status | Gross capture (bps) | Conservative net (bps) | Blocking gate |")
    add("| --- | --- | --- | --- | --- |")
    for card in scorecards:
        blocking = next(
            (gate for gate in card.gates if gate.result != GateResult.go),
            None,
        )
        blocker = f"{blocking.stage}: {blocking.reason}" if blocking else "-"
        add(
            f"| {card.pair_id} | **{card.status.value}** | "
            f"{_fmt(card.expected_gross_capture_bps)} | {_fmt(card.expected_net_bps)} | {blocker} |"
        )
    add("")

    add("## Modelled fill opportunities")
    add("")
    add("Ranges, not point estimates. Where queue position cannot be observed, the range "
        "*is* the answer; quoting only the optimistic bound misrepresents the study.")
    add("")
    add("| Pair | Conservative | Optimistic |")
    add("| --- | --- | --- |")
    for pair_id, low, high in fill_ranges:
        add(f"| {pair_id} | {low} | {high} |")
    add("")

    add("## Adverse selection (fill-conditioned markouts)")
    add("")
    add("Positive is favourable to us. Percentiles are on the unfavourable tail, so p95 "
        "is the 95th-percentile *worst* outcome.")
    add("")
    add("| Pair | Horizon | n | Mean | Median | p75 | p90 | p95 | Worst |")
    add("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for pair_id, stats in markouts:
        row = stats.as_row()
        add(
            f"| {pair_id} | {row['horizon_ms']}ms | {row['n']} | {row['mean']} | {row['median']} | "
            f"{row['p75']} | {row['p90']} | {row['p95']} | {row['worst']} |"
        )
    add("")

    if risk_events:
        add("## Risk and data-quality events")
        add("")
        add("| When | Kind | Detail |")
        add("| --- | --- | --- |")
        for when, kind, detail in risk_events:
            add(f"| {when} | {kind} | {detail} |")
        add("")

    add("## Decision")
    add("")
    candidates = [c for c in scorecards if c.status == PairStatus.research_candidate]
    if not expressible:
        add("**NO LIVE TRADING RECOMMENDED.** The venue surface available to this account "
            "has not been shown to support resting passive quotes. Until that is verified, "
            "a market-making strategy is not expressible here and per-pair statistics "
            "describe a strategy that cannot be run.")
    elif not candidates:
        add("**NO LIVE TRADING RECOMMENDED.** No pair passed every gate.")
    else:
        add("**NO LIVE TRADING RECOMMENDED.** The following pairs passed the modelled gates "
            "and warrant further study, which is a different statement from being ready to trade:")
        add("")
        for card in candidates:
            add(f"- {card.pair_id}: conservative net {_fmt(card.expected_net_bps)} bps")
        add("")
        add("Passing every gate means the simulation did not find a disqualifying reason in "
            "the data collected. It does not mean the assumptions behind the simulation - "
            "queue behaviour, fee schedule, gas cost, fill classification - have been "
            "confirmed against live venue behaviour.")
    add("")

    add("## Unverified inputs")
    add("")
    unverified = capabilities.unverified_fields()
    if unverified:
        for item in unverified:
            add(f"- `{item.field}` is **{item.confidence}**")
    else:
        add("- none")
    add("")
    return "\n".join(out)


def render_scorecard_csv(scorecards: Iterable[Scorecard]) -> str:
    buffer = io.StringIO()
    writer = csv.writer(buffer)
    writer.writerow(
        ["pair_id", "status", "gross_capture_bps", "conservative_net_bps", "blocking_gate", "reason"]
    )
    for card in scorecards:
        blocking = next((gate for gate in card.gates if gate.result != GateResult.go), None)
        writer.writerow(
            [
                card.pair_id,
                card.status.value,
                "" if card.expected_gross_capture_bps is None else f"{card.expected_gross_capture_bps:.4f}",
                "" if card.expected_net_bps is None else f"{card.expected_net_bps:.4f}",
                blocking.stage if blocking else "",
                blocking.reason if blocking else "",
            ]
        )
    return buffer.getvalue()


def render_competition_worksheet(
    *,
    target_rank: int,
    target_volume_quote: Decimal,
    hours_remaining: Decimal,
    maker_multiplier: Decimal | None,
    multiplier_confidence: str,
    quote_sizes_quote: Sequence[Decimal],
) -> str:
    out: list[str] = []
    add = out.append
    add("# Leaderboard feasibility worksheet")
    add("")
    add(f"Target: rank {target_rank} at {target_volume_quote} quote-currency volume, "
        f"{hours_remaining} hours remaining.")
    add("")
    add(f"Maker multiplier: {maker_multiplier if maker_multiplier is not None else 'unknown'} "
        f"(**{multiplier_confidence}**)")
    add("")
    if multiplier_confidence != "verified":
        add("> The multiplier and the threshold semantics are unverified. Both the "
            "adjusted-volume and raw-volume readings are shown because it is not known "
            "which one the leaderboard ranks on. Choosing one would be a guess presented "
            "as a plan.")
        add("")

    for label, effective_target in (
        ("raw volume", target_volume_quote),
        (
            "adjusted volume",
            target_volume_quote / maker_multiplier if maker_multiplier and maker_multiplier > 0 else None,
        ),
    ):
        if effective_target is None:
            continue
        add(f"## Required activity under a {label} reading")
        add("")
        add("| Quote size | Fills required | Fills per hour |")
        add("| --- | --- | --- |")
        for size in quote_sizes_quote:
            if size <= 0:
                continue
            fills = effective_target / size
            per_hour = fills / hours_remaining if hours_remaining > 0 else Decimal(0)
            add(f"| {size} | {fills:,.0f} | {per_hour:,.1f} |")
        add("")

    add("These are hypothetical fills. Nothing in this system generates volume, and no "
        "figure here should be read as a claim that the target is reachable, that "
        "qualification is assured, or that the activity would be profitable.")
    add("")
    return "\n".join(out)
