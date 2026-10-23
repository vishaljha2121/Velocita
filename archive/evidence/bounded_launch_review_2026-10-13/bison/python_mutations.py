import importlib.util,io,json,unittest
from pathlib import Path
root=Path(__file__).resolve().parents[4]
def load():
 spec=importlib.util.spec_from_file_location('bison_mutation_tests',root/'tests/test_idle_policy_renewal.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
cases=[('legacy_domain','review_transition.py',"'4':(b'velocita-direct-bison-market-backed-buy-v1',640)","'4':(b'velocita-direct-kipseli-buy-v1',640)",'test_bison_schema4_distinct_domain_and_exact_key_count','bison_not_legacy_twenty_key_domain'),('owner_mapping_removed','idle_policy_renewal.py',"(11,8),(13,10),(14,11),(22,15),(25,18)","(11,8),(13,10),(14,11),(22,15)",'test_bison_exact_transition_kind_caps_roles_and_witness','bison_preserves_caps_and_shared_roles'),('slippage_mutable','idle_policy_renewal.py',"mutable={'schema','account_keys'","mutable={'max_slippage_bps','schema','account_keys'",'test_bison_exact_transition_kind_caps_roles_and_witness','bison_preserves_caps_and_shared_roles'),('document_pin_removed','idle_policy_renewal.py',"if witness['before_review_sha256']!=R.digest(before) or witness['after_review_sha256']!=R.digest(after):","if False:",'test_bison_exact_transition_kind_caps_roles_and_witness','bison_witness_pins')]
rows=[]
for name,file,old,new,test,assertion in cases:
 m=load();path=root/'deploy/v2/swap-runtime'/file;source=path.read_text();assert source.count(old)==1,(name,source.count(old));mutated=source.replace(old,new)
 targets=[m.N] if file=='idle_policy_renewal.py' else list({id(x):x for x in (m.T.R,m.N.R)}.values())
 for target in targets:exec(compile(mutated,str(path),'exec'),target.__dict__)
 output=io.StringIO();result=unittest.TextTestRunner(stream=output).run(unittest.TestSuite([m.RenewalTests(test)]));killed=bool(result.failures) and not result.errors and assertion in output.getvalue();rows.append(dict(mutation=name,named_assertion=assertion,killed=killed,failures=len(result.failures),errors=len(result.errors)))
 # Restore only in-memory module definitions; no production file ever changed.
 for target in targets:exec(compile(source,str(path),'exec'),target.__dict__)
 if not killed:print(output.getvalue());raise SystemExit('mutation survived or errored '+name)
out=root/'docs/evidence/bounded_launch_review_2026-10-13/bison/transition-mutations.json';out.write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
