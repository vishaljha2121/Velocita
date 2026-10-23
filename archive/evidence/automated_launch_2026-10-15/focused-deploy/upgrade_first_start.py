"""Owner-only pinned executable replacement for the armed, never-started aae119b9 run.

Preserves credentials, documents and empty recovery roots. Retains start guards on
success or failure. Never starts, rearms, resets funding, deletes or retries.
"""
import argparse
import contextlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import time

ROOT=Path(__file__).resolve().parents[4]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
sys.path.insert(0,str(ROOT/'deploy/v2/swap-runtime'))
U=load('first_start_files',ROOT/'docs/evidence/live_fill_wiring_2026-10-13/deploy/upgrade_telemetry.py')
H=load('first_start_credentials',ROOT/'deploy/v2/swap-runtime/credential_handoff.py')
T=load('first_start_transition',ROOT/'deploy/v2/swap-runtime/review_transition.py')
COMMIT='1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc'
BINARY='8d0fab33246d85628cac6471c3926205fd5710f96a55731106324a101cc9abcf'
INCARNATION='aae119b95ddcea5a206f03ab5ca12c15'
U.COMMIT=COMMIT;U.BINARY=BINARY;U.RELEASE=Path('/opt/velocita/releases')/COMMIT
U.GUARD_NAME='90-first-start-upgrade-inhibit.conf'
U.PINS={'velocita-v2-gateway':BINARY,
    'service_launcher.py':'5f1123d9093cd75c69cfe57b1c19239e05d4420d7f787a6d383ccaa84a49fe38',
    'service_profile.py':'c03c0ee6e1fbea24d8c6a03bf9d4756c04155889bfd89da2fc710377be03841c',
    'fd_handoff.py':'a8ffbfc1aa25830a0a439d282898725b3ba58936a9d7b5bf68fce57f5a78712c'}
CREDENTIALS=Path('/run/velocita-swap-credentials')
ROLE_GIDS={'gateway':29001,'signer':29002}

def next_manifest(old,profile):
    profile.validate(old)
    if (old['incarnation']!=INCARNATION or old['launch_authorized'] is not True or
        old.get('initialize_swap_recovery') is not True or old.get('recovery_only',False) or
        old['runtime_image_sha256']!=U.IMAGE or old['source_commit']==COMMIT):
        raise ValueError('exact armed unsealed first-start installation required')
    new=dict(old,source_commit=COMMIT,binary=str(U.RELEASE/'velocita-v2-gateway'),binary_sha256=BINARY)
    profile.validate(new)
    return new

