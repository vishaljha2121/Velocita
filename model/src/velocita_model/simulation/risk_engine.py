"""Research kill switches and the simulated capital floor.

Two distinct kinds of stop live here, and conflating them would be a serious
misreading:

  * **Data-quality stops** protect the *research*. A gapped feed, an
    unsynchronised clock, or a thermally throttled Pi produces measurements that
    are wrong in ways no later analysis can repair. The correct response is to
    stop producing frames and mark the interval, never to interpolate.

  * **Simulated capital floors** bound the *study*. They stop the simulator when
    hypothetical losses exceed the budget being modelled. They are study
    boundaries. They are not live-trading protection and provide no guarantee
    whatsoever about a real account.

The C++ kernel has the real equivalent of the second kind: replicated risk
reservations checked atomically on apply. Nothing here substitutes for it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal
from enum import Enum


class StopKind(str, Enum):
    stale_data = "STALE_DATA"
    sequence_gap = "SEQUENCE_GAP"
    feed_error = "FEED_ERROR"
    reference_divergence = "REFERENCE_DIVERGENCE"
    extreme_volatility = "EXTREME_VOLATILITY"
    depth_collapse = "DEPTH_COLLAPSE"
    clock_unsynchronised = "CLOCK_UNSYNCHRONISED"
    host_resource = "HOST_RESOURCE"
    manual_kill = "MANUAL_KILL"
    stopped_by_risk = "STOPPED_BY_RISK"


@dataclass(frozen=True, slots=True)
class Stop:
    kind: StopKind
    pair_id: str | None
    detail: str
    at_ns: int


@dataclass(frozen=True, slots=True)
class ResearchCapitalFloor:
    starting_equity_quote: Decimal = Decimal("20.00")
    daily_drawdown_limit_quote: Decimal = Decimal("0.50")
    total_drawdown_limit_quote: Decimal = Decimal("1.00")
    equity_floor_quote: Decimal = Decimal("19.00")


@dataclass(frozen=True, slots=True)
class HostLimits:
    max_cpu_percent: float = 85.0
    max_disk_percent: float = 90.0
    max_clock_offset_ms: float = 250.0
    max_market_data_age_ns: int = 2_000_000_000
    max_reference_divergence_bps: Decimal = Decimal("200")
    extreme_volatility_bps: Decimal = Decimal("500")
    min_depth_quote: Decimal = Decimal("5.00")


@dataclass(slots=True)
class RiskEngine:
    floor: ResearchCapitalFloor = field(default_factory=ResearchCapitalFloor)
    limits: HostLimits = field(default_factory=HostLimits)
    _paused_pairs: dict[str, Stop] = field(default_factory=dict, repr=False)
    _global_stop: Stop | None = field(default=None, repr=False)
    _peak_equity_quote: Decimal | None = field(default=None, repr=False)
    _session_start_equity_quote: Decimal | None = field(default=None, repr=False)
    _stops: list[Stop] = field(default_factory=list, repr=False)

    @property
    def stops(self) -> tuple[Stop, ...]:
        return tuple(self._stops)

    @property
    def global_stop(self) -> Stop | None:
        return self._global_stop

    def is_paused(self, pair_id: str) -> bool:
        return self._global_stop is not None or pair_id in self._paused_pairs

    def pause_reason(self, pair_id: str) -> Stop | None:
        return self._global_stop or self._paused_pairs.get(pair_id)

    def _raise_stop(self, stop: Stop, *, is_global: bool) -> Stop:
        self._stops.append(stop)
        if is_global:
            self._global_stop = stop
        else:
            assert stop.pair_id is not None
            self._paused_pairs[stop.pair_id] = stop
        return stop

    def resume(self, pair_id: str) -> None:
        self._paused_pairs.pop(pair_id, None)

    def manual_kill(self, at_ns: int, detail: str = "operator kill switch") -> Stop:
        return self._raise_stop(Stop(StopKind.manual_kill, None, detail, at_ns), is_global=True)


    def check_market_data(
        self,
        *,
        pair_id: str,
        at_ns: int,
        data_age_ns: int,
        sequence_gap: bool,
        volatility_bps: Decimal | None,
        near_depth_quote: Decimal | None,
        reference_divergence_bps: Decimal | None,
    ) -> Stop | None:
        if data_age_ns > self.limits.max_market_data_age_ns:
            return self._raise_stop(
                Stop(StopKind.stale_data, pair_id, f"data age {data_age_ns/1e9:.2f}s", at_ns), is_global=False
            )
        if sequence_gap:
            return self._raise_stop(
                Stop(StopKind.sequence_gap, pair_id, "source sequence gap detected", at_ns), is_global=False
            )
        if volatility_bps is not None and volatility_bps > self.limits.extreme_volatility_bps:
            return self._raise_stop(
                Stop(StopKind.extreme_volatility, pair_id, f"{volatility_bps:.1f}bps", at_ns), is_global=False
            )
        if near_depth_quote is not None and near_depth_quote < self.limits.min_depth_quote:
            return self._raise_stop(
                Stop(StopKind.depth_collapse, pair_id, f"near depth {near_depth_quote:.2f} quote", at_ns),
                is_global=False,
            )
        if (
            reference_divergence_bps is not None
            and abs(reference_divergence_bps) > self.limits.max_reference_divergence_bps
        ):
            return self._raise_stop(
                Stop(
                    StopKind.reference_divergence,
                    pair_id,
                    f"{reference_divergence_bps:+.1f}bps from reference",
                    at_ns,
                ),
                is_global=False,
            )
        return None

    def check_host(
        self,
        *,
        at_ns: int,
        cpu_percent: float | None,
        disk_percent: float | None,
        clock_synced: bool,
        clock_offset_ms: float | None,
    ) -> Stop | None:
        if not clock_synced:
            return self._raise_stop(
                Stop(StopKind.clock_unsynchronised, None, "host clock is not synchronised", at_ns), is_global=True
            )
        if clock_offset_ms is not None and abs(clock_offset_ms) > self.limits.max_clock_offset_ms:
            return self._raise_stop(
                Stop(StopKind.clock_unsynchronised, None, f"clock offset {clock_offset_ms:.0f}ms", at_ns),
                is_global=True,
            )
        if disk_percent is not None and disk_percent > self.limits.max_disk_percent:
            return self._raise_stop(
                Stop(StopKind.host_resource, None, f"disk {disk_percent:.0f}% full", at_ns), is_global=True
            )
        if cpu_percent is not None and cpu_percent > self.limits.max_cpu_percent:
            return self._raise_stop(
                Stop(StopKind.host_resource, None, f"cpu {cpu_percent:.0f}%", at_ns), is_global=True
            )
        return None


    def check_simulated_equity(self, *, pair_id: str, at_ns: int, simulated_equity_quote: Decimal) -> Stop | None:
        if self._session_start_equity_quote is None:
            self._session_start_equity_quote = simulated_equity_quote
        self._peak_equity_quote = (
            simulated_equity_quote
            if self._peak_equity_quote is None
            else max(self._peak_equity_quote, simulated_equity_quote)
        )

        if simulated_equity_quote < self.floor.equity_floor_quote:
            return self._raise_stop(
                Stop(
                    StopKind.stopped_by_risk,
                    pair_id,
                    f"simulated equity {simulated_equity_quote:.4f} below floor {self.floor.equity_floor_quote}",
                    at_ns,
                ),
                is_global=False,
            )
        daily = self._session_start_equity_quote - simulated_equity_quote
        if daily > self.floor.daily_drawdown_limit_quote:
            return self._raise_stop(
                Stop(
                    StopKind.stopped_by_risk,
                    pair_id,
                    f"simulated session drawdown {daily:.4f} exceeds {self.floor.daily_drawdown_limit_quote}",
                    at_ns,
                ),
                is_global=False,
            )
        total = self.floor.starting_equity_quote - simulated_equity_quote
        if total > self.floor.total_drawdown_limit_quote:
            return self._raise_stop(
                Stop(
                    StopKind.stopped_by_risk,
                    pair_id,
                    f"simulated total drawdown {total:.4f} exceeds {self.floor.total_drawdown_limit_quote}",
                    at_ns,
                ),
                is_global=False,
            )
        return None
