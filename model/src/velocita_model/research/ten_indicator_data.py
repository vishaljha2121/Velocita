"""Cache-only OHLC bootstrap and registered joint-candle null transforms."""
from dataclasses import dataclass
from datetime import timedelta
from pathlib import Path
import csv
import hashlib
import io
import json
import math
import random
import statistics
import zipfile

from .fast_trend_data import Bars,ROOT,BEGIN,TRAIN,WF,END,parse_archive,cadence_indices

OUT=ROOT/'archive/evidence/ten_indicator_battery_2026-10-07'
LEDGER_HASH='278f60c660c98678d566b6292252fd7f2203eff08d23c55b8121eb387dc0e8af'


@dataclass
class OHLCBars(Bars):
    open: tuple=()
    high: tuple=()
    low: tuple=()

    def slice(self,start,end):
        return OHLCBars(self.dates[start:end],(self.close[0][start:end],),
            self if self.root is None else self.root,self.offset+start,
            open=self.open[start:end],high=self.high[start:end],low=self.low[start:end])

    def validate(self):
        super().validate()
        n=len(self.dates)
        if any(len(x)!=n for x in (self.open,self.high,self.low)):
            raise ValueError('OHLC length mismatch')
        if any(not all(math.isfinite(x) and x>0 for x in (o,h,l,c)) or not l<=min(o,c)<=max(o,c)<=h
               for o,h,l,c in zip(self.open,self.high,self.low,self.close[0])):
            raise ValueError('invalid OHLC geometry')


def approval():
    sha=hashlib.sha256((OUT/'RESEARCH_LEDGER.md').read_bytes()).hexdigest()
    if sha!=LEDGER_HASH or json.loads((OUT/'approval.json').read_text())['ledger_sha256']!=sha:
        raise RuntimeError('ten-indicator approval mismatch')


def load_cached():
    approval()
    source=ROOT/'archive/evidence/fast_trend_battery_2026-10-06/data_manifest.json'
    manifest=json.loads(source.read_text())
    if len(manifest)!=41:
        raise ValueError('expected 41 registered archives')
    dates,opened,high,low,close=[],[],[],[],[]
    for entry in manifest:
        path=Path.home()/'.cache/velocita-research/fast_trend_5m'/Path(entry['path']).name
        data=path.read_bytes()
        checksum=path.with_name(path.name+'.CHECKSUM').read_bytes()
        if (hashlib.sha256(data).hexdigest()!=entry['sha256'] or
            hashlib.sha256(checksum).hexdigest()!=entry['checksum_sha256'] or
            checksum.decode().split()[0]!=entry['sha256']):
            raise ValueError('archive checksum differs: '+path.name)
        validated=parse_archive(data)
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            with z.open(z.namelist()[0]) as f:
                rows=list(csv.reader(io.TextIOWrapper(f)))
        if len(rows)!=len(validated):
            raise ValueError('OHLC parser length differs')
        for row,(stamp,price) in zip(rows,validated):
            if BEGIN<stamp<END:
                dates.append(stamp)
                opened.append(float(row[1]));high.append(float(row[2]));low.append(float(row[3]));close.append(price)
        print('validated cached OHLC',path.name,flush=True)
    result=OHLCBars(tuple(dates),(tuple(close),),open=tuple(opened),high=tuple(high),low=tuple(low))
    result.validate()
    if result.dates[0]!=BEGIN+timedelta(minutes=5) or result.dates[-1]!=END-timedelta(minutes=5):
        raise ValueError('historical boundaries differ')
    target=OUT/'data_manifest.json'
    if target.exists():
        if json.loads(target.read_text())!=manifest: raise ValueError('source manifest changed')
    else:
        with target.open('x') as f: json.dump(manifest,f,indent=2);f.write('\n')
    return result


def aggregate(bars,minutes):
    indices=cadence_indices(bars,minutes)
    width=minutes//5
    o=[bars.open[i-width+1] for i in indices]
    h=[max(bars.high[i-width+1:i+1]) for i in indices]
    l=[min(bars.low[i-width+1:i+1]) for i in indices]
    c=[bars.close[0][i] for i in indices]
    return indices,o,h,l,c


class NullSource:
    def __init__(self,bars):
        bars.validate()
        self.bars=bars
        c=bars.close[0]
        self.innovations=[tuple(math.log(x/c[i-1]) for x in (bars.open[i],bars.high[i],bars.low[i],c[i]))
                          for i in range(1,len(c))]
        groups={}
        for i,stamp in enumerate(bars.dates[1:],1):
            groups.setdefault((stamp-timedelta(minutes=5)).date(),[]).append(i)
        self.groups=[tuple(v) for v in groups.values() if len(v)==288]
        fit=[i for i,t in enumerate(bars.dates) if i>0 and TRAIN<=t<WF]
        self.mean=statistics.mean(self.innovations[i-1][3] for i in fit)
        self.sd=statistics.stdev(self.innovations[i-1][3] for i in fit)
        self.shapes=[(self.innovations[i-1][0],bars.high[i]/max(bars.open[i],c[i]),bars.low[i]/min(bars.open[i],c[i])) for i in fit]

    def make(self,seed,kind):
        rng=random.Random(seed)
        b=self.bars
        o,h,l,c=[b.open[0]],[b.high[0]],[b.low[0]],[b.close[0][0]]
        if kind=='permute':
            order=list(range(len(self.innovations)))
            blocks=list(self.groups)
            rng.shuffle(blocks)
            for target,source in zip(self.groups,blocks):
                for dest,origin in zip(target,source): order[dest-1]=origin-1
            for i in order:
                a,top,bottom,ending=(c[-1]*math.exp(v) for v in self.innovations[i])
                o.append(a);h.append(top);l.append(bottom);c.append(ending)
        elif kind=='random_walk':
            for _ in self.innovations:
                ret=rng.gauss(self.mean,self.sd)
                gap,up,down=self.shapes[rng.randrange(len(self.shapes))]
                a,ending=c[-1]*math.exp(gap),c[-1]*math.exp(ret)
                o.append(a);h.append(max(a,ending)*up);l.append(min(a,ending)*down);c.append(ending)
        else:
            raise ValueError('unknown null transform')
        return OHLCBars(b.dates,(tuple(c),),open=tuple(o),high=tuple(h),low=tuple(l))
