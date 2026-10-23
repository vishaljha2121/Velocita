"""Owner-only first start and seal for the installed, funded, halted aae119b9 run.

No mode change, driver start, funding, quote, order, retry or journal replacement.
The gateway performs its own authenticated Raft/account/balance admission.
Persistent manual-start guards are restored on both success and failure.
"""
import argparse
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import stat
import time
import urllib.request

HERE=Path(__file__).resolve().parent
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
F=load('first_start_upgrade',HERE/'upgrade_first_start.py')
V=load('first_start_verify',HERE.parent/'verify_installed.py')
O=load('first_start_observer',F.ROOT/'deploy/v2/observability/live_observer.py')
U,T,H=F.U,F.T,F.H
MANIFEST='bf8c44952fef8c6f5e2e01b63e1e65ebad17e0dc780802e3ec126f3ba61a797f'
SEALED='f3df32529bc2e3bfd08cb4a82ddecd8fe9b128160b2417ecb7850252cd0668c4'
SEAL_PIN='0ddf2b8191ba2a19337719554707d13ef7dba9a9181e3461d1c7214f690d0eab'
ARCHIVE=Path('/var/lib/velocita-first-start-upgrade-1f8d7b6')
ATTEMPT=ARCHIVE/'first-start'
VOTERS=('100.72.127.102','100.84.151.116','100.85.36.22')

def now_ms():return time.time_ns()//1_000_000

def health_row(raw):
    def metric(name,label=''):
        rows=[line for line in raw.splitlines() if line.startswith('velocita_v2_'+name+'{') and (not label or label in line)]
        if len(rows)!=1:raise ValueError('missing or ambiguous voter health '+name)
        value=rows[0].rsplit(' ',1)[-1]
        if not re.fullmatch('[0-9]+',value):raise ValueError('invalid voter health value')
        return int(value)
    row={name:metric(name) for name in ('up','ready','failed','strategy_mode','term','leader_id')}
    row.update(commit=metric('log_index','kind="commit"'),applied=metric('log_index','kind="applied"'))
    if any(row[k]!=v for k,v in dict(up=1,ready=1,failed=0,strategy_mode=5,commit=7,applied=7).items()):
        raise ValueError('expected healthy halted funding index 7')
    if row['term']<1 or row['leader_id'] not in (1,2,3):raise ValueError('leader or term absent')
    return row

def check_health():
    rows=[]
    for ip in VOTERS:
        with urllib.request.urlopen('http://'+ip+':11410/metrics',timeout=5) as response:
            raw=response.read(1048577)
        if len(raw)>1048576:raise ValueError('oversized health response')
        rows.append(health_row(raw.decode('ascii')))
    if len({(r['term'],r['leader_id']) for r in rows})!=1:raise ValueError('voters disagree')
    return rows

def driver_stopped():
    args=['/usr/sbin/runuser','-u','velocitas','--','/usr/bin/env',
        'XDG_RUNTIME_DIR=/run/user/1003','DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1003/bus',
        '/usr/bin/systemctl','--user','show','velocita-strategy-driver.service','-p','ActiveState','-p','MainPID']
    values=dict(line.split('=',1) for line in U.run(args).splitlines())
    if values!={'ActiveState':'inactive','MainPID':'0'}:raise ValueError('strategy driver must remain stopped')

