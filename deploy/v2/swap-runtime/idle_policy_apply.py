#!/usr/bin/env python3
"""Owner-only idle renewal or pinned route transition; preserves start inhibition.

No secrets are read here; authenticated reads execute the pinned native probe under
its existing protected service identities. No saved JSON can substitute for those
reads. Recovery is explicit and only rolls forward, never resets journal ordinals.
"""
import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import stat
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parent))
import idle_policy_renewal as N
import authenticated_read_run as Q
R=N.R
SYSTEM_UNITS=R.A.UNITS
USER_UNITS=tuple('velocita-paper-'+name+'-'+role+'.service' for name in ('stochastic','sma') for role in ('edge','gateway'))+('velocita-v2-shadow-edge.service','velocita-v2-gateway.service')
# /etc is traversable by the unprivileged user manager; the private public/
# directory is not. A negative condition must never hide permission errors.
SENTINEL=Path('/etc/velocita-idle-renewal-active')
DROPIN='90-idle-renewal.conf'
USER_COMMAND=['/usr/sbin/runuser','-u','velocitas','--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1003','DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1003/bus','/usr/bin/systemctl','--user']


def read(path):
    return R.I.read(Path(path))


def private_new(path,raw,mode=0o600):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,mode)
    try:
        os.fchmod(fd,mode)
        with os.fdopen(fd,'wb',closefd=False) as stream:stream.write(raw);stream.flush();os.fsync(fd)
    finally:os.close(fd)
    sync(path.parent)


def sync(path):
    fd=os.open(path,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:os.fsync(fd)
    finally:os.close(fd)


def replace(path,raw):
    # Unique pending files preserve a failed attempt without touching native store
    # filenames. fsync replacement inode and parent before progressing.
    meta=path.lstat()
    if not stat.S_ISREG(meta.st_mode) or meta.st_nlink!=1:raise ValueError('replacement target must be single-link regular file')
    pending=path.with_name(path.name+'.idle-renewal.pending')
    if pending.exists():
        if read(pending)!=raw:raise ValueError('pending replacement differs')
    else:private_new(pending,raw,stat.S_IMODE(meta.st_mode))
    fd=os.open(pending,os.O_RDONLY|os.O_NOFOLLOW)
    try:
        if (meta.st_uid,meta.st_gid)!=(os.geteuid(),os.getegid()):os.fchown(fd,meta.st_uid,meta.st_gid)
        os.fsync(fd)
    finally:os.close(fd)
    os.replace(pending,path);sync(path.parent)


@contextmanager
def directory_locks(paths,uid=None):
    """flock the directory inode itself, matching all three native store types."""
    descriptors=[]
    try:
        for path in paths:
            # Component traversal must not follow a symlink even in an ancestor.
            fd=os.open('/',os.O_RDONLY|os.O_DIRECTORY)
            try:
                for part in Path(path).absolute().parts[1:]:
                    child=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd);os.close(fd);fd=child
                info=os.fstat(fd)
                if uid is not None and info.st_uid!=uid:raise ValueError('directory owner differs')
                fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
                descriptors.append(fd);fd=-1
            finally:
                if fd>=0:os.close(fd)
        yield descriptors
        for path,fd in zip(paths,descriptors):
            current=Path(path).lstat();original=os.fstat(fd)
            if (current.st_dev,current.st_ino)!=(original.st_dev,original.st_ino):raise ValueError('locked directory replaced')
    finally:
        for fd in reversed(descriptors):os.close(fd)


def exact_state(rows,report):
    if len(rows)!=2 or {row['role'] for row in rows}!={'signer','gateway'}:raise ValueError('both protected role reads required')
    for row in rows:
        if row['state_digest']!=report['original_state_digest'] or type(row['applied_index']) is not int or row['applied_index']<report['original_journal_index']:
            raise ValueError('committed state changed; reconciliation required')


def read_live(manifest,files,helper_sha256,output):
    profile=R.L.fields(files['runtime.profile'])
    config=Q.pins(helper_sha256,manifest['catalog_sha256'],manifest['incarnation'],profile['catalog'],profile['authority_domain'])
    # Q.run validates root-owned native binary/catalog and directly captures fresh
    # child results; it also cleans up timed-out transient units. Never load result.json.
    result=Q.run(output,config)
    if result.get('authenticated_voter_reads') is not True:raise ValueError('fresh protected reads failed')
    return result['results']


