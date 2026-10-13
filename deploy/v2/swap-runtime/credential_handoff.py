"""Create-only tmpfs credential handoff; no authentication, signing or service start."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import resource
import stat
import subprocess
import sys
import time
import uuid

sys.path.insert(0,str(Path(__file__).resolve().parent))
import fd_handoff as FD
import provision_services as PROVISION
import service_launcher as LAUNCH

ROLES={'gateway':(29001,'session.bearer'),'signer':(29002,'signer.pem')}
LIMIT=4096
OPENSSL='/opt/homebrew/opt/openssl@3/bin/openssl' if sys.platform=='darwin' else '/usr/bin/openssl'


def harden():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    os.umask(0o077)


def pin(raw,expected):
    if not isinstance(expected,str) or re.fullmatch('[0-9a-f]{64}',expected) is None or hashlib.sha256(raw).hexdigest()!=expected:
        raise ValueError('content pin mismatch')


def public_key(pem):
    if not 0<len(pem)<=LIMIT or not pem.startswith(b'-----BEGIN EC PRIVATE KEY-----\n') or not pem.endswith(b'-----END EC PRIVATE KEY-----\n'):
        raise ValueError('bounded EC private PEM required')
    check=subprocess.run([OPENSSL,'ec','-check','-noout'],input=pem,capture_output=True,timeout=10)
    if check.returncode: raise ValueError('EC private key validation failed')
    result=subprocess.run([OPENSSL,'ec','-pubout','-conv_form','compressed','-outform','DER'],input=pem,capture_output=True,timeout=10)
    prefix=bytes.fromhex('3039301306072a8648ce3d020106082a8648ce3d030107032200')
    if result.returncode or len(result.stdout)!=len(prefix)+33 or not result.stdout.startswith(prefix):
        raise ValueError('P-256 public identity required')
    return result.stdout[len(prefix):].hex()


def validate_secret(role,secret,profile,now_ms):
    if role not in ROLES or not 0<len(secret)<=LIMIT: raise ValueError('bounded fixed role credential required')
    if (profile.get('gateway_uid'),profile.get('signer_uid'))!=('29001','29002'):
        raise ValueError('fixed production credential users required')
    if role=='gateway':
        if re.fullmatch(rb'[A-Za-z0-9._~+/-]+=*',secret) is None: raise ValueError('bearer format refused')
        pin(secret,profile.get('bearer_digest'))
        if LAUNCH.unsigned(profile['expires_at_ms'])<=now_ms+60000: raise ValueError('bearer lifetime too short')
    elif public_key(secret)!=profile.get('signer_public_key'):
        raise ValueError('signer public identity mismatch')


def private_read(path,uid):
    fd=FD._open_components(str(path),uid)
    try:
        before=os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or (before.st_uid,stat.S_IMODE(before.st_mode),before.st_nlink)!=(uid,0o600,1) or not 0<before.st_size<=LIMIT:
            raise ValueError('private credential metadata refused')
        raw=os.read(fd,LIMIT+1);after=os.fstat(fd)
        if len(raw)!=before.st_size or (before.st_size,before.st_mtime_ns,before.st_ctime_ns)!=(after.st_size,after.st_mtime_ns,after.st_ctime_ns):
            raise ValueError('credential changed during read')
        return raw
    finally: os.close(fd)


def tmpfs(path):
    result=subprocess.run(['/usr/bin/findmnt','-n','-o','FSTYPE','--target',str(path)],capture_output=True,text=True,check=True,timeout=10)
    if result.stdout.strip()!='tmpfs': raise ValueError('credential storage must be tmpfs')


def exclusive(directory,name,raw,uid,gid):
    fd=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=directory)
    try:
        os.fchown(fd,uid,gid);os.fchmod(fd,0o600)
        offset=0
        while offset<len(raw):
            n=os.write(fd,raw[offset:])
            if n<=0: raise ValueError('credential write failed')
            offset+=n
        os.fsync(fd)
    finally: os.close(fd)
    os.fsync(directory)


def replace_bearer(directory,role,leaf,raw,uid,previous):
    if role!='gateway' or leaf!='session.bearer': raise ValueError('only gateway bearer rotation allowed')
    old=os.open(leaf,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=directory)
    try:
        m=os.fstat(old)
        if not stat.S_ISREG(m.st_mode) or (m.st_uid,m.st_gid,stat.S_IMODE(m.st_mode),m.st_nlink)!=(uid,uid,0o600,1) or not 0<m.st_size<=LIMIT:
            raise ValueError('old bearer metadata mismatch')
        pin(os.read(old,LIMIT+1),previous)
        pending=leaf+'.pending-'+uuid.uuid4().hex
        exclusive(directory,pending,raw,uid,uid)
        current=os.stat(leaf,dir_fd=directory,follow_symlinks=False)
        if (m.st_ino,m.st_dev,m.st_size,m.st_mtime_ns,m.st_ctime_ns)!=(current.st_ino,current.st_dev,current.st_size,current.st_mtime_ns,current.st_ctime_ns):
            raise ValueError('old bearer changed')
        os.rename(pending,leaf,src_dir_fd=directory,dst_dir_fd=directory);os.fsync(directory)
    finally: os.close(old)


def stage(role,expected,stream):
    if sys.platform!='linux' or os.getuid()!=1002 or os.geteuid()!=1002 or role not in ROLES:
        raise ValueError('stage requires fixed NUC owner user')
    parent=Path('/run/user/1002');tmpfs(parent)
    fd=FD._open_components(str(parent),1002)
    try:
        meta=os.fstat(fd)
        if not stat.S_ISDIR(meta.st_mode) or meta.st_uid!=1002 or stat.S_IMODE(meta.st_mode)!=0o700: raise ValueError('private owner runtime directory required')
        raw=stream.read(LIMIT+1)
        if not 0<len(raw)<=LIMIT: raise ValueError('bounded input required')
        pin(raw,expected)
        name='velocita-credential-'+role+'-'+uuid.uuid4().hex
        exclusive(fd,name,raw,1002,os.getgid())
        return {'staged_path':str(parent/name),'role':role,'sha256':expected,'installed':False}
    finally: os.close(fd)


def source_path(path,role):
    if role not in ROLES or re.fullmatch('/run/user/1002/velocita-credential-'+role+'-[0-9a-f]{32}',str(path)) is None:
        raise ValueError('fixed owner tmpfs staging path required')


def install(role,path,profile_pin,previous=None):
    PROVISION.require_host_root();source_path(path,role);tmpfs(path.parent)
    for name in ROLES:
        state=subprocess.run(['/usr/bin/systemctl','show','velocita-swap-'+name+'.service','-p','ActiveState','--value'],capture_output=True,text=True,check=True,timeout=10)
        if state.stdout.strip()!='inactive': raise ValueError('both services must be inactive')
    raw_profile=LAUNCH.pinned_bytes('/etc/velocita-swap/public/runtime.profile',profile_pin,8192)
    profile=LAUNCH.fields(raw_profile);raw=private_read(path,1002)
    validate_secret(role,raw,profile,time.time_ns()//1000000)
    uid,leaf=ROLES[role];destination='/run/velocita-swap-credentials/'+role
    tmpfs(destination);fd=FD._open_components(destination,uid)
    try:
        m=os.fstat(fd)
        if not stat.S_ISDIR(m.st_mode) or (m.st_uid,m.st_gid,stat.S_IMODE(m.st_mode))!=(uid,uid,0o700):
            raise ValueError('protected role directory mismatch')
        if previous is None: exclusive(fd,leaf,raw,uid,uid)
        else: replace_bearer(fd,role,leaf,raw,uid,previous)
    finally: os.close(fd)
    return {'installed':True,'role':role,'source_retained':True,'services_started':False,'live_admitted':False}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('stage','install'))
    p.add_argument('--role',choices=tuple(ROLES),required=True);p.add_argument('--sha256');p.add_argument('--source',type=Path);p.add_argument('--profile-sha256');p.add_argument('--previous-sha256')
    a=p.parse_args();harden()
    if a.mode=='stage':
        if a.source or a.profile_sha256 or a.previous_sha256 or not a.sha256: raise ValueError('stage arguments refused')
        result=stage(a.role,a.sha256,sys.stdin.buffer)
    else:
        if a.sha256 or not a.source or not a.profile_sha256: raise ValueError('install arguments refused')
        result=install(a.role,a.source,a.profile_sha256,a.previous_sha256)
    import json
    print(json.dumps(result,sort_keys=True))


if __name__=='__main__':
    try: main()
    except Exception:
        print('REFUSED: credential handoff failed; preserve partial state; no automatic retry',file=sys.stderr)
        raise SystemExit(64)