def preflight():
    U.require_host();H.harden();U.stopped();U.verify_guards();driver_stopped()
    if ATTEMPT.exists():raise ValueError('prior startup attempt requires inspection; do not retry')
    mode,old,new,before,after=T.bundle(ARCHIVE/'seal',SEAL_PIN)
    if mode!='seal' or U.digest(T.P.manifest_bytes(old))!=MANIFEST or U.digest(T.P.manifest_bytes(new))!=SEALED:
        raise ValueError('exact installed seal proposal required')
    if old['source_commit']!=F.COMMIT or old['incarnation']!=F.INCARNATION:raise ValueError('release identity differs')
    if U.read(U.PUBLIC/'manifest.json',True)!=T.P.manifest_bytes(old):raise ValueError('installed manifest differs')
    for name,raw in before.items():
        if U.read(U.PUBLIC/name,True)!=raw:raise ValueError('installed review differs')
    for name,pin in U.PINS.items():
        if U.digest(U.read(U.RELEASE/name,True))!=pin:raise ValueError('installed executable/helper differs')
    for unit in U.UNITS:
        if U.read(U.SYSTEMD/unit,True)!=T.P.render(old)[unit+'.review'].encode():raise ValueError('installed unit differs')
        if U.read(U.SYSTEMD/(unit+'.d')/U.GUARD_NAME,True)!=U.GUARD:raise ValueError('start guard differs')
    profile=H.LAUNCH.fields(before['runtime.profile'])
    F.credential_snapshot()
    for role,(uid,leaf) in H.ROLES.items():
        parent=F.CREDENTIALS/role
        V.verify_caller_key(H,parent/'caller.key',role)
        H.validate_secret(role,H.private_read(parent/leaf,uid),profile,now_ms())
    with F.locked_empty_recovery():pass
    review=H.LAUNCH.fields(before['account.review'])
    if int(review['expires_at_ms'])-now_ms()<600000:raise ValueError('at least ten minutes of reviewed lifetime required')
    return dict(manifest_sha256=MANIFEST,expires_at_ms=int(review['expires_at_ms']),
        voter_health=check_health(),health_is_authenticated_raft_proof=False)

def begin_attempt(report):
    ATTEMPT.mkdir(mode=0o700)
    fd=U.directory(ARCHIVE);os.fsync(fd);os.close(fd)
    U.fresh_write(ATTEMPT/'preflight.json',(json.dumps(report,indent=2)+'\n').encode(),0o400)

def move_guard(source,destination):
    if os.path.lexists(destination):raise ValueError('guard destination already exists')
    if U.read(source,True)!=U.GUARD:raise ValueError('guard bytes differ')
    os.rename(source,destination)
    for parent in (source.parent,destination.parent):
        fd=U.directory(parent);os.fsync(fd);os.close(fd)

def release_guards():
    for unit in U.UNITS:move_guard(U.SYSTEMD/(unit+'.d')/U.GUARD_NAME,ATTEMPT/(unit+'.guard'))
    U.run(['systemctl','daemon-reload'])
    for unit in U.UNITS:
        if U.run(['systemctl','show',unit,'-p','RefuseManualStart','--value']).strip()!='no':
            raise ValueError('another guard still prevents first start')

def restore_guards():
    for unit in U.UNITS:
        active=U.SYSTEMD/(unit+'.d')/U.GUARD_NAME;saved=ATTEMPT/(unit+'.guard')
        if saved.exists():move_guard(saved,active)
        elif U.read(active,True)!=U.GUARD:raise ValueError('persistent start guard absent')
    U.run(['systemctl','daemon-reload']);U.verify_guards()

def service_state(role):
    return dict(line.split('=',1) for line in U.run(['systemctl','show','velocita-swap-'+role+'.service',
        '-p','ActiveState','-p','SubState','-p','MainPID']).splitlines())

def start_service(role):
    check_health();driver_stopped()
    U.run(['systemctl','start','velocita-swap-'+role+'.service'])
    # A single start, not a restart/retry. Allow startup to reach admission.
    time.sleep(5 if role=='signer' else 40)
    state=service_state(role)
    if state['ActiveState']!='active' or state['SubState']!='running' or int(state['MainPID'])<=0:
        raise ValueError(role+' did not remain running; preserve partial journals')

def check_running():
    for role in H.ROLES:
        state=service_state(role)
        if state['ActiveState']!='active' or state['SubState']!='running' or int(state['MainPID'])<=0:
            raise ValueError('both trading services must remain running')
    driver_stopped();check_health()

