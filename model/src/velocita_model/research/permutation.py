"""Null-hypothesis data for the permutation tests (Masters; neurotrader's MCPT).

`permute` shuffles WHICH DAY each daily move happened on, jointly across assets:
one random ordering of days is applied to every asset, so on any day where
assets traded together, their moves stay together (cross-sectional structure
survives) while any time structure a strategy could exploit is destroyed. Each
asset keeps its exact set of daily log returns, its first price, its final
price and its listing date. An asset that lists late draws its moves from the
same ordering restricted to its own trading days.

`random_walk` replaces each asset's moves with independent Gaussian draws of
the same mean and volatility: a parametric null with no fat tails and no
cross-asset correlation.

Volumes travel with the day they came from (permute) or stay put (random walk).
"""

from __future__ import annotations

import math
import random

from .bars import DailyBars


def _return_days(series: tuple[float | None, ...], start: int) -> list[int]:
    return [i for i in range(max(1, start), len(series)) if series[i] is not None and series[i - 1] is not None]


def permute(bars: DailyBars, *, start_index: int, rng: random.Random) -> DailyBars:
    n = len(bars.dates)
    order = list(range(max(1, start_index), n))
    rng.shuffle(order)
    new_close, new_volume = [], []
    for a, series in enumerate(bars.close):
        days = _return_days(series, start_index)
        valid = set(days)
        sources = [d for d in order if d in valid]  # the shared ordering, restricted to this asset
        prices = list(series)
        volumes = list(bars.quote_volume[a]) if bars.quote_volume is not None else None
        for destination, source in zip(days, sources):
            prices[destination] = prices[destination - 1] * (series[source] / series[source - 1])
            if volumes is not None:
                volumes[destination] = bars.quote_volume[a][source]
        new_close.append(tuple(prices))
        if volumes is not None:
            new_volume.append(tuple(volumes))
    return DailyBars(bars.dates, bars.symbols, tuple(new_close),
                     tuple(new_volume) if bars.quote_volume is not None else None, bars.eligible)


def random_walk(bars: DailyBars, *, start_index: int, rng: random.Random) -> DailyBars:
    new_close = []
    for series in bars.close:
        days = _return_days(series, start_index)
        logs = [math.log(series[d] / series[d - 1]) for d in days]
        prices = list(series)
        if len(logs) >= 2:
            mean = sum(logs) / len(logs)
            sd = math.sqrt(sum((x - mean) ** 2 for x in logs) / (len(logs) - 1))
            for d in days:
                prices[d] = prices[d - 1] * math.exp(rng.gauss(mean, sd))
        new_close.append(tuple(prices))
    return DailyBars(bars.dates, bars.symbols, tuple(new_close), bars.quote_volume, bars.eligible)
