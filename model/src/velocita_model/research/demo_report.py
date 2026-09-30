"""Three demo rule strategies through the full battery with MEASURED costs.

Protocol, fixed before any result was seen:
* assets: BTC (traded on True Markets as cbBTC, costed with cbBTC quotes) and
  SOL, each alone. The largest crypto asset and the most liquid True Markets
  native asset; neither is chosen for past performance of these rules.
* data: Binance spot daily archive, checksum-verified, 2018-01 .. 2026-08.
* in-sample to 2023-07-31; walk-forward 3-year train, 91-day step.
* one tuned parameter per strategy on the grid in `rules.DEMOS`.
* costs: the mean all-in cost per side from True Markets quotes (2026-09-28),
  and a stress run at the worst quote; nothing assumed.
* confirmation on True Markets' own daily history with the in-sample choice.
"""

from __future__ import annotations

import argparse
import os
import time
from dataclasses import dataclass
from datetime import date
from pathlib import Path

from . import battery, costs, metrics
from .bars import DailyBars, load_binance, load_truemarkets
from .rules import DEMOS, Costs, backtest

ASSETS = {"BTC": ("BTCUSDT", "CBBTC"), "SOL": ("SOLUSDT", "SOL")}
IS_END = date(2023, 8, 1)


@dataclass(frozen=True)
class DemoRun:
    """Picklable RUN(bars, value) for one demo strategy under one cost model."""

    strategy: str
    costs: Costs

    def __call__(self, bars: DailyBars, value: int) -> list[float]:
        program, settings = DEMOS[self.strategy][0](value)
        return backtest(bars, 0, program, settings, self.costs).returns


def _row(label: str, returns: list[float], trades: int | None = None, years: float | None = None) -> str:
    extra = f" | {trades / years:.1f}" if trades is not None and years else " | -"
    return (f"| {label} | {metrics.profit_factor(returns):.3f} | {metrics.total_return(returns):+.1%} | "
            f"{metrics.cagr(returns):+.1%} | {metrics.max_drawdown(returns):.1%} | {metrics.sharpe(returns):.2f}{extra} |")


def _hold(bars: DailyBars, start: int, end: int, cost: Costs) -> list[float]:
    c = bars.close[0]
    r = [0.0 if c[i - 1] is None or c[i] is None else c[i] / c[i - 1] - 1 for i in range(start, end)]
    if r:
        r[0] = (1 + r[0]) * (1 - cost.buy_bps / 10_000) - 1  # one measured buy, never sold
    return r


def _trades(bars: DailyBars, strategy: str, value: int, cost: Costs, start: int, end: int) -> int:
    program, settings = DEMOS[strategy][0](value)
    positions = backtest(bars.slice(0, end), 0, program, settings, cost).positions
    return sum(1 for i in range(max(1, start), end) if positions[i] != positions[i - 1])


