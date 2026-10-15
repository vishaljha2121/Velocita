"""Owner sudo: replace the sealed aae119b9 gateway/signer executable with the route-fix release and restart both, flat and driverless.

Without --run: read-only preflight. With --run: stop both services, install the pinned release, switch only the executable pins in the
manifest and units, keep journals byte-identical, lift the start guards only to start signer then gateway, restore them, verify.
On any failure the guards are restored and both services are stopped. Never changes funding, mode, documents, credentials or journals.
"""
import argparse
import contextlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import time
import urllib.request

HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


F = load('route_fix_upgrade', HERE.parent/'focused-deploy/upgrade_first_start.py')
S = load('route_fix_start', HERE.parent/'focused-deploy/start_first_run.py')
U, T, H = F.U, F.T, F.H
COMMIT = 'a665615f111e0aab1ab31621d356d9a3143df67e'
OLD_COMMIT = '1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc'
OLD_MANIFEST = 'f3df32529bc2e3bfd08cb4a82ddecd8fe9b128160b2417ecb7850252cd0668c4'
ARCHIVE = Path('/var/lib/velocita-route-fix-upgrade-a665615')
VOTERS = S.VOTERS


def configure(binary):
    if len(binary) != 64 or any(c not in '0123456789abcdef' for c in binary):
        raise ValueError('qualified gateway binary SHA-256 required')
    U.COMMIT, U.BINARY, U.RELEASE = COMMIT, binary, Path('/opt/velocita/releases')/COMMIT
    U.PINS = dict(F.U.PINS, **{'velocita-v2-gateway': binary})
    S.ARCHIVE, S.ATTEMPT = ARCHIVE, ARCHIVE/'restart'


def next_manifest(old, profile):
    profile.validate(old)
    if (old['incarnation'] != F.INCARNATION or old['launch_authorized'] is not True or old.get('initialize_swap_recovery') is not False or
            old.get('recovery_only', False) or old['source_commit'] != OLD_COMMIT or old['runtime_image_sha256'] != U.IMAGE):
        raise ValueError('exact sealed 1f8d7b6 installation required')
    new = dict(old, source_commit=COMMIT, binary=str(U.RELEASE/'velocita-v2-gateway'), binary_sha256=U.BINARY)
    profile.validate(new)
    return new


def health():
    rows = []
    for ip in VOTERS:
        with urllib.request.urlopen('http://' + ip + ':11410/metrics', timeout=5) as response:
            raw = response.read(1048577).decode('ascii')

        def metric(name, label=''):
            found = [line for line in raw.splitlines() if line.startswith('velocita_v2_' + name + '{') and (not label or label in line)]
            if len(found) != 1:
                raise ValueError('missing voter health ' + name)
            return int(found[0].rsplit(' ', 1)[-1])
        row = {k: metric(k) for k in ('up', 'ready', 'failed', 'strategy_mode', 'term', 'leader_id')}
        row.update(commit=metric('log_index', 'kind="commit"'), applied=metric('log_index', 'kind="applied"'))
        if (row['up'], row['ready'], row['failed']) != (1, 1, 0) or row['commit'] != row['applied']:
            raise ValueError('every voter must be up, ready and applied')
        rows.append(row)
    if len({(r['term'], r['leader_id'], r['commit']) for r in rows}) != 1:
        raise ValueError('voters disagree')
    return rows


def flat_observation(require_fresh=True):
    value = json.loads(S.O.read(S.O.SETTLEMENT_SOURCE), object_pairs_hook=S.O.unique_object)
    S.O.settlements(value, F.INCARNATION, S.now_ms())
    if require_fresh and not 0 <= S.now_ms() - value['observed_at_ms'] <= 12_000:
        raise ValueError('fresh gateway observation required')
    if (value['base_atoms'], value['pending_effect'], value['last_effect'], value['quote_atoms']) != (0, 0, 0, 25_000_000):
        raise ValueError('run must be flat with no effect before the upgrade')
    return value


def journal_bytes():
    out = {}
    for name, (role, leaf, size, magic) in T.JOURNALS.items():
        path = U.STATE_ROOTS[role]/'recovery'/name/leaf
        raw = U.read(path)
        if len(raw) != size or raw[:8] != magic:
            raise ValueError('journal shape differs: ' + name)
        out[name] = raw
    return out


@contextlib.contextmanager
def locked_journal_dirs():
    fds = []
    try:
        for name, (role, _, _, _) in T.JOURNALS.items():
            fd = F.role_directory(U.STATE_ROOTS[role]/'recovery'/name, role)
            fds.append(fd)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        for fd in reversed(fds):
            os.close(fd)


def running():
    return all(S.service_state(r)['ActiveState'] == 'active' for r in H.ROLES)


