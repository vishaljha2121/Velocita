"""Stage the live stochastic swap driver on NUC1 for one incarnation; installs a manual-start user unit and never starts it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import time

NODES = ('1=100.72.127.102:44301', '2=100.84.151.116:44302', '3=100.85.36.22:44303')
RESPONSE = '100.72.127.102:44412'
UNIT = 'velocita-strategy-driver.service'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def client(root, image, incarnation, secrets):
    return ['podman', 'run', '--rm', '--network', 'host', '--userns=keep-id', '--read-only', '--security-opt', 'no-new-privileges',
            '--cap-drop', 'all', '--pids-limit', '64', '--memory', '512m', '--shm-size', '256m',
            '--entrypoint', '/usr/local/bin/velocita-v2-client',
            '-v', f'{root}/opening.record:/config/opening.record:ro', '-v', f'{root}/client:/state',
            '-v', f'{secrets}/caller-12.key:/run/velocita-secrets/caller-12.key:ro', image,
            '--caller-key-id', '12', '--caller-id', '21', '--key-file', '/run/velocita-secrets/caller-12.key',
            '--incarnation', incarnation, '--session-file', '/state/client.session', '--response-endpoint', RESPONSE,
            '--aeron-dir', '/dev/shm/velocita-strategy', *[x for node in NODES for x in ('--node', node)]]


def config(root, image, incarnation, secrets, observation, kill_epoch, executor_generation, max_orders, review_expires_ms):
    return dict(incarnation=incarnation, max_orders=max_orders, review_expires_ms=review_expires_ms, observation=str(observation), state=f'{root}/state/driver-state.json',
                opening_record='/config/opening.record', kill_epoch=kill_epoch, executor_generation=executor_generation,
                client=client(root, image, incarnation, secrets))


def unit(root, incarnation):
    return '\n'.join(['[Unit]', f'Description=Velocita stochastic swap driver for incarnation {incarnation}',
                      '', '[Service]', 'Type=simple', f'Environment=PYTHONPATH={root}/model/src',
                      f'ExecStart=/usr/bin/python3 -B -m velocita_model.shadow.live_swap_driver --config {root}/driver.json',
                      'Restart=no', 'TimeoutStopSec=40', '']) + '\n'


def stage(home, uid, archive, archive_sha, opening, opening_sha, image, incarnation, kill_epoch, executor_generation, max_orders, review_expires_ms, run=subprocess.run):
    require(re.fullmatch('[0-9a-f]{32}', incarnation), 'incarnation must be 32 lowercase hex digits')
    require(re.fullmatch(r'localhost/velocita-v2:qualified-[0-9a-f]{7,40}', image), 'qualified local voter image required')
    require(type(kill_epoch) is int and kill_epoch > 0 and type(executor_generation) is int and executor_generation > 0, 'positive epochs required')
    require(type(max_orders) is int and 1 <= max_orders <= 240, 'order limit must be 1 to 240')
    require(type(review_expires_ms) is int and review_expires_ms > time.time_ns() // 1_000_000 + 420_000,
            'review expiry must leave the full drain margin')
    require(sha256(archive) == archive_sha and sha256(opening) == opening_sha, 'archive or opening differs from its pin')
    units = home/'.config/systemd/user'
    state = run(['systemctl', '--user', 'is-active', UNIT], capture_output=True, text=True).stdout.strip()
    require(state in ('inactive', 'unknown', ''), 'driver unit must not be running')
    root = home/'velocita-strategy'/incarnation
    require(not root.exists(), 'incarnation already staged; preserve it')
    secrets = Path('/run/user')/str(uid)/'velocita-secrets'
    with tarfile.open(archive) as source:
        members = source.getmembers()
        require(all(m.isfile() or m.isdir() for m in members) and
                all(m.name.startswith('model/src/velocita_model/') or m.name in ('model', 'model/src', 'model/src/velocita_model') for m in members),
                'archive must hold only model/src/velocita_model')
        os.umask(0o077)
        root.mkdir(parents=True)
        for name in ('client', 'state'):
            (root/name).mkdir()
        source.extractall(root, members=members)
    (root/'opening.record').write_bytes(Path(opening).read_bytes())
    observation = home/'velocita-v2/textfile/strategy-settlements.json'
    (root/'driver.json').write_text(json.dumps(config(root, image, incarnation, secrets, observation, kill_epoch, executor_generation, max_orders, review_expires_ms), indent=2) + '\n')
    units.mkdir(parents=True, exist_ok=True)
    pending = units/(UNIT + '.pending')
    pending.write_text(unit(root, incarnation))
    os.replace(pending, units/UNIT)
    run(['systemctl', '--user', 'daemon-reload'], check=True)
    return dict(staged=str(root), unit=UNIT, opening_sha256=opening_sha, image=image, max_orders=max_orders, review_expires_ms=review_expires_ms, started=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('archive', 'archive-sha256', 'opening', 'opening-sha256', 'image', 'incarnation'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--kill-epoch', type=int, default=1)
    parser.add_argument('--executor-generation', type=int, default=1)
    parser.add_argument('--max-orders', type=int, required=True)
    parser.add_argument('--review-expires-ms', type=int, required=True)
    a = parser.parse_args()
    print(json.dumps(stage(Path.home(), os.getuid(), a.archive, a.archive_sha256, a.opening, a.opening_sha256, a.image, a.incarnation,
                           a.kill_epoch, a.executor_generation, a.max_orders, a.review_expires_ms), sort_keys=True))


if __name__ == '__main__':
    main()
