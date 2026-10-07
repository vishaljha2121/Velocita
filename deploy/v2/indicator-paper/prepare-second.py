"""Prepare a bounded continuation from a flat paper account with its edge stopped."""
import argparse
import json
from pathlib import Path
import time

from velocita_model.shadow.indicator_paper import snapshot
from velocita_model.shadow.stochastic_second import VARIANT,pilot_program


def prepare(config,report,now_ns):
    old=snapshot(report,config['incarnation'],now_ns/1e9)
    if old['lots'] or old['pending_reports']: raise ValueError('cutover requires flat reconciled account')
    if len(old['orders'])>=190: raise ValueError('insufficient paper order reserve')
    epoch=dict(variant=VARIANT,start_ns=now_ns,end_ns=now_ns+2100*10**9,
        previous_orders=old['orders'],cash=old['cash'],fees=old['fees'],
        volume=old['buy_volume']+old['sell_volume'],cost_bps=20)
    c=dict(config,program='/edge/indicator-1s.program',stop_ns=now_ns+1800*10**9,epoch=epoch)
    return c,pilot_program(c['catalog_root'],epoch['end_ns'])


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--program',type=Path,required=True)
    a=p.parse_args()
    c,program=prepare(json.loads(a.config.read_text()),a.report,time.time_ns())
    with a.output.open('x') as out: out.write(json.dumps(c,indent=2)+'\n')
    with a.program.open('x') as out: out.write(program)


if __name__=='__main__': main()
