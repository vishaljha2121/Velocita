"""Runs the full battery on Minara's momentum rotation and writes a report.

The protocol is fixed BEFORE looking at any result (Minara's own advice: freeze
universe, dates, costs and construction; change one variable at a time):

* universe: Minara's shortlist restricted to what True Markets lists AND
  Binance has spot history for: BTC, ETH, SOL, PENDLE, ZEC, PENGU;
* in-sample: data start to 2023-07-31; out-of-sample starts where their own
  headline window starts, so their selection window is our holdout;
* one optimized variable: momentum_days over 20..180 step 10;
* costs 30 bp per side (assumption), sensitivity at 0/10/30/60;
* walk-forward: 3-year train, 91-day step.

Known limitation stated up front: the permutation tests measure timing skill
given this universe. They cannot detect that the universe itself was chosen
with hindsight (HYPE, ZEC, PENGU are there because they went up).
"""

from __future__ import annotations

import argparse
import math
import os
import time
from dataclasses import replace
from datetime import date
from pathlib import Path

from . import metrics, validation
from .bars import DailyBars, load_binance, load_truemarkets
from .strategy import Params, backtest

UNIVERSE = {"BTC": "BTCUSDT", "ETH": "ETHUSDT", "SOL": "SOLUSDT", "PENDLE": "PENDLEUSDT",
            "ZEC": "ZECUSDT", "PENGU": "PENGUUSDT"}
TM_UNIVERSE = {"BTC": "CBBTC", "ETH": "CBETH", "SOL": "SOL", "PENDLE": "PENDLE", "ZEC": "CBZEC",
               "PENGU": "PENGU", "HYPE": "HYPE", "VVV": "VVV"}
GRID = list(range(20, 181, 10))
IS_END = date(2023, 8, 1)


def _row(label: str, returns: list[float]) -> str:
    return (f"| {label} | {metrics.profit_factor(returns):.3f} | {metrics.total_return(returns):+.1%} | "
            f"{metrics.cagr(returns):+.1%} | {metrics.max_drawdown(returns):.1%} | {metrics.sharpe(returns):.2f} |")


def _buy_hold(bars: DailyBars, symbol: str, start: int, end: int) -> list[float]:
    c = bars.close[bars.index(symbol)]
    return [0.0 if c[i - 1] is None or c[i] is None else c[i] / c[i - 1] - 1 for i in range(start, end)]


def _hist(values: list[float], real: float) -> str:
    finite = sorted(v for v in values if math.isfinite(v))
    q = lambda f: finite[min(len(finite) - 1, int(f * len(finite)))] if finite else float("nan")
    return f"null median {q(0.5):.3f}, 95th pct {q(0.95):.3f}, max {finite[-1] if finite else float('nan'):.3f}; real {real:.3f}"


