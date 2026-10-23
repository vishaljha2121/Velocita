#!/usr/bin/env python3
"""Stage the pinned Byreal executable; do not activate or change runtime identity.

Requires both prior exact, loaded start guards and preserves them. Derived from
upgrade_telemetry.py with exact artifact pins and guard reuse. The locked route
transition must separately adopt this staged release into manifest and units.
No account/transaction/profile/journal or
credential bytes may change. Owner explicitly removes guards in a later ceremony.
"""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import types

COMMIT='fe54001ce7bbd36c25947cc29ecf70833d161e37'
BINARY='e7fb8da295b0abbb7a8decf5c761068e2a732499c1a5e16c7b3433d6b5c7a590'
IMAGE='076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f'
PINS={'velocita-v2-gateway':BINARY,
 'service_launcher.py':'f32b13405021ae09ca9c11c4d9e801d645f75433c91ef3013f0ca6d9c3ab8e14',
 'service_profile.py':'c03c0ee6e1fbea24d8c6a03bf9d4756c04155889bfd89da2fc710377be03841c',
 'fd_handoff.py':'a8ffbfc1aa25830a0a439d282898725b3ba58936a9d7b5bf68fce57f5a78712c'}
ROOT_UID=0
ROLE_UIDS={'gateway':29001,'signer':29002}
SYSTEMD=Path('/etc/systemd/system')
RUNTIME=Path('/opt/velocita/runtimes')
STATE_ROOTS={r:Path('/var/lib/velocita-swap-'+r) for r in ROLE_UIDS}
PUBLIC=Path('/etc/velocita-swap/public')
DOC_PINS={'runtime.profile':'runtime_profile_sha256','account.review':'account_review_sha256','account.history':'account_history_sha256','catalog.bin':'catalog_sha256'}
RELEASE=Path('/opt/velocita/releases')/COMMIT
UNITS=('velocita-swap-gateway.service','velocita-swap-signer.service')
GUARD_NAME='90-telemetry-upgrade-inhibit.conf'
GUARD=b'[Unit]\nRefuseManualStart=yes\n[Service]\nExecCondition=/bin/false\n'
JOURNALS={'gateway/prepared':'prepare.bin','gateway/submitted':'submission.bin',
 'gateway/signing-requests':'submission.bin','gateway/results':'result.bin',
 'signer/protected-signing':'submission.bin'}


def digest(raw):return hashlib.sha256(raw).hexdigest()
def read(path,root=False):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        s=os.fstat(fd)
        if not stat.S_ISREG(s.st_mode) or s.st_nlink!=1 or s.st_size>64*1024**2:raise ValueError('bounded regular file required')
        if root and (s.st_uid not in (0,ROOT_UID) or s.st_mode&0o022):raise ValueError('root protected file required')
        with os.fdopen(fd,'rb',closefd=False) as f:raw=f.read(64*1024**2+1)
        t=os.fstat(fd)
        if (s.st_ino,s.st_size,s.st_mtime_ns,s.st_ctime_ns)!=(t.st_ino,t.st_size,t.st_mtime_ns,t.st_ctime_ns) or len(raw)!=s.st_size:raise ValueError('file changed')
        return raw
    finally:os.close(fd)
def run(args):return subprocess.run(args,check=True,capture_output=True,text=True,stdin=subprocess.DEVNULL,timeout=30).stdout

def directory(path):
    fd=os.open('/',os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:
        for part in Path(path).parts[1:]:
            n=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd)
            os.close(fd);fd=n
            s=os.fstat(fd)
            if s.st_uid not in (0,ROOT_UID) or s.st_mode&0o022:raise ValueError('root protected directory required')
        return fd
    except BaseException:os.close(fd);raise

