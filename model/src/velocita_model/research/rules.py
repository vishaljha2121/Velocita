"""Rule-program strategies, evaluated exactly as the cluster evaluates a policy.

A program is an ordered list of rules over a fixed feature vector (integer
micros, as a DecisionFrame carries them). The first matching rule wins; no
match means HOLD. `first_match` reuses the evaluator mirror's own operator
function and its whole-set availability pre-pass, and a randomized test pins it
to `evaluate_rules`. Actions map onto the cluster's directives: HOLD is
`no_action`; ENTER_LONG and EXIT are `place_limit` with a buy or a sell action
template.

Positions are long the whole stake or flat in USDC. A decision at day t's close
fills at day t+`lag_days`'s close, paying the MEASURED cost for that asset and
side (see `costs.py`).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Sequence

from ..contracts.enums import Directive, PredicateOperator
from ..simulation.rule_evaluator import _matches
from .bars import DailyBars

MICROS = 1_000_000
TREND, RET1, VOL, DEV = 0, 1, 2, 3  # feature schema: index N means one thing forever
FEATURE_NAMES = ("trend: close/SMA(sma_days)-1", "ret1: 1-day return",
                 "vol: sample stdev of daily log returns over vol_days", "dev: close/SMA(dev_days)-1")


class Action(Enum):
    HOLD = "hold"
    ENTER_LONG = "enter_long"
    EXIT = "exit"

    @property
    def directive(self) -> Directive:
        return Directive.no_action if self is Action.HOLD else Directive.place_limit


@dataclass(frozen=True)
class Rule:
    feature: int
    op: PredicateOperator
    threshold: int
    action: Action


@dataclass(frozen=True)
class Settings:
    sma_days: int = 200
    vol_days: int = 30
    dev_days: int = 20
    lag_days: int = 1


@dataclass(frozen=True)
class Costs:
    buy_bps: float
    sell_bps: float


@dataclass
class RuleResult:
    returns: list[float]
    positions: list[int]
    buys: int = 0
    sells: int = 0
    decisions: int = 0


def first_match(rules: Sequence[Rule], features: Sequence[int]) -> int | None:
    """Index of the first matching rule, or None (no match, or a rule refers to
    a feature the vector lacks: the evaluator reports that as unavailable)."""
    if any(rule.feature >= len(features) for rule in rules):
        return None
    for index, rule in enumerate(rules):
        if _matches(features[rule.feature], rule.op, rule.threshold):
            return index
    return None


def _first_available(settings: Settings) -> int:
    return max(settings.sma_days - 1, settings.vol_days, settings.dev_days - 1, 1)


def features(bars: DailyBars, asset: int, t: int, settings: Settings) -> tuple[int, ...] | None:
    """Features from closes up to and including day t; None if any window is short."""
    c = bars.close[asset]
    if t < _first_available(settings) or t >= len(c):
        return None
    window = lambda n: c[t - n + 1:t + 1]
    needed = set(range(t - _first_available(settings), t + 1))
    if any(c[i] is None for i in needed):
        return None
    sma = sum(window(settings.sma_days)) / settings.sma_days
    dev_sma = sum(window(settings.dev_days)) / settings.dev_days
    logs = [math.log(c[i] / c[i - 1]) for i in range(t - settings.vol_days + 1, t + 1)]
    mean = sum(logs) / len(logs)
    sd = math.sqrt(sum((x - mean) ** 2 for x in logs) / (len(logs) - 1)) if len(logs) > 1 else 0.0
    values = (c[t] / sma - 1, c[t] / c[t - 1] - 1, sd, c[t] / dev_sma - 1)
    return tuple(round(v * MICROS) for v in values)


def feature_series(bars: DailyBars, asset: int, settings: Settings) -> list[tuple[int, ...] | None]:
    """`features` for every day, with prefix sums for the moving averages."""
    c = bars.close[asset]
    n = len(c)
    first = _first_available(settings)
    out: list[tuple[int, ...] | None] = [None] * n
    sums, valid = [0.0], [0]
    for v in c:
        sums.append(sums[-1] + (v or 0.0))
        valid.append(valid[-1] + (v is not None))
    for t in range(first, n):
        lo = t - first
        if valid[t + 1] - valid[lo] != t + 1 - lo:
            continue
        sma = (sums[t + 1] - sums[t + 1 - settings.sma_days]) / settings.sma_days
        dev_sma = (sums[t + 1] - sums[t + 1 - settings.dev_days]) / settings.dev_days
        logs = [math.log(c[i] / c[i - 1]) for i in range(t - settings.vol_days + 1, t + 1)]
        mean = sum(logs) / len(logs)
        sd = math.sqrt(sum((x - mean) ** 2 for x in logs) / (len(logs) - 1)) if len(logs) > 1 else 0.0
        values = (c[t] / sma - 1, c[t] / c[t - 1] - 1, sd, c[t] / dev_sma - 1)
        out[t] = tuple(round(v * MICROS) for v in values)
    return out


def backtest(bars: DailyBars, asset: int, program: Sequence[Rule], settings: Settings, costs: Costs) -> RuleResult:
    c = bars.close[asset]
    n = len(c)
    feats = feature_series(bars, asset, settings)
    result = RuleResult(returns=[0.0] * n, positions=[0] * n)
    pending: dict[int, int] = {}
    position, equity, previous = 0, 1.0, 1.0
    for t in range(n):
        if t > 0 and position:
            if c[t] is None or c[t - 1] is None:
                raise ValueError("held asset has no close")
            equity *= c[t] / c[t - 1]
        f = feats[t]
        if f is not None:
            result.decisions += 1
            index = first_match(program, f)
            action = program[index].action if index is not None else Action.HOLD
            if action is not Action.HOLD and t + settings.lag_days < n:
                pending[t + settings.lag_days] = 1 if action is Action.ENTER_LONG else 0
        if t in pending:
            target = pending.pop(t)
            if target != position:
                if target:
                    equity *= 1 - costs.buy_bps / 10_000
                    result.buys += 1
                else:
                    equity *= 1 - costs.sell_bps / 10_000
                    result.sells += 1
                position = target
        result.returns[t] = equity / previous - 1
        previous = equity
        result.positions[t] = position
    return result


# -- the three demo programs --------------------------------------------------

GT, GE, LE = PredicateOperator.greater_than, PredicateOperator.greater_than_or_equal, PredicateOperator.less_than_or_equal


def trend(sma_days: int) -> tuple[list[Rule], Settings]:
    """Hold while price is above its SMA; USDC otherwise."""
    return ([Rule(TREND, GT, 0, Action.ENTER_LONG), Rule(TREND, LE, 0, Action.EXIT)],
            Settings(sma_days=sma_days))


def dip_in_uptrend(dip_pct: int) -> tuple[list[Rule], Settings]:
    """Minara's whale brief as rules: only in an uptrend (price > SMA200), buy a
    day that falls at least DIP_PCT %, sell once price is back above its SMA20."""
    return ([Rule(TREND, LE, 0, Action.EXIT),
             Rule(RET1, LE, -dip_pct * MICROS // 100, Action.ENTER_LONG),
             Rule(DEV, GE, 0, Action.EXIT)],
            Settings(sma_days=200))


def calm_trend(annual_vol_pct: int) -> tuple[list[Rule], Settings]:
    """Trend filter that steps aside when 30-day volatility (annualized
    ANNUAL_VOL_PCT %) is too high."""
    daily = round(annual_vol_pct / 100 / math.sqrt(365) * MICROS)
    return ([Rule(VOL, GE, daily, Action.EXIT), Rule(TREND, GT, 0, Action.ENTER_LONG),
             Rule(TREND, LE, 0, Action.EXIT)],
            Settings(sma_days=200))


DEMOS: dict[str, tuple[Callable[[int], tuple[list[Rule], Settings]], list[int]]] = {
    "trend": (trend, [50, 75, 100, 125, 150, 175, 200, 225, 250]),
    "dip_in_uptrend": (dip_in_uptrend, [2, 3, 4, 5, 6, 7, 8]),
    "calm_trend": (calm_trend, [40, 50, 60, 70, 80, 90, 100, 110, 120]),
}