def check_observation(value,now):
    O.settlements(value,F.INCARNATION,now)
    if not 0<=now-value['observed_at_ms']<=12000:raise ValueError('fresh authenticated producer observation required')
    for key,expected in dict(applied_index=7,base_atoms=0,quote_atoms=25000000,
                            fees_atoms=0,realized_pnl_atoms=0,last_effect=0,pending_effect=0,settlements=[]).items():
        if value[key]!=expected:raise ValueError('initial funded state differs: '+key)

def verify_initialized():
    mode,old,new,files,_=T.bundle(ARCHIVE/'seal',SEAL_PIN)
    T.state(mode,old,files)
    journals={}
    for name,(role,leaf,size,magic) in T.JOURNALS.items():
        uid=U.ROLE_UIDS[role];folder=U.STATE_ROOTS[role]/'recovery'/name
        H.LAUNCH.directory(str(folder),uid,uid,0o700)
        if {p.name for p in folder.iterdir()}!={leaf}:raise ValueError('unexpected recovery files')
        fd=H.FD._open_components(str(folder/leaf),uid)
        try:
            meta=os.fstat(fd)
            if not stat.S_ISREG(meta.st_mode) or (meta.st_uid,meta.st_gid,stat.S_IMODE(meta.st_mode),meta.st_nlink,meta.st_size)!=(uid,uid,0o600,1,size):
                raise ValueError('journal metadata differs')
            raw=os.read(fd,size+1);after=os.fstat(fd)
            if (meta.st_size,meta.st_mtime_ns,meta.st_ctime_ns)!=(after.st_size,after.st_mtime_ns,after.st_ctime_ns):
                raise ValueError('journal changed during read')
        finally:os.close(fd)
        if len(raw)!=size or raw[:8]!=magic or struct.unpack_from('<Q',raw,8)[0]!=1:
            raise ValueError('only first-use pristine journals may seal')
        journals[name]=raw
    T.validate_idle_journals(old,files,journals)
    value=json.loads(O.read(O.SETTLEMENT_SOURCE),object_pairs_hook=O.unique_object)
    check_observation(value,now_ms())
    return dict(initialized_journals=sorted(journals),authenticated_producer_observation=value)

def seal():return T.apply(ARCHIVE/'seal',SEAL_PIN)

def stop_services():
    # Failure containment, not retry or rollback. Journals and request state remain untouched.
    failures=[]
    for role in ('gateway','signer'):
        try:
            subprocess.run(['systemctl','stop','velocita-swap-'+role+'.service'],check=True,timeout=30,
                           stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        except (OSError,subprocess.SubprocessError):failures.append(role)
    if failures:raise RuntimeError('owner must inspect failed stop: '+','.join(failures))

def publish_result(value):
    U.fresh_write(ATTEMPT/'result.json',(json.dumps(value,indent=2)+'\n').encode(),0o400)

def start_and_seal():
    before=preflight();begin_attempt(before)
    try:
        release_guards();start_service('signer');start_service('gateway')
        restore_guards();check_running();initialized=verify_initialized()
        transition=seal();check_running()
        result=dict(preflight=before,initialization=initialized,seal=transition,
            services_started=True,start_guards_retained=True,trading_mode='halted',driver_started=False)
        publish_result(result)
        return result
    except BaseException:
        try:restore_guards()
        finally:stop_services()
        raise

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',action='store_true');args=parser.parse_args()
    U.require_host();H.harden()
    lock=U.directory(U.PUBLIC)
    try:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        result=start_and_seal() if args.run else preflight()
        print(json.dumps(result,sort_keys=True))
    finally:os.close(lock)

if __name__=='__main__':
    try:main()
    except Exception as error:
        print(json.dumps(dict(error_type=type(error).__name__,stop_and_inspect=True,do_not_retry=True)))
        raise SystemExit(1)
