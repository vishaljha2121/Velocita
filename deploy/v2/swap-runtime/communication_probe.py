#!/usr/bin/env python3
"""Bounded owner-run role-UID qualification, synthetic native Aeron/signer IPC only.

Requires a reviewed root-owned image userspace and native qualification helper.
Never installs users, production units, credentials, or cluster/venue state.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import grp
import re
import socket
import stat
import subprocess
import sys
import time

from service_profile import signer_network_properties

VOTERS = ['100.72.127.102', '100.84.151.116', '100.85.36.22']
ROLE_UID = {'gateway': 29001, 'signer': 29002}
HELPER = '/opt/velocita-communication-probe'
IMAGE = '076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f'
ROOTFS = Path('/opt/velocita/runtimes') / IMAGE / 'rootfs'
FIELDS = {'role', 'run', 'nonce', 'uid', 'gid', 'aeron_local_transport_passed',
          'native_signer_ipc_passed', 'authenticated_voter_reads', 'live_admitted'}


def token(value, length):
    if not isinstance(value, str) or re.fullmatch('[0-9a-f]{' + str(length) + '}', value) is None:
        raise ValueError('fixed hexadecimal identifier required')
    return value


def immutable_file(path, digest):
    """Root is the trust boundary; no symlink or role-writable ancestor accepted."""
    token(digest, 64)
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('absolute immutable path required')
    for part in list(reversed(path.parents)) + [path]:
        info = part.lstat()
        if info.st_uid != 0 or info.st_mode & 0o022 or stat.S_ISLNK(info.st_mode):
            raise ValueError('root-owned immutable file ancestry required')
        if part != path and not stat.S_ISDIR(info.st_mode):
            raise ValueError('immutable ancestor is not a directory')
    if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > 268435456
            or not info.st_mode & 0o111 or info.st_mode & 0o6000):
        raise ValueError('bounded singly linked regular helper required')
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise ValueError('native helper hash differs')


def identities():
    for role, uid in ROLE_UID.items():
        account = pwd.getpwnam('velocita-swap-' + role)
        if (account.pw_uid, account.pw_gid, account.pw_shell) != (uid, uid, '/usr/sbin/nologin'):
            raise ValueError('permanent role identity differs')
        groups = set(os.getgrouplist(account.pw_name, account.pw_gid))
        # Socket-group access is assigned only to the systemd service, never
        # permanently to every process started under the gateway account.
        if groups != {uid}:
            raise ValueError('permanent role supplementary groups differ')
    if grp.getgrnam('velocita-swap-socket').gr_gid != 29002:
        raise ValueError('socket group identity differs')


def image_userspace(rootfs):
    rootfs = Path(rootfs)
    if rootfs != ROOTFS:
        raise ValueError('NUC1 qualified image userspace pin differs')
    for directory in list(reversed(rootfs.parents)) + [rootfs, rootfs/'etc']:
        info = directory.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('root-owned immutable userspace ancestry required')
    marker = rootfs/'etc/velocita-runtime-image'
    info = marker.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_nlink != 1
            or stat.S_IMODE(info.st_mode) not in (0o444,0o644) or info.st_size != 65
            or marker.read_bytes() != (IMAGE+'\n').encode()):
        raise ValueError('qualified image marker differs')


def command(role, run_id, nonce, rootfs, binary_sha256):
    if role not in ROLE_UID:
        raise ValueError('fixed production role required')
    token(run_id, 16); token(nonce, 32); token(binary_sha256, 64)
    rootfs = Path(rootfs)
    if rootfs != ROOTFS:
        raise ValueError('fixed digest-addressed image userspace required')
    root = '/run/velocita-comm-' + run_id + '-' + role
    signer_root = '/run/velocita-comm-' + run_id + '-signer'
    props = ['User=' + str(ROLE_UID[role]), 'Group=' + str(ROLE_UID[role]),
             'UMask=0077', 'NoNewPrivileges=yes', 'ProtectSystem=strict',
             'ProtectHome=yes', 'PrivateTmp=yes', 'PrivateDevices=yes',
             'ProtectKernelTunables=yes', 'ProtectKernelModules=yes',
             'ProtectControlGroups=yes', 'RestrictSUIDSGID=yes',
             'LockPersonality=yes', 'MemoryDenyWriteExecute=yes', 'LimitCORE=0',
             'CapabilityBoundingSet=', 'AmbientCapabilities=', 'Restart=no',
             'RuntimeMaxSec=45', 'TimeoutStopSec=2', 'MemoryMax=512M', 'TasksMax=32',
             'CPUAffinity=3', 'CPUQuota=100%',
             'RootDirectory=' + str(rootfs), 'MountAPIVFS=yes',
             'BindReadOnlyPaths=/opt/velocita/qualification/' + binary_sha256 + '/communication-probe:' + HELPER,
             'Environment=AERON_TERM_BUFFER_LENGTH=1048576',
             'RuntimeDirectory=' + Path(root).name,
             'RuntimeDirectoryMode=' + ('0750' if role == 'signer' else '0700'),
             'BindPaths=' + root + ':' + root,
             'InaccessiblePaths=-/etc/velocita-swap -/var/lib/velocita-swap-gateway -/var/lib/velocita-swap-signer']
    if role == 'gateway':
        props += ['SupplementaryGroups=29002', 'BindReadOnlyPaths=' + signer_root + ':' + signer_root]
    # Deliberately stricter than a live gateway: neither fixture role needs
    # internet. Same voter-only BPF policy as the production signer.
    props += signer_network_properties(VOTERS)
    return ['/usr/bin/sudo', '-n', '/usr/bin/systemd-run', '--quiet', '--wait', '--pipe', '--collect',
            '--unit=velocita-comm-' + run_id + '-' + role] + [
                '--property=' + p for p in props] + [HELPER, role, run_id, nonce]


def evaluate(role, run_id, nonce, code, stdout):
    def distinct(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate result field')
            result[key] = value
        return result
    data = json.loads(stdout, object_pairs_hook=distinct)
    if code != 0 or not isinstance(data, dict) or set(data) != FIELDS:
        raise ValueError('native probe failed or output schema differs')
    for key, value in {'role': role, 'run': run_id, 'nonce': nonce,
                       'uid': ROLE_UID[role], 'gid': ROLE_UID[role],
                       'aeron_local_transport_passed': True, 'native_signer_ipc_passed': True,
                       'authenticated_voter_reads': False, 'live_admitted': False}.items():
        if type(data[key]) is not type(value) or data[key] != value:
            raise ValueError('native identity or communication assertion failed: ' + key)
    return data


def run(output, rootfs, binary_sha256, popen=subprocess.Popen, runner=subprocess.run):
    if sys.platform != 'linux' or socket.gethostname() != 'bulbazar-NUC' or os.geteuid() == 0:
        raise ValueError('run as non-root owner on NUC1 after sudo -v')
    identities()
    rootfs = Path(rootfs)
    run_id = os.urandom(8).hex(); nonce = os.urandom(16).hex()
    commands = [command(role, run_id, nonce, rootfs, binary_sha256) for role in ('signer', 'gateway')]
    immutable_file(Path('/opt/velocita/qualification') / binary_sha256 / 'communication-probe', binary_sha256)
    image_userspace(rootfs)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    report = {'run': run_id, 'nonce': nonce, 'aeron_local_transport_qualified': False,
              'native_signer_ipc_qualified': False, 'production_uid_communication_qualified': False,
              'authenticated_voter_reads': False, 'live_admitted': False,
              'rootfs': str(rootfs), 'helper_sha256': binary_sha256, 'commands': commands, 'results': []}
    children = []; captured = set()
    def save(name, data):
        with (output / name).open('x') as stream:
            os.chmod(output / name, 0o600)
            json.dump(data, stream, indent=2); stream.write('\n')
    try:
        deadline = time.monotonic() + 60
        children.append(popen(commands[0], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
        # systemd creates the signer runtime directory before invoking the
        # helper. The gateway needs it as its only cross-role bind mount.
        directory_deadline = time.monotonic() + 8
        signer_directory = Path('/run/velocita-comm-' + run_id + '-signer')
        while not signer_directory.is_dir():
            if children[0].poll() is not None or time.monotonic() >= directory_deadline:
                raise ValueError('signer unit did not create its bounded runtime directory')
            time.sleep(0.02)
        children.append(popen(commands[1], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
        for role, child in zip(('signer', 'gateway'), children):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ValueError('global communication drill deadline')
            stdout, stderr = child.communicate(timeout=remaining)
            save(role + '.json', {'returncode': child.returncode, 'stdout': stdout, 'stderr': stderr})
            captured.add(role)
            report['results'].append(evaluate(role, run_id, nonce, child.returncode, stdout))
        report['aeron_local_transport_qualified'] = True
        report['native_signer_ipc_qualified'] = True
        report['production_uid_communication_qualified'] = True
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        report['error_type'] = type(exc).__name__
        report['error'] = str(exc)
    finally:
        # Only this run's transient fixture units. Never production services.
        pending = ['velocita-comm-' + run_id + '-' + role + '.service'
                   for role, child in zip(('signer', 'gateway'), children) if child.poll() is None]
        report['cleanup'] = {'pending_units': pending}
        if pending:
            stop = ['/usr/bin/sudo', '-n', '/usr/bin/systemctl', 'stop'] + pending
            try:
                completed = runner(stop, capture_output=True, text=True, timeout=8, check=False)
                report['cleanup'].update(returncode=completed.returncode,
                                         stdout=completed.stdout, stderr=completed.stderr)
            except (OSError, subprocess.TimeoutExpired) as exc:
                report['cleanup']['error_type'] = type(exc).__name__
        for role, child in zip(('signer', 'gateway'), children):
            if child.poll() is None:
                child.kill()
            try:
                stdout, stderr = child.communicate(timeout=2)
                if role not in captured:
                    save(role + '.json', {'returncode': child.returncode, 'stdout': stdout, 'stderr': stderr})
            except subprocess.TimeoutExpired:
                if role not in captured:
                    save(role + '.json', {'error_type': 'TimeoutExpired'})
        report['source_sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                 for path in (Path(__file__), Path(__file__).with_name('service_profile.py'))}
        save('result.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--binary-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        print('Prepared: two fixed-UID transient services, <=45s each, synthetic native Aeron/IPC only; no cluster, credentials or live admission.')
        return 0
    report = run(args.output, args.rootfs, args.binary_sha256)
    print(json.dumps({key: report[key] for key in ('aeron_local_transport_qualified',
                     'native_signer_ipc_qualified', 'production_uid_communication_qualified',
                     'authenticated_voter_reads', 'live_admitted')}))
    return 0 if report['production_uid_communication_qualified'] else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError):
        print('Communication drill refused; preserve any partial evidence.', file=sys.stderr)
        raise SystemExit(1)
