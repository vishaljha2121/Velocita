"""Volume candidates for cbBTC on True Markets, simulated with the LIVE
execution model rather than an idealised fill:

* one decision per 30 s tick, evaluated by the cluster's first-match rule
  mirror (`rules.first_match`) on integer features;
* FOK fill at the tick's reference price, the measured per-side cost charged AT
  the fill, so it changes the cash the next buy can use (book ch52: fees are
  not a post-hoc haircut);
* after every fill the next RESYNC_TICKS ticks are lost (the kernel refuses them
  with stale_risk_state and the edge re-syncs): the earliest opposite trade is
  two ticks later;
* long-only, one position of one clip; a buy is refused by the kernel exposure
  cap (notional x (1 + fee hold) > cap) or killed by the venue when notional +
  cost exceeds cash. Every refusal is counted, so the opportunity denominator
  is honest (ch52 "Capital and margin bias").

Edge features the candidates use (index N means one thing in this schema):
    TREND  price / SMA125 of daily closes (today provisional) - 1, micros
    DEVS   price / SMA of the last `window` ticks - 1, micros       (new)
    PHASE  flips 0/1 on every decided, non-lost tick                (new)
    HELD_S seconds since the open position was bought; 0 when flat (new)
PHASE and HELD_S are state the edge must hold; they are features so the
kernel's rules stay stateless.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Sequence

from ..contracts.enums import PredicateOperator
from .rules import Action, Rule, first_match

LOT_BTC = 0.00001
MICROS = 1_000_000
TICK_S = 30
DAY_S = 86_400
WEEK_S = 7 * DAY_S
SMA_DAYS = 125
TREND, DEVS, PHASE, HELD_S, CYCLE_S = 0, 1, 2, 3, 4
FEATURE_NAMES = ("trend", "devs", "phase", "held_s", "cycle_s")

LT, LE, EQ, GE, GT = (PredicateOperator.less_than, PredicateOperator.less_than_or_equal, PredicateOperator.equal,
                      PredicateOperator.greater_than_or_equal, PredicateOperator.greater_than)


@dataclass(frozen=True)
class PricePath:
    """Evenly spaced ticks plus the daily closes before the first tick's UTC day
    (oldest first) that the 125-day trend needs."""

    times: tuple[int, ...]
    prices: tuple[float, ...]
    history: tuple[float, ...]


def day_closes(path: PricePath) -> list[float]:
    """Close of each UTC day in the path = its last tick's price."""
    out: list[float] = []
    first_day = path.times[0] // DAY_S
    for t, p in zip(path.times, path.prices):
        k = t // DAY_S - first_day
        if k == len(out):
            out.append(p)
        else:
            out[k] = p
    return out


def trend_series(path: PricePath) -> list[int]:
    """TREND per tick, as the edge computes it: the last 124 daily closes before
    the tick's day plus the tick's price as today's provisional close."""
    if len(path.history) < SMA_DAYS - 1:
        raise ValueError(f"trend needs {SMA_DAYS - 1} days of history")
    closes = list(path.history) + day_closes(path)
    base = len(path.history)
    first_day = path.times[0] // DAY_S
    out = []
    sums: dict[int, float] = {}
    for t, p in zip(path.times, path.prices):
        k = base + t // DAY_S - first_day
        if k not in sums:
            sums[k] = sum(closes[k - (SMA_DAYS - 1):k])
        out.append(round((p / ((sums[k] + p) / SMA_DAYS) - 1) * MICROS))
    return out


def devs_series(prices: Sequence[float], window: int) -> list[int | None]:
    """DEVS per tick; None until WINDOW ticks exist."""
    out: list[int | None] = [None] * len(prices)
    total = 0.0
    for i, p in enumerate(prices):
        total += p
        if i >= window:
            total -= prices[i - window]
        if i >= window - 1:
            out[i] = round((p / (total / window) - 1) * MICROS)
    return out


def minute_devs_series(times: Sequence[int], prices: Sequence[float], minutes: int) -> list[int | None]:
    """DEVS against the mean of the last MINUTES completed minute closes (a minute's
    close = its last tick's price); None until that many minutes have completed."""
    out: list[int | None] = [None] * len(prices)
    closes: list[float] = []
    total = 0.0
    current, last = None, 0.0
    for i, (t, p) in enumerate(zip(times, prices)):
        minute = t // 60
        if current is not None and minute != current:
            closes.append(last)
            total += last
            if len(closes) > minutes:
                total -= closes[-minutes - 1]
        current, last = minute, p
        if len(closes) >= minutes:
            out[i] = round((p / (total / minutes) - 1) * MICROS)
    return out


