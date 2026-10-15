"""Owner sudo: move the stopped b79e65be swap installation aside so the reviewed fresh install can run; deletes and starts nothing.

The provisioned state directories are recreated empty with their original owner and mode; their contents go to the archive. Raft caller keys stay in place; only the old venue credentials retire within tmpfs.
"""
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

UNITS = ('velocita-swap-gateway.service', 'velocita-swap-signer.service')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def retire(root, date, run=subprocess.run, uid_check=True):
    if uid_check:
        require(os.geteuid() == 0, 'root required')
    require(len(date) == 8 and date.isdigit(), 'date YYYYMMDD required')
    for unit in UNITS:
        state = run(['/usr/bin/systemctl', 'show', unit, '--property=ActiveState', '--value'], capture_output=True, text=True, check=True).stdout.strip()
        require(state == 'inactive', 'both swap services must be inactive')
    archive = root/'var/lib'/f'velocita-retired-b79e65be-{date}'
    credentials = root/'run/velocita-swap-credentials'
    retired_credentials = root/'run'/f'velocita-retired-credentials-b79e65be-{date}'
    public = root/'etc/velocita-swap/public'
    system = root/'etc/systemd/system'
    moves = [(public, archive/'public')]
    moves += [(system/u, archive/'units'/u) for u in UNITS] + [(system/(u + '.d'), archive/'units'/(u + '.d')) for u in UNITS]
    moves += [(root/'var/lib'/n, archive/'state'/n) for n in ('velocita-swap-gateway', 'velocita-swap-signer')]
    cache = root/'var/lib/velocita-live-observer/settlements.json'
    moves += [(cache, archive/'observer-settlements.json')] if cache.exists() or cache.is_symlink() else []
    require(not os.path.lexists(archive) and not os.path.lexists(retired_credentials), 'archive already exists; preserve it')
    require(all(source.exists() or source.is_symlink() for source, _ in moves) and credentials.is_dir(), 'expected installation is incomplete; stop and investigate')
    require(all(not source.is_symlink() for source, _ in moves), 'symlinked installation path refused')
    device = (root/'var/lib').stat().st_dev
    require(all(source.lstat().st_dev == device for source, _ in moves) and credentials.stat().st_dev == (root/'run').stat().st_dev,
            'every move must stay on one filesystem')
    require(not credentials.is_symlink() and set(p.name for p in credentials.iterdir()) == {'gateway', 'signer'},
            'credential layout refused')
    credential_moves = []
    credential_dirs = []
    for role, leaf in (('gateway', 'session.bearer'), ('signer', 'signer.pem')):
        directory = credentials/role
        require(directory.is_dir() and not directory.is_symlink(), 'credential layout refused')
        require(set(p.name for p in directory.iterdir()) <= {'caller.key', leaf}, 'credential layout refused')
        caller = directory/'caller.key'
        require(caller.is_file() and not caller.is_symlink(), 'caller key missing or unsafe')
        key = caller.lstat()
        require((stat.S_IMODE(key.st_mode), key.st_nlink, key.st_size) == (0o600, 1, 32), 'caller key metadata refused')
        secret = directory/leaf
        if os.path.lexists(secret):
            require(secret.is_file() and not secret.is_symlink(), 'credential layout refused')
            credential_moves.append((secret, retired_credentials/role/leaf))
        credential_dirs.append((retired_credentials/role, directory.lstat()))
    meta = public.lstat()
    states = [root/'var/lib'/n for n in ('velocita-swap-gateway', 'velocita-swap-signer')]
    require(all((d/sub).is_dir() and not (d/sub).is_symlink() for d in states for sub in ('recovery', 'metrics')), 'expected installation is incomplete; stop and investigate')
    layout = [(d/sub, (d/sub).lstat()) for d in states for sub in ('', 'recovery', 'metrics')]
    archive.mkdir(mode=0o700)
    for name in ('units', 'state'):
        (archive/name).mkdir(mode=0o700)
    for source, target in moves:
        os.rename(source, target)
    retired_credentials.mkdir(mode=0o700)
    for path, before in credential_dirs:
        path.mkdir(mode=0o700)
        os.chown(path, before.st_uid, before.st_gid)
        os.chmod(path, stat.S_IMODE(before.st_mode))
    for source, target in credential_moves:
        os.rename(source, target)
    public.mkdir()
    os.chown(public, meta.st_uid, meta.st_gid)
    os.chmod(public, stat.S_IMODE(meta.st_mode))
    for path, before in layout:
        path.mkdir()
        os.chown(path, before.st_uid, before.st_gid)
        os.chmod(path, stat.S_IMODE(before.st_mode))
    run(['/usr/bin/systemctl', 'daemon-reload'], check=True)
    for unit in UNITS:
        state = run(['/usr/bin/systemctl', 'show', unit, '--property=LoadState', '--value'], capture_output=True, text=True, check=True).stdout.strip()
        require(state == 'not-found', 'retired unit is still loaded')
    return dict(archive=str(archive), credentials_renamed_in_tmpfs=str(retired_credentials), caller_keys_preserved=True, deleted=False, started=False)


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: sudo python3 -B retire_installation.py YYYYMMDD')
    print(json.dumps(retire(Path('/'), sys.argv[1]), sort_keys=True))


if __name__ == '__main__':
    main()
