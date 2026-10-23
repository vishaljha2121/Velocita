"""Install only the reviewed observer and dashboard; never control trading services."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import subprocess

ROOT=Path(__file__).resolve().parents[4]
OBSERVER=Path('/opt/velocita/live-observer/live_observer.py')
DASHBOARD=Path('/home/velocitas/read-b79e65be-86f4408/observability/velocita-v2-live.json')
OLD_OBSERVER='4d180ef32ff1a59c0677cbc7fdca53b03a15114fc739f8c58749aa672229b58f'
NEW_OBSERVER='bf5dca9f97425e90293735cd943b73b62d2415336fd65287cfa116c2069453b0'
OLD_DASHBOARD='054bc02e411f0035da9f047e32ea09e4254ee035f008ee96ff853ca5a8c9db86'
NEW_DASHBOARD='a707dd033cc441865d829f3bacc4dbaaf367c23df94b0bc84ef4cae149428e53'
BACKUP=Path('/var/lib/velocita-live-observer/dashboard-update-backups')/NEW_DASHBOARD[:16]

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def services():
    user=pwd.getpwnam('velocitas')
    result={}
    for role in ('gateway','signer','driver'):
        command=['systemctl','show','velocita-swap-'+role+'.service','-p','MainPID','-p','ActiveState','-p','ActiveEnterTimestampMonotonic']
        if role=='driver':
            command=['runuser','-u','velocitas','--','env','XDG_RUNTIME_DIR=/run/user/'+str(user.pw_uid),'systemctl','--user','show','velocita-strategy-driver.service','-p','MainPID','-p','ActiveState','-p','ActiveEnterTimestampMonotonic']
        result[role]=subprocess.check_output(command,text=True,timeout=10)
    return result

def install(run):
    sources=[(ROOT/'deploy/v2/observability/live_observer.py',OBSERVER,OLD_OBSERVER,NEW_OBSERVER),
             (Path(__file__).with_name('after.json'),DASHBOARD,OLD_DASHBOARD,NEW_DASHBOARD)]
    prepared=[]
    for source,target,old,new in sources:
        if source.is_symlink() or target.is_symlink() or not source.is_file() or not target.is_file():
            raise ValueError('regular files required')
        raw=source.read_bytes();prior=target.read_bytes();info=target.stat()
        if digest(raw)!=new or digest(prior)!=old or info.st_nlink!=1:
            raise ValueError('file hash or metadata changed; stop without retry')
        prepared.append((target,raw,prior,info))
    before=services()
    if not run:return dict(applied=False,services=before)
    BACKUP.mkdir(parents=True,mode=0o700,exist_ok=False)
    for target,raw,prior,info in prepared:
        archive=BACKUP/target.name
        with archive.open('xb') as stream:
            stream.write(prior);stream.flush();os.fsync(stream.fileno())
        temporary=target.with_name(target.name+'.dashboard-update-'+NEW_DASHBOARD[:16])
        with temporary.open('xb') as stream:
            stream.write(raw);stream.flush()
            os.fchown(stream.fileno(),info.st_uid,info.st_gid)
            os.fchmod(stream.fileno(),info.st_mode & 0o777)
            os.fsync(stream.fileno())
        if digest(target.read_bytes())!=digest(prior):raise ValueError('target changed; stop without retry')
        os.replace(temporary,target)
        fd=os.open(target.parent,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(fd)
        finally:os.close(fd)
    after=services()
    result=dict(applied=True,services_before=before,services_after=after,services_unchanged=before==after,
                observer_sha256=digest(OBSERVER.read_bytes()),dashboard_sha256=digest(DASHBOARD.read_bytes()),
                backup=str(BACKUP),driver_updated=False,restart_commands_sent=False)
    (BACKUP/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',action='store_true')
    args=parser.parse_args()
    if os.geteuid()!=0:raise SystemExit('Owner-run sudo required; no services will be restarted.')
    print(json.dumps(install(args.run),indent=2))