def permute(path: PricePath, start_index: int, rng: random.Random) -> PricePath:
    """Shuffle which tick each tick-to-tick move happened on, from START_INDEX
    on; the first price, the multiset of moves and the history stay."""
    prices = path.prices
    ratios = [prices[i] / prices[i - 1] for i in range(max(1, start_index), len(prices))]
    rng.shuffle(ratios)
    out = list(prices[:max(1, start_index)])
    for r in ratios:
        out.append(out[-1] * r)
    return PricePath(path.times, tuple(out), path.history)


def random_walk(path: PricePath, rng: random.Random) -> PricePath:
    """Gaussian log moves with the path's own per-tick mean and stdev."""
    prices = path.prices
    logs = [math.log(prices[i] / prices[i - 1]) for i in range(1, len(prices))]
    mean = sum(logs) / len(logs)
    sd = math.sqrt(sum((x - mean) ** 2 for x in logs) / (len(logs) - 1))
    out = [prices[0]]
    for _ in logs:
        out.append(out[-1] * math.exp(rng.gauss(mean, sd)))
    return PricePath(path.times, tuple(out), path.history)


@dataclass(frozen=True)
class Candidate:
    name: str
    label: str
    program: tuple[Rule, ...]
    window: int = 0
    phase_period_s: int = 0
    cycle_s: int = 0
    window_min: int = 0

    @property
    def uses_devs(self) -> bool:
        return any(r.feature == DEVS for r in self.program)


def churn() -> Candidate:
    return Candidate("A", "churn", (Rule(PHASE, EQ, 0, Action.ENTER_LONG), Rule(PHASE, EQ, 1, Action.EXIT)))


def gated_churn() -> Candidate:
    return Candidate("B", "trend-gated churn", (Rule(TREND, LE, 0, Action.EXIT),) + churn().program)


def rotate(minutes: int, gated: bool) -> Candidate:
    exit_after = Rule(HELD_S, GE, minutes * 60, Action.EXIT)
    if gated:
        return Candidate("C", f"rotate {minutes} min, trend-gated",
                         (Rule(TREND, LE, 0, Action.EXIT), exit_after, Rule(TREND, GT, 0, Action.ENTER_LONG)))
    return Candidate("C0", f"rotate {minutes} min", (exit_after, Rule(HELD_S, GE, 0, Action.ENTER_LONG)))


def dip(window: int, x_bp: int, y_bp: int) -> Candidate:
    return Candidate("D", f"dip w={window} x={x_bp}bp y={y_bp}bp",
                     (Rule(DEVS, LE, -x_bp * 100, Action.ENTER_LONG), Rule(DEVS, GE, y_bp * 100, Action.EXIT)),
                     window=window)


def clock_churn(period_s: int, gated: bool) -> Candidate:
    """`--strategy churn|trend_churn --period-s P` as the paper tooling runs it:
    long in odd clock periods, flat in even ones."""
    rules = (Rule(PHASE, GT, 0, Action.ENTER_LONG), Rule(PHASE, LE, 0, Action.EXIT))
    if gated:
        return Candidate("B~", f"trend_churn --period-s {period_s}", (Rule(TREND, LE, 0, Action.EXIT),) + rules,
                         phase_period_s=period_s)
    return Candidate("A~", f"churn --period-s {period_s}", rules, phase_period_s=period_s)


def clock_rotate(period_s: int, exit_window_s: int) -> Candidate:
    """`--strategy rotate --value 125 --period-s P --exit-window-s W`: exit in the
    first W seconds of every clock period, otherwise long while trend > 0."""
    return Candidate("C~", f"rotate --period-s {period_s} --exit-window-s {exit_window_s}",
                     (Rule(TREND, LE, 0, Action.EXIT), Rule(CYCLE_S, LT, exit_window_s, Action.EXIT),
                      Rule(TREND, GT, 0, Action.ENTER_LONG)), cycle_s=period_s)


def minute_dip(window_min: int, enter_below_bp: int, exit_above_bp: int) -> Candidate:
    """`--strategy dip --window-min N --enter-below-bps X --exit-above-bps Y`."""
    return Candidate("D~", f"dip --window-min {window_min} --enter-below-bps {enter_below_bp} "
                           f"--exit-above-bps {exit_above_bp}",
                     (Rule(DEVS, LT, -enter_below_bp * 100, Action.ENTER_LONG),
                      Rule(DEVS, GT, exit_above_bp * 100, Action.EXIT)), window_min=window_min)


