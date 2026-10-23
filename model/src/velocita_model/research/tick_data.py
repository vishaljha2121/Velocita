"""BTCUSDT 1-second klines from the Binance public archive, sampled into the
live edge's 30 s ticks.

Every zip is verified against its published SHA-256 before a byte is parsed
(`bars.verify_checksum`), and the verified digests are returned so a report can
name the exact bytes it used. A tick at time T takes the close of the latest
1 s bar that had CLOSED by T (a bar opening at s is available at s + 1): an
as-of join, so no tick sees a price from its own future.
"""

from __future__ import annotations

import csv
import hashlib
import io
import json
import urllib.error
import zipfile
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Iterable, Iterator

from ..adapters.base import ReadOnlyHttp
from .bars import BINANCE_ARCHIVE, DataError, verify_checksum


@dataclass(frozen=True)
class TickSeries:
    times: tuple[int, ...]
    prices: tuple[float, ...]
    max_staleness_s: int
    stale_ticks: int
    sources: tuple[tuple[str, str], ...]


def _epoch(d: date) -> int:
    return int(datetime(d.year, d.month, d.day, tzinfo=timezone.utc).timestamp())


def archive_paths(pair: str, start: date, end: date) -> list[str]:
    """Monthly 1 s archives for whole months inside [START, END), daily ones for the rest."""
    paths, day = [], start
    while day < end:
        month_end = (day.replace(day=28) + timedelta(days=4)).replace(day=1)
        if day.day == 1 and month_end <= end:
            paths.append(f"/data/spot/monthly/klines/{pair}/1s/{pair}-1s-{day:%Y-%m}.zip")
            day = month_end
        else:
            paths.append(f"/data/spot/daily/klines/{pair}/1s/{pair}-1s-{day:%Y-%m-%d}.zip")
            day += timedelta(days=1)
    return paths


def fetch(paths: Iterable[str], cache: Path) -> list[tuple[Path, str]]:
    """Download (GET only) and checksum-verify each archive; cached files are re-verified."""
    http = ReadOnlyHttp(BINANCE_ARCHIVE, timeout_s=300)
    cache.mkdir(parents=True, exist_ok=True)
    out = []
    for path in paths:
        local = cache / path.rsplit("/", 1)[1]
        checksum = cache / (local.name + ".CHECKSUM")
        try:
            if not checksum.exists():
                checksum.write_bytes(http.get(path + ".CHECKSUM").body)
            if not local.exists():
                data = http.get(path).body
                verify_checksum(data, checksum.read_text())
                local.write_bytes(data)
        except urllib.error.HTTPError as error:
            raise DataError(f"{path}: HTTP {error.code}") from error
        data = local.read_bytes()
        verify_checksum(data, checksum.read_text())
        out.append((local, hashlib.sha256(data).hexdigest()))
    return out


def _rows(zip_path: Path) -> Iterator[tuple[int, float]]:
    with zipfile.ZipFile(zip_path) as archive:
        (name,) = archive.namelist()
        with archive.open(name) as raw:
            for fields in csv.reader(io.TextIOWrapper(raw, encoding="utf-8")):
                if fields and fields[0].isdigit():
                    t = int(fields[0])
                    yield (t // 1_000_000 if t >= 10**15 else t // 1000), float(fields[4])


def sample(bars: Iterable[tuple[int, float]], start_s: int, end_s: int, step_s: int) -> tuple[list[int], list[float], int, int]:
    """Ticks at START_S, START_S + STEP_S, ... < END_S from (open_time_s, close)
    1 s bars in time order. A bar opening at s is first usable at a tick T >= s + 1.
    Raises if the first tick has no closed bar before it."""
    times: list[int] = []
    prices: list[float] = []
    last_open, last_close = None, None
    worst = stale = 0
    tick = start_s

    def emit(t: int) -> None:
        nonlocal worst, stale
        if last_close is None:
            raise DataError(f"no closed 1 s bar before tick {t}")
        age = t - (last_open + 1)
        worst, stale = max(worst, age), stale + (age > 5)
        times.append(t)
        prices.append(last_close)

    for open_s, close in bars:
        if last_open is not None and open_s <= last_open:
            raise DataError(f"1 s bars out of order at {open_s}")
        while tick < end_s and tick <= open_s:
            emit(tick)
            tick += step_s
        last_open, last_close = open_s, close
    while tick < end_s:
        if tick > last_open + 3600:
            raise DataError(f"archive ends more than an hour before tick {tick}")
        emit(tick)
        tick += step_s
    return times, prices, worst, stale


def load_ticks(pair: str, start: date, end: date, cache: Path, *, step_s: int = 30) -> TickSeries:
    """30 s ticks over [START, END). The sampled series is cached as JSON keyed
    by the verified archive digests, so a changed archive cannot reuse it."""
    paths = archive_paths(pair, start - timedelta(days=1), end)
    verified = fetch(paths, cache)
    key = hashlib.sha256(json.dumps([d for _, d in verified] + [start.isoformat(), end.isoformat(), step_s]).encode()).hexdigest()[:16]
    cached = cache / f"ticks-{pair}-{start}-{end}-{step_s}s-{key}.json"
    sources = tuple((p, d) for p, (_, d) in zip(paths, verified))
    if cached.exists():
        body = json.loads(cached.read_text())
        return TickSeries(tuple(body["times"]), tuple(body["prices"]), body["max_staleness_s"], body["stale_ticks"], sources)

    def all_rows() -> Iterator[tuple[int, float]]:
        for local, _ in verified:
            yield from _rows(local)

    times, prices, worst, stale = sample(all_rows(), _epoch(start), _epoch(end), step_s)
    cached.write_text(json.dumps({"times": times, "prices": prices, "max_staleness_s": worst, "stale_ticks": stale}))
    return TickSeries(tuple(times), tuple(prices), worst, stale, sources)
