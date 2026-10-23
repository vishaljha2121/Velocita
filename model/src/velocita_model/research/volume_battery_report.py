"""Volume candidates (A churn, B gated churn, C/C0 rotate, D dip, E trend,
buy-and-hold) through the battery under the live execution model.

Protocol, splits, grids, seeds and the recommendation rule are fixed in
archive/evidence/volume_battery_2026-09-30/RESEARCH_LEDGER.md, written before
any data was seen. Stage `research` uses only the research period; stage
`holdout` runs the untouched final four weeks once, with the choices the
research stage saved.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import statistics
import time
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, replace
from datetime import date
from pathlib import Path

from . import costs as measured_costs
from . import volume_battery as vb
from .bars import load_binance
from .tick_data import TickSeries, load_ticks

PAIR = "BTCUSDT"
RESEARCH = (date(2026, 3, 2), date(2026, 8, 31))
HOLDOUT = (date(2026, 8, 31), date(2026, 9, 28))
TRAIN_WEEKS, STEP_WEEKS = 8, 2
WINDOWS = (20, 120)
SEED = 20260930
CAP_USD = 12.0
BATTERY_IDS = ("A", "B", "C", "C0", "C0p", "D", "E")
ALL_IDS = BATTERY_IDS + ("BH",)
CACHE = Path.home() / ".cache" / "velocita-research"
Grid = dict[str, list[tuple[str, vb.Candidate]]]


@dataclass(frozen=True)
class Setup:
    buy_bps: float
    sell_bps: float
    grid: Grid
    chosen: dict[str, int]
    wf_start: int
    op_lots: tuple[int, ...]

    @property
    def signal(self) -> vb.Execution:
        return vb.signal_execution(self.buy_bps, self.sell_bps)

    def operational(self, lots: int, **kw) -> vb.Execution:
        kw.setdefault("weekly_loss_cap_usd", CAP_USD)
        return vb.Execution(lots, self.buy_bps, self.sell_bps, **kw)

    def candidate(self, cid: str) -> vb.Candidate:
        return self.grid[cid][self.chosen[cid]][1]


def paced_rotation(minutes: int) -> vb.Candidate:
    """C0 with R computed from the budget, not fitted (ledger amendment 3)."""
    return vb.Candidate("C0p", f"budget-paced rotate {minutes} min", vb.rotate(minutes, gated=False).program)


def pace_minutes(clip_lots: int, price: float, buy_bps: float, sell_bps: float) -> int:
    """R such that a week of rotations spends the cap: N = floor(cap / round-trip
    cost) round trips, each R minutes long plus the one lost 30 s tick and the
    30 s until the re-buy."""
    round_trip = clip_lots * vb.LOT_BTC * price * (buy_bps + sell_bps) / 10_000
    n = math.floor(CAP_USD / round_trip)
    return max(1, math.floor(7 * 24 * 60 / n - 1))


def load_path(start: date, end: date) -> tuple[vb.PricePath, TickSeries, int]:
    ticks = load_ticks(PAIR, start, end, CACHE / "btc1s")
    month_before = date(start.year - (start.month == 1), (start.month - 2) % 12 + 1, 1)
    daily = load_binance({"BTC": PAIR}, date(2025, 6, 1), month_before if start.day == 1 else start.replace(day=1),
                         CACHE / "binance")
    history = tuple(c for d, c in zip(daily.dates, daily.close[0]) if d < start)
    days_before = sum(1 for d in daily.dates if d < start)
    if daily.dates[days_before - 1] != date.fromordinal(start.toordinal() - 1):
        raise ValueError("daily history does not end the day before the tick period")
    return vb.PricePath(ticks.times, ticks.prices, history[-200:]), ticks, days_before


_PATH: vb.PricePath | None = None
_SETUP: Setup | None = None


def _init(path: vb.PricePath, setup: Setup) -> None:
    global _PATH, _SETUP
    _PATH, _SETUP = path, setup


def _null(job: tuple[str, int]) -> tuple[str, int, dict]:
    kind, seed = job
    rng = random.Random(seed)
    if kind == "perm":
        path = vb.permute(_PATH, 1, rng)
    elif kind == "rw":
        path = vb.random_walk(_PATH, rng)
    else:
        path = vb.permute(_PATH, _SETUP.wf_start, rng)
    feats = vb.features(path, WINDOWS)
    out: dict = {}
    for cid in ALL_IDS:
        grid = _SETUP.grid[cid]
        if kind == "wf":
            if cid in BATTERY_IDS:
                out[cid] = vb.walk_forward(vb.run_grid(path, feats, grid, _SETUP.signal),
                                           train_weeks=TRAIN_WEEKS, step_weeks=STEP_WEEKS).score_bp
            continue
        entry = {}
        if cid in BATTERY_IDS:
            results = vb.run_grid(path, feats, grid, _SETUP.signal)
            entry["score"] = results[vb.best_index(results)].score_bp
        cand = _SETUP.candidate(cid)
        for lots in _SETUP.op_lots:
            op = vb.simulate(path, cand, _SETUP.operational(lots), trend=feats.trend, devs=feats.devs.get(cand.window))
            entry[lots] = [w.net for w in op.weeks]
        out[cid] = entry
    return kind, seed, out


def _map(jobs: list, path: vb.PricePath, setup: Setup, workers: int) -> list:
    if workers <= 1:
        _init(path, setup)
        return [_null(j) for j in jobs]
    with ProcessPoolExecutor(max_workers=workers, initializer=_init, initargs=(path, setup)) as pool:
        return list(pool.map(_null, jobs, chunksize=max(1, len(jobs) // (workers * 4))))


def p_value(real: float, null: list[float]) -> float:
    return (1 + sum(1 for x in null if x >= real)) / (len(null) + 1)


def usd(x: float) -> str:
    return f"-${-x:,.2f}" if x < 0 else f"${x:,.2f}"


def vpd(volume: float, net: float) -> str:
    return f"${volume / -net:,.0f}" if net < 0 else "no loss"


def pct(xs: list[float], q: float) -> float:
    s = sorted(xs)
    return s[min(len(s) - 1, max(0, round(q * (len(s) - 1))))]


@dataclass
class Op:
    """Operational metrics for one run."""

    weeks: int
    days: float
    volume_w: float
    fees_w: float
    price_w: float
    net_w: float
    vpd: str
    vpd_value: float
    week_dd: float
    worst_week: float
    p12_real: float
    fills_day: float
    kills_day: float
    exposure_day: float
    bound_day: float
    cap_weeks: int
    cap_hours: float | None
    net_sd: float
    week_vol: list[float]
    week_net: list[float]


def op_metrics(r: vb.Result, days: float) -> Op:
    ws = r.weeks
    n = len(ws)
    cap = [w.cap_first_s / 3600 for w in ws if w.cap_first_s is not None]
    nets = [w.net for w in ws]
    return Op(n, days, r.volume / n, r.total("fees") / n, r.total("price_pnl") / n, r.net / n, vpd(r.volume, r.net),
              r.volume / -r.net if r.net < 0 else 0.0, max(w.drawdown for w in ws), min(nets),
              sum(1 for x in nets if x < -CAP_USD) / n,
              (r.total("buys") + r.total("sells")) / days, r.total("killed_capital") / days,
              r.total("refused_exposure") / days, r.total("position_bound") / days, len(cap),
              statistics.median(cap) if cap else None, statistics.pstdev(nets) if n > 1 else 0.0,
              [w.volume for w in ws], nets)


def cid_label(cid: str, setup: Setup) -> str:
    value, cand = setup.grid[cid][setup.chosen[cid]]
    return f"{cid} {cand.label}" if value == "-" or cid == "C0p" else f"{cid} {cand.label} ({value})"


def duration(days: float) -> str:
    return "never" if days == math.inf else (f"{days * 24:,.1f} h" if days < 1 else f"{days:,.1f} d")


def clip_star(path: vb.PricePath, feats: vb.Features, buy_bps: float, sell_bps: float) -> int:
    """Ledger amendment 2: the largest clip at which churn (the fastest spender)
    is stopped by the weekly cap and never by the $25 capital."""
    for lots in range(25, 0, -1):
        r = vb.simulate(path, vb.churn(), vb.Execution(lots, buy_bps, sell_bps, weekly_loss_cap_usd=CAP_USD),
                        trend=feats.trend)
        if r.total("killed_capital") == 0 and r.total("refused_exposure") == 0:
            return lots
    return 0


def research(out_dir: Path, *, workers: int, n_is: int, n_rw: int, n_wf: int) -> dict:
    started = time.time()
    path, ticks, _ = load_path(*RESEARCH)
    cost = measured_costs.model(measured_costs.EVIDENCE, "CBBTC", "mean")
    days = (path.times[-1] + vb.TICK_S - path.times[0]) / vb.DAY_S
    feats = vb.features(path, WINDOWS)
    star = clip_star(path, feats, cost.buy_bps, cost.sell_bps)
    median_price = statistics.median(path.prices)
    pace = pace_minutes(star, median_price, cost.buy_bps, cost.sell_bps)
    grid: Grid = dict(vb.GRID)
    grid["C0p"] = [(f"R={pace}", paced_rotation(pace))]
    sig = vb.signal_execution(cost.buy_bps, cost.sell_bps)
    real = {cid: vb.run_grid(path, feats, grid[cid], sig) for cid in ALL_IDS}
    chosen = {cid: vb.best_index(real[cid]) for cid in ALL_IDS}
    op_lots = tuple(sorted({25, 12, star}, reverse=True))
    setup = Setup(cost.buy_bps, cost.sell_bps, grid, chosen, vb.week_start_index(path, TRAIN_WEEKS), op_lots)
    wf = {cid: vb.walk_forward(real[cid], train_weeks=TRAIN_WEEKS, step_weeks=STEP_WEEKS) for cid in BATTERY_IDS}

    jobs = ([("perm", SEED + i) for i in range(n_is)] + [("rw", SEED + 10_000 + i) for i in range(n_rw)]
            + [("wf", SEED + 20_000 + i) for i in range(n_wf)])
    by_kind: dict[str, list[dict]] = {"perm": [], "rw": [], "wf": []}
    for kind, _, out in _map(jobs, path, setup, workers):
        by_kind[kind].append(out)

    def op_run(cid: str, ex: vb.Execution) -> vb.Result:
        c = setup.candidate(cid)
        return vb.simulate(path, c, ex, trend=feats.trend, devs=feats.devs.get(c.window))

    signal_chosen = {cid: real[cid][chosen[cid]] for cid in ALL_IDS}
    loss_per_lot_week = {cid: -signal_chosen[cid].net / len(signal_chosen[cid].weeks) / 25 for cid in ALL_IDS}
    clip_u = {cid: (25 if x <= 0 else min(25, math.floor(CAP_USD / x))) for cid, x in loss_per_lot_week.items()}
    ops = {(cid, lots): op_metrics(op_run(cid, setup.operational(lots)), days) for cid in ALL_IDS for lots in op_lots}

    null_p12 = {}
    for cid in ALL_IDS:
        for lots in op_lots:
            for kind in ("perm", "rw"):
                weeks = [x for out in by_kind[kind] for x in out[cid][lots]]
                null_p12[(cid, lots, kind)] = sum(1 for x in weeks if x < -CAP_USD) / len(weeks)

    scenarios = [("measured x0.8", dict(buy_bps=cost.buy_bps * 0.8, sell_bps=cost.sell_bps * 0.8)),
                 ("measured x1.0", {}),
                 ("measured x1.25", dict(buy_bps=cost.buy_bps * 1.25, sell_bps=cost.sell_bps * 1.25)),
                 ("PaperVenue 115/120", dict(buy_bps=115.0, sell_bps=120.0)),
                 ("2 lost ticks", dict(resync_ticks=2)),
                 ("fill 30 s late", dict(fill_lag_ticks=1))]
    sens = {}
    for cid in ALL_IDS:
        for lots in op_lots:
            for name, kw in scenarios:
                r = op_run(cid, replace(setup.operational(lots), **kw))
                sens[(cid, lots, name)] = (r.volume / len(r.weeks), r.net / len(r.weeks), vpd(r.volume, r.net))

    n_weeks = len(real["A"][0].weeks)
    paper_pred = {}
    for cid in ALL_IDS:
        c = setup.candidate(cid)
        dv = feats.devs.get(c.window)
        for lots in op_lots:
            rows = []
            for k in range(n_weeks):
                a = vb.week_start_index(path, k)
                b = vb.week_start_index(path, k + 1) if k + 1 < n_weeks else len(path.times)
                sub = vb.PricePath(path.times[a:b], path.prices[a:b], path.history)
                ex = vb.Execution(lots, 115.0, 120.0, capital_usd=25.0, weekly_loss_cap_usd=CAP_USD, weekly_topup=False)
                r = vb.simulate(sub, c, ex, trend=feats.trend[a:b], devs=None if dv is None else dv[a:b], keep_fills=True)
                w = r.weeks[0]
                rows.append(dict(vol_day=sum(f.price * f.qty_btc for f in r.fills if f.tick < 2880), vol_week=w.volume,
                                 net_week=w.net, fills=w.buys + w.sells, kills=w.killed_capital,
                                 cap_h=None if w.cap_first_s is None else w.cap_first_s / 3600))
            paper_pred[(cid, lots)] = rows

    battery = {}
    for cid in BATTERY_IDS:
        score = signal_chosen[cid].score_bp
        is_null = [out[cid]["score"] for out in by_kind["perm"]]
        rw_null = [out[cid]["score"] for out in by_kind["rw"]]
        wf_null = [out[cid] for out in by_kind["wf"]]
        battery[cid] = dict(real=score, p_is=p_value(score, is_null), is_med=statistics.median(is_null),
                            p_rw=p_value(score, rw_null), rw_med=statistics.median(rw_null), wf=wf[cid].score_bp,
                            p_wf=p_value(wf[cid].score_bp, wf_null), wf_med=statistics.median(wf_null))
    edge = [cid for cid in BATTERY_IDS if battery[cid]["p_is"] < 0.05 and battery[cid]["p_wf"] < 0.10]
    eligible = [cid for cid in BATTERY_IDS if max(ops[(cid, lots)].volume_w for lots in op_lots) >= 500.0]
    ranked = sorted(eligible, key=lambda c: -ops[(c, star)].vpd_value)
    top = ops[(ranked[0], star)].vpd_value if ranked else 0.0
    near = sorted([c for c in ranked if ops[(c, star)].vpd_value >= 0.95 * top], key=lambda c: ops[(c, star)].net_sd)

    spreads = {f"{cid}:{lots}": dict(vol=[pct(o.week_vol, q) for q in (0.05, 0.5, 0.95)],
                                     net=[pct(o.week_net, q) for q in (0.05, 0.5, 0.95)])
               for (cid, lots), o in ops.items()}
    result = dict(costs=dict(buy_bps=cost.buy_bps, sell_bps=cost.sell_bps), chosen=chosen, pace_minutes=pace,
                  median_price=median_price, clip_star=star, clip_u=clip_u, op_lots=op_lots, edge=edge,
                  eligible=eligible, ranked=ranked, near_ties_by_sd=near, battery=battery, spreads=spreads,
                  runtime_s=round(time.time() - started))
    (out_dir / "research_results.json").write_text(json.dumps(result, indent=1, default=str))
    report = render_research(path, ticks, days, cost, setup, real, wf, battery, ops, null_p12, sens, scenarios,
                             paper_pred, star, clip_u, loss_per_lot_week, edge, eligible, ranked, near, pace,
                             median_price, (n_is, n_rw, n_wf), time.time() - started)
    (out_dir / "REPORT.md").write_text(report)
    return result


def render_research(path, ticks, days, cost, setup, real, wf, battery, ops, null_p12, sens, scenarios, paper_pred,
                    star, clip_u, loss_per_lot_week, edge, eligible, ranked, near, pace, median_price, counts,
                    runtime) -> str:
    n_is, n_rw, n_wf = counts
    weeks = len(real["A"][0].weeks)
    prices = path.prices
    lots_all = setup.op_lots
    fee_only = (cost.buy_bps + cost.sell_bps) / 2
    L = [f"# Volume battery on cbBTC (BTCUSDT 30 s proxy), research period ({date.today().isoformat()})", "",
         "Evidence classes (book ch52 \"The honesty rule\"): costs are OBSERVED (True Markets quotes, "
         "2026-09-28); every P&L, volume and constraint count below is BACKTESTED (historical Binance prices, "
         "modelled FOK fills); every null distribution is SIMULATED (shuffled or random-walk paths). Each table "
         "says which. Generated by `python -m velocita_model.research.volume_battery_report research`.", "",
         "## Data and model", "",
         f"- Ticks: {len(prices):,} at 30 s, {vb.utc(path.times[0])} .. {vb.utc(path.times[-1])} UTC "
         f"({weeks} weeks, {days:.1f} days); BTCUSDT 1 s klines, {len(ticks.sources)} archives, each SHA-256 "
         f"verified against its published CHECKSUM (`SOURCES.txt`). Oldest price used by any tick: "
         f"{ticks.max_staleness_s} s; ticks with a price older than 5 s: {ticks.stale_ticks}.",
         f"- BTC in the period: ${min(prices):,.0f} .. ${max(prices):,.0f}, median ${median_price:,.0f} (first "
         f"${prices[0]:,.0f}, last ${prices[-1]:,.0f}, {prices[-1] / prices[0] - 1:+.1%}). 1 lot = 0.00001 BTC; "
         f"25 lots = ${0.00025 * min(prices):,.2f} .. ${0.00025 * max(prices):,.2f}.",
         f"- Costs: mean measured cbBTC all-in cost vs the TM reference, buy {cost.buy_bps:.1f} bp, sell "
         f"{cost.sell_bps:.1f} bp ({cost.buy_bps + cost.sell_bps:.1f} bp a round trip), charged at each fill. "
         f"Paying only the fee means volume per dollar lost = 10,000 / {fee_only:.1f} = ${10_000 / fee_only:,.1f}.",
         "- Execution: one decision per 30 s tick; FOK at the tick price; the tick after every fill is lost "
         "(stale_risk_state re-sync); long-only, one clip; kernel exposure cap $25 with a 150 bp fee hold; a buy "
         "whose notional + cost exceeds cash is killed. Operational runs add the $12 weekly loss cap (no buy that "
         "could take the week past $12 after its round-trip cost) and restore equity to $25 each Monday.",
         f"- clip* = {star} lots: the largest clip at which churn is stopped by the $12 cap and never by the $25 "
         f"capital in the research period (ledger amendment 2). C0p's R = {pace} min: {CAP_USD:.0f} / round-trip "
         f"cost at {star} lots and the median price (amendment 3).", ""]

    L += ["## 1. Battery [signal score BACKTESTED; nulls SIMULATED]", "",
          "Score = net P&L / traded volume (bp) on a signal run (25 lots, unlimited cash, no cap: a hypothetical "
          f"signal test). A candidate that only pays the fee scores about -{fee_only:.1f} bp. p = share of null "
          f"draws scoring at least as well. In-sample permutation {n_is} draws, random walk {n_rw}, walk-forward "
          f"permutation {n_wf} ({TRAIN_WEEKS}-week train, {STEP_WEEKS}-week step, {weeks - TRAIN_WEEKS} "
          "out-of-sample weeks); every draw re-optimises.", "",
          "| Candidate (in-sample choice) | Round trips (signal run) | In-sample score | IS perm p (null median) | "
          "Random-walk p (null median) | Walk-forward score | WF perm p (null median) |", "|---|---|---|---|---|---|---|"]
    for cid in BATTERY_IDS:
        b = battery[cid]
        L.append(f"| {cid_label(cid, setup)} | {real[cid][setup.chosen[cid]].total('sells'):,} | {b['real']:+.1f} bp | "
                 f"{b['p_is']:.3f} ({b['is_med']:+.1f}) | {b['p_rw']:.3f} ({b['rw_med']:+.1f}) | {b['wf']:+.1f} bp | "
                 f"{b['p_wf']:.3f} ({b['wf_med']:+.1f}) |")
    L += ["", "Grid scores (in-sample, bp, round trips): " + "; ".join(
        f"{cid}: " + ", ".join(f"{v} {r.score_bp:+.1f} ({r.total('sells')})" for (v, _), r in zip(setup.grid[cid], real[cid]))
        for cid in ("C", "C0", "D")), "",
          "Walk-forward choices: " + "; ".join(
        f"{cid}: " + ", ".join(setup.grid[cid][k][0] for _, k in wf[cid].choices) for cid in ("C", "C0", "D")), "",
          f"Candidates with in-sample p < 0.05 and walk-forward p < 0.10: {', '.join(edge) if edge else 'NONE'}.", ""]

    L += ["## 2. Operational results, research period [BACKTESTED; null columns SIMULATED]", "",
          f"$12/week loss cap, $25 capital restored weekly, measured costs. Per-week figures are means over {weeks} "
          "weeks. Volume counts both sides at the reference price. VPD = volume per dollar lost. Week DD = largest "
          "fall from the week's high. P(wk loss>$12): real = share of research weeks; perm / rw = pooled weeks of "
          "the same operational run on every permuted / random-walk path. Time to spend $10 / $12 = budget / mean "
          "loss rate WITHOUT the cap. Kills = buys killed for cash; Expo = buys refused by the kernel exposure cap; "
          "Bound = enter_long refused because already long; Cap = weeks in which the cap stopped buying, median "
          "hours into the week.", "",
          "| Candidate | Lots | Volume/wk | Fees/wk | Price P&L/wk | Net/wk | VPD | Week DD max | Worst week | "
          "P(wk loss>$12) real / perm / rw | Time to $10 / $12 | Fills/day | Kills/day | Expo/day | Bound/day | Cap weeks, median h |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for cid in ALL_IDS:
        for lots in lots_all:
            o = ops[(cid, lots)]
            daily_loss = loss_per_lot_week[cid] * lots / 7
            to = " / ".join(duration(b / daily_loss if daily_loss > 0 else math.inf) for b in (10, 12))
            nulls = " / ".join(f"{null_p12[(cid, lots, k)]:.3f}" for k in ("perm", "rw"))
            capw = f"{o.cap_weeks}/{o.weeks}, {o.cap_hours:.1f} h" if o.cap_hours is not None else f"0/{o.weeks}"
            L.append(f"| {cid_label(cid, setup)} | {lots}{'*' if lots == star else ''} | {usd(o.volume_w)} | {usd(o.fees_w)} | "
                     f"{usd(o.price_w)} | {usd(o.net_w)} | {o.vpd} | {usd(o.week_dd)} | {usd(o.worst_week)} | "
                     f"{o.p12_real:.3f} / {nulls} | {to} | {o.fills_day:,.1f} | {o.kills_day:,.1f} | "
                     f"{o.exposure_day:,.1f} | {o.bound_day:,.0f} | {capw} |")
    L += ["", f"\\* clip* = {star}. clip_u (the pre-registered reading: largest clip whose mean UNCAPPED weekly loss "
          "is <= $12): " + ", ".join(f"{cid} {clip_u[cid]}" if clip_u[cid] >= 1 else
                                    f"{cid} none (1 lot loses {usd(loss_per_lot_week[cid])}/wk)" for cid in ALL_IDS) + ".", ""]

    L += ["## 3. Weekly spread [BACKTESTED]", "",
          "5th / 50th / 95th percentile over the research weeks, operational run (inputs to the prediction interval).", "",
          "| Candidate | Lots | Volume/wk p5 / p50 / p95 | Net/wk p5 / p50 / p95 | Net/wk stdev |", "|---|---|---|---|---|"]
    for cid in ALL_IDS:
        for lots in lots_all:
            o = ops[(cid, lots)]
            L.append(f"| {cid_label(cid, setup)} | {lots} | " + " / ".join(usd(pct(o.week_vol, q)) for q in (0.05, 0.5, 0.95))
                     + " | " + " / ".join(usd(pct(o.week_net, q)) for q in (0.05, 0.5, 0.95)) + f" | {usd(o.net_sd)} |")

    L += ["", "## 4. Sensitivity: cost multipliers and latency (book ch52) [BACKTESTED]", "",
          "Operational run, $12 cap, weekly top-up. Each cell: volume/week, net/week, VPD.", "",
          "| Candidate | Lots | " + " | ".join(n for n, _ in scenarios) + " |", "|---|---|" + "---|" * len(scenarios)]
    for cid in ALL_IDS:
        for lots in lots_all:
            L.append(f"| {cid_label(cid, setup)} | {lots} | " + " | ".join(
                f"{usd(sens[(cid, lots, n)][0])}, {usd(sens[(cid, lots, n)][1])}, {sens[(cid, lots, n)][2]}"
                for n, _ in scenarios) + " |")

    L += ["", "## 5. What a paper week should show [BACKTESTED on research weeks]", "",
          "PaperVenue as deployed (buy 115 bp, sell 120 bp, fixed $25 quote balance, NO top-up) plus the $12 weekly "
          f"cap, started at each of the {weeks} research Mondays and run for one week. p5 / p50 / p95.", "",
          "| Candidate | Lots | Volume first 24 h | Volume in the week | Net in the week | Fills in the week | "
          "Weeks with a capital kill | Cap reached (h) |", "|---|---|---|---|---|---|---|---|"]
    for cid in ALL_IDS:
        for lots in lots_all:
            rows = paper_pred[(cid, lots)]
            f3 = lambda key, fmt: " / ".join(fmt(pct([r[key] for r in rows], q)) for q in (0.05, 0.5, 0.95))
            caps = [r["cap_h"] for r in rows if r["cap_h"] is not None]
            L.append(f"| {cid_label(cid, setup)} | {lots} | {f3('vol_day', usd)} | {f3('vol_week', usd)} | "
                     f"{f3('net_week', usd)} | {f3('fills', lambda x: f'{x:,.0f}')} | "
                     f"{sum(1 for r in rows if r['kills'])}/{len(rows)} | "
                     + (" / ".join(f"{pct(caps, q):.1f}" for q in (0.05, 0.5, 0.95)) + f" ({len(caps)} wk)" if caps else "never")
                     + " |")

    L += ["", "## Recommendation rule outcome (RESEARCH_LEDGER.md, with amendment 1)", "",
          f"- Edge (IS p < 0.05 and WF p < 0.10): {', '.join(edge) if edge else 'none'}.",
          f"- Eligible (mean volume >= $500/week at some clip): {', '.join(eligible) if eligible else 'none'}.",
          f"- Ranked by VPD at clip* ({star} lots): " + ", ".join(
              f"{c} {ops[(c, star)].vpd} (net sd {usd(ops[(c, star)].net_sd)})" for c in ranked) + ".",
          f"- Within 5 % of the best VPD, by weekly predictability (smallest net stdev first): {', '.join(near)}.", "",
          "## Caveats", "",
          "- BTCUSDT on Binance is a proxy for cbBTC's True Markets reference price; the reference is a 30 s price "
          "whose construction is not documented, so short-horizon features (DEVS) may behave differently live.",
          "- Costs are the mean of three quotes per side on one afternoon; the x0.8 / x1.25 columns bound them.",
          "- The kernel re-sync is modelled as losing exactly one tick after a fill; killed or refused orders are "
          "assumed not to cost a tick (unverified).",
          "- The weekly loss cap and weekly top-up are modelled operator rules; the kernel has no weekly loss limit.",
          "- Ticks are exactly 30 s apart; live ticks wait for the next ~12 s True Markets refresh after the 30 s "
          "throttle, so live cadence is slower (about 36 s) and churn volume per hour lower; the weekly volume at the "
          "cap is unchanged, only the time to reach it.",
          f"- Runtime {runtime:.0f} s, {os.cpu_count()} CPUs.", ""]
    return "\n".join(L)


def holdout(out_dir: Path, recommended: list[tuple[str, int]]) -> str:
    saved = json.loads((out_dir / "research_results.json").read_text())
    chosen = {k: int(v) for k, v in saved["chosen"].items()}
    cost = measured_costs.model(measured_costs.EVIDENCE, "CBBTC", "mean")
    grid: Grid = dict(vb.GRID)
    grid["C0p"] = [(f"R={saved['pace_minutes']}", paced_rotation(int(saved["pace_minutes"])))]
    lots_all = tuple(int(x) for x in saved["op_lots"])
    setup = Setup(cost.buy_bps, cost.sell_bps, grid, chosen, 0, lots_all)
    path, ticks, _ = load_path(*HOLDOUT)
    days = (path.times[-1] + vb.TICK_S - path.times[0]) / vb.DAY_S
    feats = vb.features(path, WINDOWS)
    prices = path.prices
    L = [f"# Holdout: the untouched final {round(days / 7)} weeks ({date.today().isoformat()})", "",
         "[BACKTESTED] Run ONCE, after the recommendation was written into RESEARCH_LEDGER.md. Parameters, clip* and "
         "C0p's R are the research stage's; nothing was re-chosen here.", "",
         f"- Ticks: {len(prices):,}, {vb.utc(path.times[0])} .. {vb.utc(path.times[-1])} UTC, {len(ticks.sources)} "
         f"archives SHA-256 verified; oldest price used {ticks.max_staleness_s} s; >5 s old: {ticks.stale_ticks}.",
         f"- BTC ${min(prices):,.0f} .. ${max(prices):,.0f} ({prices[-1] / prices[0] - 1:+.1%}).", "",
         "| Candidate | Lots | Volume/wk | Fees/wk | Price P&L/wk | Net/wk | VPD | Week DD max | Worst week | "
         "Weeks loss>$12 | Fills/day | Kills/day | Cap weeks, median h | Research p5..p95 volume/wk; net/wk | Inside? |",
         "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    rec = set(recommended)
    for cid in ALL_IDS:
        c = setup.candidate(cid)
        for lots in lots_all:
            o = op_metrics(vb.simulate(path, c, setup.operational(lots), trend=feats.trend,
                                       devs=feats.devs.get(c.window)), days)
            capw = f"{o.cap_weeks}/{o.weeks}, {o.cap_hours:.1f} h" if o.cap_hours is not None else f"0/{o.weeks}"
            band = saved["spreads"][f"{cid}:{lots}"]
            inside = all(band["vol"][0] <= v <= band["vol"][2] for v in o.week_vol) and all(
                band["net"][0] <= x <= band["net"][2] for x in o.week_net)
            mark = " **(recommended)**" if (cid, lots) in rec else ""
            L.append(f"| {cid_label(cid, setup)}{mark} | {lots} | {usd(o.volume_w)} | {usd(o.fees_w)} | {usd(o.price_w)} | "
                     f"{usd(o.net_w)} | {o.vpd} | {usd(o.week_dd)} | {usd(o.worst_week)} | "
                     f"{sum(1 for x in o.week_net if x < -CAP_USD)}/{o.weeks} | {o.fills_day:,.1f} | {o.kills_day:,.1f} | "
                     f"{capw} | {usd(band['vol'][0])}..{usd(band['vol'][2])}; {usd(band['net'][0])}..{usd(band['net'][2])} | "
                     f"{'all weeks' if inside else 'NO'} |")
    sig = vb.signal_execution(cost.buy_bps, cost.sell_bps)
    scores = []
    for cid in ALL_IDS:
        c = setup.candidate(cid)
        scores.append(f"{cid} {vb.simulate(path, c, sig, trend=feats.trend, devs=feats.devs.get(c.window)).score_bp:+.1f} bp")
    L += ["", "Signal score on the holdout (25 lots, unlimited cash, no cap; fee-only = "
          f"-{(cost.buy_bps + cost.sell_bps) / 2:.1f} bp): " + ", ".join(scores) + ".", ""]
    L += ["## The paper tooling's definitions on the holdout, 12 lots", "",
          "| Flags | Run | Volume/wk | Net/wk | VPD | Fills/day | Kills/day | First capital kill (h) | Cap reached (h) |",
          "|---|---|---|---|---|---|---|---|---|"]
    for flags, c in deployed_candidates():
        o = op_metrics(vb.simulate(path, c, setup.operational(12), trend=feats.trend), days)
        L.append(f"| {flags} | measured costs, $12 cap, top-up | {usd(o.volume_w)} | {usd(o.net_w)} | {o.vpd} | "
                 f"{o.fills_day:,.1f} | {o.kills_day:,.1f} | - | "
                 + (f"{o.cap_weeks}/{o.weeks} wk, median {o.cap_hours:.1f}" if o.cap_hours is not None else "never") + " |")
        for cap in (None, CAP_USD):
            ws = paper_weeks(path, feats.trend, c, 12, cap)
            kills = [w.first_kill_s / 3600 for w in ws if w.first_kill_s is not None]
            caps = [w.cap_first_s / 3600 for w in ws if w.cap_first_s is not None]
            vol = sum(w.volume for w in ws) / len(ws)
            net = sum(w.net for w in ws) / len(ws)
            L.append(f"| {flags} | PaperVenue 115/120, fixed $25, {'no cap' if cap is None else '$12 cap'} | {usd(vol)} | "
                     f"{usd(net)} | {vpd(vol, net)} | {sum(w.buys + w.sells for w in ws) / days:,.1f} | "
                     f"{sum(w.killed_capital for w in ws) / days:,.1f} | "
                     + (", ".join(f"{x:.1f}" for x in kills) if kills else "never") + " | "
                     + (", ".join(f"{x:.1f}" for x in caps) if caps else "never") + " |")
    L.append("")
    report = "\n".join(L)
    (out_dir / "HOLDOUT.md").write_text(report)
    return report


def deployed_candidates() -> list[tuple[str, vb.Candidate]]:
    """The strategies the paper tooling runs (2026-09-30), with the flags it takes."""
    return [("--strategy churn --period-s 60", vb.clock_churn(60, gated=False)),
            ("--strategy trend_churn --value 125 --period-s 60", vb.clock_churn(60, gated=True)),
            ("--strategy rotate --value 125 --period-s 1800 --exit-window-s 60", vb.clock_rotate(1800, 60)),
            ("--strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10", vb.minute_dip(30, 20, 10))]


_CANDS: list[vb.Candidate] = []
_SIG: vb.Execution | None = None


def _init_fixed(path: vb.PricePath, cands: list[vb.Candidate], sig: vb.Execution) -> None:
    global _PATH, _CANDS, _SIG
    _PATH, _CANDS, _SIG = path, cands, sig


def _null_fixed(job: tuple[str, int]) -> tuple[str, list[float]]:
    kind, seed = job
    rng = random.Random(seed)
    path = vb.permute(_PATH, 1, rng) if kind == "perm" else vb.random_walk(_PATH, rng)
    trend = vb.trend_series(path)
    return kind, [vb.simulate(path, c, _SIG, trend=trend).score_bp for c in _CANDS]


def paper_weeks(path: vb.PricePath, trend: list[int], cand: vb.Candidate, lots: int, cap: float | None) -> list[vb.Week]:
    """PaperVenue as deployed (115/120 bp, fixed $25, no top-up) from each Monday for one week."""
    n_weeks = round((path.times[-1] - path.times[0]) / vb.WEEK_S)
    out = []
    for k in range(n_weeks):
        a = vb.week_start_index(path, k)
        b = vb.week_start_index(path, k + 1) if k + 1 < n_weeks else len(path.times)
        sub = vb.PricePath(path.times[a:b], path.prices[a:b], path.history)
        ex = vb.Execution(lots, 115.0, 120.0, capital_usd=25.0, weekly_loss_cap_usd=cap, weekly_topup=False)
        out.append(vb.simulate(sub, cand, ex, trend=trend[a:b]).weeks[0])
    return out


def deployed(out_dir: Path, *, workers: int, n: int) -> str:
    started = time.time()
    cost = measured_costs.model(measured_costs.EVIDENCE, "CBBTC", "mean")
    cands = deployed_candidates()
    L = [f"# The paper tooling's strategies, as it defines them ({date.today().isoformat()})", "",
         "The paper-enablement work defines phase on the clock (floor(event_time_s / period_s) mod 2), rotation on "
         "the clock (exit in the first `exit-window-s` of every period) and the dip on minute closes. These differ "
         "from the research candidates A-D (phase flips per decided tick, rotation after a held time, dip on a tick "
         "SMA), so the paper run is compared with THIS file's prediction. Research period only; the holdout is "
         "untouched. Nothing here was optimised: every parameter is the paper tooling's. [BACKTESTED; nulls SIMULATED]", "",
         "| Flags | Research counterpart | Difference |", "|---|---|---|",
         "| churn --period-s 60 | A churn | same 2-minute cycle at exact 30 s ticks; with a slower cadence or an extra "
         "lost tick the clock version waits for the next odd minute |",
         "| trend_churn --value 125 --period-s 60 | B | as above, gated on trend > 0 |",
         "| rotate --value 125 --period-s 1800 --exit-window-s 60 | C (R=30, the in-sample choice) | clock period 30 min "
         "vs 30 min after each buy (31-min cycle) |",
         "| dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | D | 30 completed minute closes, strict < and > "
         "(the D grid had 10- and 60-minute tick windows, 25-100 bp entries); NOT in the pre-registered grid |", ""]
    for step in (30, 36):
        path, ticks, _ = load_path(*RESEARCH) if step == 30 else _load_step(RESEARCH, step)
        days = (path.times[-1] + step - path.times[0]) / vb.DAY_S
        trend = vb.trend_series(path)
        L += [f"## Cadence {step} s ({len(path.times):,} ticks{'; live cadence: 30 s throttle + ~12 s refresh' if step == 36 else ''})", "",
              "### Operational run: measured costs, $12/week cap, $25 restored weekly", "",
              "| Flags | Lots | Volume/wk | Net/wk | VPD | Week DD max | Worst week | Fills/day | Kills/day | Cap weeks, median h |",
              "|---|---|---|---|---|---|---|---|---|---|"]
        for flags, c in cands:
            for lots in (12, 16):
                o = op_metrics(vb.simulate(path, c, vb.Execution(lots, cost.buy_bps, cost.sell_bps,
                                                                  weekly_loss_cap_usd=CAP_USD), trend=trend), days)
                capw = f"{o.cap_weeks}/{o.weeks}, {o.cap_hours:.1f} h" if o.cap_hours is not None else f"0/{o.weeks}"
                L.append(f"| {flags} | {lots} | {usd(o.volume_w)} | {usd(o.net_w)} | {o.vpd} | {usd(o.week_dd)} | "
                         f"{usd(o.worst_week)} | {o.fills_day:,.1f} | {o.kills_day:,.1f} | {capw} |")
        L += ["", "### Paper as configured: PaperVenue 115/120 bp, fixed $25, no top-up; one week from each research Monday",
              "", "p5 / p50 / p95 over the research weeks. 'No cap' is the system as deployed (the kernel has no weekly "
              "loss limit); '$12 cap' assumes the operator halts buying at $12.", "",
              "| Flags | Lots | Cap | Volume first 24 h | Volume in the week | Net in the week | Fills | First capital kill (h) | Cap reached (h) |",
              "|---|---|---|---|---|---|---|---|---|"]
        for flags, c in cands:
            for lots in (12,):
                for cap in (None, CAP_USD):
                    ws = paper_weeks(path, trend, c, lots, cap)
                    f3 = lambda xs, fmt: " / ".join(fmt(pct(xs, q)) for q in (0.05, 0.5, 0.95)) if xs else "never"
                    first_day = []
                    for k, w in enumerate(ws):
                        a = vb.week_start_index(path, k)
                        b = min(len(path.times), a + vb.DAY_S // step)
                        sub = vb.PricePath(path.times[a:b], path.prices[a:b], path.history)
                        r = vb.simulate(sub, c, vb.Execution(lots, 115.0, 120.0, capital_usd=25.0,
                                                             weekly_loss_cap_usd=cap, weekly_topup=False), trend=trend[a:b])
                        first_day.append(r.volume)
                    kills = [w.first_kill_s / 3600 for w in ws if w.first_kill_s is not None]
                    caps = [w.cap_first_s / 3600 for w in ws if w.cap_first_s is not None]
                    L.append(f"| {flags} | {lots} | {'$12' if cap else 'none'} | {f3(first_day, usd)} | "
                             f"{f3([w.volume for w in ws], usd)} | {f3([w.net for w in ws], usd)} | "
                             f"{f3([w.buys + w.sells for w in ws], lambda x: f'{x:,.0f}')} | "
                             f"{f3(kills, lambda x: f'{x:.1f}')} ({len(kills)}/{len(ws)} wk) | "
                             f"{f3(caps, lambda x: f'{x:.1f}')} ({len(caps)}/{len(ws)} wk) |")
        L.append("")
        if step == 30:
            sig = vb.signal_execution(cost.buy_bps, cost.sell_bps)
            real = [vb.simulate(path, c, sig, trend=trend) for _, c in cands]
            jobs = [("perm", SEED + 30_000 + i) for i in range(n)] + [("rw", SEED + 40_000 + i) for i in range(n)]
            if workers <= 1:
                _init_fixed(path, [c for _, c in cands], sig)
                nulls = [_null_fixed(j) for j in jobs]
            else:
                with ProcessPoolExecutor(max_workers=workers, initializer=_init_fixed,
                                         initargs=(path, [c for _, c in cands], sig)) as pool:
                    nulls = list(pool.map(_null_fixed, jobs, chunksize=max(1, len(jobs) // (workers * 4))))
            L += [f"### Fixed-parameter battery (30 s; signal run, 25 lots, unlimited cash; {n} permutations, {n} random walks; "
                  "no optimisation, so no walk-forward)", "",
                  "| Flags | Round trips | Score | Perm p (null median) | Random-walk p (null median) |", "|---|---|---|---|---|"]
            for j, (flags, _) in enumerate(cands):
                perm = [x[j] for kind, x in nulls if kind == "perm"]
                rw = [x[j] for kind, x in nulls if kind == "rw"]
                L.append(f"| {flags} | {real[j].total('sells'):,} | {real[j].score_bp:+.1f} bp | {p_value(real[j].score_bp, perm):.3f} "
                         f"({statistics.median(perm):+.1f}) | {p_value(real[j].score_bp, rw):.3f} ({statistics.median(rw):+.1f}) |")
            L.append("")
    L += [f"Runtime {time.time() - started:.0f} s.", ""]
    report = "\n".join(L)
    (out_dir / "DEPLOYED.md").write_text(report)
    return report


def _load_step(period: tuple[date, date], step: int) -> tuple[vb.PricePath, TickSeries, int]:
    base, _, days_before = load_path(*period)
    ticks = load_ticks(PAIR, period[0], period[1], CACHE / "btc1s", step_s=step)
    return vb.PricePath(ticks.times, ticks.prices, base.history), ticks, days_before


def write_sources(out_dir: Path) -> None:
    lines = ["# BTCUSDT 1 s archives used (data.binance.vision), SHA-256 verified against each published CHECKSUM", ""]
    for label, (start, end) in (("research", RESEARCH), ("holdout", HOLDOUT)):
        ticks = load_ticks(PAIR, start, end, CACHE / "btc1s")
        lines += [f"## {label} {start} .. {end} (exclusive)"] + [f"{d}  {p}" for p, d in ticks.sources] + [""]
    (out_dir / "SOURCES.txt").write_text("\n".join(lines))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("stage", choices=("research", "deployed", "holdout"))
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--n-is", type=int, default=200)
    parser.add_argument("--n-rw", type=int, default=200)
    parser.add_argument("--n-wf", type=int, default=100)
    parser.add_argument("--recommended", default="", help="holdout: CID:LOTS,... as written in the ledger")
    args = parser.parse_args(argv)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.stage == "research":
        print(json.dumps(research(args.out_dir, workers=args.workers, n_is=args.n_is, n_rw=args.n_rw, n_wf=args.n_wf),
                         indent=1, default=str))
    elif args.stage == "deployed":
        print(deployed(args.out_dir, workers=args.workers, n=args.n_is))
    else:
        rec = [(c, int(l)) for c, l in (x.split(":") for x in args.recommended.split(",") if x)]
        print(holdout(args.out_dir, rec))
        write_sources(args.out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