def run(cache: Path, out: Path, *, n_is: int, n_wf: int, n_rw: int, workers: int, end: date) -> str:
    started = time.time()
    lines = [f"# Demo rule strategies — full battery with measured True Markets costs ({date.today()})", "",
             "Costs per side (bp) from `truemarkets_markup_2026-09-28/quotes_buy_sell_1631utc.json`:"]
    cost_models = {}
    for asset, (_pair, tm_symbol) in ASSETS.items():
        mean, worst = costs.model(costs.EVIDENCE, tm_symbol, "mean"), costs.model(costs.EVIDENCE, tm_symbol, "worst")
        cost_models[asset] = (mean, worst)
        lines.append(f"- {asset} (as {tm_symbol}): buy {mean.buy_bps:.1f} / sell {mean.sell_bps:.1f} mean; "
                     f"worst quote buy {worst.buy_bps:.1f} / sell {worst.sell_bps:.1f}")
    lines += ["", "Profit factor = sum of daily log gains / sum of daily log losses. p = share of null draws "
              "(each fully re-optimized) scoring at least the real result.", ""]

    summary = ["## Summary", "", "| Asset | Strategy | IS choice | IS perm p | WF PF | WF total | WF max DD | "
               "Hold total (same dates) | WF perm p | Random-walk p |", "|---|---|---|---|---|---|---|---|---|---|"]
    details: list[str] = []
    for asset, (pair, tm_symbol) in ASSETS.items():
        bars = load_binance({asset: pair}, date(2018, 1, 1), end, cache)
        split = bars.date_index(IS_END)
        mean, worst = cost_models[asset]
        for name, (_build, grid) in DEMOS.items():
            run_mean = DemoRun(name, mean)
            ins = bars.slice(0, split)
            best = battery.optimize(ins, grid, run_mean)
            is_test = battery.in_sample_test(ins, grid, run_mean, n=n_is, seed=100, workers=workers)
            rw_test = battery.in_sample_test(ins, grid, run_mean, n=n_rw, seed=300, workers=workers,
                                             kind="random_walk")
            wf = battery.walk_forward(bars, grid, run_mean, train_days=1095, step_days=91)
            wf_test = battery.walk_forward_test(bars, grid, run_mean, train_days=1095, step_days=91, n=n_wf,
                                                seed=500, workers=workers)
            oos, n = wf.first_oos_index, len(bars.dates)
            years = (n - oos) / 365
            wf_worst = battery.walk_forward(bars, grid, DemoRun(name, worst), train_days=1095, step_days=91)
            hold = _hold(bars, oos, n, mean)
            trades = sum(_trades(bars, name, v, mean, bars.date_index(date.fromisoformat(d)),
                                 min(n, bars.date_index(date.fromisoformat(d)) + 91)) for d, v in wf.choices)
            summary.append(
                f"| {asset} | {name} | {best.value} | {is_test.p_value:.3f} | {metrics.profit_factor(wf.returns):.3f} | "
                f"{metrics.total_return(wf.returns):+.1%} | {metrics.max_drawdown(wf.returns):.1%} | "
                f"{metrics.total_return(hold):+.1%} | {wf_test.p_value:.3f} | {rw_test.p_value:.3f} |")
            details += [f"## {asset} · {name}", "",
                        f"In-sample {bars.dates[0]} .. {bars.dates[split - 1]}; grid scores: " +
                        ", ".join(f"{v}: {s:.3f}" for v, s in best.scores) + f". Best **{best.value}**.", "",
                        f"Walk-forward out-of-sample {bars.dates[oos]} .. {bars.dates[-1]}; choices: " +
                        ", ".join(f"{d}: {v}" for d, v in wf.choices), "",
                        "| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |",
                        "|---|---|---|---|---|---|---|",
                        _row("walk-forward, mean measured cost", wf.returns, trades, years),
                        _row("walk-forward, worst measured quote", wf_worst.returns),
                        _row(f"hold {asset} (one measured buy)", hold), "",
                        f"Null tests: in-sample permutation p = {is_test.p_value:.3f} ({n_is}), walk-forward "
                        f"permutation p = {wf_test.p_value:.3f} ({n_wf}), random walk p = {rw_test.p_value:.3f} ({n_rw}).",
                        ""]
            try:
                tm = load_truemarkets([tm_symbol])
                tm = DailyBars(tm.dates, (asset,), tm.close, None)
                tm_run = run_mean(tm, best.value)
                program, settings = DEMOS[name][0](best.value)
                first = next((i for i, p in enumerate(backtest(tm, 0, program, settings, mean).positions) if p), None)
                start = first if first is not None else len(tm.dates) - 1
                b0, b1 = bars.date_index(tm.dates[start]), len(bars.dates)
                t1 = min(len(tm.dates), start + (b1 - b0))
                details += [f"True Markets confirmation ({tm_symbol} history, in-sample choice {best.value}), "
                            f"{tm.dates[start]} .. {tm.dates[t1 - 1]}:", "",
                            "| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |",
                            "|---|---|---|---|---|---|---|",
                            _row("True Markets prices", tm_run[start:t1]),
                            _row("Binance prices, same dates", run_mean(bars, best.value)[b0:b0 + (t1 - start)]), ""]
            except Exception as error:  # confirmation is reported, never silently dropped
                details += [f"True Markets confirmation not run: {type(error).__name__}: {error}", ""]
    lines += summary + [""] + details + [
        "## Caveats", "",
        "- Daily closes; a decision fills at the next close. Intraday price at the real fill will differ.",
        "- Costs measured at $10-$100 on one day; larger size or other days may cost more (route price).",
        "- BTC is costed as cbBTC (Base); cbBTC tracks BTC 1:1 but trades on a different venue path.",
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
