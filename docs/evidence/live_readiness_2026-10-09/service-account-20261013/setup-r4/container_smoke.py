#!/usr/bin/env python3
"""Synthetic preflight only; container UIDs are NOT permanent host principals."""
import json,os,subprocess,time
from pathlib import Path
run=os.urandom(8).hex();nonce=os.urandom(16).hex();children=[];result={}
for role,uid in [('signer',29002),('gateway',29001)]:
    root=Path('/run/velocita-comm-'+run+'-'+role);root.mkdir(mode=0o750 if role=='signer' else 0o700);os.chown(root,uid,uid)

def drop(uid):
    def apply():
        os.setgroups([uid,29002] if uid==29001 else [uid]);os.setgid(uid);os.setuid(uid)
    return apply
try:
    for role,uid in [('signer',29002),('gateway',29001)]:
        children.append((role,subprocess.Popen(['/opt/velocita-communication-probe',role,run,nonce],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,preexec_fn=drop(uid),env={'PATH':'/usr/bin:/bin','AERON_TERM_BUFFER_LENGTH':'1048576'})))
    deadline=time.monotonic()+45
    for role,child in children:
        out,err=child.communicate(timeout=max(1,deadline-time.monotonic()));result[role]={'returncode':child.returncode,'stdout':out,'stderr':err}
finally:
    for role,child in children:
        if child.poll() is None:child.kill();child.wait(timeout=3)
result['host_production_uids_qualified']=False
result['preflight_only']=True
print(json.dumps(result,indent=2))
raise SystemExit(0 if all(result.get(role,{}).get('returncode')==0 for role in ['signer','gateway']) else 1)
