"""Prepare an ongoing paper session from a finalized pilot; no venue/cluster calls."""
import argparse
import json
from pathlib import Path
import sqlite3
import time

from .indicator_paper import snapshot
from .paper_completion import open_completion
from .stochastic_second import CASH_VARIANT,NO_DEADLINE,pilot_program


def prepare(c,ledger,now_ns):
    if c['mode']!='paper' or c['strategy']!='stochastic': raise ValueError('paper stochastic only')
    if c['epoch']['previous_orders']: raise ValueError('mixed prior cost epochs need explicit migration')
    s=snapshot(c['venue_report'],c['incarnation'],now_ns/1e9,c['epoch'])
    db,final,_=open_completion(c,now_ns/1e9)
    try:
        if s['orders']!=final['orders'] or s['cash']!=final['cash'] or s['lots']:
            raise ValueError('completed account changed')
        if len(s['orders'])>=240: raise ValueError('paper order capacity exhausted')
        epoch=dict(variant=CASH_VARIANT,start_ns=now_ns,end_ns=NO_DEADLINE,
            previous_orders=s['orders'],previous_cost_bps=20,cash=s['cash'],fees=s['fees'],
            volume=s['buy_volume']+s['sell_volume'],cost_bps=20)
        path=Path(ledger)
        with path.open('xb'): pass
        out=sqlite3.connect(path)
        try:
            db.backup(out)
            with out:
                out.execute('alter table completion rename to prior_completion')
                out.execute('alter table variant_epoch rename to prior_variant_epoch')
                out.execute('alter table stops rename to prior_stops')
                out.execute('create table variant_epoch (id integer primary key check(id=1), payload text not null)')
                out.execute('insert into variant_epoch values (1,?)',
                    (json.dumps(epoch,sort_keys=True,separators=(',',':')),))
        finally: out.close()
    finally: db.close()
    return dict(c,ledger=str(path),epoch=epoch,start_ns=now_ns,stop_ns=0,
                program='/edge/indicator-cash.program')


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ledger',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists() or Path('/edge/indicator-cash.program').exists():
        raise FileExistsError('continuation already prepared')
    c=prepare(json.loads(a.config.read_text()),a.ledger,time.time_ns())
    with Path(c['program']).open('x') as f:
        f.write(pilot_program(c['catalog_root'],NO_DEADLINE,CASH_VARIANT))
    with a.output.open('x') as f: json.dump(c,f,indent=2);f.write('\n')
    print(json.dumps(dict(cash=c['epoch']['cash'],prior_fills=len(c['epoch']['previous_orders']),
        ledger=c['ledger'],variant=CASH_VARIANT)))


if __name__=='__main__': main()
