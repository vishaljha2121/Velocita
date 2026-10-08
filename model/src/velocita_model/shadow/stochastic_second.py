"""Bounded one-second stochastic pilot; causal signals and continuous paper cash."""
from collections import deque
from decimal import Decimal, InvalidOperation
import hashlib
import json

from .indicator_paper import NS, Source, PROGRAM, program_text, snapshot
from .edge import Tick
from ..research import rules

VARIANT='stochastic_1s_v1'
CASH_VARIANT='stochastic_1s_cash_v1'
NO_DEADLINE=2**63-1
LIMIT=2400
HEARTBEAT_NS=30*10**9


def candle(start,end,o,h,l,c):
    if type(start) is not int or type(end) is not int or start%1000 or end!=start+999:
        raise ValueError('one-second candle boundary')
    try: values=tuple(Decimal(str(x)) for x in (o,h,l,c))
    except InvalidOperation as error: raise ValueError('candle price') from error
    a,b,d,e=values
    if not all(x.is_finite() and x>0 for x in values) or not d<=min(a,e)<=max(a,e)<=b:
        raise ValueError('candle geometry')
    return ((end+1)//1000,*(float(x) for x in values))


class EMA:
    def __init__(self,n):
        self.n=n;self.count=0;self.total=0.;self.value=None

    def push(self,x):
        self.count+=1
        if self.count<=self.n:
            self.total+=x
            if self.count==self.n: self.value=self.total/self.n
        else: self.value+=(2/(self.n+1))*(x-self.value)
        return self.value


class Indicators:
    def __init__(self,window=900,k_smooth=10,d_smooth=30,fast=300,slow=1200):
        self.window=window;self.ks=k_smooth;self.ds=d_smooth
        self.high=deque();self.low=deque();self.raw=deque(maxlen=k_smooth)
        self.smooth=deque(maxlen=d_smooth);self.fast=EMA(fast);self.slow=EMA(slow)
        self.count=0;self.last=None

    def push(self,bar):
        stamp,o,h,l,c=bar
        if self.last is not None and stamp!=self.last+1: raise ValueError('candle gap')
        self.last=stamp;self.count+=1
        while self.high and self.high[-1][1]<=h: self.high.pop()
        while self.low and self.low[-1][1]>=l: self.low.pop()
        self.high.append((stamp,h));self.low.append((stamp,l))
        while self.high[0][0]<=stamp-self.window: self.high.popleft()
        while self.low[0][0]<=stamp-self.window: self.low.popleft()
        f,s=self.fast.push(c),self.slow.push(c)
        if self.count<self.window: return None
        hi,lo=self.high[0][1],self.low[0][1]
        self.raw.append(50. if hi==lo else 100*(c-lo)/(hi-lo))
        if len(self.raw)<self.ks: return None
        k=sum(self.raw)/self.ks;self.smooth.append(k)
        if len(self.smooth)<self.ds or f is None or s is None: return None
        return k,sum(self.smooth)/self.ds,f,s


class Signals:
    def __init__(self): self.previous=None;self.entry=0;self.exit=0

    def push(self,value):
        if value is None: return (0,0)
        k,d,f,s=value
        condition=k>d and k>=60 and f>s
        crossed=self.previous is not None and self.previous[0]<=self.previous[1] and k>d
        self.entry=(self.entry+1 if self.entry else int(crossed)) if condition else 0
        self.exit=self.exit+1 if k<d and k<=40 else 0
        self.previous=value
        return (1000000 if self.exit>=3 else 0,1000000 if self.entry>=3 else 0)


def pilot_program(root,until_ns,variant=VARIANT):
    text=program_text('stochastic',root,until_ns)
    if variant not in (VARIANT,CASH_VARIANT): raise ValueError('unknown strategy variant')
    cash=variant==CASH_VARIANT
    digest=hashlib.sha256(variant.encode()).hexdigest()
    lines=[]
    for line in text.splitlines():
        key=line.split(' = ')[0]
        value={'policy.id':'129' if cash else '128','policy.feature_schema_id':'139' if cash else '138',
               'policy.model_digest':digest,'order.max_market_data_age_ns':'2000000000'}.get(key)
        lines.append(f'{key} = {value}' if value else line)
    return '\n'.join(lines)+'\n'


def register_epoch(journal,epoch):
    encoded=json.dumps(epoch,sort_keys=True,separators=(',',':'))
    with journal.db:
        journal.db.execute('create table if not exists variant_epoch (id integer primary key check(id=1), payload text not null)')
        old=journal.db.execute('select payload from variant_epoch where id=1').fetchone()
        if old and old[0]!=encoded: raise ValueError('immutable variant epoch mismatch')
        journal.db.execute('insert or ignore into variant_epoch values (1,?)',(encoded,))


class FastSource(Source):
    def __init__(self,journal,book,epoch):
        super().__init__('stochastic',journal,book)
        register_epoch(journal,epoch)
        self.epoch=epoch;self.cash_run=epoch['variant']==CASH_VARIANT
        self.terminal_reason=None;self.reset();self.last_sent_bucket=None
        self.needs_bootstrap=False;self.last_forwarded_ns=None

    def reset(self):
        self.indicators=Indicators();self.signals=Signals();self.previous_flags=None

    def used(self):
        return self.journal.db.execute('select coalesce(sum(count),0) from attempts where bar>=?',
            (self.epoch['start_ns'],)).fetchone()[0]

    def tick(self,raw,*,receive_ns,tick_id):
        self.book.refresh()
        self.last_outcome='invalid'
        try:
            data=json.loads(raw)
            if 'bootstrap' in data:
                self.reset()
                for row in data['bootstrap']:
                    self.signals.push(self.indicators.push(candle(row[0],row[6],*row[1:5])))
                previous=self.signals.previous
                self.signals=Signals();self.signals.previous=previous
                self.previous_flags=None;self.needs_bootstrap=False
                self.last_outcome='warming_up';return None
            k=data['k']
            if data.get('s')!='SOLUSDT' or k['s']!='SOLUSDT' or k['i']!='1s': raise ValueError('stream identity')
            if k['x'] is not True: self.last_outcome='open_bar';return None
            bar=candle(k['t'],k['T'],k['o'],k['h'],k['l'],k['c'])
            event_ns=bar[0]*NS
            if self.indicators.last is not None and bar[0]<=self.indicators.last:
                if self.retry_bar!=event_ns:
                    self.last_outcome='duplicate';return None
                flags=self.retry_flags
            else:
                flags=self.previous_flags
                self.previous_flags=self.signals.push(self.indicators.push(bar))
            self.journal.mark(bar[0],int(Decimal(k['c'])*100))
        except (ValueError,KeyError,TypeError,IndexError,InvalidOperation):
            self.reset();self.retry_bar=None;self.needs_bootstrap=True;return None
        retrying=self.retry_bar==event_ns
        self.retry_bar=None
        def dropped(outcome):
            if retrying: self.journal.finish(event_ns)
            self.last_outcome=outcome;return None
        age=receive_ns-event_ns
        if age<0: return dropped('invalid')
        if age>2*NS: return dropped('stale')
        price=int(Decimal(k['c'])*100)
        if flags is None or self.indicators.count<1200:
            protective=self.book.gate(event_ns,price) in ('duration','capacity','drawdown')
            if not (protective and self.book.lots): return dropped('warming_up')
            flags=(0,0)
        if not self.cash_run and self.used()>=LIMIT: return dropped('capacity')
        stop=self.book.gate(event_ns,price)
        count=self.journal.db.execute('select count(*) from fills').fetchone()[0]
        if not self.cash_run and count>=len(self.epoch['previous_orders'])+40:
            stop='capacity'
            with self.journal.db:
                self.journal.db.execute('insert or ignore into stops values (?,?)',('capacity',event_ns//NS))
        if self.cash_run and self.book.lots==0 and stop in ('insufficient_balance','capacity','drawdown'):
            self.terminal_reason=stop
            with self.journal.db:
                self.journal.db.execute('insert or ignore into stops values (?,?)',(stop,event_ns//NS))
            self.journal.finish(event_ns)
            self.last_outcome=stop
            return None
        last_exit=self.journal.db.execute('select max(stamp) from fills where side=2').fetchone()[0]
        exit_flag,entry_flag=flags
        if last_exit is not None and event_ns/NS-last_exit<60: entry_flag=0
        if stop=='unreconciled': exit_flag=entry_flag=0
        elif stop in ('duration','capacity','drawdown'): exit_flag,entry_flag=1000000,0
        if self.book.lots is not None:
            exit_flag=exit_flag if self.book.lots else 0
            entry_flag=entry_flag if not self.book.lots and stop is None else 0
        if not retrying:
            idle=self.last_forwarded_ns is not None and 0<=event_ns-self.last_forwarded_ns<HEARTBEAT_NS
            if idle and not (exit_flag or entry_flag):
                self.last_outcome='quiet';return None
            if self.last_sent_bucket==receive_ns//NS:
                self.last_outcome='throttled';return None
            if not self.journal.attempt(event_ns,receive_ns):
                self.last_outcome='duplicate';return None
        self.last_sent_bucket=receive_ns//NS
        self.retry_flags=flags
        self.last_forwarded_ns=event_ns
        features=(exit_flag,entry_flag,price)
        with self.journal.db:
            self.journal.db.execute('insert or ignore into evidence values (?,?)',(event_ns,raw))
        self.last_outcome='forwarded'
        return Tick(tick_id,event_ns,receive_ns,age,features,
            hashlib.sha256(raw.encode()).hexdigest(),rules.first_match(PROGRAM,features))
