"""The shadow edge: True Markets reference prices -> rule features -> the C++
shadow submitter (`velocita-v2-shadow-submitter`) -> decisions and metrics.

* A tick is one `reference_prices` update for SYMBOL (default BTC|USD). Its
  evidence is the SHA-256 of the raw message, so every decision the cluster
  records names the exact bytes it was computed from.
* Features come from `research.rules.features` - the function the backtests
  used - on daily closes up to yesterday plus the live price as today's
  provisional close. History must end exactly yesterday: a gap would make every
  moving-average window count the wrong days, so the tick is refused instead.
* The price feature is converted to ticks with exact decimals using the
  catalog's USD-per-tick; the program file is generated from the same Python
  rule program, so research and cluster cannot drift.
* Agreement: the Python rule engine's match is compared with the cluster's on
  every decision and counted, never hidden (M3a).
* Volume strategies (`volume.py`) append one feature after the four daily ones;
  the price is always the last feature. In paper mode the paper book
  (`paper_book.py`) may add `exits_only=1` to a TICK line (the weekly loss stop).
"""

from __future__ import annotations

import hashlib
import json
import random
import re
import select
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import date, datetime, timedelta, timezone
from decimal import ROUND_DOWN, Decimal, InvalidOperation
from typing import Callable, Iterable, Sequence

from ..adapters.websocket import HandshakeRefused, ProtocolError
from ..collectors.market_stream import publish
from ..contracts.enums import PredicateOperator
from ..research import rules
from ..research.bars import DailyBars
from . import volume

NO_RULE = 65535
SHADOW_RECORDED = 17
ACTION = 1
PROPOSED = 1
DIRECTIVES = {1: "no_action", 2: "place_limit", 3: "cancel", 4: "replace"}
FEATURE_NAMES = ("trend", "ret1", "vol", "dev")
_OPS = {PredicateOperator.less_than: "lt", PredicateOperator.less_than_or_equal: "le",
        PredicateOperator.equal: "eq", PredicateOperator.greater_than_or_equal: "ge",
        PredicateOperator.greater_than: "gt"}
_ACTIONS = {rules.Action.ENTER_LONG: "enter_long", rules.Action.EXIT: "exit", rules.Action.HOLD: "hold"}


@dataclass(frozen=True)
class CatalogRow:
    root: str
    account: int
    venue: int
    instrument: int
    instrument_spec_version: int
    venue_capabilities_version: int


def model_digest(strategy: str, value: int) -> str:
    return hashlib.sha256(f"velocita.rules.v1 {strategy} {value}".encode()).hexdigest()


def strategy_program(strategy: str, value: int,
                     params: volume.Params | None = None) -> tuple[list[rules.Rule], rules.Settings]:
    if strategy in volume.STRATEGIES:
        return volume.program(strategy, value, params or volume.Params())
    return rules.DEMOS[strategy][0](value)


def feature_names(strategy: str) -> tuple[str, ...]:
    spec = volume.STRATEGIES.get(strategy)
    return FEATURE_NAMES + ((spec.feature,) if spec else ())


def program_text(strategy: str, value: int, catalog: CatalogRow, *, policy_id: int, policy_version: int,
                 feature_schema_id: int, valid_from_ns: int, valid_until_ns: int, order_quantity_lots: int,
                 max_notional_micros: int, max_age_ns: int, source_id: int, time_in_force: str,
                 post_only: str, params: volume.Params | None = None) -> str:
    program, _settings = strategy_program(strategy, value, params)
    if strategy in volume.STRATEGIES:
        params = params or volume.Params()
        header = f"# generated from {volume.model_text(strategy, value, params)}"
        digest = volume.model_digest(strategy, value, params)
    else:
        header, digest = f"# generated from research.rules.{strategy}({value})", model_digest(strategy, value)
    lines = [header,
             f"catalog.root = {catalog.root}", f"catalog.account = {catalog.account}",
             f"catalog.venue = {catalog.venue}", f"catalog.instrument = {catalog.instrument}",
             f"catalog.instrument_spec_version = {catalog.instrument_spec_version}",
             f"catalog.venue_capabilities_version = {catalog.venue_capabilities_version}",
             f"policy.id = {policy_id}", f"policy.version = {policy_version}",
             f"policy.feature_schema_id = {feature_schema_id}", "policy.risk_version = 1",
             f"policy.model_digest = {digest}",
             f"policy.valid_from_ns = {valid_from_ns}", f"policy.valid_until_ns = {valid_until_ns}",
             f"features.generic = {len(feature_names(strategy))}"]
    lines += [f"rule.{i + 1} = {r.feature} {_OPS[r.op]} {r.threshold} {_ACTIONS[r.action]}" for i, r in enumerate(program)]
    lines += [f"order.time_in_force = {time_in_force}", f"order.post_only = {post_only}",
              f"order.quantity_lots = {order_quantity_lots}", f"order.max_notional_micros = {max_notional_micros}",
              f"order.max_market_data_age_ns = {max_age_ns}", f"source.id = {source_id}"]
    return "\n".join(lines) + "\n"