def publish(replacements,archive,recover):
    """Durable before/after archive, strict preflight, explicit idempotent roll-forward."""
    paths=list(replacements)
    record={'schema':1,'files':[{ 'path':str(path),'before':R.digest(pair[0]),'after':R.digest(pair[1])} for path,pair in replacements.items()]}
    raw=(json.dumps(record,sort_keys=True)+'\n').encode()
    for path,(old,new) in replacements.items():
        current=read(path)
        if current not in ((old,new) if recover else (old,)):raise ValueError('unexpected installed bytes')
    if recover:
        if not archive.is_dir() or archive.is_symlink():raise ValueError('recovery archive absent')
    else:
        if archive.exists():raise ValueError('explicit recovery required')
        archive.mkdir(mode=0o700);sync(archive.parent)
    expected={'plan.json':raw}
    for index,path in enumerate(paths):
        expected.update({prefix+'-'+str(index):value for prefix,value in zip(('before','after'),replacements[path])})
    ready=archive/'ready'
    if not ready.exists() and any(read(path)!=old for path,(old,_) in replacements.items()):raise ValueError('incomplete archive with modified destination')
    if {p.name for p in archive.iterdir()}-set(expected)-{'ready','complete'}:raise ValueError('unexpected archive file')
    for name,value in expected.items():
        target=archive/name
        if target.exists():
            if read(target)!=value:raise ValueError('archive bytes changed')
        elif ready.exists():raise ValueError('ready archive is incomplete')
        else:private_new(target,value,0o400)
    marker=R.digest(raw).encode()+b'\n'
    if ready.exists():
        if read(ready)!=marker:raise ValueError('archive ready marker differs')
    else:private_new(ready,marker,0o400)
    for path,(_,new) in replacements.items():
        if read(path)!=new:replace(path,new)
    if not (archive/'complete').exists():private_new(archive/'complete',R.digest(raw).encode()+b'\n',0o400)
    elif read(archive/'complete')!=R.digest(raw).encode()+b'\n':raise ValueError('completion marker differs')


def checked_publish(manifest,files,helper_sha256,output,proposal,replacements,archive,recover):
    # Invoked only inside the host's inhibitor and all five directory locks.
    # The native reader call is mandatory even for an explicit recovery.
    exact_state(read_live(manifest,files,helper_sha256,output),proposal['report'])
    publish(replacements,archive,recover)


def command(args):return R.I.command(args)


def condition_verified(raw):
    value=R.L.strict_json(raw.encode())
    if set(value)!={'type','data'} or value['type']!='a(sbbsi)' or not isinstance(value['data'],list):raise ValueError('systemd condition schema differs')
    expected=['ConditionPathExists',False,True,str(SENTINEL)]
    if not any(isinstance(row,list) and len(row)==5 and row[:4]==expected and type(row[1]) is bool and type(row[2]) is bool for row in value['data']):raise ValueError('effective start inhibition is absent')


def check_condition(unit,user):
    # systemctl v249 prints compound Conditions as [unprintable]; query its typed
    # D-Bus property instead, verifying negation and non-trigger (AND) semantics.
    object_path='/org/freedesktop/systemd1/unit/'+''.join(c if c.isalnum() else '_'+format(ord(c),'02x') for c in unit)
    prefix=(USER_COMMAND[:-2]+['/usr/bin/busctl','--user']) if user else ['/usr/bin/busctl']
    condition_verified(command(prefix+['--json=short','get-property','org.freedesktop.systemd1',object_path,'org.freedesktop.systemd1.Unit','Conditions']))


