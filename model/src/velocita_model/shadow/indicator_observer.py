"""Read-only HTTP metrics from the paper venue report and the edge's fill ledger."""
import argparse
from http.server import BaseHTTPRequestHandler,HTTPServer
import json
from pathlib import Path
import sqlite3
import time

from .indicator_paper import identity_text,snapshot


def labelled(text,strategy,prefix):
    lines=[]
    for line in text.splitlines():
        if not line or line.startswith('#'): continue
        metric,value=line.rsplit(' ',1)
        if not metric.startswith('velocita_'): continue
        if not metric.startswith('velocita_indicator_'):
            metric=prefix+metric[len('velocita_'):]
        if '{' in metric:
            metric=metric.replace('{',f'{{strategy="{strategy}",',1)
        else: metric+=f'{{strategy="{strategy}"}}'
        lines.append(metric+' '+value)
    return '\n'.join(lines)+'\n'


MEASURED_BUY_BPS,MEASURED_SELL_BPS=100,98


def fee(notional,bps): return -(-notional*bps//10000)


def measured_cost_account(fills):
    cash,fees=25000000,0
    for side,notional in fills:
        charge=fee(notional,MEASURED_BUY_BPS if side==1 else MEASURED_SELL_BPS)
        cash+=-(notional+charge) if side==1 else notional-charge
        fees+=charge
    return cash,fees


def open_ledger(path,strategy,incarnation):
    db=sqlite3.connect(f'file:{path}?mode=ro',uri=True)
    row=db.execute('select value from identity where id=1').fetchone()
    if row is None or row[0]!=identity_text(strategy,incarnation):
        db.close();raise ValueError('paper journal identity mismatch')
    return db


def file_metrics(c,strategy,now,emit,out):
    for key,name in (('edge_metrics','edge'),('gateway_metrics','gateway')):
        try:
            path=Path(c[key])
            emit('source_file_age_seconds',max(0,now-path.stat().st_mtime),f',source="{name}"')
            out.append(labelled(path.read_text(),strategy,'velocita_indicator_'))
        except OSError: pass


def metrics(c,now):
    strategy=c['strategy']
    out=[]
    def emit(name,value,extra=''):
        out.append(f'velocita_indicator_{name}{{strategy="{strategy}"{extra}}} {value}')
    emit('initial_cash_usd',25)
    emit('stop_timestamp_seconds',c['stop_ns']/1e9)
    emit('paper_mode',1)
    emit('cost_bps',30)
    emit('measured_cost_bps',MEASURED_BUY_BPS,',side="buy"')
    emit('measured_cost_bps',MEASURED_SELL_BPS,',side="sell"')
    file_metrics(c,strategy,now,emit,out)
    try:
        s=snapshot(c['venue_report'],c['incarnation'],now)
    except (ValueError,OSError,KeyError,TypeError):
        s=None
    emit('reconciled',int(s is not None))
    try:
        db=open_ledger(c['ledger'],strategy,c['incarnation'])
    except (sqlite3.Error,ValueError,OSError):
        emit('ledger_readable',0)
        return '\n'.join(out)+'\n'
    try:
        emit('ledger_readable',1)
        fills=db.execute('select id,side,price,lots,notional,fee,stamp,first_seen from fills order by rowid').fetchall()
        mark=db.execute('select stamp,price from marks order by stamp desc limit 1').fetchone()
        if mark:
            emit('price_usd',mark[1]/100)
            emit('price_age_seconds',max(0,now-mark[0]))
        emit('unknown_fill_times',sum(f[6] is None for f in fills))
        for days in (7,14):
            recent=[f for f in fills if f[6] is not None and f[6]>=now-days*86400]
            emit('period_volume_usd',sum(f[4] for f in recent)/1e6,f',days="{days}"')
            emit('period_fees_usd',sum(f[5] for f in recent)/1e6,f',days="{days}"')
            emit('period_completed_trades',sum(f[1]==2 for f in recent),f',days="{days}"')
        delays=[f[6]-f[6]//300*300 for f in fills if f[6] is not None]
        if delays:
            emit('bar_close_to_fill_seconds',delays[-1],',stat="last"')
            emit('bar_close_to_fill_seconds',sum(delays)/len(delays),',stat="mean"')
            emit('bar_close_to_fill_seconds',max(delays),',stat="max"')
            emit('bar_close_to_fill_samples',len(delays))
        for side,name in ((1,'buy'),(2,'sell')):
            trades=[f for f in fills if f[1]==side and f[6] is not None]
            emit('fills_total',sum(f[1]==side for f in fills),f',side="{name}"')
            if trades:
                f=trades[-1]
                emit('last_fill_price_usd',f[2]/100,f',side="{name}"')
                emit('last_fill_timestamp_seconds',f[6],f',side="{name}"')
                emit('fill_marker_usd',f[2]/100 if 0<=now-f[6]<30 else 'NaN',f',side="{name}"')
        if s is None: return '\n'.join(out)+'\n'
        emit('snapshot_age_seconds',now-s['stamp'])
        emit('cash_usd',s['cash']/1e6)
        emit('position_sol',s['lots']/100)
        emit('fees_usd_total',s['fees']/1e6)
        volume=s['buy_volume']+s['sell_volume']
        emit('volume_usd_total',volume/1e6)
        emit('order_count',len(s['orders']))
        emit('order_capacity',240)
        emit('lifetime_completed_trades',sum(o['side']==2 for o in s['orders']))
        emit('ledger_fills_behind',len(s['orders'])-len(fills))
        if mark and 0<=now-mark[0]<=600:
            value=s['lots']*mark[1]*100
            equity=s['cash']+value-fee(value,30)
            emit('equity_usd',equity/1e6)
            emit('net_pnl_usd',(equity-25000000)/1e6)
            recorded=db.execute('select max(equity) from equities').fetchone()[0] or 0
            emit('drawdown_usd',(max(25000000,recorded,equity)-equity)/1e6)
            for days in (7,14):
                before=db.execute('select equity from equities where stamp<=? order by stamp desc limit 1',(now-days*86400,)).fetchone()
                emit('period_pnl_usd',(equity-(before[0] if before else 25000000))/1e6,f',days="{days}"')
            if equity<25000000: emit('volume_per_usd_lost',volume/(25000000-equity),',cost="scenario"')
            measured_cash,measured_fees=measured_cost_account([(o['side'],o['notional']) for o in s['orders']])
            measured_equity=measured_cash+value-fee(value,MEASURED_SELL_BPS)
            emit('measured_cost_fees_usd_total',measured_fees/1e6)
            emit('measured_cost_net_pnl_usd',(measured_equity-25000000)/1e6)
            if measured_equity<25000000:
                emit('volume_per_usd_lost',volume/(25000000-measured_equity),',cost="measured"')
        return '\n'.join(out)+'\n'
    finally: db.close()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--port',type=int,default=9460)
    args=parser.parse_args()
    configs=json.loads(args.config.read_text())
    if len(configs)!=1 or configs[0]['strategy'] not in ('sma','stochastic'):
        raise ValueError('exactly one active paper profile is required')
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path!='/metrics': self.send_error(404);return
            try:
                text=''.join(metrics(c,time.time()) for c in configs)
            except Exception as error:
                print(f'observer refused: {type(error).__name__}: {error}',flush=True)
                self.send_error(503);return
            payload=text.encode()
            self.send_response(200)
            self.send_header('Content-Type','text/plain; version=0.0.4')
            self.send_header('Content-Length',str(len(payload)))
            self.end_headers();self.wfile.write(payload)

        def log_message(self,*args): pass
    HTTPServer(('127.0.0.1',args.port),Handler).serve_forever()


if __name__=='__main__': main()
