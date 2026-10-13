"""Isolated named mutation checks for credential guards; synthetic material only."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
CASES=[
 ('rotation_role',"if role!='gateway' or leaf!='session.bearer':",'if False:','test_rotation_requires_old_pin_and_never_rotates_signer','signer_rotation_refused'),
 ('rotation_pin','        pin(os.read(old,LIMIT+1),previous)','        pass','test_rotation_requires_old_pin_and_never_rotates_signer','old_bearer_pin_required'),
 ('bearer_pin','        pin(secret,profile.get(\'bearer_digest\'))','        pass','test_wrong_bearer_refused','wrong_bearer_refused'),
 ('key_pin',"elif public_key(secret)!=profile.get('signer_public_key'):","elif False:",'test_wrong_signer_refused','wrong_signer_refused'),
 ('lifetime',"if LAUNCH.unsigned(profile['expires_at_ms'])<=now_ms+60000:",'if False:','test_expired_bearer_refused','expired_bearer_refused'),
 ('users',"if (profile.get('gateway_uid'),profile.get('signer_uid'))!=('29001','29002'):",'if False:','test_wrong_role_users_refused','wrong_role_users_refused'),
 ('tmpfs',"if result.stdout.strip()!='tmpfs':",'if False:','test_persistent_storage_refused','persistent_storage_refused'),
 ('exclusive','os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW','os.O_WRONLY|os.O_CREAT|os.O_NOFOLLOW','test_private_files_and_create_only','existing_credential_never_overwritten'),
 ('inactive',"if state.stdout.strip()!='inactive':",'if False:','test_active_services_refuse_before_private_read','active_services_refused')]

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);rows=[]
 for name,before,after,test,assertion in [('baseline',None,None,None,None)]+CASES:
  root=a.output/name;shutil.copytree(ROOT/'deploy/v2/swap-runtime',root/'deploy/v2/swap-runtime',ignore=shutil.ignore_patterns('__pycache__'))
  (root/'tests').mkdir();shutil.copy2(ROOT/'tests/test_swap_credential_handoff.py',root/'tests/test_swap_credential_handoff.py')
  if before:
   path=root/'deploy/v2/swap-runtime/credential_handoff.py';s=path.read_text();assert s.count(before)==1;path.write_text(s.replace(before,after))
  target='test_swap_credential_handoff'+('.HandoffTests.'+test if test else '')
  r=subprocess.run([sys.executable,'-B','-m','unittest',target],cwd=root,env=dict(os.environ,PYTHONPATH=str(root/'tests')),capture_output=True,text=True,timeout=30)
  log=r.stdout+r.stderr;(root/'result.log').write_text(log)
  passed=r.returncode==0 if not before else r.returncode==1 and '\nFAIL:' in log and '\nERROR:' not in log and assertion in log
  rows.append(dict(name=name,passed=passed,assertion=assertion,exit=r.returncode))
 (a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2));return 0 if all(r['passed'] for r in rows) else 1
if __name__=='__main__':raise SystemExit(main())