def validate_review(manifest,docs):
    files={n:docs[n] for n in T.A.DOCUMENTS}
    T.validate('seal',manifest,dict(manifest,initialize_swap_recovery=False),files,files,time.time_ns()//1_000_000)

def role_directory(path,role):
    uid=U.ROLE_UIDS[role];fd=H.FD._open_components(str(path),uid)
    meta=os.fstat(fd)
    if not stat.S_ISDIR(meta.st_mode) or (meta.st_uid,meta.st_gid,stat.S_IMODE(meta.st_mode))!=(uid,ROLE_GIDS[role],0o700):
        os.close(fd);raise ValueError('private role directory metadata differs')
    return fd

@contextlib.contextmanager
def locked_empty_recovery():
    fds=[]
    try:
        for role,base in U.STATE_ROOTS.items():
            fd=role_directory(base/'recovery',role);fds.append(fd)
            fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            if os.listdir(fd):raise ValueError('first-start recovery root must be empty')
        yield fds
        for fd in fds:
            if os.listdir(fd):raise ValueError('recovery root changed during upgrade')
    finally:
        for fd in reversed(fds):os.close(fd)

def credential_snapshot():
    result={}
    for role,uid in U.ROLE_UIDS.items():
        base=CREDENTIALS/role;H.tmpfs(base);fd=role_directory(base,role)
        try:
            names=set(os.listdir(fd));leaf=H.ROLES[role][1]
            if leaf not in names or names-{leaf,'caller.key'}:raise ValueError('expected venue credential and optional caller key only')
            for name in names:
                meta=os.stat(name,dir_fd=fd,follow_symlinks=False)
                if not stat.S_ISREG(meta.st_mode) or (meta.st_uid,meta.st_gid,stat.S_IMODE(meta.st_mode),meta.st_nlink)!=(uid,ROLE_GIDS[role],0o600,1):
                    raise ValueError('private credential metadata differs')
                if name=='caller.key' and meta.st_size!=32:raise ValueError('caller key size differs')
                result[(role,name)]=(meta.st_ino,meta.st_mode,meta.st_uid,meta.st_gid,meta.st_mtime_ns,meta.st_ctime_ns,U.read(base/name))
        finally:os.close(fd)
    return result

def seal_proposal(archive,new,docs,profile):
    path=archive/'seal';path.mkdir(mode=0o700)
    fd=U.directory(archive);os.fsync(fd);os.close(fd)
    files={'old-manifest.json':profile.manifest_bytes(new),'new-manifest.json':profile.manifest_bytes(dict(new,initialize_swap_recovery=False))}
    for name in T.A.DOCUMENTS:files['old-'+name]=files['new-'+name]=docs[name]
    for name,raw in files.items():U.fresh_write(path/name,raw,0o400)
    spec=(json.dumps(dict(schema=1,mode='seal',files={n:U.digest(b) for n,b in files.items()}),sort_keys=True)+'\n').encode()
    U.fresh_write(path/'transition.json',spec,0o400)
    return U.digest(spec)

def apply(base,old_pin,archive):
    U.require_host();H.harden();data,profile=U.payload(base)
    installer=U.directory(U.PUBLIC)
    try:
        fcntl.flock(installer,fcntl.LOCK_EX|fcntl.LOCK_NB)
        oldraw=U.read(U.PUBLIC/'manifest.json',True)
        if U.digest(oldraw)!=old_pin:raise ValueError('reviewed old manifest pin differs')
        old=json.loads(oldraw);new=next_manifest(old,profile)
        docs={n:U.read(U.PUBLIC/n,True) for n in (*U.DOC_PINS,'transaction.review')}
        for name,key in U.DOC_PINS.items():
            if U.digest(docs[name])!=old[key]:raise ValueError('installed document pin differs')
        validate_review(old,docs)
        if U.digest(U.read(Path(old['binary']),True))!=old['binary_sha256']:raise ValueError('old binary pin differs')
        rootfs=U.RUNTIME/U.IMAGE/'rootfs'
        if U.read(rootfs/'etc/velocita-runtime-image',True)!=(U.IMAGE+'\n').encode():raise ValueError('runtime image marker differs')
        false=(rootfs/'bin/false').resolve()
        if not false.is_relative_to(rootfs.resolve()) or not stat.S_ISREG(false.stat().st_mode) or not false.stat().st_mode&0o111:raise ValueError('runtime guard executable absent')
        units={u:U.read(U.SYSTEMD/u,True) for u in U.UNITS}
        if any(units[u]!=profile.render(old)[u+'.review'].encode() for u in units):raise ValueError('installed unit differs from manifest')
        U.stopped()
        if U.RELEASE.exists() or archive.exists():raise ValueError('fresh release and archive required')
        for unit in U.UNITS:
            if os.path.lexists(U.SYSTEMD/(unit+'.d')/U.GUARD_NAME):raise ValueError('existing upgrade guard requires reconciliation')
        credentials=credential_snapshot()
        with locked_empty_recovery() as recovery_fds:
            for unit in U.UNITS:
                parent=U.SYSTEMD/(unit+'.d')
                if not parent.exists():
                    parent.mkdir(mode=0o755);os.chmod(parent,0o755)
                    fd=U.directory(U.SYSTEMD);os.fsync(fd);os.close(fd)
                fd=U.directory(parent);os.close(fd);U.fresh_write(parent/U.GUARD_NAME,U.GUARD)
            U.run(['systemctl','daemon-reload']);U.verify_guards();U.stopped()
            if U.read(U.PUBLIC/'manifest.json',True)!=oldraw:raise ValueError('manifest changed before publication')
            validate_review(old,docs)
            if any(os.listdir(fd) for fd in recovery_fds):raise ValueError('recovery root changed before publication')
            if any(U.read(U.PUBLIC/n,True)!=raw for n,raw in docs.items()):raise ValueError('public document changed before publication')
            if credential_snapshot()!=credentials:raise ValueError('credentials changed before publication')
            for parent in (archive.parent,U.RELEASE.parent):fd=U.directory(parent);os.close(fd)
            archive.mkdir(mode=0o700);os.chmod(archive,0o700)
            fd=U.directory(archive.parent);os.fsync(fd);os.close(fd)
            U.fresh_write(archive/'manifest.json',oldraw,0o400)
            for unit,raw in units.items():U.fresh_write(archive/unit,raw,0o400)
            U.RELEASE.mkdir(mode=0o755);os.chmod(U.RELEASE,0o755)
            fd=U.directory(U.RELEASE.parent);os.fsync(fd);os.close(fd)
            for name,raw in data.items():U.fresh_write(U.RELEASE/name,raw,0o555 if name=='velocita-v2-gateway' else 0o444)
            for unit,raw in units.items():U.replace(U.SYSTEMD/unit,raw,profile.render(new)[unit+'.review'].encode())
            U.replace(U.PUBLIC/'manifest.json',oldraw,profile.manifest_bytes(new))
            if any(U.read(U.PUBLIC/n,True)!=raw for n,raw in docs.items()):raise ValueError('public document changed')
            if credential_snapshot()!=credentials:raise ValueError('credential changed during publication')
            U.run(['systemd-analyze','verify','--man=no',*[str(U.SYSTEMD/u) for u in units]])
            U.run(['systemctl','daemon-reload']);U.verify_guards();U.stopped()
            seal_pin=seal_proposal(archive,new,docs,profile)
            result=dict(source_commit=COMMIT,binary_sha256=BINARY,manifest_sha256=U.digest(profile.manifest_bytes(new)),seal_bundle_sha256=seal_pin,
                services_started=False,start_guards_retained=True,credentials_preserved=True,recovery_roots_empty=True,launch_rearmed=False)
            U.fresh_write(archive/'result.json',(json.dumps(result,indent=2)+'\n').encode(),0o400)
        return result
    finally:os.close(installer)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',type=Path,required=True);p.add_argument('--old-manifest-sha256',required=True)
    p.add_argument('--archive',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args()
    if a.apply:result=apply(a.bundle,a.old_manifest_sha256,a.archive)
    else:U.payload(a.bundle);result=dict(payload_verified=True,applied=False)
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':
    try:main()
    except Exception as error:
        print(json.dumps(dict(applied=False,error_type=type(error).__name__,stop_and_inspect=True,do_not_retry=True)));raise SystemExit(1)
