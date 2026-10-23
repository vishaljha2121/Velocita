"""Daily bars from two public, credential-free sources.

* Binance spot archives (data.binance.vision): monthly zip files, each with a
  published SHA-256 CHECKSUM file that is verified before a byte is parsed.
* True Markets price history (`/v1/defi/market/prices/history`): closes only,
  no volume, about three years deep.

Both go through `ReadOnlyHttp` (GET only). A series may start late (listing);
a gap after the first bar is refused rather than filled.
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
from typing import Iterable, Mapping, Sequence

from ..adapters.base import ReadOnlyHttp

BINANCE_ARCHIVE = "https://data.binance.vision"
TRUEMARKETS_API = "https://api.truemarkets.co"


class DataError(RuntimeError):
    """Input data failed an integrity check; nothing was guessed to repair it."""


@dataclass(frozen=True)
class DailyBars:
    """Asset-major daily closes over one contiguous UTC date range. `None`
    before an asset lists. `quote_volume` is None when the source has none."""

    dates: tuple[date, ...]
    symbols: tuple[str, ...]
    close: tuple[tuple[float | None, ...], ...]
    quote_volume: tuple[tuple[float | None, ...], ...] | None
    eligible: tuple[tuple[bool, ...], ...] | None = None

    def index(self, symbol: str) -> int:
        return self.symbols.index(symbol)

    def slice(self, start: int, end: int) -> "DailyBars":
        return DailyBars(
            dates=self.dates[start:end], symbols=self.symbols,
            close=tuple(series[start:end] for series in self.close),
            quote_volume=None if self.quote_volume is None else tuple(v[start:end] for v in self.quote_volume),
            eligible=None if self.eligible is None else tuple(e[start:end] for e in self.eligible),
        )

    def date_index(self, day: date) -> int:
        """Index of DAY, or of the first bar after it."""
        for i, d in enumerate(self.dates):
            if d >= day:
                return i
        return len(self.dates)


def verify_checksum(data: bytes, checksum_file_text: str) -> None:
    expected = checksum_file_text.strip().split()[0].lower()
    if hashlib.sha256(data).hexdigest() != expected:
        raise DataError("archive does not match its published SHA-256")


def _utc_day(open_time: int) -> date:
    seconds = open_time / 1_000_000 if open_time >= 10**15 else open_time / 1000
    return datetime.fromtimestamp(seconds, tz=timezone.utc).date()


def parse_binance_kline_zip(data: bytes) -> list[tuple[date, float, float]]:
    """(day, close, quote_volume) rows. Columns per the Binance archive README:
    open_time, open, high, low, close, volume, close_time, quote_volume, ..."""
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        (name,) = archive.namelist()
        text = archive.read(name).decode("utf-8")
    rows = []
    for fields in csv.reader(io.StringIO(text)):
        if not fields or not fields[0].isdigit():
            continue
        rows.append((_utc_day(int(fields[0])), float(fields[4]), float(fields[7])))
    return rows


def assemble(series: Mapping[str, Sequence[tuple[date, float, float | None]]], *,
             has_volume: bool = True, allow_gaps: bool = False) -> DailyBars:
    """Align per-symbol rows on one contiguous date range. Gaps and early ends
    are refused unless ALLOW_GAPS (survivorship-free universes, where coins are
    halted and delisted); a gap is then None, never filled."""
    by_symbol: dict[str, dict[date, tuple[float, float | None]]] = {}
    for symbol, rows in series.items():
        days = sorted(rows, key=lambda r: r[0])
        for (a, *_), (b, *_) in zip(days, days[1:]):
            if b == a or (b - a != timedelta(days=1) and not allow_gaps):
                raise DataError(f"{symbol}: gap or duplicate between {a} and {b}")
        by_symbol[symbol] = {d: (c, v) for d, c, v in days}
    first = min(min(rows) for rows in by_symbol.values() if rows)
    last = max(max(rows) for rows in by_symbol.values() if rows)
    dates = tuple(first + timedelta(days=i) for i in range((last - first).days + 1))
    symbols = tuple(series)
    close = tuple(tuple(by_symbol[s].get(d, (None, None))[0] for d in dates) for s in symbols)
    volume = tuple(tuple(by_symbol[s].get(d, (None, None))[1] for d in dates) for s in symbols) if has_volume else None
    for s, col in zip(symbols, close):
        present = [i for i, c in enumerate(col) if c is not None]
        if present and present[-1] != len(dates) - 1 and not allow_gaps:
            raise DataError(f"{s}: series ends before the others (delisted or missing data)")
    return DailyBars(dates=dates, symbols=symbols, close=close, quote_volume=volume)


def _months(start: date, end: date) -> Iterable[str]:
    y, m = start.year, start.month
    while (y, m) <= (end.year, end.month):
        yield f"{y:04d}-{m:02d}"
        y, m = (y + 1, 1) if m == 12 else (y, m + 1)


def load_binance(symbols: Mapping[str, str], start: date, end: date, cache_dir: Path) -> DailyBars:
    """SYMBOLS maps our name to the Binance pair (e.g. {"BTC": "BTCUSDT"}).
    END is the last whole month to fetch. Months before listing are skipped
    (404); a missing month after listing is a gap and is refused."""
    http = ReadOnlyHttp(BINANCE_ARCHIVE, timeout_s=60)
    cache_dir.mkdir(parents=True, exist_ok=True)
    out: dict[str, list[tuple[date, float, float]]] = {}
    for name, pair in symbols.items():
        rows: list[tuple[date, float, float]] = []
        for month in _months(start, end):
            path = f"/data/spot/monthly/klines/{pair}/1d/{pair}-1d-{month}.zip"
            cached = cache_dir / f"{pair}-1d-{month}.zip"
            if not cached.exists():
                try:
                    data = http.get(path).body
                except urllib.error.HTTPError as error:
                    if error.code == 404 and not rows:
                        continue
                    raise DataError(f"{pair} {month}: HTTP {error.code}") from error
                verify_checksum(data, http.get(path + ".CHECKSUM").body.decode("utf-8"))
                cached.write_bytes(data)
            rows += parse_binance_kline_zip(cached.read_bytes())
        if not rows:
            raise DataError(f"{pair}: no data in range")
        out[name] = rows
    return assemble(out)


def truemarkets_daily_rows(points: Iterable[Mapping[str, str]]) -> list[tuple[date, float, None]]:
    """A 1d point stamped 00:00Z on day d is treated as the CLOSE OF DAY d-1.

    INFERRED, not documented (2026-09-28): against Binance daily closes, the
    point correlates 0.74-0.90 with the previous day's close and 0.09-0.25 with
    the same day's. A point not stamped exactly at midnight is refused rather
    than assigned to a guessed day."""
    rows = []
    for point in points:
        stamp = datetime.fromisoformat(point["t"].replace("Z", "+00:00"))
        if (stamp.hour, stamp.minute, stamp.second, stamp.microsecond) != (0, 0, 0, 0):
            raise DataError(f"daily point {point['t']} is not stamped at midnight UTC")
        rows.append((stamp.date() - timedelta(days=1), float(point["price"]), None))
    return rows


def load_truemarkets(symbols: Sequence[str], window: str = "36M") -> DailyBars:
    """Daily closes from True Markets' public history endpoint. No volume."""
    http = ReadOnlyHttp(TRUEMARKETS_API, timeout_s=60)
    out: dict[str, list[tuple[date, float, float | None]]] = {}
    for symbol in symbols:
        body = json.loads(http.get("/v1/defi/market/prices/history",
                                   {"symbol": symbol, "window": window, "resolution": "1d"}).body)
        out[symbol] = truemarkets_daily_rows(body.get("points") or ())
        if not out[symbol]:
            raise DataError(f"{symbol}: no True Markets history")
    return assemble(out, has_volume=False)


