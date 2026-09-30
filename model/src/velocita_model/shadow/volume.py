"""Volume strategies for paper trading: rule programs whose extra feature comes
from the tick's own event time or from the live reference stream.

Every volume vector is the four daily features (`rules.FEATURE_NAMES`, same
indices as the trend program) followed by ONE strategy feature at index 4, then
the price in ticks. Each strategy has its own policy id and feature schema id,
because index 4 means something different in each.

  churn        phase:     (floor(event_ns / period) mod 2) * 1e6
  trend_churn  phase, gated by trend (daily SMA)
  rotate       rotation:  (event_ns mod period) / 1000, microseconds into the period
  dip          mdev:      price / SMA(last W minute closes) - 1, in micros

Phase and rotation are pure functions of the venue's event timestamp, so a
replay or a restarted edge computes the same value for the same tick. The
minute window depends only on the ordered observations the edge has seen since
it started; it is refused (warming_up) until it covers W minutes.
"""

from __future__ import annotations

import hashlib
from collections import deque
from dataclasses import dataclass
from decimal import ROUND_HALF_EVEN, Decimal

from ..contracts.enums import PredicateOperator
from ..research import rules

MICROS = 1_000_000
NS = 1_000_000_000
MINUTE_NS = 60 * NS
MAX_WINDOW_MINUTES = 1440
EXTRA = 4  # the strategy feature's index, after trend, ret1, vol, dev

GT, LE, LT = PredicateOperator.greater_than, PredicateOperator.less_than_or_equal, PredicateOperator.less_than
ENTER, EXIT = rules.Action.ENTER_LONG, rules.Action.EXIT


@dataclass(frozen=True)
class Params:
    period_s: int | None = None       # churn / trend_churn: length of one phase; rotate: rotation period
    exit_window_s: int = 60           # rotate: the exit lasts this long at the start of each period
    window_min: int = 30              # dip: moving-average window in minutes
    enter_below_bps: int = 20         # dip: buy when price is this far below the average
    exit_above_bps: int = 10          # dip: sell when price is this far above it


@dataclass(frozen=True)
class Spec:
    policy_id: int
    feature_schema_id: int
    feature: str
    default_period_s: int | None


STRATEGIES = {
    "churn": Spec(22, 32, "phase", 60),
    "trend_churn": Spec(23, 33, "phase", 60),
    "rotate": Spec(24, 34, "rotation", 1800),
    "dip": Spec(25, 35, "mdev", None),
}
TREND_IDENTITY = (21, 31)  # policy id, feature schema id of every rules.DEMOS program


def identity(strategy: str) -> tuple[int, int]:
    spec = STRATEGIES.get(strategy)
    return (spec.policy_id, spec.feature_schema_id) if spec else TREND_IDENTITY


def resolved(strategy: str, params: Params) -> Params:
    """PARAMS with the strategy's default period filled in, validated."""
    spec = STRATEGIES[strategy]
    period = params.period_s if params.period_s is not None else spec.default_period_s
    out = Params(period, params.exit_window_s, params.window_min, params.enter_below_bps, params.exit_above_bps)
    if spec.feature in ("phase", "rotation") and (out.period_s is None or out.period_s < 1):
        raise ValueError(f"{strategy}: --period-s must be at least 1")
    if spec.feature == "rotation" and not 0 < out.exit_window_s < out.period_s:
        raise ValueError("rotate: --exit-window-s must be above 0 and below --period-s")
    if spec.feature == "mdev":
        if not 1 <= out.window_min <= MAX_WINDOW_MINUTES:
            raise ValueError(f"dip: --window-min must be 1..{MAX_WINDOW_MINUTES}")
        if out.enter_below_bps < 0 or out.exit_above_bps < 0:
            raise ValueError("dip: thresholds are distances in basis points, not negative")
    return out