def fresh_write(path,raw,mode=0o444,gid=None):
    parent=directory(path.parent)
    try:
        fd=os.open(path.name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,mode,dir_fd=parent)
        try:
            if gid is not None:os.fchown(fd,ROOT_UID,gid)
            os.fchmod(fd,mode)
            with os.fdopen(fd,'wb',closefd=False) as f:f.write(raw);f.flush()
            os.fsync(fd)
        finally:os.close(fd)
        os.fsync(parent)
    finally:os.close(parent)

def stopped():
    for unit in UNITS:
        values=dict(line.split('=',1) for line in run(['systemctl','show',unit,'-p','ActiveState','-p','MainPID','-p','ControlPID','-p','UnitFileState']).splitlines())
        if values.get('ActiveState') not in ('inactive','failed') or values.get('MainPID')!='0' or values.get('ControlPID')!='0' or values.get('UnitFileState')!='static':raise ValueError('both static production services must be stopped')

def verify_guards():
    for unit in UNITS:
        guard=SYSTEMD/(unit+'.d')/GUARD_NAME
        values=dict(line.split('=',1) for line in run(['systemctl','show',unit,'-p','DropInPaths','-p','RefuseManualStart','-p','ExecCondition']).splitlines())
        if str(guard) not in values.get('DropInPaths','').split() or values.get('RefuseManualStart')!='yes' or 'path=/bin/false' not in values.get('ExecCondition',''):
            raise ValueError('systemd has not loaded both persistent start guards')
        if read(guard,True)!=GUARD:raise ValueError('start guard changed')

def payload(base):
    result={name:read(base/name) for name in PINS}
    if any(digest(result[n])!=p for n,p in PINS.items()):raise ValueError('qualified source or binary pin mismatch')
    module=types.ModuleType('qualified_service_profile')
    exec(compile(result['service_profile.py'],'qualified_service_profile.py','exec'),module.__dict__)
    return result,module

@contextlib.contextmanager
def locked_journals():
    fds=[];snapshots={}
    try:
        for key,leaf in JOURNALS.items():
            role,name=key.split('/')
            path=STATE_ROOTS[role]/'recovery'/name
            # Native stores flock their directory inode. Metadata below allows only
            # the exact role-owned private leaf, never path aliases or symlinks.
            parent=os.open('/',os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
            try:
                for part in path.parts[1:]:
                    child=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=parent)
                    os.close(parent);parent=child
                    meta=os.fstat(parent)
                    if meta.st_uid not in (0,ROOT_UID,ROLE_UIDS[role]) or meta.st_mode&0o022:raise ValueError('unsafe journal ancestor')
                fd=parent;parent=-1
            finally:
                if parent!=-1:os.close(parent)
            fds.append(fd);s=os.fstat(fd)
            if s.st_uid!=ROLE_UIDS[role] or stat.S_IMODE(s.st_mode)!=0o700:raise ValueError('journal directory identity')
            fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            snapshots[path/leaf]=read(path/leaf)
        yield snapshots
        if any(read(p)!=b for p,b in snapshots.items()):raise ValueError('journal changed during upgrade')
    finally:
        for fd in reversed(fds):os.close(fd)

def require_host():
    if os.geteuid()!=0 or socket.gethostname()!='bulbazar-NUC':raise ValueError('root on NUC1 required')