def trend125() -> Candidate:
    return Candidate("E", "trend(125) per tick", (Rule(TREND, GT, 0, Action.ENTER_LONG), Rule(TREND, LE, 0, Action.EXIT)))


def buy_and_hold() -> Candidate:
    return Candidate("BH", "buy and hold", (Rule(HELD_S, GE, 0, Action.ENTER_LONG),))


GRID: dict[str, list[tuple[str, Candidate]]] = {
    "A": [("-", churn())],
    "B": [("-", gated_churn())],
    "C": [(f"R={r}", rotate(r, True)) for r in (30, 120, 480, 1440)],
    "C0": [(f"R={r}", rotate(r, False)) for r in (30, 120, 480, 1440)],
    "D": [(f"w={w},x={x},y={y}", dip(w, x, y)) for w in (20, 120) for x in (25, 50, 100) for y in (0, 50)],
    "E": [("-", trend125())],
    "BH": [("-", buy_and_hold())],
}


@dataclass(frozen=True)
class Execution:
    clip_lots: int
    buy_bps: float
    sell_bps: float
    capital_usd: float | None = 25.0
    exposure_cap_usd: float | None = 25.0
    fee_hold_bps: float = 150.0
    resync_ticks: int = 1
    fill_lag_ticks: int = 0
    weekly_loss_cap_usd: float | None = None
    weekly_topup: bool = True


@dataclass
class Week:
    start_s: int
    start_equity: float = 0.0
    end_equity: float = 0.0
    volume: float = 0.0
    fees: float = 0.0
    buys: int = 0
    sells: int = 0
    decisions: int = 0
    lost_ticks: int = 0
    killed_capital: int = 0
    refused_exposure: int = 0
    refused_cap: int = 0
    position_bound: int = 0
    exit_flat: int = 0
    topup: float = 0.0
    cap_first_s: int | None = None
    drawdown: float = 0.0
    first_kill_s: int | None = None

    @property
    def net(self) -> float:
        return self.end_equity - self.start_equity

    @property
    def price_pnl(self) -> float:
        return self.net + self.fees


@dataclass(frozen=True)
class Fill:
    tick: int
    side: str
    price: float
    qty_btc: float
    fee: float


@dataclass
class Result:
    weeks: list[Week]
    fills: list[Fill] = field(default_factory=list)
    max_drawdown_usd: float = 0.0

    def total(self, name: str) -> float:
        return sum(getattr(w, name) for w in self.weeks)

    @property
    def volume(self) -> float:
        return self.total("volume")

    @property
    def net(self) -> float:
        return self.total("net")

    @property
    def score_bp(self) -> float:
        """Net P&L per unit of traded volume, in basis points: the battery's score."""
        v = self.volume
        return self.net / v * 10_000 if v > 0 else -math.inf


