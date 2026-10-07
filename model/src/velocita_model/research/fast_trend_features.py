"""Causal registered indicators; sparse research views are not wire schemas."""
from array import array
from collections import deque
from dataclasses import dataclass
import math

from . import rules
from .fast_trend_data import cadence_indices
from ..contracts.enums import PredicateOperator as Op

MISSING = -(2**63)
GRID = tuple(range(1,13))


def decode(family, value):
    if family not in ('B','M') or value not in GRID:
        raise ValueError('unregistered fast trend configuration')
    windows = (120,360,720) if family == 'B' else ((30,120),(60,360),(120,720))
    return (5,10)[(value-1)//6], windows[((value-1)%6)//2], (0,50)[(value-1)%2]


def ema(prices, span):
    if span < 1 or int(span) != span:
        raise ValueError('invalid EMA span')
    if any(not math.isfinite(p) or p <= 0 for p in prices):
        raise ValueError('invalid EMA price')
    out = [None]*len(prices)
    if len(prices) < span:
        return out
    value = sum(prices[:span])/span
    out[span-1] = value
    alpha = 2/(span+1)
    for i in range(span,len(prices)):
        value = alpha*prices[i]+(1-alpha)*value
        out[i] = value
    return out


def channel(prices, span):
    if span < 2 or span % 2:
        raise ValueError('channel requires even span')
    upper, lower, out = deque(), deque(), []
    for i,price in enumerate(prices):
        while upper and upper[0] < i-span:
            upper.popleft()
        while lower and lower[0] < i-span//2:
            lower.popleft()
        out.append(None if i < span else
                   (round((price/prices[upper[0]]-1)*rules.MICROS),
                    round((price/prices[lower[0]]-1)*rules.MICROS)))
        while upper and prices[upper[-1]] <= price:
            upper.pop()
        while lower and prices[lower[-1]] >= price:
            lower.pop()
        upper.append(i)
        lower.append(i)
    return out


def program(family, value):
    _,_,band = decode(family,value)
    if family == 'B':
        return [rules.Rule(5,Op.less_than,0,rules.Action.EXIT),
                rules.Rule(4,Op.greater_than,band*100,rules.Action.ENTER_LONG)]
    return [rules.Rule(4,Op.less_than,-band*100,rules.Action.EXIT),
            rules.Rule(4,Op.greater_than,band*100,rules.Action.ENTER_LONG)]


@dataclass(frozen=True)
class SparseFeatures:
    extra: tuple

    def __len__(self):
        return 4+len(self.extra)

    def __getitem__(self,index):
        if index < 4:
            raise IndexError('reserved base feature not materialized')
        return self.extra[index-4]


class Frames:
    def __init__(self,first,second=None):
        self.first,self.second = first,second

    def __len__(self):
        return len(self.first)

    def __getitem__(self,index):
        value = self.first[index]
        if value == MISSING:
            return None
        extra = (value,) if self.second is None else (value,self.second[index])
        return SparseFeatures(extra)


class Bank:
    def __init__(self,bars):
        self.bars = bars
        self.indices = {d:cadence_indices(bars,d) for d in (5,10)}
        self.cache = {}

    def get(self,family,value):
        d,window,_ = decode(family,value)
        key = family,d,window
        if key in self.cache:
            return self.cache[key]
        indices = self.indices[d]
        prices = [self.bars.close[0][i] for i in indices]
        first = array('q',[MISSING])*len(self.bars.dates)
        second = array('q',[MISSING])*len(first) if family == 'B' else None
        if family == 'B':
            for i,pair in zip(indices,channel(prices,window//d)):
                if pair is not None:
                    first[i],second[i] = pair
        else:
            fast,slow = (ema(prices,w//d) for w in window)
            for i,f,s in zip(indices,fast,slow):
                if f is not None and s is not None:
                    first[i] = round((f/s-1)*rules.MICROS)
        frames = Frames(first,second)
        self.cache[key] = frames
        return frames