def inhibit(pin):
    for parent in (SENTINEL.parent,*SENTINEL.parent.parents):
        meta=parent.lstat()
        if not stat.S_ISDIR(meta.st_mode) or not meta.st_mode & stat.S_IXOTH:raise ValueError('inhibition sentinel must be globally stat-able')
    marker=(pin+'\n').encode()
    if SENTINEL.exists():
        previous=read(SENTINEL)
        if previous!=marker:
            prior=previous.decode('ascii').strip();Q.token(prior,64)
            archive=Path(R.P.PUBLIC)/('idle-renewal-archive-'+prior)
            plan=read(archive/'plan.json')
            if read(archive/'complete')!=R.digest(plan).encode()+b'\n':raise ValueError('another unfinished renewal inhibition exists')
            record=R.L.strict_json(plan)
            allowed={str(Path(R.P.PUBLIC)/name) for name in ('manifest.json',*R.A.DOCUMENTS)}|{'/etc/systemd/system/'+unit for unit in SYSTEM_UNITS}|{'/var/lib/velocita-swap-signer/recovery/protected-signing/submission.bin'}
            if set(record)!={'schema','files'} or record['schema']!=1 or len(record['files'])!=len(allowed) or {row['path'] for row in record['files']}!=allowed:raise ValueError('prior archive target set differs')
            if any(R.digest(read(row['path']))!=row['after'] for row in record['files']):raise ValueError('prior completed generation is no longer installed')
            replace(SENTINEL,marker)
    else:private_new(SENTINEL,marker,0o444)
    content=('[Unit]\nConditionPathExists=!'+str(SENTINEL)+'\n').encode()
    for prefix,units,root in ((['/usr/bin/systemctl'],SYSTEM_UNITS,Path('/etc/systemd/system')),(USER_COMMAND,USER_UNITS,Path('/etc/systemd/user'))):
        for unit in units:
            directory=root/(unit+'.d')
            if not directory.exists():R.I.mkdir(str(directory),mode=0o755)
            check_fd=R.I.PROVISION.open_directory(str(directory));os.close(check_fd)
            dest=directory/DROPIN
            if dest.exists():
                if read(dest)!=content:raise ValueError('inhibition drop-in differs')
            else:private_new(dest,content,0o444)
        command(prefix+['daemon-reload'])
        # Stop processes, but refuse a retained systemd failed state. The owner
        # must preserve its evidence and explicitly clear that flag separately.
        # Never reset recovery data or start/enable; inhibition survives loss.
        command(prefix+['stop',*units])
        for unit in units:
            if command(prefix+['show',unit,'--property=ActiveState','--value'])!='inactive':raise ValueError('producer remains active')
            state=command(prefix+['show',unit,'--property=LoadState','--value'])
            if state!='masked':
                if str(root/(unit+'.d')/DROPIN) not in command(prefix+['show',unit,'--property=DropInPaths','--value']):raise ValueError('start inhibition was not loaded')
                check_condition(unit,prefix==USER_COMMAND)


def bundle(base,pin):
    raw=read(base/'renewal.json')
    if R.digest(raw)!=pin:raise ValueError('renewal bundle pin mismatch')
    spec=R.L.strict_json(raw)
    if set(spec)!={'schema','evaluated_at_ms','helper_sha256','files'} or type(spec['schema']) is not int or spec['schema']!=1:raise ValueError('exact renewal bundle required')
    names={p+'/'+n for p in ('before','after') for n in ('manifest.json',*R.A.DOCUMENTS)}|{p+'/journals/'+n for p in ('before','after') for n in R.JOURNALS}
    if 'session-refresh.json' in spec['files']:names.add('session-refresh.json')
    if 'route-review.json' in spec['files']:names.add('route-review.json')
    if 'release-review.json' in spec['files']:names.add('release-review.json')
    if set(spec['files'])!=names:raise ValueError('exact artifact set required')
    artifacts={n:read(base/n) for n in names}
    if any(R.digest(raw)!=spec['files'][n] for n,raw in artifacts.items()):raise ValueError('artifact pin differs')
    old,new=(R.L.strict_json(artifacts[p+'/manifest.json']) for p in ('before','after'))
    before,after=({n:artifacts[p+'/'+n] for n in R.A.DOCUMENTS} for p in ('before','after'))
    journals={n:artifacts['before/journals/'+n] for n in R.JOURNALS}
    witness=R.L.strict_json(artifacts['session-refresh.json']) if 'session-refresh.json' in artifacts else None
    route=R.L.strict_json(artifacts['route-review.json']) if 'route-review.json' in artifacts else None
    release=R.L.strict_json(artifacts['release-review.json']) if 'release-review.json' in artifacts else None
    proposal=N.propose(old,new,before,after,journals,spec['evaluated_at_ms'],session_refresh=witness,route_review=route,release_review=release)
    if proposal['artifacts']!=artifacts:raise ValueError('derived renewal differs from bundle')
    Q.token(spec['helper_sha256'],64)
    return spec,old,new,before,after,proposal