def price_ticks(price: Decimal, usd_per_tick: Decimal) -> int:
    return int((price / usd_per_tick).to_integral_value(rounding=ROUND_DOWN))


def rfc3339_ns(text: str) -> int:
    stamp = datetime.fromisoformat(text.replace("Z", "+00:00"))
    if stamp.tzinfo is None:
        raise ValueError("timestamp without a zone")
    whole = int(stamp.replace(microsecond=0).timestamp())
    return whole * 1_000_000_000 + stamp.microsecond * 1000


@dataclass
class Tick:
    tick_id: int
    event_ns: int
    receive_ns: int
    age_ns: int
    features: list[int]
    evidence: str
    expected_rule: int | None
    exits_only: bool = False

    def line(self) -> str:
        return (f"TICK id={self.tick_id} event_ns={self.event_ns} receive_ns={self.receive_ns} age_ns={self.age_ns} "
                f"features={','.join(map(str, self.features))} evidence={self.evidence}"
                + (" exits_only=1" if self.exits_only else ""))


class FeatureSource:
    def __init__(self, strategy: str, value: int, *, closes: Sequence[tuple[date, float]], usd_per_tick: Decimal,
                 symbol: str = "BTC|USD", min_interval_ns: int = 0, params: volume.Params | None = None,
                 window: volume.MinuteWindow | None = None, price_source: str = "reference",
                 price_chain: str | None = None) -> None:
        if price_source not in ("reference", "candles"):
            raise ValueError(f"unknown price source {price_source!r}")
        if price_source == "candles" and not price_chain:
            raise ValueError("candle prices need the chain that is traded")
        self.price_source = price_source
        self.price_chain = price_chain
        self.program, self.settings = strategy_program(strategy, value, params)
        self.feature_names = feature_names(strategy)
        spec = volume.STRATEGIES.get(strategy)
        self.extra = spec.feature if spec else None
        self.params = volume.resolved(strategy, params or volume.Params()) if spec else None
        if window is None and self.extra == "mdev":
            window = volume.MinuteWindow(self.params.window_min)
        self.window = window
        self.closes = list(closes)
        self.usd_per_tick = usd_per_tick
        self.symbol = symbol
        self.last_event_ns = -1
        self.min_interval_ns = min_interval_ns
        self.last_forward_ns: int | None = None
        self.last_outcome = ""

    def tick(self, raw: str, *, receive_ns: int, tick_id: int) -> Tick | None:
        self.last_outcome, tick = self._classify(raw, receive_ns, tick_id)
        return tick

    def _classify(self, raw: str, receive_ns: int, tick_id: int) -> tuple[str, Tick | None]:
        try:
            message = json.loads(raw)
        except ValueError:
            return "other", None
        if self.price_source == "candles":
            if message.get("type") != "price_candles":
                return "other", None
            row = next((r for r in message.get("data") or () if isinstance(r, dict) and r.get("symbol") == self.symbol
                        and r.get("chain") == self.price_chain and r.get("interval") == "30s"), None)
            if row is None:
                return "other", None
            try:
                mid = Decimal(row["close"])
                stamp = message["timestamp"]
                if not isinstance(stamp, int) or isinstance(stamp, bool) or stamp <= 0:
                    raise ValueError("candle message without a whole-second stamp")
                event_ns = stamp * 1_000_000_000
            except (KeyError, ValueError, TypeError, InvalidOperation):
                return "other", None
        else:
            if message.get("type") != "reference_prices":
                return "other", None
            row = next((r for r in message.get("data") or () if isinstance(r, dict) and r.get("symbol") == self.symbol),
                       None)
            if row is None:
                return "other", None
            try:
                mid = Decimal(row["mid_price"])
                event_ns = rfc3339_ns(row["timestamp"])
            except (KeyError, ValueError, TypeError, InvalidOperation):
                return "other", None
        if self.window is not None:
            self.window.observe(event_ns, mid)
        if event_ns <= self.last_event_ns:
            return "resend", None
        if self.last_forward_ns is not None and receive_ns - self.last_forward_ns < self.min_interval_ns:
            return "throttled", None
        day = datetime.fromtimestamp(event_ns // 1_000_000_000, timezone.utc).date()
        history = [(d, c) for d, c in self.closes if d < day]
        if not history or history[-1][0] != day - timedelta(days=1):
            return "no_history", None
        dates = tuple(d for d, _ in history) + (day,)
        bars = DailyBars(dates, ("X",), (tuple(c for _, c in history) + (float(mid),),), None)
        feats = rules.features(bars, 0, len(history), self.settings)
        if feats is None:
            return "no_history", None
        if self.extra is not None:
            extra = self._extra(event_ns)
            if extra is None:
                return "warming_up", None
            feats = tuple(feats) + (extra,)
        self.last_event_ns = event_ns
        self.last_forward_ns = receive_ns
        return "tick", Tick(tick_id, event_ns, receive_ns, max(0, receive_ns - event_ns),
                    list(feats) + [price_ticks(mid, self.usd_per_tick)],
                    hashlib.sha256(raw.encode("utf-8")).hexdigest(), rules.first_match(self.program, feats))

    def _extra(self, event_ns: int) -> int | None:
        if self.extra == "phase":
            return volume.phase(event_ns, self.params.period_s)
        if self.extra == "rotation":
            return volume.rotation(event_ns, self.params.period_s)
        return self.window.deviation_micros()


_TEXT_FIELDS = ("id", "authorization", "mode")


def parse_decision(line: str) -> dict[str, int | str]:
    fields = dict(part.split("=", 1) for part in line.split()[1:] if "=" in part)
    return {k: (v if k in _TEXT_FIELDS else int(v)) for k, v in fields.items()}


_BUCKETS_MS = (1, 2, 5, 10, 20, 50, 100, 250, 500, 1000, 2500)
_SUBMITTER_REASONS = frozenset({"bad_tick", "source", "decision", "authorization", "setup"})


@dataclass
class EdgeMetrics:
    shadow_recorded_code: int
    ticks: int = 0
    errors: int = 0
    submitter_ready: int = 0
    submitter_starts: int = 0
    decisions: dict[int, int] = field(default_factory=dict)
    resolutions: dict[int, int] = field(default_factory=dict)
    last_age_ms: float = 0.0
    age_sum_ms: float = 0.0
    rules_matched: dict[str, int] = field(default_factory=dict)
    agreement: dict[str, int] = field(default_factory=lambda: {"agree": 0, "disagree": 0})
    hop_sum_ms: dict[str, float] = field(default_factory=lambda: {"edge": 0.0, "source_commit": 0.0,
                                                                   "decision_commit": 0.0})
    total_sum_ms: float = 0.0
    total_count: int = 0
    buckets: list[int] = field(default_factory=lambda: [0] * len(_BUCKETS_MS))
    last_decision_ns: int = 0
    messages: dict[str, int] = field(default_factory=dict)
    directives: dict[tuple[str, str], int] = field(default_factory=dict)
    last_features: tuple[int, ...] = ()
    feature_names: tuple[str, ...] = FEATURE_NAMES
    last_price_usd: Decimal | None = None
    paper: bool = False
    authorizations: dict[str, int] = field(default_factory=dict)
    resyncs: int = 0
    risk_version: int | None = None
    position_version: int | None = None
    position_lots: int | None = None
    market_data_connected: int = 0
    market_data_errors: dict[str, int] = field(default_factory=dict)
    submitter_errors: dict[tuple[str, str], int] = field(default_factory=dict)

    def observe_submitter_error(self, line: str) -> None:
        self.errors += 1
        fields = dict(part.split("=", 1) for part in line.split()[1:] if "=" in part)
        reason, code = fields.get("reason", ""), fields.get("code", "")
        key = (reason if reason in _SUBMITTER_REASONS else "other",
               code if re.fullmatch(r"-?\d{1,3}", code) else "none")
        self.submitter_errors[key] = self.submitter_errors.get(key, 0) + 1

    def observe_message(self, outcome: str, tick: Tick | None, usd_per_tick: Decimal) -> None:
        self.messages[outcome] = self.messages.get(outcome, 0) + 1
        if tick is not None:
            self.last_features = tuple(tick.features[:-1])
            self.last_price_usd = tick.features[-1] * usd_per_tick

    def observe(self, d: dict, *, tick_receive_ns: int, expected_rule: int | None, age_ns: int = 0) -> None:
        code = int(d["decision_code"])
        if "resolution" in d:
            resolution = int(d["resolution"])
            self.resolutions[resolution] = self.resolutions.get(resolution, 0) + 1
        self.last_age_ms = age_ns / 1e6
        self.age_sum_ms += age_ns / 1e6
        self.decisions[code] = self.decisions.get(code, 0) + 1
        for stage in ("proposed", "effective"):
            if stage in d:
                key = (stage, DIRECTIVES.get(int(d[stage]), f"code_{int(d[stage])}"))
                self.directives[key] = self.directives.get(key, 0) + 1
        rule = int(d["rule"])
        self.rules_matched[str(rule)] = self.rules_matched.get(str(rule), 0) + 1
        cluster_rule = None if rule == NO_RULE else rule
        self.agreement["agree" if cluster_rule == expected_rule else "disagree"] += 1
        read, source, decision = int(d["t_read_ns"]), int(d["t_source_ns"]), int(d["t_decision_ns"])
        self.hop_sum_ms["edge"] += (read - tick_receive_ns) / 1e6
        self.hop_sum_ms["source_commit"] += (source - read) / 1e6
        self.hop_sum_ms["decision_commit"] += (decision - source) / 1e6
        total = (decision - tick_receive_ns) / 1e6
        self.total_sum_ms += total
        self.total_count += 1
        for i, bound in enumerate(_BUCKETS_MS):
            if total <= bound:
                self.buckets[i] += 1
        self.last_decision_ns = decision
        if "authorization" in d:
            self.paper = True
            self.risk_version, self.position_version = int(d["risk_version"]), int(d["position_version"])
            if d["authorization"] not in ("none", "withheld"):
                name = str(d["authorization"])
                self.authorizations[name] = self.authorizations.get(name, 0) + 1
            if int(d.get("resync", 0)):
                self.resyncs += 1
                self.position_lots = int(d["position_lots"])

    def render(self, *, now_ns: int) -> str:
        out = ["# TYPE velocita_shadow_submitter_ready gauge", f"velocita_shadow_submitter_ready {self.submitter_ready}",
               "# TYPE velocita_shadow_submitter_starts_total counter",
               f"velocita_shadow_submitter_starts_total {self.submitter_starts}",
               "# TYPE velocita_shadow_ticks_total counter", f"velocita_shadow_ticks_total {self.ticks}",
               "# TYPE velocita_shadow_errors_total counter", f"velocita_shadow_errors_total {self.errors}",
               "# TYPE velocita_shadow_decisions_total counter"]
        out += [f'velocita_shadow_decisions_total{{decision_code="{c}"}} {n}' for c, n in sorted(self.decisions.items())]
        out.append("# TYPE velocita_shadow_action_resolutions_total counter")
        out += [f'velocita_shadow_action_resolutions_total{{resolution="{c}"}} {n}'
                for c, n in sorted(self.resolutions.items())]
        out += ["# TYPE velocita_shadow_market_data_age_ms gauge", f"velocita_shadow_market_data_age_ms {self.last_age_ms:.3f}",
                "# TYPE velocita_shadow_market_data_age_ms_sum counter",
                f"velocita_shadow_market_data_age_ms_sum {self.age_sum_ms:.3f}"]
        out.append("# TYPE velocita_shadow_rule_matches_total counter")
        out += [f'velocita_shadow_rule_matches_total{{rule="{r}"}} {n}' for r, n in sorted(self.rules_matched.items())]
        out.append("# TYPE velocita_shadow_agreement_total counter")
        out += [f'velocita_shadow_agreement_total{{result="{k}"}} {v}' for k, v in sorted(self.agreement.items())]
        out.append("# TYPE velocita_shadow_hop_ms_sum counter")
        out += [f'velocita_shadow_hop_ms_sum{{hop="{h}"}} {v:.3f}' for h, v in self.hop_sum_ms.items()]
        out.append("# TYPE velocita_shadow_tick_to_decision_ms histogram")
        out += [f'velocita_shadow_tick_to_decision_ms_bucket{{le="{b}"}} {n}' for b, n in zip(_BUCKETS_MS, self.buckets)]
        out += [f'velocita_shadow_tick_to_decision_ms_bucket{{le="+Inf"}} {self.total_count}',
                f"velocita_shadow_tick_to_decision_ms_sum {self.total_sum_ms:.3f}",
                f"velocita_shadow_tick_to_decision_ms_count {self.total_count}"]
        out.append("# TYPE velocita_shadow_messages_total counter")
        out += [f'velocita_shadow_messages_total{{outcome="{k}"}} {n}' for k, n in sorted(self.messages.items())]
        out.append("# TYPE velocita_shadow_directives_total counter")
        out += [f'velocita_shadow_directives_total{{stage="{st}",directive="{dv}"}} {n}'
                for (st, dv), n in sorted(self.directives.items())]
        if self.last_features:
            out.append("# TYPE velocita_shadow_feature_micros gauge")
            out += [f'velocita_shadow_feature_micros{{name="{name}"}} {v}'
                    for name, v in zip(self.feature_names, self.last_features)]
        if self.last_price_usd is not None:
            out += ["# TYPE velocita_shadow_price_usd gauge", f"velocita_shadow_price_usd {self.last_price_usd}"]
        if self.last_decision_ns:
            out += ["# TYPE velocita_shadow_last_decision_age_seconds gauge",
                    f"velocita_shadow_last_decision_age_seconds {max(0, now_ns - self.last_decision_ns) / 1e9:.3f}"]
        if self.paper:
            out += ["# TYPE velocita_paper_mode gauge", "velocita_paper_mode 1",
                    "# TYPE velocita_paper_authorizations_total counter"]
            out += [f'velocita_paper_authorizations_total{{code="{c}"}} {n}' for c, n in sorted(self.authorizations.items())]
            out += ["# TYPE velocita_paper_resyncs_total counter", f"velocita_paper_resyncs_total {self.resyncs}"]
            if self.risk_version is not None:
                out += ["# TYPE velocita_paper_risk_version gauge", f"velocita_paper_risk_version {self.risk_version}",
                        "# TYPE velocita_paper_position_version gauge",
                        f"velocita_paper_position_version {self.position_version}"]
            if self.position_lots is not None:
                out += ["# TYPE velocita_paper_position_lots gauge", f"velocita_paper_position_lots {self.position_lots}"]
        out += ["# TYPE velocita_shadow_market_data_connected gauge",
                f"velocita_shadow_market_data_connected {self.market_data_connected}",
                "# TYPE velocita_shadow_market_data_errors_total counter"]
        out += [f'velocita_shadow_market_data_errors_total{{kind="{k}"}} {n}'
                for k, n in sorted(self.market_data_errors.items())]
        out.append("# TYPE velocita_shadow_submitter_errors_total counter")
        out += [f'velocita_shadow_submitter_errors_total{{reason="{r}",code="{c}"}} {n}'
                for (r, c), n in sorted(self.submitter_errors.items())]
        return "\n".join(out) + "\n"


def _read_line(process: subprocess.Popen, timeout_s: float) -> str | None:
    ready, _, _ = select.select([process.stdout], [], [], timeout_s)
    if not ready:
        return None
    line = process.stdout.readline()
    return line.rstrip("\n") if line else None


BACKOFF_CAP_S = 60.0
MARKET_DATA_ERRORS = (ProtocolError, OSError, UnicodeDecodeError)


def backoff_delay(failures: int, jitter: Callable[[], float] = random.random) -> float:
    step = min(BACKOFF_CAP_S, 2.0 ** min(failures - 1, 16))
    return step * (0.5 + 0.5 * jitter())


def market_data_error_kind(error: BaseException) -> str:
    if isinstance(error, HandshakeRefused):
        return "http_5xx" if error.status is not None and 500 <= error.status <= 599 else "handshake"
    if isinstance(error, TimeoutError):
        return "timeout"
    if isinstance(error, OSError):
        return "connection"
    return "protocol"


@dataclass
class RunResult:
    decisions: int = 0
    errors: int = 0
    reason: str = ""


def run_once(socket_factory: Callable[[], object], submitter_argv: Sequence[str], source: FeatureSource, *,
             textfile: str | None, shadow_recorded_code: int, clock_ns: Callable[[], int] = time.time_ns,
             metrics: EdgeMetrics | None = None, reply_timeout_s: float = 60.0,
             topics: Iterable[str] = ("reference_prices",), ledger=None, paper_book=None,
             reconnect: Callable[[], bool] | None = None, sleep: Callable[[float], None] | None = None,
             jitter: Callable[[], float] = random.random) -> RunResult:
    metrics = metrics or EdgeMetrics(shadow_recorded_code)
    metrics.feature_names = source.feature_names
    sleep = sleep or time.sleep
    result = RunResult()
    last_price: Decimal | None = None
    paper = False
    recorded_code = shadow_recorded_code
    next_id = 0

    def rendered() -> str:
        text = metrics.render(now_ns=clock_ns())
        text = text + ledger.render(last_price) if ledger is not None and last_price is not None else text
        return text + paper_book.render() if paper and paper_book is not None else text

    def published() -> None:
        if textfile:
            publish(rendered(), textfile)

    def forward(raw: str) -> str | None:
        nonlocal next_id, last_price
        receive_ns = clock_ns()
        next_id += 1
        tick = source.tick(raw, receive_ns=receive_ns, tick_id=next_id)
        metrics.observe_message(source.last_outcome, tick, source.usd_per_tick)
        if tick is None:
            return None
        stop = paper_book.gate(tick.event_ns, tick.features[-1]) if paper and paper_book is not None else None
        tick.exits_only = stop is not None
        if process.poll() is not None:
            return f"submitter exited with code {process.returncode}"
        try:
            process.stdin.write(tick.line() + "\n")
            process.stdin.flush()
        except OSError as error:
            return f"submitter closed its input ({type(error).__name__}; exit code {process.poll()})"
        metrics.ticks += 1
        reply = _read_line(process, reply_timeout_s)
        if reply is None:
            code = process.poll()
            return "submitter stopped answering" if code is None else f"submitter exited with code {code}"
        if reply.startswith("DECISION "):
            decision = parse_decision(reply)
            if hasattr(source, "observe_decision"):
                source.observe_decision(decision, tick)
            metrics.observe(decision, tick_receive_ns=tick.receive_ns,
                            expected_rule=tick.expected_rule, age_ns=tick.age_ns)
            result.decisions += 1
            rule = int(decision["rule"])
            last_price = Decimal(tick.features[-1]) * source.usd_per_tick
            if (ledger is not None and int(decision["decision_code"]) == recorded_code
                    and int(decision.get("resolution", 0)) == PROPOSED and rule < len(source.program)):
                ledger.observe(tick.event_ns, last_price, source.program[rule].action.value)
            if paper and paper_book is not None:
                action = source.program[rule].action.value if rule < len(source.program) else None
                paper_book.observe(tick.event_ns, tick.features[-1], action=action,
                                   authorization=str(decision.get("authorization", "none")),
                                   resync_position=int(decision["position_lots"]) if int(
                                       decision.get("resync", 0)) else None, stop=stop)
        else:
            print(f"edge: submitter replied: {reply}", file=sys.stderr, flush=True)
            metrics.observe_submitter_error(reply)
            result.errors += 1
        published()
        return None

    process = subprocess.Popen(list(submitter_argv), stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                               bufsize=1)
    metrics.submitter_starts += 1
    try:
        ready = _read_line(process, reply_timeout_s)
        if not ready or not ready.startswith("READY "):
            result.reason = f"submitter not ready: {ready!r}"
            return result
        metrics.submitter_ready = 1
        paper = " mode=paper" in f" {ready}"
        recorded_code = ACTION if paper else shadow_recorded_code
        metrics.paper = metrics.paper or paper
        failures = 0
        while True:
            socket, error = None, None
            try:
                socket = socket_factory()
                socket.open()
                socket.subscribe(list(topics))
                metrics.market_data_connected = 1
                published()
                while True:
                    raw = socket.recv_text()
                    if raw is None:
                        break
                    failures = 0
                    ended = forward(raw)
                    if ended:
                        result.reason = ended
                        return result
            except MARKET_DATA_ERRORS as caught:
                error = caught
            finally:
                metrics.market_data_connected = 0
                if socket is not None:
                    try:
                        socket.close()
                    except OSError:
                        pass
            again = reconnect is not None and reconnect()
            kind = market_data_error_kind(error) if error is not None else "closed"
            if error is not None or again:
                metrics.market_data_errors[kind] = metrics.market_data_errors.get(kind, 0) + 1
                what = f"{type(error).__name__}: {error}" if error is not None else "the server closed the stream"
                print(f"edge: market data {kind}: {what}", file=sys.stderr, flush=True)
            if not again:
                result.reason = "market data ended" if error is None else f"market data {kind}: {error}"
                return result
            failures += 1
            published()
            sleep(backoff_delay(failures, jitter))
    finally:
        metrics.submitter_ready = 0
        metrics.market_data_connected = 0
        for stream in (process.stdin, process.stdout):
            try:
                if stream:
                    stream.close()
            except OSError:
                pass
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
        published()
