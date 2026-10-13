"""Send one reviewed credential over SSH stdin into owner-only NUC1 tmpfs."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
H=load('credential_handoff',ROOT/'deploy/v2/swap-runtime/credential_handoff.py')
SESSION=load('stage_session',ROOT/'tools/tm-account-session.py')
HISTORY=load('stage_history',ROOT/'tools/tm-account-history-check.py')


def material(role,path,profile):
    document=SESSION.read_private(path)
    if role=='gateway':
        pair,expiry=SESSION.token_pair(document)
        if int(expiry*1000)<H.LAUNCH.unsigned(profile['expires_at_ms']): raise ValueError('profile outlives session')
        raw=pair['access_token'].encode('ascii')
    else:
        jwk=document.get('private_key',{})
        if document.get('algorithm')!='ES256' or jwk.get('kty')!='EC' or jwk.get('crv')!='P-256': raise ValueError('P-256 API key required')
        key_id=document.get('key_id')
        if not isinstance(key_id,str) or re.fullmatch('[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}',key_id) is None: raise ValueError('API key identifier malformed')
        if HISTORY.key_bytes(profile.get('signer_id')).hex()!=profile.get('owner_wallet'): raise ValueError('signer wallet mismatch')
        raw=SESSION.MINT.jwk_to_pem(jwk)
    H.validate_secret(role,raw,profile,time.time_ns()//1000000)
    return raw


def send(role,raw,receiver,receiver_sha):
    if re.fullmatch('/home/vishal/\\.cache/velocita-build/[a-zA-Z0-9_/-]+/credential_handoff.py',receiver) is None or '..' in Path(receiver).parts:
        raise ValueError('reviewed NUC receiver path required')
    if re.fullmatch('[0-9a-f]{64}',receiver_sha) is None: raise ValueError('receiver pin required')
    bootstrap='import hashlib,importlib.util,pathlib,sys; p=pathlib.Path(sys.argv[1]); assert hashlib.sha256(p.read_bytes()).hexdigest()==sys.argv[2]; s=importlib.util.spec_from_file_location("receiver",p); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); sys.argv=[str(p),"stage","--role",sys.argv[3],"--sha256",sys.argv[4]]; m.main()'
    digest=hashlib.sha256(raw).hexdigest()
    command=shlex.join(['python3','-B','-c',bootstrap,receiver,receiver_sha,role,digest])
    result=subprocess.run(['ssh','-T','-o','BatchMode=yes','-o','ConnectTimeout=10','vishal@100.72.127.102',command],input=raw,capture_output=True,timeout=30)
    if result.returncode: raise ValueError('remote staging refused; no automatic retry')
    if len(result.stdout)>4096: raise ValueError('staging response too large')
    report=json.loads(result.stdout)
    if set(report)!={'staged_path','role','sha256','installed'} or report['role']!=role or report['sha256']!=digest or report['installed'] is not False:
        raise ValueError('staging response identity mismatch')
    H.source_path(Path(report['staged_path']),role)
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--role',choices=('gateway','signer'),required=True)
    p.add_argument('--private-source',type=Path,required=True);p.add_argument('--profile',type=Path,required=True)
    p.add_argument('--profile-sha256',required=True);p.add_argument('--receiver',required=True);p.add_argument('--receiver-sha256',required=True);p.add_argument('--send',action='store_true')
    a=p.parse_args();H.harden()
    if not a.send:
        print(json.dumps(dict(proposal_only=True,role=a.role,destination='vishal@100.72.127.102 private tmpfs',credentials_read=False)));return
    fd=os.open(a.profile,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        import stat
        if not stat.S_ISREG(os.fstat(fd).st_mode): raise ValueError('regular profile required')
        raw=os.read(fd,8193)
    finally:os.close(fd)
    if len(raw)>8192: raise ValueError('bounded profile required')
    H.pin(raw,a.profile_sha256);profile=H.LAUNCH.fields(raw)
    secret=material(a.role,a.private_source,profile)
    print(json.dumps(send(a.role,secret,a.receiver,a.receiver_sha),sort_keys=True))


if __name__=='__main__':
    try:main()
    except Exception:
        print('REFUSED: credential preparation or staging failed; no automatic retry',file=sys.stderr)
        raise SystemExit(64)
