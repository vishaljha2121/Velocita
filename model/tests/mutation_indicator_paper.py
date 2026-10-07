"""Named assertion mutation checks in disposable copies of the paper edge."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'docs/evidence/dual_indicator_paper_2026-10-07'
MUTANTS=(
    ('delay','signal_index=len(bars.dates)-2','signal_index=len(bars.dates)-1','test_sma_delayed_signal_and_integer_price'),
    ('journal_identity',"if row is not None and row[0]!=expected:","if False:",'test_ledgers_isolate_and_bind_on_restart'),
    ('snapshot_identity',"s['incarnation']!=incarnation",'False','test_confirmed_cash_fee_and_volume_oracle'),
    ('snapshot_age',"0<=now-s['stamp']<=5","0<=now-s['stamp']<=600",'test_confirmed_cash_fee_and_volume_oracle'),
    ('commit_reconciliation',"if (cash,lots,fees,buy,sell)!=(s['cash'],s['lots'],s['fees'],s['buy_volume'],s['sell_volume']):",'if False:','test_confirmed_cash_fee_and_volume_oracle'),
    ('fee_rounding',"fee!=-(-notional*order_bps//10000)",'False','test_confirmed_cash_fee_and_volume_oracle'),
    ('freshness','age>20*NS','age>200*NS','test_source_staleness_resync_bound_and_restart'),
    ('resync_bound',"and row[0]==1):","and row[0]>=1):",'test_source_staleness_resync_bound_and_restart'),
)


def main():
    root=OUT/'scratch/mutations';root.mkdir(parents=True,exist_ok=True)
    rows=[]
    for name,before,after,test in MUTANTS:
        case=root/name
        if case.exists(): raise RuntimeError('mutation directory already exists')
        shutil.copytree(ROOT/'model/src',case/'src',ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copy(ROOT/'model/tests/test_indicator_paper.py',case/'test_indicator_paper.py')
        target=case/'src/velocita_model/shadow/indicator_paper.py'
        text=target.read_text()
        if text.count(before)!=1: raise RuntimeError('mutation anchor mismatch '+name)
        target.write_text(text.replace(before,after))
        env={**os.environ,'PYTHONPATH':str(case/'src')}
        proc=subprocess.run([sys.executable,'-B','-m','unittest',f'test_indicator_paper.IndicatorPaperTests.{test}'],
            cwd=case,env=env,text=True,capture_output=True)
        log=proc.stdout+proc.stderr
        (case/'result.log').write_text(log)
        killed=proc.returncode!=0 and f'FAIL: {test}' in log and 'ERROR:' not in log
        rows.append(dict(mutation=name,test=test,killed_by_named_assertion=killed))
    (OUT/'mutation_results.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(json.dumps(rows,indent=2))
    if not all(r['killed_by_named_assertion'] for r in rows): raise SystemExit(1)


if __name__=='__main__': main()
