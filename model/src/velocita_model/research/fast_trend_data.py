"""Registered five-minute SOL archives, strict timestamps and causal views."""
from bisect import bisect_left
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
import csv
import hashlib
import io
import json
import math
import zipfile

from .bars import BINANCE_ARCHIVE, DataError, _months, verify_checksum
from ..adapters.base import ReadOnlyHttp

UTC = timezone.utc
BEGIN = datetime(2023, 4, 1, tzinfo=UTC)
TRAIN = datetime(2023, 9, 1, tzinfo=UTC)
WF = datetime(2024, 9, 1, tzinfo=UTC)
END = datetime(2026, 9, 1, tzinfo=UTC)
LEDGER_HASH = '36831661681edfa1e8c83b0338aed5a401fc8d06fd9c9b246903162847bb4ace'
ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / 'docs/evidence/fast_trend_battery_2026-10-06'


@dataclass
class Bars:
    dates: tuple
    close: tuple
    root: object = None
    offset: int = 0
    symbols: tuple = ('SOL',)
    quote_volume: object = None

    def date_index(self, stamp):
        return bisect_left(self.dates, stamp)

    def slice(self, start, end):
        return Bars(self.dates[start:end], (self.close[0][start:end],),
                    self if self.root is None else self.root, self.offset + start)

    def validate(self):
        if not self.dates or len(self.dates) != len(self.close[0]):
            raise DataError('empty or misaligned five-minute data')
        if any(t.tzinfo is None or t.utcoffset() != timedelta(0) or
               t.minute % 5 or t.second or t.microsecond for t in self.dates):
            raise DataError('invalid five-minute close timestamp')
        if any(b-a != timedelta(minutes=5) for a, b in zip(self.dates, self.dates[1:])):
            raise DataError('noncontiguous five-minute data')
        if any(not math.isfinite(p) or p <= 0 for p in self.close[0]):
            raise DataError('invalid positive finite price')


def parse_archive(data):
    out = []
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        if len(names) != 1 or not names[0].endswith('.csv'):
            raise DataError('expected one CSV member')
        with archive.open(names[0]) as raw:
            for row in csv.reader(io.TextIOWrapper(raw)):
                if len(row) != 12:
                    raise DataError('expected twelve kline fields')
                try:
                    opening, ending = int(row[0]), int(row[6])
                    unit = 1_000_000 if opening >= 10**15 else 1000
                    if opening % (300*unit) or ending != opening+300*unit-1:
                        raise DataError('invalid kline interval')
                    stamp = datetime.fromtimestamp(opening//unit, UTC)
                    if unit != (1_000_000 if stamp.year >= 2025 else 1000):
                        raise DataError('timestamp unit contradicts archive year')
                    o, h, l, c = map(float, row[1:5])
                    if (not all(math.isfinite(p) and p > 0 for p in (o,h,l,c)) or
                            not l <= min(o,c) <= max(o,c) <= h):
                        raise DataError('invalid OHLC values')
                    if any(not math.isfinite(float(row[i])) or float(row[i]) < 0
                           for i in (5,7,9,10)) or int(row[8]) < 0:
                        raise DataError('invalid volume or trade count')
                except (ValueError, OverflowError) as exc:
                    raise DataError('malformed kline row') from exc
                out.append((stamp+timedelta(minutes=5), c))
    if not out:
        raise DataError('empty archive')
    return out


def cadence_indices(bars, minutes):
    if minutes not in (5,10):
        raise ValueError('unregistered cadence')
    bars.validate()
    return tuple(i for i,t in enumerate(bars.dates)
                 if t.minute % minutes == 0 and
                 (minutes == 5 or i > 0))


def check_approval():
    actual = hashlib.sha256((OUT/'RESEARCH_LEDGER.md').read_bytes()).hexdigest()
    approval = json.loads((OUT/'approval.json').read_text())
    if actual != LEDGER_HASH or approval['ledger_sha256'] != actual:
        raise RuntimeError('registered ledger approval mismatch')


def load_registered():
    check_approval()
    cache = Path.home()/'.cache/velocita-research/fast_trend_5m'
    cache.mkdir(parents=True, exist_ok=True)
    http = ReadOnlyHttp(BINANCE_ARCHIVE, timeout_s=60)
    rows, sources = [], []
    for month in _months(BEGIN.date(), (END-timedelta(days=1)).date()):
        name = f'SOLUSDT-5m-{month}.zip'
        path = f'/data/spot/monthly/klines/SOLUSDT/5m/{name}'
        zipped, checksum = cache/name, cache/(name+'.CHECKSUM')
        if not checksum.exists():
            data = http.get(path+'.CHECKSUM').body
            with checksum.open('xb') as f:
                f.write(data)
        data = zipped.read_bytes() if zipped.exists() else http.get(path).body
        verify_checksum(data, checksum.read_text())
        parsed = parse_archive(data)
        y, m = map(int, month.split('-'))
        first = datetime(y,m,1,tzinfo=UTC)
        last = first.replace(year=y+(m==12), month=m%12+1)
        expected = int((last-first).total_seconds()//300)
        if (len(parsed) != expected or parsed[0][0] != first+timedelta(minutes=5)
                or parsed[-1][0] != last or any(b[0]-a[0] != timedelta(minutes=5)
                for a,b in zip(parsed,parsed[1:]))):
            raise DataError('monthly archive coverage mismatch: '+month)
        if not zipped.exists():
            with zipped.open('xb') as f:
                f.write(data)
        rows.extend((t,p) for t,p in parsed if BEGIN < t < END)
        sources.append(dict(path=path,sha256=hashlib.sha256(data).hexdigest(),
                            checksum_sha256=hashlib.sha256(checksum.read_bytes()).hexdigest()))
        print('validated archive', month, len(parsed), flush=True)
    bars = Bars(tuple(t for t,p in rows),(tuple(p for t,p in rows),))
    bars.validate()
    if bars.dates[0] != BEGIN+timedelta(minutes=5) or bars.dates[-1] != END-timedelta(minutes=5):
        raise DataError('study boundary mismatch')
    manifest = OUT/'data_manifest.json'
    if manifest.exists():
        if json.loads(manifest.read_text()) != sources:
            raise DataError('historical source bytes changed')
    else:
        with manifest.open('x') as f:
            json.dump(sources,f,indent=2)
            f.write('\n')
    return bars


if __name__ == '__main__':
    bars = load_registered()
    print('validated historical closes', len(bars.dates), flush=True)
