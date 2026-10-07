"""Isolated diagnostic paper profiles using frozen indicators and closed proxy bars."""
from datetime import datetime,timezone
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
import sqlite3
import time

from ..research.ten_indicator_data import OHLCBars
from ..research.ten_indicator_features import Bank
from ..research import rules
from ..contracts.enums import PredicateOperator as Op
from .edge import Tick,price_ticks

SPECS={'sma':(2,10,126,136),'stochastic':(7,5,127,137)}
NS=1000000000
BAR_NS=300*NS
RECONCILE_ERRORS=(OSError,ValueError,KeyError,TypeError,sqlite3.Error)
PROGRAM=[rules.Rule(0,Op.greater_than_or_equal,1000000,rules.Action.EXIT),
         rules.Rule(1,Op.greater_than_or_equal,1000000,rules.Action.ENTER_LONG)]


def decode_bars(rows,now_ns):
    if not isinstance(rows,list) or not 2<=len(rows)<=300:
        raise ValueError('OHLC row count')
    dates,o,h,l,c=[],[],[],[],[]
    previous=None
    for row in rows:
        if not isinstance(row,list) or len(row)!=12:
            raise ValueError('OHLC row shape')
        start,end=row[0],row[6]+1
        if type(start) is not int or type(end) is not int or start%300000 or end!=start+300000:
            raise ValueError('OHLC UTC boundary')
        if previous is not None and start!=previous:
            raise ValueError('OHLC gap')
        previous=end
        values=tuple(float(row[i]) for i in (1,2,3,4))
        a,b,d,e=values
        if not all(math.isfinite(v) and v>0 for v in values) or not d<=min(a,e)<=max(a,e)<=b:
            raise ValueError('OHLC geometry')
        if end*1000000>now_ns:
            continue
        dates.append(datetime.fromtimestamp(end/1000,timezone.utc))
        o.append(a);h.append(b);l.append(d);c.append(e)
    return OHLCBars(tuple(dates),(tuple(c),),open=tuple(o),high=tuple(h),low=tuple(l))


def frame(strategy,bars):
    family,cadence,_,_=SPECS[strategy]
    if len(bars.dates)<2: return None
    signal_index=len(bars.dates)-2
    if int(bars.dates[signal_index].timestamp())%(cadence*60): return None
    features=Bank(bars).get(family,cadence)[signal_index]
    if features is None: return None
    return dict(flags=(features[4],features[5]),
        signal_ns=int(bars.dates[signal_index].timestamp())*NS,
        event_ns=int(bars.dates[-1].timestamp())*NS,
        price_ticks=price_ticks(Decimal(str(bars.close[0][-1])),Decimal('.01')))


def identity_text(strategy,incarnation):
    return json.dumps(dict(strategy=strategy,incarnation=incarnation,spec=SPECS[strategy],
        initial_cash=25000000,clip=12,cost_bps=30,mode='paper',version=1),sort_keys=True)


class Journal:
    def __init__(self,path,strategy,incarnation):
        if strategy not in SPECS or len(incarnation)!=32 or len(bytes.fromhex(incarnation))!=16:
            raise ValueError('paper identity')
        self.db=sqlite3.connect(path)
        self.db.execute('pragma journal_mode=WAL')
        self.db.execute('pragma synchronous=FULL')
        self.db.executescript('''
            create table if not exists identity (id integer primary key check(id=1), value text not null);
            create table if not exists attempts (bar integer primary key, count integer not null, done integer not null);
            create table if not exists observations (id integer primary key, stamp real not null, payload text not null);
            create table if not exists fills (id text primary key, side integer not null, price integer not null,
                lots integer not null, notional integer not null, fee integer not null, stamp real,
                first_seen real not null);
            create table if not exists marks (stamp integer primary key, price integer not null);
            create table if not exists equities (stamp integer primary key, equity integer not null);
            create table if not exists stops (reason text primary key, stamp integer not null);
            create table if not exists evidence (bar integer primary key, payload text not null);
        ''')
        expected=identity_text(strategy,incarnation)
        row=self.db.execute('select value from identity where id=1').fetchone()
        if row is not None and row[0]!=expected:
            self.db.close();raise ValueError('paper journal identity mismatch')
        with self.db:
            self.db.execute('insert or ignore into identity values (1,?)',(expected,))
        with self.db:
            self.db.execute('begin immediate')
            if self.db.execute('pragma table_info(fills)').fetchone()[2].upper()=='INTEGER':
                old=self.db.execute('select * from fills').fetchall()
                record=self.db.execute('select payload from observations where id=1').fetchone()
                order=[o['id'] for o in json.loads(record[0])['orders']] if record else []
                by_id={row[0]:row for row in old}
                if len(order)!=len(by_id) or set(order)!=set(by_id):
                    raise ValueError('paper fill migration requires confirmed ordering evidence')
                self.db.execute('alter table fills rename to fills_signed_v1')
                self.db.execute('''create table fills (id text primary key, side integer not null,
                    price integer not null, lots integer not null, notional integer not null,
                    fee integer not null, stamp real, first_seen real not null)''')
                self.db.executemany('insert into fills values (?,?,?,?,?,?,?,?)',
                    [(str(i),*by_id[i][1:]) for i in order])
        self.strategy,self.incarnation=strategy,incarnation

    def attempt(self,bar,now_ns):
        with self.db:
            row=self.db.execute('select count,done from attempts where bar=?',(bar,)).fetchone()
            if row and (row[1] or row[0]>=2): return False
            if row:
                return False
            self.db.execute('insert into attempts values (?,1,0)',(bar,))
            return True

    def retry_after_resync(self,bar):
        with self.db:
            self.db.execute('update attempts set count=2 where bar=? and count=1 and done=0',(bar,))

    def finish(self,bar):
        with self.db: self.db.execute('update attempts set done=1 where bar=?',(bar,))

    def mark(self,stamp,price):
        with self.db: self.db.execute('insert or ignore into marks values (?,?)',(stamp,price))

    def close(self): self.db.close()


