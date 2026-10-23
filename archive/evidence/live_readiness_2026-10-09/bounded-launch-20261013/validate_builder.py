"""Build the offline owner tool and mutation-check it against production libraries."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[4]
target=a.build/'CMakeFiles/v2-swap-runtime-production-tests.dir'
flags={x.split(' = ',1)[0]:shlex.split(x.split(' = ',1)[1]) for x in (target/'flags.make').read_text().splitlines() if ' = ' in x}
link=shlex.split((target/'link.txt').read_text())
source=(root/'tools/prepare-swap-mvp.cpp').read_text()
test=(root/'tests/test_prepare_swap_mvp.cpp').read_text().replace('#include "../tools/prepare-swap-mvp.cpp"','')
cases=[('baseline',None,None,None),
 ('scope', ' && swap_mvp_buy_scope(state,c.transaction),"opening reducer or one-buy scope refused"', ',"opening reducer or one-buy scope refused"', 'loss_cap_one_atom_refused'),
 ('balances', 'opening.base.value()==c.history.base_atoms && ', '', 'balance_discrepancy_refused'),
 ('mark_age', 'now-mark_time<=l.max_source_age', 'true', 'stale_mark_refused'),
 ('net_floor', 'net.code==BuyNetCostCode::within_arithmetic_bounds_only && intent.min_base_receive.value()>0', 'intent.min_base_receive.value()>0', 'one_lamport_receipt_shortfall_refused'),
 ('quote_age', 'now-quote_time<c.transaction.policy.max_envelope_age_ms', 'true', 'expired_quote_refused')]
results=[]

def build(text,name):
    src=a.output/(name+'.cpp');obj=a.output/(name+'.o');exe=a.output/name
    src.write_text(text)
    command=[link[0],*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],
             '-I'+str(root/'tests'),'-c',str(src),'-o',str(obj)]
    c=subprocess.run(command,cwd=a.build,capture_output=True,text=True)
    (a.output/(name+'-compile.log')).write_text(c.stdout+c.stderr)
    if c.returncode: raise RuntimeError('compile error is not a mutation kill: '+name)
    command=[str(obj) if x.endswith('tests/v2_swap_runtime_production_tests.cpp.o') else x for x in link]
    command[command.index('-o')+1]=str(exe)
    subprocess.run(command,cwd=a.build,check=True,capture_output=True)
    return exe

for name,old,new,assertion in cases:
    changed=source
    if old:
        if changed.count(old)!=1:raise RuntimeError('mutation anchor: '+name)
        changed=changed.replace(old,new)
    exe=build('#define VELOCITA_MVP_BUILDER_TEST\n'+changed+'\n'+test,name)
    run=subprocess.run([str(exe),str(a.output/(name+'-run'))],capture_output=True,text=True,timeout=15)
    passed=run.returncode==0 if assertion is None else run.returncode==1 and 'FAIL '+assertion in run.stderr
    results.append(dict(name=name,passed=passed,exit=run.returncode,assertion=assertion,stdout=run.stdout,stderr=run.stderr))
if all(row['passed'] for row in results):
    build(source,'prepare-swap-mvp')
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(dict(passed=all(row['passed'] for row in results),cases=len(results))))
raise SystemExit(0 if all(row['passed'] for row in results) else 1)
