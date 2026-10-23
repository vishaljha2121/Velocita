#!/usr/bin/env python3
"""Install a pinned release and dormant units on NUC1; never start or enable them.

Fresh destinations only. Every supplied byte is checked before mutation. Partial
installation is preserved on error, with no automatic retry, removal or overwrite.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import service_profile as PROFILE
import provision_services as PROVISION
import install_runtime as RUNTIME

FILES = {'service_launcher.py', 'service_profile.py', 'fd_handoff.py',
         'velocita-v2-gateway', 'catalog.bin', 'manifest.json',
         'velocita-swap-gateway.service', 'velocita-swap-signer.service'}


def payload(base, expected_bundle):
    raw = read(base/'install-bundle.json')
    if hashlib.sha256(raw).hexdigest() != expected_bundle:
        raise ValueError('installation bundle digest mismatch')
    pins = json.loads(raw)
    if set(pins) != {'schema', 'files'} or pins['schema'] != 1 or set(pins['files']) != FILES:
        raise ValueError('exact installation artifact set required')
    data = {name: read(base/name) for name in FILES}
    if any(hashlib.sha256(data[name]).hexdigest() != pins['files'][name] for name in FILES):
        raise ValueError('installation artifact hash mismatch')
    m = json.loads(data['manifest.json'])
    PROFILE.validate(m)
    if m['launch_authorized'] is not False:
        raise ValueError('only dormant installation is authorized')
    if (m['gateway_uid'], m['gateway_gid'], m['signer_uid'], m['socket_gid'],
        m['gateway_caller'], m['signer_caller'], m['gateway_key_id'], m['signer_key_id'],
        m['gateway_response_port'], m['signer_response_port']) != (
            29001, 29001, 29002, 29002, 22, 23, 13, 14, 44413, 44414):
        raise ValueError('reviewed fixed identities required')
    if not data['velocita-v2-gateway'].startswith(b'\x7fELF') or \
       pins['files']['velocita-v2-gateway'] != m['binary_sha256'] or \
       pins['files']['catalog.bin'] != m['catalog_sha256']:
        raise ValueError('binary or catalog differs from manifest')
    rendered = PROFILE.render(m)
    if data['manifest.json'] != PROFILE.manifest_bytes(m):
        raise ValueError('canonical manifest required')
    for role in ('gateway', 'signer'):
        name = 'velocita-swap-'+role+'.service'
        if data[name] != rendered[name+'.review'].encode():
            raise ValueError('unit differs from reviewed renderer')
    return m, data


def read(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISREG(meta.st_mode) or meta.st_nlink != 1 or not 0 < meta.st_size <= 64*1024**2:
            raise ValueError('bounded single-link regular artifact required')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            raw = stream.read(64*1024**2+1)
        if len(raw) != meta.st_size: raise ValueError('artifact size changed')
        return raw
    finally: os.close(fd)


def destinations(m):
    release = '/opt/velocita/releases/'+m['source_commit']
    return {name: (release+'/'+name if name in (
                'service_launcher.py', 'service_profile.py', 'fd_handoff.py', 'velocita-v2-gateway')
            else '/etc/systemd/system/'+name if name.endswith('.service')
            else PROFILE.PUBLIC+'/'+name) for name in FILES}


def fresh(path):
    if os.path.lexists(path): raise ValueError('destination already exists: '+path)


def mkdir(path, uid=0, gid=0, mode=0o755):
    p = Path(path)
    parent = PROVISION.open_directory(str(p.parent))
    try:
        os.mkdir(p.name, 0o700, dir_fd=parent)
        child = os.open(p.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
        try:
            os.fchown(child, uid, gid); os.fchmod(child, mode); os.fsync(child)
        finally: os.close(child)
        os.fsync(parent)
    finally: os.close(parent)


def command(args):
    return subprocess.run(args, check=True, stdin=subprocess.DEVNULL, capture_output=True,
                          text=True, timeout=30).stdout.strip()


def installed_bytes(path, expected):
    parent = PROVISION.open_directory(str(Path(path).parent))
    try:
        fd = os.open(Path(path).name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
        try:
            meta = os.fstat(fd)
            if not stat.S_ISREG(meta.st_mode) or meta.st_uid != 0 or meta.st_nlink != 1 or meta.st_mode & 0o022:
                raise ValueError('installed artifact ownership/type differs')
            with os.fdopen(fd,'rb',closefd=False) as stream: raw=stream.read(64*1024**2+1)
            if raw != expected: raise ValueError('installed artifact differs from previous bundle')
        finally: os.close(fd)
    finally: os.close(parent)


def replacement(old, new):
    PROFILE.validate(old); PROFILE.validate(new)
    allowed={'source_commit','binary','binary_sha256'}
    if (old['launch_authorized'] or new['launch_authorized'] or
        any(old.get(k) != new.get(k) for k in (set(old)|set(new))-allowed) or
        old['source_commit']==new['source_commit']):
        raise ValueError('dormant replacement may change release location only')


def replacement_credential_metadata(uid, directory, entries):
    if not stat.S_ISDIR(directory.st_mode) or (directory.st_uid,directory.st_gid,stat.S_IMODE(directory.st_mode)) != (uid,uid,0o700):
        raise ValueError('replacement credential directory mismatch')
    if set(entries)-{'caller.key'}:
        raise ValueError('replacement permits existing Raft caller only; venue credentials must be absent')
    if 'caller.key' in entries:
        meta=entries['caller.key']
        if not stat.S_ISREG(meta.st_mode) or (meta.st_uid,meta.st_gid,stat.S_IMODE(meta.st_mode),meta.st_nlink,meta.st_size) != (uid,uid,0o600,1,32):
            raise ValueError('replacement caller metadata mismatch')


def install(base, digest, previous=None, previous_digest=None):
    PROVISION.require_host_root()
    m, data = payload(base, digest)
    if PROVISION.plan(*PROVISION.snapshot())['actions']:
        raise ValueError('existing provisioned users/directories required')
    runtime = '/opt/velocita/runtimes/'+m['runtime_image_sha256']+'/rootfs'
    fd = PROVISION.open_directory(runtime); os.close(fd)
    if read(Path(runtime)/'etc/velocita-runtime-image') != (m['runtime_image_sha256']+'\n').encode():
        raise ValueError('runtime identity mismatch')
    if command(['/usr/bin/findmnt','-n','-o','FSTYPE','--target','/run']) != 'tmpfs':
        raise ValueError('credential parent must be tmpfs')
    paths = destinations(m)
    old_paths = {}
    if previous is not None:
        old, old_data = payload(previous, previous_digest)
        replacement(old,m)
        old_paths = destinations(old)
        for name,path in old_paths.items(): installed_bytes(path,old_data[name])
    for name,path in paths.items():
        if path not in old_paths.values(): fresh(path)
        elif name != 'catalog.bin': fresh(path+'.'+m['source_commit']+'.pending')
    release_parent = '/opt/velocita/releases'
    release = release_parent+'/'+m['source_commit']
    fresh(release)
    if previous is None: fresh('/run/velocita-swap-credentials')
    else:
        parent=PROVISION.open_directory('/run/velocita-swap-credentials')
        try:
            for role,uid in (('gateway',29001),('signer',29002)):
                fd=os.open(role,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=parent)
                try:
                    entries={name:os.stat(name,dir_fd=fd,follow_symlinks=False) for name in os.listdir(fd)}
                    replacement_credential_metadata(uid,os.fstat(fd),entries)
                finally: os.close(fd)
        finally: os.close(parent)
    for path in ('/opt/velocita', PROFILE.PUBLIC, '/etc/systemd/system', '/run'):
        fd = PROVISION.open_directory(path); os.close(fd)
    if os.path.lexists(release_parent):
        fd = PROVISION.open_directory(release_parent); os.close(fd)
    for role in ('gateway', 'signer'):
        unit = 'velocita-swap-'+role+'.service'
        if previous is None:
            if command(['/usr/bin/systemctl','show',unit,'--property=LoadState','--value']) != 'not-found':
                raise ValueError('existing service definition must not be replaced')
        else:
            for key,value in (('LoadState','loaded'),('ActiveState','inactive'),('SubState','dead'),('UnitFileState','static')):
                if command(['/usr/bin/systemctl','show',unit,'--property='+key,'--value']) != value:
                    raise ValueError('replacement requires inactive static services')
    if not os.path.lexists(release_parent): mkdir(release_parent)
    mkdir(release)
    if previous is None:
        mkdir('/run/velocita-swap-credentials')
        for role, uid in (('gateway',29001),('signer',29002)):
            mkdir('/run/velocita-swap-credentials/'+role, uid, uid, 0o700)
    for name, path in paths.items():
        if path in old_paths.values() and name=='catalog.bin': continue
        parent = PROVISION.open_directory(str(Path(path).parent))
        try:
            replace = path in old_paths.values()
            leaf=Path(path).name
            staged=leaf+'.'+m['source_commit']+'.pending' if replace else leaf
            RUNTIME.new_file(parent, staged, data[name],
                             0o555 if name == 'velocita-v2-gateway' else 0o444)
            if replace:
                installed_bytes(path,old_data[name])
                os.rename(staged,leaf,src_dir_fd=parent,dst_dir_fd=parent)
            os.fsync(parent)
        finally: os.close(parent)
    units = [paths['velocita-swap-'+r+'.service'] for r in ('gateway','signer')]
    command(['/usr/bin/systemd-analyze','verify','--man=no',*units])
    command(['/usr/bin/systemctl','daemon-reload'])
    states = {}
    for role in ('gateway','signer'):
        unit = 'velocita-swap-'+role+'.service'
        state = command(['/usr/bin/systemctl','show',unit,'-p','LoadState','-p','ActiveState',
                         '-p','SubState','-p','UnitFileState'])
        states[role] = dict(line.split('=',1) for line in state.splitlines())
        if states[role] != dict(LoadState='loaded',ActiveState='inactive',SubState='dead',UnitFileState='static'):
            raise ValueError('installed service must remain inactive and static')
    return dict(units_installed=True, service_states=states, source_commit=m['source_commit'],
                credentials_installed=False, authenticated_voter_reads=False, live_admitted=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--bundle-sha256',required=True)
    p.add_argument('--apply',action='store_true')
    p.add_argument('--previous-bundle',type=Path)
    p.add_argument('--previous-bundle-sha256')
    args = p.parse_args()
    try:
        if bool(args.previous_bundle) != bool(args.previous_bundle_sha256):
            raise ValueError('previous bundle and digest required together')
        result = install(args.bundle,args.bundle_sha256,args.previous_bundle,args.previous_bundle_sha256) if args.apply else \
            dict(validated=True,source_commit=payload(args.bundle,args.bundle_sha256)[0]['source_commit'],live_admitted=False)
        print(json.dumps(result,indent=2,sort_keys=True))
    except (ValueError,OSError,KeyError,subprocess.SubprocessError) as exc:
        print('REFUSED: '+str(exc)+'; preserve partial state; do not rerun',file=sys.stderr)
        raise SystemExit(64)