def simulate(path: PricePath, candidate: Candidate, ex: Execution, *, trend: Sequence[int] | None = None,
             devs: Sequence[int | None] | None = None, keep_fills: bool = False) -> Result:
    """Run CANDIDATE over PATH. Weeks start at the first tick and every 7 days
    after; equity is marked at the reference price (mid)."""
    times, prices = path.times, path.prices
    n = len(prices)
    trend = trend_series(path) if trend is None else trend
    if candidate.uses_devs and devs is None:
        devs = (minute_devs_series(times, prices, candidate.window_min) if candidate.window_min
                else devs_series(prices, candidate.window))
    phase_period, cycle = candidate.phase_period_s, candidate.cycle_s
    program = candidate.program
    clip = ex.clip_lots * LOT_BTC
    buy_rate, sell_rate = ex.buy_bps / 10_000, ex.sell_bps / 10_000
    hold_rate = ex.fee_hold_bps / 10_000
    capital = ex.capital_usd
    cash = capital if capital is not None else 0.0
    qty = 0.0
    phase = 0
    bought_at = 0
    blocked_through = -1
    topups = 0.0
    peak = worst = week_peak = 0.0
    weeks: list[Week] = []
    fills: list[Fill] = []
    week: Week | None = None
    t0 = times[0]
    next_week_s = t0
    for i in range(n):
        t, p = times[i], prices[i]
        if t >= next_week_s:
            if week is not None:
                week.end_equity = cash + qty * p
            week = Week(start_s=next_week_s)
            weeks.append(week)
            next_week_s += WEEK_S
            if ex.weekly_topup and capital is not None and cash + qty * p < capital:
                week.topup = capital - (cash + qty * p)
                cash += week.topup
                topups += week.topup
            week.start_equity = cash + qty * p
            week_peak = week.start_equity
        equity = cash + qty * p
        pnl = equity - topups - (capital or 0.0)
        peak = max(peak, pnl)
        worst = max(worst, peak - pnl)
        week_peak = max(week_peak, equity)
        week.drawdown = max(week.drawdown, week_peak - equity)
        if i <= blocked_through:
            week.lost_ticks += 1
            continue
        d = devs[i] if devs is not None else 0
        if d is None:
            continue
        week.decisions += 1
        index = first_match(program, (trend[i], d, (t // phase_period) % 2 if phase_period else phase,
                                      t - bought_at if qty > 0 else 0, t % cycle if cycle else 0))
        phase ^= 1
        if index is None:
            continue
        action = program[index].action
        fill_price = prices[min(n - 1, i + ex.fill_lag_ticks)]
        if action is Action.ENTER_LONG:
            if qty > 0:
                week.position_bound += 1
                continue
            notional = clip * fill_price
            fee = notional * buy_rate
            if ex.weekly_loss_cap_usd is not None and (
                    week.start_equity - (cash + qty * p) + notional * (buy_rate + sell_rate) > ex.weekly_loss_cap_usd):
                week.refused_cap += 1
                if week.cap_first_s is None:
                    week.cap_first_s = t - week.start_s
                continue
            if ex.exposure_cap_usd is not None and clip * p * (1 + hold_rate) > ex.exposure_cap_usd:
                week.refused_exposure += 1
                continue
            if capital is not None and notional + fee > cash:
                week.killed_capital += 1
                if week.first_kill_s is None:
                    week.first_kill_s = t - week.start_s
                continue
            cash -= notional + fee
            qty = clip
            bought_at = t
            week.buys += 1
            side = "buy"
        elif action is Action.EXIT:
            if qty == 0:
                week.exit_flat += 1
                continue
            notional = qty * fill_price
            fee = notional * sell_rate
            cash += notional - fee
            qty = 0.0
            week.sells += 1
            side = "sell"
        else:
            continue
        week.volume += notional
        week.fees += fee
        blocked_through = i + ex.resync_ticks
        if keep_fills:
            fills.append(Fill(i, side, fill_price, clip, fee))
    if week is not None:
        week.end_equity = cash + qty * prices[-1]
    return Result(weeks, fills, worst)


@dataclass(frozen=True)
class Features:
    trend: list[int]
    devs: dict[int, list[int | None]]


def features(path: PricePath, windows: Sequence[int]) -> Features:
    return Features(trend_series(path), {w: devs_series(path.prices, w) for w in windows})


def signal_execution(buy_bps: float, sell_bps: float, clip_lots: int = 25) -> Execution:
    """Unlimited cash, no caps: a hypothetical signal test (ch52), used only for the score."""
    return Execution(clip_lots, buy_bps, sell_bps, capital_usd=None, exposure_cap_usd=None)


def run_grid(path: PricePath, feats: Features, grid: Sequence[tuple[str, Candidate]], ex: Execution) -> list[Result]:
    return [simulate(path, c, ex, trend=feats.trend, devs=feats.devs.get(c.window)) for _, c in grid]


def best_index(results: Sequence[Result], weeks: range | None = None) -> int:
    """Highest score over WEEKS (all weeks if None); ties go to the first."""
    def score(r: Result) -> float:
        ws = r.weeks if weeks is None else [r.weeks[k] for k in weeks]
        v = sum(w.volume for w in ws)
        return sum(w.net for w in ws) / v * 10_000 if v > 0 else -math.inf
    scores = [score(r) for r in results]
    return scores.index(max(scores))


@dataclass(frozen=True)
class WalkForward:
    score_bp: float
    choices: list[tuple[int, int]]
    oos_weeks: list[Week]


def walk_forward(results: Sequence[Result], *, train_weeks: int, step_weeks: int) -> WalkForward:
    """Choose on the TRAIN_WEEKS before each step, trade the next STEP_WEEKS with
    the chosen value. Each grid value's run covers the whole path, so position
    and cash carry across steps as they would live."""
    n = len(results[0].weeks)
    choices, oos = [], []
    for start in range(train_weeks, n, step_weeks):
        k = best_index(results, range(start - train_weeks, start))
        choices.append((start, k))
        oos += results[k].weeks[start:min(n, start + step_weeks)]
    v = sum(w.volume for w in oos)
    return WalkForward(sum(w.net for w in oos) / v * 10_000 if v > 0 else -math.inf, choices, oos)


def week_start_index(path: PricePath, week: int) -> int:
    target = path.times[0] + week * WEEK_S
    return next(i for i, t in enumerate(path.times) if t >= target)


def utc(t: int) -> str:
    return datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m-%d %H:%M")
