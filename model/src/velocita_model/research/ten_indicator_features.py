"""Ten fixed indicator definitions and integer readiness features."""
from array import array
import math
from statistics import fmean

from . import indicators,rules
from .fast_trend_features import Frames,MISSING,ema
from .ten_indicator_data import aggregate
from ..contracts.enums import PredicateOperator as Op

NAMES={1:'EMA momentum',2:'SMA momentum',3:'MACD momentum',4:'RSI momentum',5:'ADX/DI trend',
       6:'Rate-of-change momentum',7:'Stochastic momentum',8:'Bollinger breakout',9:'EMA/ATR channel',10:'Volatility-filtered EMA'}
GRID=(5,10)


def signed_ema(values,n):
    out=[None]*len(values)
    seed=[]
    state=None
    alpha=2/(n+1)
    for i,value in enumerate(values):
        if value is None:
            seed=[];state=None
        elif state is None:
            seed.append(value)
            if len(seed)==n: state=fmean(seed);out[i]=state
        else:
            state=alpha*value+(1-alpha)*state
            out[i]=state
    return out


def stochastic(high,low,close,n=14,smoothing=3):
    k=[None]*len(close)
    d=[None]*len(close)
    for i in range(n-1,len(close)):
        top=max(high[i-n+1:i+1]);bottom=min(low[i-n+1:i+1])
        k[i]=50. if top==bottom else 100*(close[i]-bottom)/(top-bottom)
        if i>=n+smoothing-2: d[i]=fmean(k[i-smoothing+1:i+1])
    return k,d


def pop_sd(values,n):
    out=[None]*len(values)
    for i in range(n-1,len(values)):
        window=values[i-n+1:i+1]
        mean=fmean(window)
        out[i]=math.sqrt(sum((x-mean)**2 for x in window)/n)
    return out


def rv_reference(values,n=144):
    out=[None]*len(values)
    valid=[]
    total=0.
    for i,value in enumerate(values):
        if len(valid)>=n: out[i]=total/n
        if value is not None:
            valid.append(value);total+=value
            if len(valid)>n: total-=valid[-n-1]
    return out


def decision(family,x):
    if family in (1,2): return x['trend']<=0,x['trend']>0
    if family==3: return x['hist']<=0,x['hist']>0
    if family==4: return x['rsi']<=450000,x['rsi']>=550000
    if family==5: return x['adx']<=200000 or x['di']<=0,x['adx']>=250000 and x['di']>0
    if family==6: return x['roc']<=0,x['roc']>=5000
    if family==7: return x['kd']<=0 or x['k']<=400000,x['kd']>0 and x['k']>=500000
    if family in (8,9): return x['middle']<=0,x['upper']>0
    if family==10: return x['trend']<=0 or x['regime']>500000,x['trend']>0 and x['regime']<=500000
    raise ValueError('unknown indicator family')


def policy():
    return [rules.Rule(4,Op.greater_than_or_equal,1000000,rules.Action.EXIT),
            rules.Rule(5,Op.greater_than_or_equal,1000000,rules.Action.ENTER_LONG)]


class Bank:
    def __init__(self,bars):
        self.bars=bars
        self.aggregates={}
        self.cache={}

    def get(self,family,cadence):
        if family not in NAMES or cadence not in GRID: raise ValueError('unregistered configuration')
        key=family,cadence
        if key in self.cache: return self.cache[key]
        if cadence not in self.aggregates: self.aggregates[cadence]=aggregate(self.bars,cadence)
        indices,o,h,l,c=self.aggregates[cadence]
        n=len(c)
        columns={}
        q=lambda value:round(value*rules.MICROS)
        def ratio(a,b):
            return [None if x is None or y is None else q(x/y-1) for x,y in zip(a,b)]
        if family in (1,10): columns['trend']=ratio(ema(c,9),ema(c,21))
        if family==2: columns['trend']=ratio(indicators.sma(c,20),indicators.sma(c,50))
        if family==3:
            fast,slow=ema(c,12),ema(c,26)
            macd=[None if a is None or b is None else a-b for a,b in zip(fast,slow)]
            signal=signed_ema(macd,9)
            columns['hist']=[None if a is None or b is None else q((a-b)/price) for a,b,price in zip(macd,signal,c)]
        if family==4: columns['rsi']=[None if x is None else q(x/100) for x in indicators.rsi(c,14)]
        if family in (5,9):
            atr,plus,minus,adx=indicators.directional(h,l,c,14)
            if family==5:
                columns['adx']=[None if x is None else q(x/100) for x in adx]
                columns['di']=[None if a is None or b is None else q((a-b)/100) for a,b in zip(plus,minus)]
        if family==6: columns['roc']=[None]*12+[q(c[i]/c[i-12]-1) for i in range(12,n)]
        if family==7:
            k,d=stochastic(h,l,c)
            columns['k']=[None if x is None else q(x/100) for x in k]
            columns['kd']=[None if a is None or b is None else q((a-b)/100) for a,b in zip(k,d)]
        if family in (8,9):
            middle=indicators.sma(c,20) if family==8 else ema(c,20)
            spread=pop_sd(c,20) if family==8 else atr
            upper=[None if a is None or b is None else a+2*b for a,b in zip(middle,spread)]
            columns['middle']=ratio(c,middle);columns['upper']=ratio(c,upper)
        if family==10:
            rv=indicators.realized_vol(c,24)
            reference=rv_reference(rv)
            columns['regime']=[None if a is None or b is None else
                (0 if a==0 else 1000000) if b==0 else q(a/b-1) for a,b in zip(rv,reference)]
        exits=array('q',[MISSING])*len(self.bars.dates)
        entries=array('q',[MISSING])*len(exits)
        for j,index in enumerate(indices):
            values={name:values[j] for name,values in columns.items()}
            if any(value is None for value in values.values()): continue
            leave,enter=decision(family,values)
            exits[index]=int(leave)*rules.MICROS;entries[index]=int(enter)*rules.MICROS
        self.cache[key]=Frames(exits,entries)
        return self.cache[key]