def apply(base,pin,recover=False):
    R.I.PROVISION.require_host_root();R.H.harden()
    spec,old,new,before,after,proposal=bundle(base,pin)
    now=time.time_ns()//1000000
    witness=R.L.strict_json(proposal['artifacts']['session-refresh.json']) if 'session-refresh.json' in proposal['artifacts'] else None
    route=R.L.strict_json(proposal['artifacts']['route-review.json']) if 'route-review.json' in proposal['artifacts'] else None
    release=R.L.strict_json(proposal['artifacts']['release-review.json']) if 'release-review.json' in proposal['artifacts'] else None
    if not recover:N.propose(old,new,before,after,{n:proposal['artifacts']['before/journals/'+n] for n in R.JOURNALS},now,session_refresh=witness,route_review=route,release_review=release)
    public=Path(R.P.PUBLIC)
    # One owner transition at a time, separate from the native journal locks.
    with directory_locks([public],0):
        inhibit(pin)
        paths={name:Path('/var/lib/velocita-swap-'+role+'/recovery')/name/leaf for name,(role,leaf,_,_) in R.JOURNALS.items()}
        for name,path in paths.items():
            uid=29002 if R.JOURNALS[name][0]=='signer' else 29001
            R.L.directory(str(path.parent),uid,uid,0o700)
            meta=path.lstat()
            if not stat.S_ISREG(meta.st_mode) or meta.st_uid!=uid or meta.st_gid!=uid or stat.S_IMODE(meta.st_mode)!=0o600 or meta.st_nlink!=1:raise ValueError('journal metadata differs')
        with directory_locks([p.parent for p in paths.values()]):
            replacements={paths['protected-signing']:(proposal['artifacts']['before/journals/protected-signing'],proposal['artifacts']['after/journals/protected-signing'])}
            # Four gateway journals must remain byte-identical, even in recovery.
            for name,path in paths.items():
                if name!='protected-signing' and read(path)!=proposal['artifacts']['before/journals/'+name]:raise ValueError('gateway journal changed')
            replacements.update({public/n:(before[n],after[n]) for n in R.A.DOCUMENTS})
            replacements[public/'manifest.json']=(R.P.manifest_bytes(old),R.P.manifest_bytes(new))
            for unit in SYSTEM_UNITS:replacements[Path('/etc/systemd/system')/unit]=(R.P.render(old)[unit+'.review'].encode(),R.P.render(new)[unit+'.review'].encode())
            # All file/role/binary identity validation precedes a read and write.
            for path,(previous,following) in replacements.items():
                if read(path) not in ((previous,following) if recover else (previous,)):raise ValueError('installed generation differs')
            R.L.pinned_bytes(new['binary'],new['binary_sha256'],64*1024**2)
            if release is not None:
                for name,expected in release['files'].items():R.L.pinned_bytes(str(Path(new['binary']).parent/name),expected,64*1024**2)
            proof_dir=public/('idle-renewal-read-'+pin+'-'+os.urandom(8).hex())
            checked_publish(old,before,spec['helper_sha256'],proof_dir,proposal,replacements,public/('idle-renewal-archive-'+pin),recover)
            command(['/usr/bin/systemctl','daemon-reload'])
            for unit in SYSTEM_UNITS:
                if command(['/usr/bin/systemctl','show',unit,'--property=ActiveState','--value'])!='inactive':raise ValueError('service unexpectedly active')
    return dict(renewed=True,services_inhibited=True,services_started=False,orders_submitted=False,live_admitted=False,manifest_sha256=R.digest(R.P.manifest_bytes(new)))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--sha256',required=True)
    mode=p.add_mutually_exclusive_group();mode.add_argument('--apply',action='store_true');mode.add_argument('--recover',action='store_true');a=p.parse_args()
    result=apply(a.bundle,a.sha256,a.recover) if a.apply or a.recover else dict(validated=bool(bundle(a.bundle,a.sha256)),applied=False)
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':
    try:main()
    except Exception as error:
        print('REFUSED '+type(error).__name__+': '+str(error)+'; preserve inhibition and archives; no automatic retry',file=sys.stderr);raise SystemExit(64)