def apply(base,old_pin,archive):
    require_host()
    data,profile=payload(base)
    old_raw=read(PUBLIC/'manifest.json',True)
    if digest(old_raw)!=old_pin:raise ValueError('reviewed previous manifest mismatch')
    old=json.loads(old_raw);profile.validate(old)
    if old['source_commit']==COMMIT or old.get('initialize_swap_recovery') is not False or old.get('recovery_only',False):raise ValueError('sealed earlier release required')
    if old['runtime_image_sha256']!=IMAGE:raise ValueError('qualified runtime image required')
    fd=directory(PUBLIC);os.close(fd)
    before={PUBLIC/n:read(PUBLIC/n,True) for n in ['manifest.json',*DOC_PINS,'transaction.review']}
    for name,key in DOC_PINS.items():
        if digest(before[PUBLIC/name])!=old[key]:raise ValueError('public document pin mismatch')
    lines=[line for line in before[PUBLIC/'runtime.profile'].decode('ascii').splitlines() if line and not line.startswith('#')]
    fields=dict(line.split('=',1) for line in lines)
    if len(fields)!=len(lines) or digest(before[PUBLIC/'transaction.review'])!=fields.get('transaction_review_digest'):raise ValueError('transaction document pin mismatch')
    if digest(read(Path(old['binary']),True))!=old['binary_sha256']:raise ValueError('old binary mismatch')
    if read(RUNTIME/IMAGE/'rootfs/etc/velocita-runtime-image',True)!=(IMAGE+'\n').encode():raise ValueError('runtime marker mismatch')
    rootfs=RUNTIME/IMAGE/'rootfs';false=(rootfs/'bin/false').resolve()
    if not false.is_relative_to(rootfs.resolve()) or not stat.S_ISREG(false.stat().st_mode) or not false.stat().st_mode&0o111:raise ValueError('runtime false executable required for guard')
    old_units={u:read(SYSTEMD/u,True) for u in UNITS}
    for role in ('gateway','signer'):
        u='velocita-swap-'+role+'.service'
        if old_units[u]!=profile.render(old)[u+'.review'].encode():raise ValueError('old unit has unreviewed changes')
    stopped()
    if RELEASE.exists() or archive.exists():raise ValueError('fresh release and archive required')
    # This second upgrade may only reuse the exact guards left by the reviewed
    # first upgrade. Missing, partial, changed or unloaded guards require owner
    # reconciliation; do not create, repair, overwrite or remove any guard here.
    verify_guards();stopped()
    with locked_journals() as journals:
        if read(PUBLIC/'manifest.json',True)!=old_raw:raise ValueError('manifest changed')
        fd=directory(archive.parent);os.close(fd)
        archive.mkdir(mode=0o700);os.chmod(archive,0o700)
        fd=directory(archive.parent);os.fsync(fd);os.close(fd)
        fd=directory(archive);os.close(fd)
        fresh_write(archive/'manifest.json',old_raw,0o400)
        for u,b in old_units.items():fresh_write(archive/u,b,0o400)
        fresh_write(archive/'preserved-sha256.json',(json.dumps({str(p):digest(b) for p,b in {**before,**journals}.items()},indent=2)+'\n').encode(),0o400)
        fd=directory(RELEASE.parent);os.close(fd)
        RELEASE.mkdir(mode=0o755);os.chmod(RELEASE,0o755)
        fd=directory(RELEASE.parent);os.fsync(fd);os.close(fd)
        for name,raw in data.items():fresh_write(RELEASE/name,raw,0o555 if name=='velocita-v2-gateway' else 0o444)
        if any(read(p,True)!=b for p,b in before.items()):raise ValueError('reviewed public documents changed')
        if any(read(SYSTEMD/u,True)!=b for u,b in old_units.items()):raise ValueError('installed unit changed')
        verify_guards();stopped()
        fresh_write(archive/'result.json',(json.dumps(dict(source_commit=COMMIT,binary_sha256=BINARY,release_staged=True,manifest_adopted=False,services_started=False,start_guards_retained=True,journals_preserved=True),indent=2)+'\n').encode(),0o400)
    return {'source_commit':COMMIT,'release_staged':True,'manifest_adopted':False,'services_started':False,'start_guards_retained':True}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',type=Path,required=True);p.add_argument('--old-manifest-sha256',required=True)
    p.add_argument('--archive',type=Path,required=True);p.add_argument('--apply',action='store_true')
    a=p.parse_args()
    if a.apply:print(json.dumps(apply(a.bundle,a.old_manifest_sha256,a.archive)))
    else:payload(a.bundle);print(json.dumps({'payload_verified':True,'applied':False}))
