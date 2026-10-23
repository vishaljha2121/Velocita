"""Named-assertion mutations for dormant adoption and identity translation."""
import importlib.util
import io
import json
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[4]

def load(path,name):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

cases=[
 ('receiver_cli_typo','test_tm_stage_swap_credential.py','S','a.receiver,a.receiver_sha256)','a.receiver,a.receiver_sha)','test_cli_passes_receiver_digest_to_transfer','cli_receiver_digest_reaches_transfer'),
 ('raw_incarnation','test_swap_service_launcher.py','L',"hashlib.sha256(bytes.fromhex(manifest['incarnation'])).hexdigest()","manifest['incarnation']",'test_profile_uses_cpp_incarnation_digest_not_raw_cluster_id','cpp_incarnation_digest_admitted'),
 ('api_key_as_wallet','test_tm_stage_swap_credential.py','S',"HISTORY.key_bytes(profile.get('signer_id')).hex()!=profile.get('owner_wallet')","key_id.replace('-','').lower()!=profile.get('signer_id')",'test_wallet_signer_id_is_distinct_from_api_key_identifier','wallet_signer_not_api_key_id'),
 ('wallet_check_removed','test_tm_stage_swap_credential.py','S',"if HISTORY.key_bytes(profile.get('signer_id')).hex()!=profile.get('owner_wallet'): raise ValueError('signer wallet mismatch')","if False: raise ValueError('signer wallet mismatch')",'test_wrong_wallet_refused_before_conversion','wrong_wallet_refused'),
 ('profile_digest_removed','test_swap_account_adoption.py','A',"if key and sha(files[name])!=new[key]","if False",'test_all_content_pins_and_exact_files','document_tampering_refused'),
 ('existing_profile_allowed','test_swap_account_adoption.py','A',"if any(old[k] is not None or new[k] is None for k in PROFILE.PENDING_ACCOUNT_PINS)","if False",'test_existing_profile_cannot_be_overwritten','existing_profile_refused'),
]
results=[]
for name,testfile,alias,old,new,method,assertion in cases:
 t=load(ROOT/'tests'/testfile,'test_'+name);m=getattr(t,alias);source=Path(m.__file__).read_text()
 if source.count(old)!=1:raise ValueError('mutation anchor '+name)
 scope=dict(m.__dict__);exec(compile(source.replace(old,new),m.__file__,'exec'),scope)
 for key,value in scope.items():setattr(m,key,value)
 cls=next(c for c in t.__dict__.values() if isinstance(c,type) and issubclass(c,unittest.TestCase) and hasattr(c,method))
 stream=io.StringIO();r=unittest.TextTestRunner(stream=stream).run(unittest.TestSuite([cls(method)]));output=stream.getvalue()
 killed=bool(r.failures) and not r.errors and assertion in output
 results.append(dict(name=name,killed=killed,named_assertion=assertion,errors=len(r.errors)))
print(json.dumps(results,indent=2))
if not all(x['killed'] for x in results):raise SystemExit(1)
