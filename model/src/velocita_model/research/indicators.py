"""Daily indicators with explicit Wilder seeding; ratios are not rounded internally."""
from __future__ import annotations

import math
from collections import deque
from typing import Sequence


def wilder(values: Sequence[float | None], n: int) -> list[float | None]:
    if n < 1:
        raise ValueError('period must be positive')
    out = [None] * len(values)
    seed = []
    value = None
    for i, x in enumerate(values):
        if x is None:
            seed = []
            value = None
        elif value is None:
            seed.append(x)
            if len(seed) == n:
                value = sum(seed) / n
                out[i] = value
        else:
            value = ((n - 1) * value + x) / n
            out[i] = value
    return out


def rsi(close: Sequence[float], n: int) -> list[float | None]:
    delta = [b-a for a,b in zip(close,close[1:])]
    gain = wilder([None] + [max(x,0) for x in delta], n)
    loss = wilder([None] + [max(-x,0) for x in delta], n)
    return [None if g is None else (50.0 if g+l == 0 else 100*g/(g+l)) for g,l in zip(gain,loss)]


def directional(high, low, close, n=14):
    tr, plus, minus = [None], [None], [None]
    for i in range(1,len(close)):
        up, down = high[i]-high[i-1], low[i-1]-low[i]
        tr.append(max(high[i]-low[i], abs(high[i]-close[i-1]), abs(low[i]-close[i-1])))
        plus.append(up if up > 0 and up > down else 0.0)
        minus.append(down if down > 0 and down > up else 0.0)
    atr, p, m = wilder(tr,n), wilder(plus,n), wilder(minus,n)
    dip = [None if a is None else (100*x/a if a else 0.0) for a,x in zip(atr,p)]
    dim = [None if a is None else (100*x/a if a else 0.0) for a,x in zip(atr,m)]
    dx = [None if p is None else (100*abs(p-m)/(p+m) if p+m else 0.0) for p,m in zip(dip,dim)]
    return atr, dip, dim, wilder(dx,n)


def sma(close, n):
    out = [None] * len(close)
    total = 0.0
    for i,c in enumerate(close):
        total += c
        if i >= n:
            total -= close[i-n]
        if i >= n-1:
            out[i] = total/n
    return out


def realized_vol(close, n=30):
    if n < 2:
        raise ValueError('sample variance requires two returns')
    logs = [math.log(b/a) for a,b in zip(close,close[1:])]
    out = [None]*len(close)
    for t in range(n,len(close)):
        xs=logs[t-n:t]
        mean=sum(xs)/n
        out[t]=math.sqrt(sum((x-mean)**2 for x in xs)/(n-1))
    return out


def _prior_extreme(values, n, maximum):
    q=deque()
    out=[None]*len(values)
    for t,x in enumerate(values):
        while q and q[0] < t-n:
            q.popleft()
        if t >= n:
            out[t]=values[q[0]]
        while q and ((values[q[-1]] <= x) if maximum else (values[q[-1]] >= x)):
            q.pop()
        q.append(t)
    return out


def donchian(high, low, close, upper, lower):
    hh=_prior_extreme(high,upper,True)
    ll=_prior_extreme(low,lower,False)
    return ([None if h is None else c/h-1 for c,h in zip(close,hh)],
            [None if l is None else c/l-1 for c,l in zip(close,ll)])