def program(strategy: str, value: int, params: Params) -> tuple[list[rules.Rule], rules.Settings]:
    """First-match rules over the volume vector. VALUE is the trend SMA in days."""
    p = resolved(strategy, params)
    settings = rules.Settings(sma_days=value)
    if strategy == "churn":
        return [rules.Rule(EXTRA, GT, 0, ENTER), rules.Rule(EXTRA, LE, 0, EXIT)], settings
    if strategy == "trend_churn":
        return ([rules.Rule(rules.TREND, LE, 0, EXIT), rules.Rule(EXTRA, GT, 0, ENTER),
                 rules.Rule(EXTRA, LE, 0, EXIT)], settings)
    if strategy == "rotate":
        return ([rules.Rule(rules.TREND, LE, 0, EXIT), rules.Rule(EXTRA, LT, p.exit_window_s * MICROS, EXIT),
                 rules.Rule(rules.TREND, GT, 0, ENTER)], settings)
    if strategy == "dip":
        # No match between the two thresholds: HOLD.
        return ([rules.Rule(EXTRA, LT, -p.enter_below_bps * 100, ENTER),
                 rules.Rule(EXTRA, GT, p.exit_above_bps * 100, EXIT)], settings)
    raise KeyError(strategy)


def model_text(strategy: str, value: int, params: Params) -> str:
    p = resolved(strategy, params)
    if strategy in ("churn", "trend_churn"):
        shaped = f"period_s={p.period_s}"
    elif strategy == "rotate":
        shaped = f"period_s={p.period_s} exit_window_s={p.exit_window_s}"
    else:
        shaped = f"window_min={p.window_min} enter_below_bps={p.enter_below_bps} exit_above_bps={p.exit_above_bps}"
    return f"velocita.volume.v1 {strategy} sma_days={value} {shaped}"


def model_digest(strategy: str, value: int, params: Params) -> str:
    return hashlib.sha256(model_text(strategy, value, params).encode()).hexdigest()


def phase(event_ns: int, period_s: int) -> int:
    return MICROS * ((event_ns // (period_s * NS)) % 2)


def rotation(event_ns: int, period_s: int) -> int:
    return (event_ns % (period_s * NS)) // 1000


class MinuteWindow:
    """The last observed price in each UTC minute, for the newest W minutes.

    A minute without an observation carries the previous minute's close
    forward; the newest minute's close is the newest price (provisional, like
    today's close in the daily features). At most W entries plus one carried
    close are kept."""

    def __init__(self, minutes: int) -> None:
        if not 1 <= minutes <= MAX_WINDOW_MINUTES:
            raise ValueError(f"window must be 1..{MAX_WINDOW_MINUTES} minutes")
        self.minutes = minutes
        self.closes: deque[tuple[int, Decimal]] = deque()
        self.carry: Decimal | None = None
        self.last_event_ns = -1

    def observe(self, event_ns: int, price: Decimal) -> None:
        if event_ns <= self.last_event_ns:
            return
        self.last_event_ns = event_ns
        minute = event_ns // MINUTE_NS
        if self.closes and self.closes[-1][0] == minute:
            self.closes[-1] = (minute, price)
        else:
            self.closes.append((minute, price))
        while self.closes[0][0] <= minute - self.minutes:
            self.carry = self.closes.popleft()[1]

    def deviation_micros(self) -> int | None:
        """round_half_even((newest / SMA - 1) * 1e6), or None until W minutes are covered."""
        if not self.closes:
            return None
        newest_minute, newest = self.closes[-1]
        value, total, i = self.carry, Decimal(0), 0
        for minute in range(newest_minute - self.minutes + 1, newest_minute + 1):
            if i < len(self.closes) and self.closes[i][0] == minute:
                value = self.closes[i][1]
                i += 1
            if value is None:
                return None
            total += value
        ratio = newest * self.minutes / total - 1
        return int((ratio * MICROS).to_integral_value(rounding=ROUND_HALF_EVEN))
