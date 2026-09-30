"""The five-test battery (Masters, "Permutation and Randomization Tests for
Trading System Development"; neurotrader's walk-through):

1. in-sample: optimize one parameter on the in-sample period;
2. in-sample permutation: re-run the WHOLE optimization on permuted histories;
   if the real best score is not rare among the permuted best scores, the
   "edge" is what an optimizer finds in noise (selection bias measured);
3. walk-forward: re-optimize on a rolling training window, trade the next
   step out of sample, and stitch the out-of-sample days together;
4. walk-forward permutation: permute only the data after the first training
   window and repeat 3; is the real out-of-sample score rare?
5. random walk: repeat 1 on Gaussian random walks with each asset's own drift
   and volatility (a parametric null that ignores fat tails and correlation).

The optimizer re-runs inside every null draw. Scoring a fixed parameter on the
nulls would ignore the selection step, which is the thing being tested.
"""

from __future__ import annotations

import random
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, replace
from typing import Callable, Sequence

from . import metrics, permutation
from .bars import DailyBars
from .strategy import Params, backtest

Objective = Callable[[Sequence[float]], float]


@dataclass(frozen=True)
class Best:
    momentum_days: int
    score: float
    params: Params


def optimize(bars: DailyBars, base: Params, grid: Sequence[int], objective: Objective = metrics.profit_factor) -> Best:
    best: Best | None = None
    for m in grid:
        params = replace(base, momentum_days=m)
        score = objective(backtest(bars, params).returns)
        if best is None or score > best.score:
            best = Best(m, score, params)
    assert best is not None, "empty grid"
    return best


@dataclass(frozen=True)
class WalkForward:
    returns: list[float]           # out-of-sample daily returns, stitched
    first_oos_index: int
    choices: list[tuple[str, int]]  # (segment start date, chosen momentum_days)


def walk_forward(bars: DailyBars, base: Params, grid: Sequence[int], *, train_days: int, step_days: int,
                 objective: Objective = metrics.profit_factor) -> WalkForward:
    n = len(bars.dates)
    returns: list[float] = []
    choices: list[tuple[str, int]] = []
    for start in range(train_days, n, step_days):
        end = min(n, start + step_days)
        best = optimize(bars.slice(start - train_days, start), base, grid, objective)
        run = backtest(bars.slice(0, end), best.params)  # full history up to END: warm-up and state carry over
        returns += run.returns[start:end]
        choices.append((bars.dates[start].isoformat(), best.momentum_days))
    return WalkForward(returns, train_days, choices)


# -- null distributions (module-level so worker processes can import them) --

def _is_null(args: tuple) -> float:
    bars, base, grid, seed, kind = args
    rng = random.Random(seed)
    make = permutation.permute if kind == "permute" else permutation.random_walk
    return optimize(make(bars, start_index=1, rng=rng), base, grid).score


def _wf_null(args: tuple) -> float:
    bars, base, grid, seed, train, step = args
    shuffled = permutation.permute(bars, start_index=train, rng=random.Random(seed))
    return metrics.profit_factor(walk_forward(shuffled, base, grid, train_days=train, step_days=step).returns)


@dataclass(frozen=True)
class NullTest:
    real: float
    null: list[float]
    p_value: float


def _run(fn, jobs: list, workers: int) -> list[float]:
    if workers <= 1:
        return [fn(j) for j in jobs]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(fn, jobs, chunksize=max(1, len(jobs) // (workers * 4))))


def in_sample_permutation_test(bars: DailyBars, base: Params, grid: Sequence[int], *, n: int, seed: int,
                               workers: int = 1, kind: str = "permute") -> NullTest:
    real = optimize(bars, base, grid).score
    null = _run(_is_null, [(bars, base, tuple(grid), seed + i, kind) for i in range(n)], workers)
    return NullTest(real, null, metrics.p_value(real, null))


def walk_forward_permutation_test(bars: DailyBars, base: Params, grid: Sequence[int], *, train_days: int,
                                  step_days: int, n: int, seed: int, workers: int = 1) -> NullTest:
    real = metrics.profit_factor(walk_forward(bars, base, grid, train_days=train_days, step_days=step_days).returns)
    jobs = [(bars, base, tuple(grid), seed + i, train_days, step_days) for i in range(n)]
    null = _run(_wf_null, jobs, workers)
    return NullTest(real, null, metrics.p_value(real, null))
