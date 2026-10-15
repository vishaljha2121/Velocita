"""Retire one voter's old incarnation into an archive and stage the new one, or switch it to recover; never deletes or starts anything."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import urllib.request

OLD = 'b79e65be1ea14e219016ec48ea02efef'
OLD_IMAGE = 'localhost/velocita-v2:qualified-1a46fd8'
NODES = {'bulbazar-NUC': (1, '100.72.127.102'), 'vishalkapi': (2, '100.84.151.116'), 'nuc2': (3, '100.85.36.22')}
UNITS = ('velocita-paper-stochastic-node', 'velocita-paper-stochastic-exporter')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def replace(path, raw):
    pending = path.with_name(path.name + '.pending')
    with pending.open('xb') as out:
        os.fchmod(out.fileno(), path.stat().st_mode & 0o777 if path.exists() else 0o644)
        out.write(raw)
        out.flush()
        os.fsync(out.fileno())
    os.replace(pending, path)
    fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def stage(home, node, new, image, date, run=subprocess.run):
    require(re.fullmatch('[0-9a-f]{32}', new) and new != OLD, 'new 32-hex incarnation required')
    require(re.fullmatch(r'localhost/velocita-v2:qualified-[0-9a-f]{7}', image) and image != OLD_IMAGE, 'new qualified image required')
    require(re.fullmatch('[0-9]{8}', date), 'date YYYYMMDD required')
    for unit in UNITS:
        state = run(['systemctl', '--user', 'show', unit + '.service', '--property=ActiveState', '--value'], capture_output=True, text=True, check=True).stdout.strip()
        require(state in ('inactive', 'failed'), 'voter and exporter must be stopped first')
    require(run(['podman', 'image', 'exists', image]).returncode == 0, 'new image missing on this host')
    base = home/'velocita-indicator-paper'
    live, retired, units = base/'stochastic', base/'retired'/f'stochastic-{OLD[:8]}-{date}', home/'.config/systemd/user'
    require(not retired.exists(), 'retirement archive already exists; preserve it')
    conf = live/'config'/f'node-{node}.conf'
    raw = conf.read_bytes()
    require(raw.count(f'cluster.incarnation = {OLD}\n'.encode()) == 1 and raw.count(b'\nstartup = recover\n') == 1, 'running old incarnation in recover mode required')
    befores = {u: (units/(u + '.service')).read_bytes() for u in UNITS}
    require(all(b.count(OLD_IMAGE.encode()) == 1 for b in befores.values()), 'units must run the old qualified image')
    catalog = (live/'config/catalog.bin').read_bytes()
    retired.parent.mkdir(exist_ok=True)
    live.rename(retired)
    (retired/'units-before').mkdir()
    for u, b in befores.items():
        (retired/'units-before'/(u + '.service')).write_bytes(b)
    (live/'config').mkdir(parents=True)
    (live/'data').mkdir()
    (live/'config/catalog.bin').write_bytes(catalog)
    replace(live/'config'/f'node-{node}.conf', raw.replace(OLD.encode(), new.encode()).replace(b'\nstartup = recover\n', b'\nstartup = bootstrap\n'))
    for u, b in befores.items():
        replace(units/(u + '.service'), b.replace(OLD_IMAGE.encode(), image.encode()))
    run(['systemctl', '--user', 'daemon-reload'], check=True)
    return dict(node=node, retired=str(retired), incarnation=new, image=image, startup='bootstrap', started=False,
                retired_config_sha256=hashlib.sha256(raw).hexdigest())


def ready(raw, name, extra=''):
    found = re.findall(r'^velocita_v2_' + name + r'\{[^\n]*' + extra + r'[^\n]*\} ([0-9.]+)$', raw, re.M)
    require(len(found) == 1, 'ambiguous metric ' + name)
    return float(found[0])


def recover(home, node, ip, new, fetch=lambda url: urllib.request.urlopen(url, timeout=8).read().decode()):
    raw = fetch(f'http://{ip}:11410/metrics')
    require(ready(raw, 'up') == 1 and ready(raw, 'ready') == 1 and ready(raw, 'failed') == 0, 'voter not ready')
    conf = home/'velocita-indicator-paper/stochastic/config'/f'node-{node}.conf'
    body = conf.read_bytes()
    require(body.count(f'cluster.incarnation = {new}\n'.encode()) == 1 and body.count(b'\nstartup = bootstrap\n') == 1, 'bootstrapped new incarnation required')
    replace(conf, body.replace(b'\nstartup = bootstrap\n', b'\nstartup = recover\n'))
    return dict(node=node, incarnation=new, startup='recover', restarted=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=('stage', 'recover'))
    p.add_argument('--incarnation', required=True)
    p.add_argument('--image')
    p.add_argument('--date')
    a = p.parse_args()
    node, ip = NODES[socket.gethostname()]
    home = Path.home()
    result = stage(home, node, a.incarnation, a.image, a.date) if a.mode == 'stage' else recover(home, node, ip, a.incarnation)
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