def battery(bars: DailyBars, split: int, base: Params, *, n_is: int, n_wf: int, n_rw: int,
            workers: int) -> list[str]:
    """Sections 1-5 and the cost table, identical for every universe."""
    lines: list[str] = []
    # 1. In-sample optimization
    ins = bars.slice(0, split)
    best = validation.optimize(ins, base, GRID)
    scores = [(m, metrics.profit_factor(backtest(ins, replace(base, momentum_days=m)).returns)) for m in GRID]
    lines += ["## 1. In-sample optimization", "",
              "momentum_days → profit factor: " + ", ".join(f"{m}: {s:.3f}" for m, s in scores), "",
              f"Best: **{best.momentum_days} days**, profit factor {best.score:.3f} (Minara's 90: "
              f"{dict(scores)[90]:.3f}).", "",
              "| Series | Profit factor | Total | CAGR | Max DD | Sharpe |", "|---|---|---|---|---|---|",
              _row(f"strategy, {best.momentum_days}d, in-sample", backtest(ins, best.params).returns),
              _row("BTC buy & hold, in-sample", _buy_hold(bars, "BTC", 1, split)), ""]

    # 2. In-sample permutation test
    t = validation.in_sample_permutation_test(ins, base, GRID, n=n_is, seed=1000, workers=workers)
    lines += ["## 2. In-sample permutation test", "",
              f"{n_is} permutations, each fully re-optimized. **p = {t.p_value:.3f}**. {_hist(t.null, t.real)}.",
              "Low p (< 0.05): the optimized in-sample score is rarely matched by an optimizer run on shuffled "
              "history. High p: the optimizer finds this much 'edge' in noise.", ""]

    # 3. Walk-forward
    wf = validation.walk_forward(bars, base, GRID, train_days=1095, step_days=91)
    oos = wf.first_oos_index
    lines += ["## 3. Walk-forward (3-year train, 91-day step)", "",
              f"Out-of-sample {bars.dates[oos]} .. {bars.dates[-1]}. Chosen windows: " +
              ", ".join(f"{d}: {m}" for d, m in wf.choices), "",
              "| Series | Profit factor | Total | CAGR | Max DD | Sharpe |", "|---|---|---|---|---|---|",
              _row("walk-forward strategy", wf.returns),
              _row("fixed 90d (Minara) same dates", backtest(bars, base).returns[oos:]),
              _row("BTC buy & hold same dates", _buy_hold(bars, "BTC", oos, len(bars.dates))), ""]
    minara_window = (bars.date_index(date(2023, 8, 1)), bars.date_index(date(2025, 8, 2)))
    lines += [f"Minara's headline window 2023-08-01 .. 2025-08-01 inside our walk-forward: " +
              _row("walk-forward", wf.returns[minara_window[0] - oos:minara_window[1] - oos]).strip("| ").replace(" | ", ", "),
              ""]

    # 4. Walk-forward permutation test
    wt = validation.walk_forward_permutation_test(bars, base, GRID, train_days=1095, step_days=91, n=n_wf,
                                                  seed=5000, workers=workers)
    lines += ["## 4. Walk-forward permutation test", "",
              f"{n_wf} permutations of the data after the first training window. **p = {wt.p_value:.3f}**. "
              f"{_hist(wt.null, wt.real)}.", ""]

    # 5. Random-walk null
    rw = validation.in_sample_permutation_test(ins, base, GRID, n=n_rw, seed=9000, workers=workers, kind="random_walk")
    lines += ["## 5. Random-walk test", "",
              f"{n_rw} Gaussian random-walk histories (each asset's own drift and volatility, no correlation), "
              f"fully re-optimized. **p = {rw.p_value:.3f}**. {_hist(rw.null, rw.real)}.", ""]

    # Cost sensitivity (out-of-sample, walk-forward choices re-run per cost)
    lines += ["## Cost sensitivity (walk-forward, out-of-sample)", "",
              "| Cost per side | Profit factor | Total | CAGR | Max DD | Sharpe |", "|---|---|---|---|---|---|"]
    for bps in (0.0, 10.0, 30.0, 60.0):
        r = validation.walk_forward(bars, replace(base, cost_bps=bps), GRID, train_days=1095, step_days=91)
        lines.append(_row(f"{bps:.0f} bp", r.returns))
    lines.append("")
    return lines


