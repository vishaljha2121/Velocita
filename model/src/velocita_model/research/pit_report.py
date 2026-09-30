"""Minara's rotation on a POINT-IN-TIME universe: the same battery as
`minara_report`, but the coins come from each month's top 10 Binance USDT spot
pairs by the previous month's quote volume, chosen from every pair that ever
existed (delisted ones included). Protocol fixed before any result was seen.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from collections import Counter
from datetime import date
from pathlib import Path

from ..adapters.base import ReadOnlyHttp
from . import universe
from .bars import DailyBars, load_binance_daily_api, load_binance_monthly_volume
from .minara_report import IS_END, battery
from .strategy import Params

ARCHIVE_LISTING = "https://s3-ap-northeast-1.amazonaws.com"
TOP_N = 10


def archive_usdt_pairs(cache: Path) -> list[str]:
    """Every USDT spot pair Binance has ever archived, delisted ones included."""
    cached = cache / "archive-pairs.json"
    if cached.exists():
        return json.loads(cached.read_text())
    http = ReadOnlyHttp(ARCHIVE_LISTING, timeout_s=60)
    pairs, marker = [], ""
    while True:
        params = {"delimiter": "/", "prefix": "data/spot/monthly/klines/"}
        if marker:
            params["marker"] = marker
        text = http.get("/data.binance.vision", params).body.decode("utf-8")
        pairs += re.findall(r"<Prefix>data/spot/monthly/klines/([^/<]+)/</Prefix>", text)
        if "<IsTruncated>true" not in text:
            break
        marker = re.findall(r"<NextMarker>([^<]+)</NextMarker>", text)[0]
    usdt = sorted(p for p in pairs if p.endswith("USDT"))
    cache.mkdir(parents=True, exist_ok=True)
    cached.write_text(json.dumps(usdt))
    return usdt


def _months(start: date, end: date) -> list[str]:
    out, y, m = [], start.year, start.month
    while (y, m) <= (end.year, end.month):
        out.append(f"{y:04d}-{m:02d}")
        y, m = (y + 1, 1) if m == 12 else (y, m + 1)
    return out


def run(cache: Path, out: Path, *, n_is: int, n_wf: int, n_rw: int, workers: int, end: date) -> str:
    started = time.time()
    start = date(2018, 1, 1)
    candidates = [p for p in archive_usdt_pairs(cache) if universe.is_candidate(p)]
    volumes = load_binance_monthly_volume(candidates, date(2017, 12, 1), end, cache / "api")
    months = _months(start, end)
    members = universe.monthly_members(volumes, months, top_n=TOP_N, always={"BTCUSDT"})
    ever = sorted(set().union(*members.values()))
    pairs = {p[:-4]: p for p in ever}
    bars = load_binance_daily_api(pairs, start, end, cache / "api")
    renamed = {pair: name for name, pair in pairs.items()}
    named_members = {m: {renamed[p] for p in ps if renamed[p] in bars.symbols} for m, ps in members.items()}
    bars = DailyBars(bars.dates, bars.symbols, bars.close, bars.quote_volume,
                     universe.eligibility_mask(bars.dates, bars.symbols, named_members))
    split = bars.date_index(IS_END)
    base = Params()

    delisted = [s for a, s in enumerate(bars.symbols) if bars.close[a][-1] is None]
    by_year = {}
    for month, names in named_members.items():
        by_year.setdefault(month[:4], Counter()).update(names)
    lines = [f"# Minara rotation on a point-in-time universe ({date.today().isoformat()})", "",
             f"Candidates: {len(candidates)} Binance USDT spot pairs ever archived (stablecoins, fiat, leveraged, "
             f"wrapped and gold tokens excluded by a rule fixed in advance). Each month's universe = top {TOP_N} "
             "by the previous month's quote volume, plus BTC for the regime filter.",
             f"{len(bars.symbols)} coins were ever in the universe; {len(delisted)} of them no longer trade at the end "
             f"of the data ({', '.join(delisted) or 'none'}). A held coin that stops trading is sold at its last close.",
             f"Source: Binance public market-data API (TLS; no published checksum). Daily bars {bars.dates[0]} .. "
             f"{bars.dates[-1]}. In-sample to {bars.dates[split - 1]}.", "",
             "Most frequent members by year: " + "; ".join(
                 f"{y}: " + ", ".join(s for s, _ in c.most_common(12)) for y, c in sorted(by_year.items())), ""]
    lines += battery(bars, split, base, n_is=n_is, n_wf=n_wf, n_rw=n_rw, workers=workers)
    lines += ["## Caveats", "",
              "- Membership is point-in-time by volume; permutations shuffle prices but keep each month's "
              "membership, so the nulls test timing within the same monthly universes.",
              "- Selling a delisted coin at its last close is generous for collapses (LUNA-style).",
              "- Costs are assumptions (30 bp per side) until True Markets fees are known; True Markets does not "
              "list most of these coins, so this tests the IDEA, not a tradable True Markets strategy.",
              f"- Runtime {time.time() - started:.0f} s with {workers} workers.", ""]
    report = "\n".join(lines)
    out.write_text(report, encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cache", type=Path, default=Path.home() / ".cache" / "velocita-research" / "pit")
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
