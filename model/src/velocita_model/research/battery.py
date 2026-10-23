"""The five-test battery for any strategy expressed as RUN(bars, value) -> daily
returns, where VALUE is the one tuned parameter. RUN must be picklable (a
module-level class instance) so null draws can run in worker processes. Same
definitions as `validation.py`: the optimizer re-runs inside every null draw."""

from __future__ import annotations

import random
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from typing import Callable, Sequence

from . import metrics, permutation
from .bars import DailyBars

Run = Callable[[DailyBars, int], list[float]]


@dataclass(frozen=True)
class Best:
    value: int
    score: float
    scores: tuple[tuple[int, float], ...]


def optimize(bars: DailyBars, grid: Sequence[int], run: Run) -> Best:
    scores = tuple((v, metrics.profit_factor(run(bars, v))) for v in grid)
    value, score = max(scores, key=lambda s: s[1])
    return Best(value, score, scores)


@dataclass(frozen=True)
class WalkForward:
    returns: list[float]
    first_oos_index: int
    choices: list[tuple[str, int]]


def walk_forward(bars: DailyBars, grid: Sequence[int], run: Run, *, train_days: int, step_days: int) -> WalkForward:
    returns, choices = [], []
    n = len(bars.dates)
    for start in range(train_days, n, step_days):
        end = min(n, start + step_days)
        best = optimize(bars.slice(start - train_days, start), grid, run)
        returns += run(bars.slice(0, end), best.value)[start:end]
        choices.append((bars.dates[start].isoformat(), best.value))
    return WalkForward(returns, train_days, choices)


def _is_null(args: tuple) -> float:
    bars, grid, run, seed, kind = args
    make = permutation.permute if kind == "permute" else permutation.random_walk
    return optimize(make(bars, start_index=1, rng=random.Random(seed)), grid, run).score


def _wf_null(args: tuple) -> float:
    bars, grid, run, seed, train, step = args
    shuffled = permutation.permute(bars, start_index=train, rng=random.Random(seed))
    return metrics.profit_factor(walk_forward(shuffled, grid, run, train_days=train, step_days=step).returns)


@dataclass(frozen=True)
class NullTest:
    real: float
    null: list[float]
    p_value: float


def _map(fn, jobs: list, workers: int) -> list[float]:
    if workers <= 1:
        return [fn(j) for j in jobs]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(fn, jobs, chunksize=max(1, len(jobs) // (workers * 4))))


def in_sample_test(bars: DailyBars, grid: Sequence[int], run: Run, *, n: int, seed: int, workers: int,
                   kind: str = "permute") -> NullTest:
    real = optimize(bars, grid, run).score
    null = _map(_is_null, [(bars, tuple(grid), run, seed + i, kind) for i in range(n)], workers)
    return NullTest(real, null, metrics.p_value(real, null))


def walk_forward_test(bars: DailyBars, grid: Sequence[int], run: Run, *, train_days: int, step_days: int,
                      n: int, seed: int, workers: int) -> NullTest:
    real = metrics.profit_factor(walk_forward(bars, grid, run, train_days=train_days, step_days=step_days).returns)
    null = _map(_wf_null, [(bars, tuple(grid), run, seed + i, train_days, step_days) for i in range(n)], workers)
    return NullTest(real, null, metrics.p_value(real, null))
