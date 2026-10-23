"""Offline named-assertion mutations; no installation or journal access."""
import importlib.util
import json
from pathlib import Path
import types
import unittest
ROOT=Path(__file__).resolve().parents[3]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
source=ROOT/'deploy/v2/swap-runtime/idle_policy_renewal.py';original=source.read_text()
mutations=[
 ('review_pin',"if witness['before_review_sha256']!=R.digest(before) or witness['after_review_sha256']!=R.digest(after):",'if False:','route_review_pins_required'),
 ('execution_caps',"if any(prior[k]!=current[k] for k in prior if k not in mutable):",'if False:','route_scope_checked_before_identity'),
 ('owner_accounts',"if a[old*64:(old+1)*64]!=b[new*64:(new+1)*64]:",'if False:','route_scope_checked_before_identity')]
rows=[]
for name,before,after,label in mutations:
    if original.count(before)!=1:raise RuntimeError('mutation anchor '+name)
    mutant=types.ModuleType('mutant');mutant.__file__=str(source);exec(compile(original.replace(before,after),str(source),'exec'),mutant.__dict__)
    test=load('route_mutation_tests',ROOT/'tests/test_idle_policy_renewal.py');test.N=mutant
    result=unittest.TestResult();test.RenewalTests('test_route_migration_rejects_unpinned_witness_and_changed_caps').run(result)
    killed=bool(result.failures) and not result.errors and any(label in message for _,message in result.failures)
    rows.append(dict(name=name,named_assertion=label,killed=killed,test_errors=len(result.errors)))
    if not killed:raise RuntimeError('not named assertion kill '+name)
out=Path.home()/'.cache/velocita-build/byreal-qualification-20261013/transition-mutations.json'
out.write_text(json.dumps(dict(results=rows,named_kills=len(rows)),indent=2)+'\n');print(json.dumps(rows,indent=2))
