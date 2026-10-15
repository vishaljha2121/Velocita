"""Owner sudo: move the stopped b79e65be swap installation aside so the reviewed fresh install can run; deletes and starts nothing."""
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
    require(not archive.exists() and not retired_credentials.exists(), 'archive already exists; preserve it')
    require(all(source.exists() or source.is_symlink() for source, _ in moves) and credentials.is_dir(), 'expected installation is incomplete; stop and investigate')
    require(all(not source.is_symlink() for source, _ in moves), 'symlinked installation path refused')
    device = (root/'var/lib').stat().st_dev
    require(all(source.lstat().st_dev == device for source, _ in moves) and credentials.stat().st_dev == (root/'run').stat().st_dev,
            'every move must stay on one filesystem')
    meta = public.lstat()
    archive.mkdir(mode=0o700)
    for name in ('units', 'state'):
        (archive/name).mkdir(mode=0o700)
    for source, target in moves:
        os.rename(source, target)
    os.rename(credentials, retired_credentials)
    public.mkdir()
    os.chown(public, meta.st_uid, meta.st_gid)
    os.chmod(public, stat.S_IMODE(meta.st_mode))
    run(['/usr/bin/systemctl', 'daemon-reload'], check=True)
    for unit in UNITS:
        state = run(['/usr/bin/systemctl', 'show', unit, '--property=LoadState', '--value'], capture_output=True, text=True, check=True).stdout.strip()
        require(state == 'not-found', 'retired unit is still loaded')
    return dict(archive=str(archive), credentials_renamed_in_tmpfs=str(retired_credentials), deleted=False, started=False)


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: sudo python3 -B retire_installation.py YYYYMMDD')
    print(json.dumps(retire(Path('/'), sys.argv[1]), sort_keys=True))


if __name__ == '__main__':
    main()
