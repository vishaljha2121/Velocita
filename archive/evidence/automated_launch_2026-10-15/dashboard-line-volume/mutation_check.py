"""Compile isolated mutants and require the independent oracle to reject each."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import types
import unittest

ROOT=Path(__file__).resolve().parents[4]
sys.path[:0]=[str(ROOT/'model/src'),str(ROOT/'model/tests')]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

observer_tests=load('observer_tests',ROOT/'tests/test_live_observer.py')
observer=ROOT/'deploy/v2/observability/live_observer.py'
cases=[
    ('buy_sell_sign',observer,"*(1 if event['side']=='buy' else -1)","*1",observer_tests,'ConfirmedSettlementsTests.test_decision_difference_buy_and_sell_oracles_and_missing'),
    ('wrong_effect',observer,"references.get(event['effect'])","next(iter(references.values()),None)",observer_tests,'ConfirmedSettlementsTests.test_reference_joins_only_same_effect_side_and_preconfirmation_time'),
    ('wrong_side',observer,"ref['side']==event['side']","True",observer_tests,'ConfirmedSettlementsTests.test_reference_joins_only_same_effect_side_and_preconfirmation_time'),
    ('conflicting_reference',observer,"result[row['effect']]!=row","False",observer_tests,'ConfirmedSettlementsTests.test_reference_identity_conflicts_invalid_and_missing'),
]
results=[]
for name,path,old,new,tests,test in cases:
    source=path.read_text()
    if source.count(old)!=1:raise RuntimeError('mutation anchor '+name)
    module=types.ModuleType('isolated_mutant')
    module.__package__=''
    exec(compile(source.replace(old,new),str(path)+'::'+name,'exec'),module.__dict__)
    attribute='O'
    prior=getattr(tests,attribute)
    setattr(tests,attribute,module)
    stream=io.StringIO()
    suite=unittest.defaultTestLoader.loadTestsFromName(test,tests)
    result=unittest.TextTestRunner(stream=stream).run(suite)
    setattr(tests,attribute,prior)
    killed=bool(result.failures) and not result.errors
    results.append(dict(mutant=name,recompiled=True,killed=killed,output=stream.getvalue()))
Path(__file__).with_name('reference-mutations.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps([dict(mutant=r['mutant'],killed=r['killed']) for r in results]))
if not all(r['killed'] for r in results):raise SystemExit(1)
