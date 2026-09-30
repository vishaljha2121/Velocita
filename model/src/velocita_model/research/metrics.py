"""Scores over a series of simple daily returns."""

from __future__ import annotations

import math
from typing import Sequence


def profit_factor(returns: Sequence[float]) -> float:
    """Sum of positive daily log returns over the magnitude of the negative ones.
    inf when there are gains and no losses; 0 when nothing moved."""
    gains = losses = 0.0
    for r in returns:
        x = math.log1p(r)
        if x > 0:
            gains += x
        elif x < 0:
            losses -= x
    if losses == 0:
        return math.inf if gains > 0 else 0.0
    return gains / losses


def total_return(returns: Sequence[float]) -> float:
    equity = 1.0
    for r in returns:
        equity *= 1 + r
    return equity - 1


def max_drawdown(returns: Sequence[float]) -> float:
    equity = peak = 1.0
    worst = 0.0
    for r in returns:
        equity *= 1 + r
        peak = max(peak, equity)
        worst = max(worst, 1 - equity / peak)
    return worst


def sharpe(returns: Sequence[float], periods_per_year: int = 365) -> float:
    n = len(returns)
    if n < 2:
        return 0.0
    mean = sum(returns) / n
    var = sum((r - mean) ** 2 for r in returns) / (n - 1)
    return 0.0 if var == 0 else mean / math.sqrt(var) * math.sqrt(periods_per_year)


def cagr(returns: Sequence[float], periods_per_year: int = 365) -> float:
    if not returns:
        return 0.0
    growth = 1 + total_return(returns)
    return growth ** (periods_per_year / len(returns)) - 1 if growth > 0 else -1.0


def p_value(real: float, null: Sequence[float]) -> float:
    """(1 + #null >= real) / (N + 1): the real result counts as one draw, so a
    p-value is never exactly zero."""
    return (1 + sum(1 for x in null if x >= real)) / (len(null) + 1)