BINANCE_API = "https://data-api.binance.vision"


def _api_klines(http: ReadOnlyHttp, pair: str, interval: str, start: date, end: date, cache: Path) -> list[list]:
    """All klines for PAIR in [START, END], paginated 1000 at a time and cached
    as JSON. Unlike the archive there is no published checksum: integrity rests
    on TLS, and the report says which source was used."""
    cached = cache / f"{pair}-{interval}-{start}-{end}.json"
    if cached.exists():
        return json.loads(cached.read_text())
    begin = int(datetime(start.year, start.month, start.day, tzinfo=timezone.utc).timestamp() * 1000)
    stop = int(datetime(end.year, end.month, end.day, tzinfo=timezone.utc).timestamp() * 1000)
    rows: list[list] = []
    while begin <= stop:
        try:
            batch = json.loads(http.get("/api/v3/klines", {"symbol": pair, "interval": interval,
                                                           "startTime": begin, "endTime": stop,
                                                           "limit": 1000}).body)
        except urllib.error.HTTPError as error:
            if error.code == 400:
                break
            raise
        if not batch:
            break
        rows += batch
        begin = batch[-1][0] + 1
    cache.mkdir(parents=True, exist_ok=True)
    cached.write_text(json.dumps(rows))
    return rows


def load_binance_monthly_volume(pairs: Sequence[str], start: date, end: date,
                                cache: Path) -> dict[str, dict[str, float]]:
    """Quote volume per calendar month, from monthly klines."""
    http = ReadOnlyHttp(BINANCE_API, timeout_s=60)
    out: dict[str, dict[str, float]] = {}
    for pair in pairs:
        rows = _api_klines(http, pair, "1M", start, end, cache)
        out[pair] = {_utc_day(int(r[0])).strftime("%Y-%m"): float(r[7]) for r in rows}
    return out


def load_binance_daily_api(pairs: Mapping[str, str], start: date, end: date, cache: Path) -> DailyBars:
    """Daily closes and quote volume for PAIRS ({name: pair}); halts and
    delistings stay as gaps (None)."""
    http = ReadOnlyHttp(BINANCE_API, timeout_s=60)
    out = {}
    for name, pair in pairs.items():
        rows = [(_utc_day(int(r[0])), float(r[4]), float(r[7])) for r in _api_klines(http, pair, "1d", start, end, cache)]
        rows = [r for r in rows if start <= r[0] <= end]
        if rows:
            out[name] = rows
    return assemble(out, allow_gaps=True)
