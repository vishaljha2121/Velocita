"""Owner-only, create-only restoration of the two retained 20261015 caller keys.

Both services must be stopped and both recovery directories empty. Reads fixed
retained tmpfs paths; creates only the two missing active keys. Never starts,
rearms, retries, deletes, overwrites or prints credential material. Sources remain
in tmpfs. On any partial failure, stop and inspect; do not rerun automatically.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess

import verify_installed as VERIFY


def require(condition, message):
    if not condition:
        raise ValueError(message)


def restore(H, root=Path('/'), run=subprocess.run, uid_check=True):
    if uid_check:
        H.PROVISION.require_host_root()
    source = root/'run/velocita-retired-credentials-b79e65be-20261015'
    destination = root/'run/velocita-swap-credentials'
    prepared = []
    try:
        for role in ('gateway', 'signer'):
            output = run(['/usr/bin/systemctl', 'show', 'velocita-swap-' + role + '.service',
                          '-p', 'ActiveState', '-p', 'MainPID'],
                         capture_output=True, text=True, check=True, timeout=10).stdout
            state = dict(line.split('=', 1) for line in output.splitlines())
            require(state.get('ActiveState') in ('inactive', 'failed') and state.get('MainPID') == '0',
                    'both services must be stopped with no main PID')
            uid, _ = H.ROLES[role]
            recovery = root/f'var/lib/velocita-swap-{role}/recovery'
            fd = H.FD._open_components(str(recovery), uid)
            try:
                meta = os.fstat(fd)
                require(stat.S_ISDIR(meta.st_mode) and not os.listdir(fd), 'recovery directory must be empty')
            finally:
                os.close(fd)
            raw = VERIFY.read_caller_key(H, source/role/'caller.key', uid)
            path = destination/role
            H.tmpfs(path)
            directory = H.FD._open_components(str(path), uid)
            prepared.append((role, directory, raw))
            meta = os.fstat(directory)
            require(stat.S_ISDIR(meta.st_mode) and
                    (meta.st_uid, meta.st_gid, stat.S_IMODE(meta.st_mode)) == (uid, uid, 0o700),
                    'active role directory metadata refused')
            try:
                os.stat('caller.key', dir_fd=directory, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise ValueError('active caller key already exists; do not overwrite or retry')
        for role, directory, raw in prepared:
            uid, _ = H.ROLES[role]
            H.exclusive(directory, 'caller.key', raw, uid, uid)
        for role, _, _ in prepared:
            VERIFY.verify_caller_key(H, destination/role/'caller.key', role, run=run)
        return dict(restored_roles=['gateway', 'signer'], source_retained_in_tmpfs=True,
                    services_started=False, deleted=False, overwritten=False)
    finally:
        for _, directory, _ in prepared:
            os.close(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receiver', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('handoff', args.receiver/'credential_handoff.py')
    H = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(H)
    H.harden()
    print(json.dumps(restore(H), sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps(dict(restored=False, error_type=type(error).__name__,
                              stop_and_inspect=True, do_not_retry=True)))
        raise SystemExit(1)