def run(cache: Path, out: Path, *, n_is: int, n_wf: int, n_rw: int, workers: int, end: date) -> str:
    started = time.time()
    bars = load_binance(UNIVERSE, date(2018, 1, 1), end, cache)
    split = bars.date_index(IS_END)
    base = Params()
    lines = [f"# Minara momentum rotation — permutation-test battery ({date.today().isoformat()})", "",
             f"Data: Binance spot daily closes {bars.dates[0]} .. {bars.dates[-1]} ({len(bars.dates)} days), "
             f"universe {', '.join(bars.symbols)}; first close per asset: " +
             ", ".join(f"{s} {bars.dates[next(i for i, c in enumerate(bars.close[a]) if c is not None)]}"
                       for a, s in enumerate(bars.symbols)) + ".",
             f"In-sample {bars.dates[0]} .. {bars.dates[split - 1]}; out-of-sample from {bars.dates[split]}.",
             f"Costs {base.cost_bps:.0f} bp per side (assumption); fill at next close; Monday rebalance; "
             f"invested fraction 0.5 x BTC-regime exposure.", ""]

    lines += battery(bars, split, base, n_is=n_is, n_wf=n_wf, n_rw=n_rw, workers=workers)

    # True Markets confirmation (fixed 90-day rule, no volume screen: TM history has no volume)
    try:
        tm = load_truemarkets(list(TM_UNIVERSE.values()))
        tm = DailyBars(tm.dates, tuple(TM_UNIVERSE), tm.close, None)
        tm_params = replace(base, min_quote_volume=0.0)
        tm_result = backtest(tm, tm_params)
        # Compare only from the first day the True Markets run could hold anything
        # (after its warm-up) to the last day both sources cover.
        first = next(i for i, (sym, _f) in enumerate(tm_result.holdings) if sym is not None)
        start, end = tm.dates[first], min(tm.dates[-1], bars.dates[-1])
        t0, t1 = first, tm.dates.index(end) + 1
        b0, b1 = bars.date_index(start), bars.dates.index(end) + 1
        bn_run = backtest(bars, tm_params).returns[b0:b1]
        tm_only = backtest(DailyBars(tm.dates, tm.symbols, tm.close, None),
                           tm_params).returns  # same as tm_result; kept explicit for the 6-asset variant below
        six = [s for s in tm.symbols if s in bars.symbols]
        tm6 = DailyBars(tm.dates, tuple(six), tuple(tm.close[tm.index(s)] for s in six), None)
        lines += ["## True Markets confirmation (fixed 90-day rule, no volume screen)", "",
                  f"True Markets daily history {tm.dates[0]} .. {tm.dates[-1]}; compared {start} .. {end}, "
                  "the first day the True Markets run could hold a position to the last day both sources cover.",
                  "", "| Series | Profit factor | Total | CAGR | Max DD | Sharpe |", "|---|---|---|---|---|---|",
                  _row("TM prices, 8 assets (adds HYPE, VVV)", tm_only[t0:t1]),
                  _row("TM prices, same 6 assets as Binance", backtest(tm6, tm_params).returns[t0:t1]),
                  _row("Binance prices, 6 assets", bn_run), ""]
    except Exception as error:  # confirmation is best-effort; the report says so
        lines += ["## True Markets confirmation", "", f"Not run: {type(error).__name__}: {error}", ""]

    lines += ["## Caveats", "",
              "- Universe chosen with hindsight by Minara; the tests above cannot measure that bias.",
              "- Costs are assumptions until True Markets' DeFi fee schedule and RFQ markups are known (D-5).",
              "- Daily closes only: fills at the next close ignore intraday slippage.",
              "- Random-walk null ignores fat tails and cross-asset correlation; the permutation nulls keep both.",
              "- The grid (20..180) was fixed before any result was seen; a best value on its edge is reported, "
              "not chased by widening the grid afterwards.",
              f"- Runtime {time.time() - started:.0f} s with {workers} workers.", ""]
    report = "\n".join(lines)
    out.write_text(report, encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cache", type=Path, default=Path.home() / ".cache" / "velocita-research" / "binance")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--n-is", type=int, default=1000)
    parser.add_argument("--n-wf", type=int, default=200)
    parser.add_argument("--n-rw", type=int, default=1000)
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--end", type=date.fromisoformat, default=date(2026, 8, 31))
    args = parser.parse_args(argv)
    print(run(args.cache, args.out, n_is=args.n_is, n_wf=args.n_wf, n_rw=args.n_rw, workers=args.workers,
              end=args.end))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