def snapshot(path,incarnation,now,epoch=None):
    raw=Path(path).read_bytes()
    if len(raw)>100000: raise ValueError('paper snapshot capacity')
    s=json.loads(raw)
    cost_bps=20 if epoch is not None else 30
    prior=epoch['previous_orders'] if epoch is not None else []
    if epoch is not None and (s['orders'][:len(prior)]!=prior or len(s['orders'])<len(prior)):
        raise ValueError('paper cost epoch prefix mismatch')
    if (s['version']!=1 or s['incarnation']!=incarnation or s['account']!=2 or
            s['venue']!=20 or s['instrument']!=2 or s['funding']!=25000000 or
            s['buy_bps']!=cost_bps or s['sell_bps']!=cost_bps or s['failed']):
        raise ValueError('paper snapshot identity or cost mismatch')
    if not 0<=now-s['stamp']<=5: raise ValueError('paper snapshot stale')
    if s['capacity']!=256 or len(s['orders'])>256: raise ValueError('paper capacity mismatch')
    if s['pending_reports']: raise ValueError('paper reports pending')
    cash,lots,fees,buy,sell=25000000,0,0,0,0
    seen=set()
    for order_index,order in enumerate(s['orders']):
        order_bps=30 if order_index<len(prior) else cost_bps
        identifier=order['id']
        if type(identifier) is not int or not 0<identifier<2**64 or identifier in seen:
            raise ValueError('paper duplicate order')
        seen.add(identifier)
        price,quantity,notional,fee=(order[k] for k in ('price','lots','notional','fee'))
        if (any(type(x) is not int for x in (price,quantity,notional,fee)) or price<=0 or quantity!=12 or
                notional!=price*quantity*100 or fee!=-(-notional*order_bps//10000)):
            raise ValueError('paper fill economics')
        if order['side']==1 and lots==0:
            cash-=notional+fee;lots+=quantity;buy+=notional
        elif order['side']==2 and lots==quantity:
            cash+=notional-fee;lots-=quantity;sell+=notional
        else: raise ValueError('paper inventory sequence')
        fees+=fee
        if cash<0: raise ValueError('paper negative cash')
    if (cash,lots,fees,buy,sell)!=(s['cash'],s['lots'],s['fees'],s['buy_volume'],s['sell_volume']):
        raise ValueError('paper balances not fully committed')
    return s


def ingest(journal,s,now):
    old=journal.db.execute('select id,side,price,lots,notional,fee from fills order by rowid').fetchall()
    old=[(int(row[0]),*row[1:]) for row in old]
    values=[tuple(o[k] for k in ('id','side','price','lots','notional','fee')) for o in s['orders']]
    if len(old)>len(values) or old!=values[:len(old)]:
        raise ValueError('paper ledger prefix changed')
    added=values[len(old):]
    with journal.db:
        for i,v in enumerate(added):
            stamp=s['last_fill_stamp'] if i==len(added)-1 and s['last_fill_stamp']>0 else None
            journal.db.execute('insert into fills values (?,?,?,?,?,?,?,?)',(str(v[0]),*v[1:],stamp,now))
        journal.db.execute('insert or replace into observations values (1,?,?)',(now,json.dumps(s)))


class Book:
    def __init__(self,journal,report,stop_ns,epoch=None):
        self.epoch=epoch
        self.cost_bps=20 if epoch is not None else 30
        self.journal,self.report,self.stop_ns=journal,Path(report),stop_ns
        self.stop=None
        self.lots=None

    def refresh(self):
        try:
            s=snapshot(self.report,self.journal.incarnation,time.time(),self.epoch)
            ingest(self.journal,s,time.time())
            return s
        except RECONCILE_ERRORS:
            return None

    def gate(self,event_ns,price):
        s=self.refresh()
        if s is None:
            self.lots=None
            self.stop='unreconciled';return self.stop
        self.lots=s['lots']
        notional=price*12*100
        mark=s['cash']+s['lots']*price*100
        reserve=-(-(s['lots']*price*100)*self.cost_bps//10000)
        with self.journal.db:
            self.journal.db.execute('insert or replace into equities values (?,?)',(event_ns//NS,mark-reserve))
        peak=self.journal.db.execute('select max(equity) from equities').fetchone()[0]
        if event_ns>=self.stop_ns: self.stop='duration'
        elif len(s['orders'])>=240: self.stop='capacity'
        elif max(25000000,peak)-(mark-reserve)>=10000000: self.stop='drawdown'
        elif s['lots']==0 and s['cash']<notional-(-notional*self.cost_bps//10000): self.stop='insufficient_balance'
        else: self.stop=None
        if self.stop in ('duration','capacity','drawdown'):
            with self.journal.db:
                self.journal.db.execute('insert or ignore into stops values (?,?)',(self.stop,event_ns//NS))
        latched=self.journal.db.execute('select reason from stops order by stamp limit 1').fetchone()
        if latched: self.stop=latched[0]
        return self.stop

    def observe(self,*args,**kwargs): pass

    def render(self):
        reasons=('unreconciled','duration','capacity','drawdown','insufficient_balance')
        return ''.join(f'velocita_indicator_buy_stop{{reason="{r}"}} {int(self.stop==r)}\n' for r in reasons)


def program_text(strategy,root,valid_until_ns):
    _,_,policy,schema=SPECS[strategy]
    digest=hashlib.sha256(json.dumps(dict(strategy=strategy,spec=SPECS[strategy],
        features=['exit_flag','entry_flag'],lag_seconds=300,clip_lots=12,cost_bps=30),sort_keys=True).encode()).hexdigest()
    return '\n'.join([f'catalog.root = {root}','catalog.account = 2','catalog.venue = 20',
        'catalog.instrument = 2','catalog.instrument_spec_version = 1','catalog.venue_capabilities_version = 1',
        f'policy.id = {policy}','policy.version = 1',f'policy.feature_schema_id = {schema}',
        'policy.risk_version = 1',f'policy.model_digest = {digest}','policy.valid_from_ns = 1',
        f'policy.valid_until_ns = {valid_until_ns}','features.generic = 2',
        'rule.1 = 0 ge 1000000 exit','rule.2 = 1 ge 1000000 enter_long',
        'order.time_in_force = fok','order.post_only = prohibited','order.quantity_lots = 12',
        'order.max_notional_micros = 25000000','order.max_market_data_age_ns = 20000000000',
        'source.id = 41'])+'\n'


class Source:
    feature_names=('exit_flag','entry_flag')
    program=PROGRAM
    usd_per_tick=Decimal('.01')

    def __init__(self,strategy,journal,book=None):
        self.strategy,self.journal=strategy,journal
        self.book=book
        self.last_outcome='warming_up'
        self.retry_bar=None

    def tick(self,raw,*,receive_ns,tick_id):
        self.last_outcome='invalid'
        if self.book is not None: self.book.refresh()
        try: bars=decode_bars(json.loads(raw),receive_ns)
        except ValueError: return None
        if not bars.dates: return None
        last_ns=int(bars.dates[-1].timestamp())*NS
        self.journal.mark(last_ns//NS,price_ticks(Decimal(str(bars.close[0][-1])),self.usd_per_tick))
        item=frame(self.strategy,bars)
        if item is None:
            self.last_outcome='warming_up_or_cadence';return None
        if item['event_ns']<receive_ns//BAR_NS*BAR_NS:
            self.last_outcome='bar_missing';return None
        age=receive_ns-item['event_ns']
        if age<0 or age>20*NS:
            self.last_outcome='stale';return None
        if self.retry_bar==item['event_ns']:
            self.retry_bar=None
        elif not self.journal.attempt(item['event_ns'],receive_ns):
            self.last_outcome='duplicate';return None
        self.last_outcome='forwarded'
        features=(*item['flags'],item['price_ticks'])
        if self.book is not None:
            stop=self.book.gate(item['event_ns'],item['price_ticks'])
            if stop=='unreconciled': features=(0,0,item['price_ticks'])
            elif stop in ('duration','capacity','drawdown'): features=(1000000,0,item['price_ticks'])
            if self.book.lots is not None:
                features=(features[0] if self.book.lots else 0,
                          features[1] if not self.book.lots else 0,features[2])
        with self.journal.db:
            self.journal.db.execute('insert or ignore into evidence values (?,?)',(item['event_ns'],raw))
        matched=rules.first_match(self.program,features)
        return Tick(tick_id,item['event_ns'],receive_ns,age,features,
                    hashlib.sha256(raw.encode()).hexdigest(),matched)

    def observe_decision(self,decision,tick):
        row=self.journal.db.execute('select count from attempts where bar=?',(tick.event_ns,)).fetchone()
        if (decision.get('authorization')=='stale_risk_state' and int(decision.get('resync',0)) and row[0]==1):
            self.journal.retry_after_resync(tick.event_ns)
            self.retry_bar=tick.event_ns
        else:
            self.journal.finish(tick.event_ns)
