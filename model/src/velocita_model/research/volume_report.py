"""Band-rebalancer sweep on hourly closes: how much volume each band produces,
what it costs, and whether the rebalancing premium beats shuffled history.

Hourly closes miss intra-hour crossings, so trade counts and volume are
UNDERSTATED relative to a live monitor that checks more often.
"""

from __future__ import annotations

import argparse
import csv
import io
import math
import random
import time
import urllib.error
import zipfile
from datetime import date, datetime, timezone
from pathlib import Path

from ..adapters.base import ReadOnlyHttp
from . import metrics
from .bars import BINANCE_ARCHIVE, DataError, _months, verify_checksum
from .volume import BandParams, band_rebalance

BANDS = (0.005, 0.01, 0.02, 0.05)
COSTS = (1.0, 5.0, 10.0, 30.0)
HOURS_PER_YEAR = 24 * 365


def load_hourly(pair: str, start: date, end: date, cache: Path) -> tuple[list[int], list[float]]:
    http = ReadOnlyHttp(BINANCE_ARCHIVE, timeout_s=60)
    cache.mkdir(parents=True, exist_ok=True)
    times, closes = [], []
    for month in _months(start, end):
        path = f"/data/spot/monthly/klines/{pair}/1h/{pair}-1h-{month}.zip"
        cached = cache / f"{pair}-1h-{month}.zip"
        if not cached.exists():
            try:
                data = http.get(path).body
            except urllib.error.HTTPError as error:
                raise DataError(f"{pair} {month}: HTTP {error.code}") from error
            verify_checksum(data, http.get(path + ".CHECKSUM").body.decode("utf-8"))
            cached.write_bytes(data)
        with zipfile.ZipFile(io.BytesIO(cached.read_bytes())) as archive:
            (name,) = archive.namelist()
            for fields in csv.reader(io.StringIO(archive.read(name).decode("utf-8"))):
                if fields and fields[0].isdigit():
                    t = int(fields[0])
                    times.append(t // 1_000_000 if t >= 10**15 else t // 1000)
                    closes.append(float(fields[4]))
    return times, closes


def _shuffled(prices: list[float], rng: random.Random) -> list[float]:
    ratios = [prices[i] / prices[i - 1] for i in range(1, len(prices))]
    rng.shuffle(ratios)
    out = [prices[0]]
    for r in ratios:
        out.append(out[-1] * r)
    return out


def _premium(prices: list[float], band: float) -> float:
    """Growth of the rebalanced 50/50 over the un-rebalanced 50/50, cost-free."""
    rebalanced = 1 + metrics.total_return(band_rebalance(prices, BandParams(band=band, cost_bps=0.0)).returns)
    held = 0.5 + 0.5 * prices[-1] / prices[0]
    return math.log(rebalanced / held)


def run(pair: str, cache: Path, out: Path, *, n_perm: int, stake: float, start: date, end: date) -> str:
    started = time.time()
    times, prices = load_hourly(pair, start, end, cache)
    years = len(prices) / HOURS_PER_YEAR
    gaps = sum(1 for a, b in zip(times, times[1:]) if b - a != 3600)
    held = 0.5 + 0.5 * prices[-1] / prices[0]
    lines = [f"# Band rebalancer (volume strategy) on {pair} hourly closes ({date.today().isoformat()})", "",
             f"Binance archive (checksum-verified), {datetime.fromtimestamp(times[0], timezone.utc):%Y-%m-%d} .. "
             f"{datetime.fromtimestamp(times[-1], timezone.utc):%Y-%m-%d}, {len(prices)} hours ({years:.2f} years), "
             f"{gaps} gaps in the hourly series. 50/50 {pair[:-4]}/USDC, rebalanced to 50% when the share leaves "
             f"50% +/- band. Un-rebalanced 50/50 over the period: {held - 1:+.1%}.", "",
             f"Volume for a ${stake:,.0f} stake = stake x yearly volume multiple.", "",
             "| Band | Cost/side | Trades/yr | Volume multiple/yr | Volume/yr on stake | Cost drag/yr | "
             "Total return | vs un-rebalanced | Volume per $1 cost |",
             "|---|---|---|---|---|---|---|---|---|"]
    for band in BANDS:
        for bps in COSTS:
            r = band_rebalance(prices, BandParams(band=band, cost_bps=bps))
            total = metrics.total_return(r.returns)
            lines.append(f"| {band:.1%} | {bps:.0f} bp | {r.trades / years:,.0f} | {r.volume / years:,.1f}x | "
                         f"${stake * r.volume / years:,.0f} | {r.cost / years:.2%} | {total:+.1%} | "
                         f"{(1 + total) / held - 1:+.1%} | ${r.volume_per_cost:,.0f} |")
    lines += ["", "## Is the rebalancing premium more than noise?", "",
              "Rebalancing earns a premium even on a random walk (volatility harvesting), so the null is the same "
              f"hourly moves in shuffled order. {n_perm} shuffles per band, cost-free; p = share of shuffles with a "
              "premium at least as large.", "", "| Band | Real premium (log) | Null median | p |", "|---|---|---|---|"]
    rng = random.Random(42)
    shuffles = [_shuffled(prices, rng) for _ in range(n_perm)]
    for band in BANDS:
        real = _premium(prices, band)
        null = sorted(_premium(s, band) for s in shuffles)
        lines.append(f"| {band:.1%} | {real:+.4f} | {null[len(null) // 2]:+.4f} | {metrics.p_value(real, null):.3f} |")
    lines += ["", "## Caveats", "",
              "- Hourly closes miss intra-hour crossings: live volume at the same band would be higher.",
              "- Costs are assumptions until the True Markets markup is measured (tools/measure-markup.py).",
              "- A shuffled-null p-value > 0.05 means the premium is ordinary volatility harvesting, not "
              "exploitable mean reversion; the strategy then pays its costs from volatility alone.",
              f"- Runtime {time.time() - started:.0f} s.", ""]
    report = "\n".join(lines)
    out.write_text(report, encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pair", default="SOLUSDT")
    parser.add_argument("--cache", type=Path, default=Path.home() / ".cache" / "velocita-research" / "hourly")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--n-perm", type=int, default=200)
    parser.add_argument("--stake", type=float, default=20.0)
    parser.add_argument("--start", type=date.fromisoformat, default=date(2023, 1, 1))
    parser.add_argument("--end", type=date.fromisoformat, default=date(2026, 8, 31))
    args = parser.parse_args(argv)
    print(run(args.pair, args.cache, args.out, n_perm=args.n_perm, stake=args.stake, start=args.start, end=args.end))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
