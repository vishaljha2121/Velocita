import io,json,sys,types,unittest
from pathlib import Path
import argparse
root=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);output=p.parse_args().output
sys.path.insert(0,str(root/'tests'))
import test_idle_policy_renewal as t
out=[]
cases=[
 ('domain-label',t.T.R,'journal_bindings',"b'velocita-direct-invariant-buy-v1'","b'velocita-direct-bison-market-backed-buy-v1'",'test_invariant_schema5_distinct_domain_and_exact_key_count','invariant_schema5_exact_domain_golden'),
 ('schema-key-count',t.T.R,'journal_bindings',"buy-v1',608)","buy-v1',640)",'test_invariant_schema5_distinct_domain_and_exact_key_count','invariant_schema5_domain_admitted'),
 ('owner-role',t.N,'route_transition',"(0,0),(1,1),(3,2),(4,3),(5,4),(8,5),(9,6),(10,7),(11,8),(13,9),(14,10),(22,15)","(0,0),(3,2),(4,3),(5,4),(8,5),(9,6),(10,7),(11,8),(13,9),(14,10),(22,15)",'test_invariant_exact_transition_kind_caps_roles_and_witness','invariant_preserves_caps_and_shared_roles'),
 ('fee-account-role',t.N,'route_transition',"(0,0),(1,1),(3,2),(4,3),(5,4),(8,5),(9,6),(10,7),(11,8),(13,9),(14,10),(22,15)","(0,0),(1,1),(3,2),(4,3),(8,5),(9,6),(10,7),(11,8),(13,9),(14,10),(22,15)",'test_invariant_exact_transition_kind_caps_roles_and_witness','invariant_preserves_caps_and_shared_roles'),
 ('slippage-limit',t.N,'route_transition',"mutable={'schema','account_keys'","mutable={'max_slippage_bps','schema','account_keys'",'test_invariant_exact_transition_kind_caps_roles_and_witness','invariant_preserves_caps_and_shared_roles'),
 ('protected-generation',t.N,'propose',"struct.pack_into('<Q',record,8,ordinal+1)","struct.pack_into('<Q',record,8,ordinal)",'test_invariant_transition_preserves_gateway_journals_and_funding','invariant_protected_ordinal_advances'),
]
for name,module,function,old,new,test,assertion in cases:
 source=Path(module.__file__).read_text()
 # Shared old role mappings may recur for other routes; replace only final occurrence.
 pos=source.rfind(old)
 assert pos>=0,name
 changed=source[:pos]+new+source[pos+len(old):]
 namespace=dict(module.__dict__);exec(compile(changed,module.__file__,'exec'),namespace)
 saved=getattr(module,function);setattr(module,function,namespace[function])
 try:
  result=unittest.TextTestRunner(stream=io.StringIO()).run(unittest.TestSuite([t.RenewalTests(test)]))
  failures='\n'.join(body for _,body in result.failures)
  assert not result.errors and result.failures and assertion in failures,(name,result.errors,failures)
  out.append(dict(mutation=name,killed_by=assertion,errors=0))
 finally:setattr(module,function,saved)
print(json.dumps(out,indent=2))
output.write_text(json.dumps(out,indent=2)+'\n')
