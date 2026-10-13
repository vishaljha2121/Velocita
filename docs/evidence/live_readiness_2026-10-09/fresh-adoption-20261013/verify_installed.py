"""Read-only verification of dormant public pins and isolated credential metadata."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import time

BASE=Path('/home/vishal/.cache/velocita-build/fresh-adoption-20261013')
SPEC=importlib.util.spec_from_file_location('handoff',BASE/'release/receiver/credential_handoff.py')
H=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(H)
PUBLIC=Path('/etc/velocita-swap/public')
EXPECTED_PROFILE='17370a3cbc1063fd7c8c8c2d2ebd57ea7d471b68e2d40b0e8e894cb4305912f0'
EXPECTED_MANIFEST='ca4bb2fbec356c6ecdd4ba405d322b954337cd76ae76d181bc42439d586d4442'
PINS={'gateway':'4bf9a890eb3e995d00b8bfd8822dbabc611f94d12fc9506bf6e0890a2c247bcc','signer':'9c8ca5d45a1c4fd508c19b024f0dafa8c429ab1d44880cfe0420aad7702aa67f'}

def main():
    H.harden();H.PROVISION.require_host_root()
    profile_raw=H.LAUNCH.pinned_bytes(str(PUBLIC/'runtime.profile'),EXPECTED_PROFILE,8192)
    profile=H.LAUNCH.fields(profile_raw)
    m=json.loads(H.LAUNCH.pinned_bytes(str(PUBLIC/'manifest.json'),EXPECTED_MANIFEST,8192))
    if m['launch_authorized'] is not False:raise ValueError('launch authorized unexpectedly')
    files={name:H.LAUNCH.pinned_bytes(str(PUBLIC/name),m[field],limit) for name,field,limit in [('runtime.profile','runtime_profile_sha256',8192),('account.review','account_review_sha256',8192),('account.history','account_history_sha256',4096)]}
    H.LAUNCH.pinned_bytes(str(PUBLIC/'transaction.review'),profile['transaction_review_digest'],32768)
    now=time.time_ns()//1000000
    for role in H.ROLES:H.LAUNCH.check_bindings(m,files['runtime.profile'],files['account.review'],files['account.history'],now,role)
    H.LAUNCH.pinned_bytes(m['binary'],m['binary_sha256'],64*1024**2)
    results={}
    for role,(uid,leaf) in H.ROLES.items():
        path=Path('/run/velocita-swap-credentials')/role/leaf
        H.tmpfs(path.parent);raw=H.private_read(path,uid);H.pin(raw,PINS[role]);H.validate_secret(role,raw,profile,now)
        meta=path.stat()
        if meta.st_gid!=uid:raise ValueError('credential group mismatch')
        other='signer' if role=='gateway' else 'gateway';other_uid=H.ROLES[other][0]
        probe=subprocess.run(['/usr/sbin/runuser','-u','velocita-swap-'+other,'--','/usr/bin/test','-r',str(path)],capture_output=True,timeout=10)
        if probe.returncode!=1:raise ValueError('cross-role readability or probe error')
        unit='velocita-swap-'+role+'.service'
        output=subprocess.check_output(['/usr/bin/systemctl','show',unit,'-p','ActiveState','-p','SubState','-p','UnitFileState'],text=True)
        state=dict(line.split('=',1) for line in output.splitlines())
        if state!={'ActiveState':'inactive','SubState':'dead','UnitFileState':'static'}:raise ValueError('service state changed')
        installed=H.LAUNCH.pinned_bytes('/etc/systemd/system/'+unit,hashlib.sha256(H.LAUNCH.PROFILE.render(m)[unit+'.review'].encode()).hexdigest(),32768)
        recovery=Path('/var/lib/velocita-swap-'+role+'/recovery')
        if list(recovery.iterdir()):raise ValueError('unexpected recovery initialization')
        results[role]=dict(uid=uid,gid=meta.st_gid,mode=oct(stat.S_IMODE(meta.st_mode)),single_link=meta.st_nlink==1,tmpfs=True,credential_hash_matches=True,credential_matches_profile=True,other_role_cannot_read=True,unit_matches_manifest=True,recovery_directory_empty=True,service=state)
    print(json.dumps(dict(checked_at_ms=now,source_commit=m['source_commit'],binary_sha256=m['binary_sha256'],manifest_sha256=EXPECTED_MANIFEST,profile_sha256=EXPECTED_PROFILE,account_documents_installed=True,credentials_installed=True,roles=results,account_review_current=True,account_review_expires_ms=int(H.LAUNCH.fields(files['account.review'])['expires_at_ms']),launch_authorized=False,ledger_initialized=False,services_started=False,signed=False,broadcast=False,live_admitted=False),indent=2))

if __name__=='__main__':
    try:main()
    except Exception as e:print(json.dumps(dict(verified=False,error_type=type(e).__name__)));raise SystemExit(1)
