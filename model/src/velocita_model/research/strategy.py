"""Minara's "whale" momentum rotation (published 2026-08-20), long-only spot.

Rules, as in the published source: on a rebalance day, among assets with at
least `min_history` daily closes and a median quote volume (last 30 days) of at
least `min_quote_volume`, hold only the one with the highest `momentum_days`
return. Invested fraction = `leverage_cap` x exposure, where exposure is 1.0,
`caution_exposure` when BTC is below its `ma_short`-day average, and
`risk_off_exposure` when BTC is below its `ma_long`-day average AND its
`btc_momentum_days` return is negative. The rest stays in cash (USDC).

Differences from their engine, all deliberate:
* spot, no leverage and no funding: the 0.5 cap means half the equity in cash;
* a signal at day t's close fills at day t+`lag_days`'s close (they used
  next-bar fills; with daily closes only, filling at the signal close would
  trade on a price nobody could have acted on);
* top-1 by raw momentum equals top-1 by their robust z-score (a monotone map).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .bars import DailyBars


@dataclass(frozen=True)
class Params:
    momentum_days: int = 90
    min_history: int = 200
    min_quote_volume: float = 1.0e7
    volume_window: int = 30
    ma_short: int = 100
    ma_long: int = 200
    btc_momentum_days: int = 30
    leverage_cap: float = 0.5
    caution_exposure: float = 0.50
    risk_off_exposure: float = 0.15
    cost_bps: float = 30.0          # per side, on traded value; an ASSUMPTION until venue fees are known
    lag_days: int = 1
    rebalance_weekday: Optional[int] = 0  # Monday; None rebalances daily
    regime_symbol: str = "BTC"


@dataclass
class Result:
    returns: list[float]                       # simple return of equity, close i-1 -> close i
    holdings: list[tuple[str | None, float]]  # target held after close i
    fills: int = 0
    turnover: float = 0.0                     # traded value / equity, summed
    exposure_days: int = 0


def _prefix(values: list[float | None]) -> tuple[list[float], list[int]]:
    sums, counts = [0.0], [0]
    for v in values:
        sums.append(sums[-1] + (v or 0.0))
        counts.append(counts[-1] + (v is not None))
    return sums, counts


def backtest(bars: DailyBars, p: Params) -> Result:
    n, symbols = len(bars.dates), bars.symbols
    close = bars.close
    btc = symbols.index(p.regime_symbol)
    btc_sums, btc_counts = _prefix(list(close[btc]))
    counts = [_prefix(list(series))[1] for series in close]
    volumes = bars.quote_volume if p.min_quote_volume > 0 else None
    eligible = bars.eligible

    def decide(i: int) -> tuple[int | None, float]:
        c = close[btc]
        if (i + 1 < p.ma_long or btc_counts[i + 1] - btc_counts[i + 1 - p.ma_long] < p.ma_long
                or i < p.btc_momentum_days or c[i - p.btc_momentum_days] is None):
            return None, 0.0
        last = c[i]
        ma_long = (btc_sums[i + 1] - btc_sums[i + 1 - p.ma_long]) / p.ma_long
        ma_short = (btc_sums[i + 1] - btc_sums[i + 1 - p.ma_short]) / p.ma_short
        if last < ma_long and last / c[i - p.btc_momentum_days] - 1 < 0:
            exposure = p.risk_off_exposure
        elif last < ma_short:
            exposure = p.caution_exposure
        else:
            exposure = 1.0
        best, best_momentum = None, None
        for a, series in enumerate(close):
            if eligible is not None and not eligible[a][i]:
                continue
            if series[i] is None or i < p.momentum_days or series[i - p.momentum_days] is None:
                continue
            if i + 1 < p.min_history or counts[a][i + 1] - counts[a][i + 1 - p.min_history] < p.min_history:
                continue
            if volumes is not None:
                window = sorted(v for v in volumes[a][max(0, i + 1 - p.volume_window):i + 1] if v is not None)
                if not window or window[len(window) // 2] < p.min_quote_volume:
                    continue
            momentum = series[i] / series[i - p.momentum_days] - 1
            if best_momentum is None or momentum > best_momentum:
                best, best_momentum = a, momentum
        if best is None:
            return None, 0.0
        return best, p.leverage_cap * exposure

    result = Result(returns=[0.0] * n, holdings=[(None, 0.0)] * n)
    pending: dict[int, tuple[int | None, float]] = {}
    asset: int | None = None
    fraction, value, cash, previous = 0.0, 0.0, 1.0, 1.0
    for i in range(n):
        if i > 0 and asset is not None:
            if close[asset][i] is None:
                # Halted or delisted: sold at its last close (generous for a
                # collapse, since the last print may not have been fillable).
                equity = cash + value
                cash, value = equity - value * p.cost_bps / 10_000, 0.0
                asset, fraction = None, 0.0
            else:
                value *= close[asset][i] / close[asset][i - 1]
        if p.rebalance_weekday is None or bars.dates[i].weekday() == p.rebalance_weekday:
            if i + p.lag_days < n:
                pending[i + p.lag_days] = decide(i)
        if i in pending:
            target, target_fraction = pending.pop(i)
            if target is not None and close[target][i] is None:
                target, target_fraction = None, 0.0  # stopped trading before the fill
            equity = cash + value
            new_value = target_fraction * equity
            traded = abs(new_value - value) if target == asset else value + new_value
            if traded > 0:
                equity -= traded * p.cost_bps / 10_000
                result.fills += 1
                result.turnover += traded / (cash + value)
            value = target_fraction * equity
            cash = equity - value
            asset, fraction = (target, target_fraction) if target_fraction > 0 else (None, 0.0)
        equity = cash + value
        result.returns[i] = equity / previous - 1
        previous = equity
        result.holdings[i] = (symbols[asset] if asset is not None else None, fraction)
        result.exposure_days += asset is not None
    return result