def preflight(data, profile):
    U.require_host()
    H.harden()
    S.driver_stopped()
    U.verify_guards()
    oldraw = U.read(U.PUBLIC/'manifest.json', True)
    if U.digest(oldraw) != OLD_MANIFEST:
        raise ValueError('installed manifest is not the sealed 1f8d7b6 manifest')
    old = json.loads(oldraw)
    new = next_manifest(old, profile)
    docs = {n: U.read(U.PUBLIC/n, True) for n in (*U.DOC_PINS, 'transaction.review')}
    for name, key in U.DOC_PINS.items():
        if U.digest(docs[name]) != old[key]:
            raise ValueError('installed document pin differs')
    if U.digest(U.read(Path(old['binary']), True)) != old['binary_sha256']:
        raise ValueError('installed binary differs from its pin')
    units = {u: U.read(U.SYSTEMD/u, True) for u in U.UNITS}
    if any(units[u] != profile.render(old)[u + '.review'].encode() for u in units):
        raise ValueError('installed unit differs from manifest')
    if U.RELEASE.exists() or ARCHIVE.exists():
        raise ValueError('fresh release and archive required; inspect a prior attempt')
    review = H.LAUNCH.fields(docs['account.review'])
    if int(review['expires_at_ms']) - S.now_ms() < 3_600_000:
        raise ValueError('at least an hour of reviewed lifetime required')
    report = dict(old_manifest=OLD_MANIFEST, new_manifest=U.digest(profile.manifest_bytes(new)), voters=health(), observation=flat_observation(running()),
                  services_running=running(), review_expires_ms=int(review['expires_at_ms']))
    return oldraw, old, new, docs, units, report


def upgrade_and_restart(base):
    data, profile = U.payload(base)
    oldraw, old, new, docs, units, report = preflight(data, profile)
    credentials = F.credential_snapshot()
    ARCHIVE.mkdir(mode=0o700)
    U.fresh_write(ARCHIVE/'preflight.json', (json.dumps(report, indent=2) + '\n').encode(), 0o400)
    S.stop_services()
    U.stopped()
    try:
        with locked_journal_dirs():
            before = journal_bytes()
            U.fresh_write(ARCHIVE/'manifest.json', oldraw, 0o400)
            for unit, raw in units.items():
                U.fresh_write(ARCHIVE/unit, raw, 0o400)
            U.RELEASE.mkdir(mode=0o755)
            os.chmod(U.RELEASE, 0o755)
            for name, raw in data.items():
                U.fresh_write(U.RELEASE/name, raw, 0o555 if name == 'velocita-v2-gateway' else 0o444)
            for unit, raw in units.items():
                U.replace(U.SYSTEMD/unit, raw, profile.render(new)[unit + '.review'].encode())
            U.replace(U.PUBLIC/'manifest.json', oldraw, profile.manifest_bytes(new))
            U.run(['systemd-analyze', 'verify', '--man=no', *[str(U.SYSTEMD/u) for u in units]])
            U.run(['systemctl', 'daemon-reload'])
            U.verify_guards()
            U.stopped()
            if journal_bytes() != before or F.credential_snapshot() != credentials:
                raise ValueError('journal or credential changed during upgrade')
            if any(U.read(U.PUBLIC/n, True) != raw for n, raw in docs.items()):
                raise ValueError('public document changed during upgrade')
        S.ATTEMPT.mkdir(mode=0o700)
        S.release_guards()
        for role in ('signer', 'gateway'):
            health()
            S.driver_stopped()
            U.run(['systemctl', 'start', 'velocita-swap-' + role + '.service'])
            time.sleep(5 if role == 'signer' else 40)
            if not S.service_state(role)['ActiveState'] == 'active':
                raise ValueError(role + ' did not stay running')
        S.restore_guards()
        if not running():
            raise ValueError('both services must stay running')
        result = dict(report, upgraded_to=COMMIT, binary_sha256=U.BINARY, observation_after=flat_observation(), services_running=True,
                      start_guards_retained=True, journals_unchanged_before_start=True, driver_started=False)
        U.fresh_write(ARCHIVE/'result.json', (json.dumps(result, indent=2) + '\n').encode(), 0o400)
        return result
    except BaseException:
        try:
            S.restore_guards()
        finally:
            S.stop_services()
        raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--binary-sha256', required=True)
    p.add_argument('--run', action='store_true')
    a = p.parse_args()
    configure(a.binary_sha256)
    lock = U.directory(U.PUBLIC)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if a.run:
            result = upgrade_and_restart(a.bundle)
        else:
            data, profile = U.payload(a.bundle)
            result = dict(preflight(data, profile)[5], applied=False)
        print(json.dumps(result, sort_keys=True))
    finally:
        os.close(lock)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps(dict(error_type=type(error).__name__, error=str(error)[:200], stop_and_inspect=True, do_not_retry=True)))
        raise SystemExit(1)
