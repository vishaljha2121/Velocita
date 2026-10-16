"""Run as velocitas on one voter host while the cluster is halted: back up config and data, swap only the image token in the voter and exporter units, restart and wait for the voter to rejoin."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import time
import urllib.request

HOMES = {'bulbazar-NUC': (1, '100.72.127.102'), 'vishalkapi': (2, '100.84.151.116'), 'nuc2': (3, '100.85.36.22')}
NAMES = ('velocita-paper-stochastic-node', 'velocita-paper-stochastic-exporter')
OLD = 'localhost/velocita-v2:qualified-ba299ff'
NEW = 'localhost/velocita-v2:qualified-64e6f9f'
INCARNATION = 'aae119b95ddcea5a206f03ab5ca12c15'
READY_S = 180


def run(argv):
    return subprocess.check_output(argv, text=True, timeout=45).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree(path):
    out = {}
    for item in sorted(path.rglob('*')):
        if item.is_symlink():
            raise ValueError('symlink in voter tree: '+str(item))
        if item.is_file():
            out[str(item.relative_to(path))] = digest(item)
    return out


def health(address):
    raw = urllib.request.urlopen('http://'+address+':11410/metrics', timeout=4).read().decode()

    def metric(name, label=''):
        rows = [x for x in raw.splitlines() if x.startswith('velocita_v2_'+name+'{') and (not label or label in x)]
        if len(rows) != 1:
            raise ValueError('missing voter health '+name)
        return int(rows[0].rsplit(' ', 1)[1])
    row = {n: metric(n) for n in ('up', 'ready', 'failed', 'strategy_mode', 'term', 'leader_id')}
    row.update(commit=metric('log_index', 'kind="commit"'), applied=metric('log_index', 'kind="applied"'),
               durable=metric('log_index', 'kind="durable"'))
    return row


def settled(row):
    return (row['up'] == row['ready'] == 1 and row['failed'] == 0 and row['strategy_mode'] == 5 and
            row['commit'] == row['applied'] == row['durable'])


def main(pin):
    node, ip = HOMES[socket.gethostname()]
    home = Path.home()
    root = home/'velocita-indicator-paper/stochastic'
    units = home/'.config/systemd/user'
    archive = home/'.cache/velocita-build'/('effect25-backup-64e6f9f-node'+str(node))
    before = [health(address) for _, address in HOMES.values()]
    if not all(settled(r) for r in before) or len({(r['term'], r['leader_id'], r['commit']) for r in before}) != 1:
        raise ValueError('all three voters must be halted, settled and agree')
    if run(['podman', 'image', 'inspect', NEW, '--format', '{{.Id}}']) != pin:
        raise ValueError('packaged image differs from its pin')
    if archive.exists():
        raise ValueError('archive exists; inspect a prior attempt')
    config = (root/'config'/('node-'+str(node)+'.conf')).read_text()
    fields = {k.strip(): v.strip() for k, _, v in (x.partition('=') for x in config.splitlines() if '=' in x and not x.lstrip().startswith('#'))}
    if fields['startup'] != 'recover' or fields['cluster.incarnation'] != INCARNATION:
        raise ValueError('voter config is not the recovering aae119b9 incarnation')
    old_units = {n: (units/(n+'.service')).read_bytes() for n in NAMES}
    if not all(raw.count(OLD.encode()) == 1 and NEW.encode() not in raw for raw in old_units.values()):
        raise ValueError('units must name exactly the ba299ff image once')
    if not all(run(['systemctl', '--user', 'is-active', n]) == 'active' for n in NAMES):
        raise ValueError('voter and exporter must be active')
    archive.mkdir(mode=0o700)
    (archive/'preflight.json').write_text(json.dumps(dict(health=before, node=node, image_id=pin), indent=2)+'\n')
    try:
        subprocess.run(['systemctl', '--user', 'stop', *NAMES], check=True, timeout=45)
        if not all(run(['systemctl', '--user', 'show', n, '-p', 'ActiveState', '--value']) == 'inactive' for n in NAMES):
            raise ValueError('voter did not stop')
        for n, raw in old_units.items():
            if (units/(n+'.service')).read_bytes() != raw:
                raise ValueError('unit changed while stopping')
            (archive/(n+'.service')).write_bytes(raw)
        copies = {}
        for leaf in ('config', 'data'):
            shutil.copytree(root/leaf, archive/leaf)
            copies[leaf] = tree(archive/leaf)
            if tree(root/leaf) != copies[leaf]:
                raise ValueError('backup differs from '+leaf)
        (archive/'backup-sha256.json').write_text(json.dumps(copies, indent=2)+'\n')
        os.sync()
        for n, raw in old_units.items():
            path = units/(n+'.service')
            pending = path.with_name(path.name+'.64e6f9f-pending')
            with pending.open('xb') as f:
                os.fchmod(f.fileno(), stat.S_IMODE(path.stat().st_mode))
                f.write(raw.replace(OLD.encode(), NEW.encode()))
                f.flush()
                os.fsync(f.fileno())
            os.replace(pending, path)
        fd = os.open(units, os.O_RDONLY | os.O_DIRECTORY)
        os.fsync(fd)
        os.close(fd)
        if tree(root/'data') != copies['data'] or tree(root/'config') != copies['config']:
            raise ValueError('voter tree changed while stopped')
        subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True, timeout=30)
        subprocess.run(['systemctl', '--user', 'start', *NAMES], check=True, timeout=45)
        deadline = time.monotonic()+READY_S
        while True:
            try:
                row = health(ip)
                if row['failed']:
                    raise RuntimeError('new voter reports failure')
                if settled(row) and row['commit'] >= before[0]['commit']:
                    break
            except (OSError, ValueError):
                pass
            if time.monotonic() >= deadline:
                raise TimeoutError('voter recovery not ready')
            time.sleep(2)
        for n in NAMES:
            if run(['podman', 'inspect', n, '--format', '{{.Image}}']) != pin or run(['systemctl', '--user', 'is-active', n]) != 'active':
                raise ValueError(n+' is not running the packaged image')
        if tree(root/'config') != copies['config']:
            raise ValueError('voter config changed')
        result = dict(node=node, host=socket.gethostname(), source_commit='64e6f9f4bd499127c7584bea02f20d6fc129597b', image_id=pin,
                      archive=str(archive), configuration_unchanged=True, data_unchanged_before_restart=True, backup_verified=True, health=row)
        (archive/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result), flush=True)
    except BaseException:
        subprocess.run(['systemctl', '--user', 'stop', *NAMES], timeout=45, check=False)
        raise


if __name__ == '__main__':
    if len(sys.argv) != 2 or len(sys.argv[1]) != 64:
        raise SystemExit('usage: rolling_voter_upgrade.py <packaged image id>')
    main(sys.argv[1])
