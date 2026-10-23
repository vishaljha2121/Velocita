"""Owner sudo: install 64e6f9f recovery-only, rotate the gateway to an owner-minted bearer and run the gateway once to resolve effect 25.

Without --run: read-only preflight. With --run: stop both services, install the pinned release, derive a runtime profile that
differs only in bearer_digest and expires_at_ms, publish a recovery_only manifest, rotate the gateway bearer, lift only the gateway
start guard for one start, wait for the gateway to exit, restore the guard and verify effect 25 is no longer pending.
The signer is never started. Journals, funding, documents other than the profile, and the account review are unchanged.
"""
import argparse
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import time

HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M = load('effect25_base', HERE.parent/'route-fix/owner_upgrade_restart.py')
U, S, F, H = M.U, M.S, M.F, M.H
COMMIT = '64e6f9f4bd499127c7584bea02f20d6fc129597b'
OLD_COMMIT = 'ba299ff6c89392177cfd4702fda11dabb7ee1719'
OLD_BINARY = '9aa06eb11b23f30db37f8c628a2ca4efac7ea32d9b08c21f08a1a95df7942023'
OLD_MANIFEST = '7668ba129bfdf013ac95ac9407cd6af311113414d2693182abb656fe1e7a8a70'
ARCHIVE = Path('/var/lib/velocita-effect25-recovery-64e6f9f')
VOTER_IMAGE = 'localhost/velocita-v2:qualified-64e6f9f'
EFFECT = 25
QUOTE_ATOMS = 24_047_860
RUN_LIMIT_S = 900


def configure(binary):
    if len(binary) != 64 or any(c not in '0123456789abcdef' for c in binary):
        raise ValueError('qualified gateway binary SHA-256 required')
    M.configure(binary)
    M.COMMIT, U.COMMIT, U.RELEASE = COMMIT, COMMIT, Path('/opt/velocita/releases')/COMMIT
    M.ARCHIVE, S.ARCHIVE, S.ATTEMPT = ARCHIVE, ARCHIVE, ARCHIVE/'recovery'


def next_profile(raw, bearer_sha256, expires_at_ms, now_ms):
    if len(bearer_sha256) != 64 or any(c not in '0123456789abcdef' for c in bearer_sha256):
        raise ValueError('bearer SHA-256 required')
    if type(expires_at_ms) is not int or not now_ms + 600_000 <= expires_at_ms <= now_ms + 86_400_000:
        raise ValueError('bearer must stay valid at least ten minutes and at most a day')
    lines = raw.decode('ascii').split('\n')
    if lines[-1] != '' or len(lines) < 3:
        raise ValueError('canonical profile required')
    seen = set()
    out = []
    for line in lines[:-1]:
        key, sep, _ = line.partition('=')
        if not sep or key in seen:
            raise ValueError('canonical profile required')
        seen.add(key)
        out.append('bearer_digest='+bearer_sha256 if key == 'bearer_digest' else
                   'expires_at_ms='+str(expires_at_ms) if key == 'expires_at_ms' else line)
    if not {'bearer_digest', 'expires_at_ms'} <= seen:
        raise ValueError('profile lacks a gateway session')
    new = ('\n'.join(out)+'\n').encode('ascii')
    if new == raw:
        raise ValueError('bearer and expiry must change')
    return new


def next_manifest(old, profile, profile_sha256):
    profile.validate(old)
    if (old['incarnation'] != F.INCARNATION or old['launch_authorized'] is not True or old.get('initialize_swap_recovery') is not False or
            old.get('recovery_only', False) or old['source_commit'] != OLD_COMMIT or old['binary_sha256'] != OLD_BINARY or old['runtime_image_sha256'] != U.IMAGE):
        raise ValueError('exact installed ba299ff live manifest required')
    new = dict(old, source_commit=COMMIT, binary=str(U.RELEASE/'velocita-v2-gateway'), binary_sha256=U.BINARY,
               runtime_profile_sha256=profile_sha256, recovery_only=True)
    profile.validate(new)
    return new


def pending_effect25(value):
    shape = (value['pending_effect'], value['last_effect'], value['base_atoms'], value['quote_atoms'])
    if shape != (EFFECT, EFFECT, 0, QUOTE_ATOMS):
        raise ValueError('effect 25 must be the pending buy with the reviewed ledger: '+str(shape))


def resolved_effect25(value):
    shape = (value['pending_effect'], value['last_effect'], value['base_atoms'], value['quote_atoms'])
    if shape != (0, EFFECT, 0, QUOTE_ATOMS):
        raise ValueError('effect 25 did not resolve without a charge: '+str(shape))


def producer_observation():
    value = json.loads(S.O.read(S.O.SETTLEMENT_SOURCE), object_pairs_hook=S.O.unique_object)
    S.O.settlements(value, F.INCARNATION, S.now_ms())
    return value


def halted_health():
    rows = M.health()
    if len(rows) != 3 or any(row['strategy_mode'] != 5 for row in rows):
        raise ValueError('all three voters must be owner-halted')
    return rows


def local_voter_image():
    args = ['/usr/sbin/runuser', '-u', 'velocitas', '--', '/usr/bin/env', 'XDG_RUNTIME_DIR=/run/user/1003',
            '/usr/bin/podman', 'inspect', '--format', '{{.ImageName}}', 'velocita-paper-stochastic-node']
    image = U.run(args).strip()
    if image != VOTER_IMAGE:
        raise ValueError('NUC1 voter must already run '+VOTER_IMAGE+', found '+image)


def gateway_stopped():
    state = S.service_state('gateway')
    if state['ActiveState'] not in ('inactive', 'failed'):
        raise ValueError('gateway must be stopped before recovery')


def preflight(data, profile, bearer_sha256, expires_at_ms, staged):
    U.require_host()
    H.harden()
    S.driver_stopped()
    U.verify_guards()
    gateway_stopped()
    oldraw = U.read(U.PUBLIC/'manifest.json', True)
    if U.digest(oldraw) != OLD_MANIFEST:
        raise ValueError('installed manifest is not the reviewed ba299ff manifest')
    old = json.loads(oldraw)
    docs = {n: U.read(U.PUBLIC/n, True) for n in (*U.DOC_PINS, 'transaction.review')}
    for name, key in U.DOC_PINS.items():
        if U.digest(docs[name]) != old[key]:
            raise ValueError('installed document pin differs')
    if U.digest(U.read(Path(old['binary']), True)) != old['binary_sha256']:
        raise ValueError('installed binary differs from its pin')
    units = {u: U.read(U.SYSTEMD/u, True) for u in U.UNITS}
    if any(units[u] != profile.render(old)[u+'.review'].encode() for u in units):
        raise ValueError('installed unit differs from manifest')
    new_profile = next_profile(docs['runtime.profile'], bearer_sha256, expires_at_ms, S.now_ms())
    new = next_manifest(old, profile, U.digest(new_profile))
    old_bearer = H.LAUNCH.fields(docs['runtime.profile'])['bearer_digest']
    H.source_path(staged, 'gateway')
    if U.digest(H.private_read(staged, 1002)) != bearer_sha256:
        raise ValueError('staged bearer differs from the supplied digest')
    if U.RELEASE.exists() or ARCHIVE.exists():
        raise ValueError('fresh release and archive required; inspect a prior attempt')
    local_voter_image()
    observation = producer_observation()
    pending_effect25(observation)
    report = dict(old_manifest=OLD_MANIFEST, new_manifest=U.digest(profile.manifest_bytes(new)), new_profile=U.digest(new_profile),
                  bearer_sha256=bearer_sha256, expires_at_ms=expires_at_ms, voters=halted_health(), observation=observation,
                  gateway=S.service_state('gateway')['ActiveState'], signer=S.service_state('signer')['ActiveState'])
    return oldraw, old, new, docs, units, new_profile, old_bearer, report


def release_gateway_guard():
    unit = 'velocita-swap-gateway.service'
    S.move_guard(U.SYSTEMD/(unit+'.d')/U.GUARD_NAME, S.ATTEMPT/(unit+'.guard'))
    U.run(['systemctl', 'daemon-reload'])
    if U.run(['systemctl', 'show', unit, '-p', 'RefuseManualStart', '--value']).strip() != 'no':
        raise ValueError('another guard still prevents the gateway start')
    if U.run(['systemctl', 'show', 'velocita-swap-signer.service', '-p', 'RefuseManualStart', '--value']).strip() != 'yes':
        raise ValueError('signer guard must stay in place')


def wait_for_exit():
    deadline = time.monotonic()+RUN_LIMIT_S
    while time.monotonic() < deadline:
        state = U.run(['systemctl', 'show', 'velocita-swap-gateway.service', '-p', 'ActiveState', '-p', 'ExecMainStatus', '-p', 'ExecMainCode'])
        values = dict(line.split('=', 1) for line in state.splitlines())
        if values['ActiveState'] in ('inactive', 'failed'):
            return values
        time.sleep(5)
    raise ValueError('gateway still running after the recovery limit; stop and inspect')


def recover(base, binary, bearer_sha256, expires_at_ms, staged):
    data, profile = U.payload(base)
    oldraw, old, new, docs, units, new_profile, old_bearer, report = preflight(data, profile, bearer_sha256, expires_at_ms, staged)
    credentials = F.credential_snapshot()
    ARCHIVE.mkdir(mode=0o700)
    U.fresh_write(ARCHIVE/'preflight.json', (json.dumps(report, indent=2)+'\n').encode(), 0o400)
    S.stop_services()
    U.stopped()
    try:
        with M.locked_journal_dirs():
            before = M.journal_bytes()
            U.fresh_write(ARCHIVE/'manifest.json', oldraw, 0o400)
            U.fresh_write(ARCHIVE/'runtime.profile', docs['runtime.profile'], 0o400)
            for unit, raw in units.items():
                U.fresh_write(ARCHIVE/unit, raw, 0o400)
            U.RELEASE.mkdir(mode=0o755)
            os.chmod(U.RELEASE, 0o755)
            for name, raw in data.items():
                U.fresh_write(U.RELEASE/name, raw, 0o555 if name == 'velocita-v2-gateway' else 0o444)
            U.replace(U.PUBLIC/'runtime.profile', docs['runtime.profile'], new_profile)
            for unit, raw in units.items():
                U.replace(U.SYSTEMD/unit, raw, profile.render(new)[unit+'.review'].encode())
            U.replace(U.PUBLIC/'manifest.json', oldraw, profile.manifest_bytes(new))
            U.run(['systemd-analyze', 'verify', '--man=no', *[str(U.SYSTEMD/u) for u in units]])
            U.run(['systemctl', 'daemon-reload'])
            U.verify_guards()
            U.stopped()
            if M.journal_bytes() != before or F.credential_snapshot() != credentials:
                raise ValueError('journal or credential changed during install')
        installed = H.install('gateway', staged, U.digest(new_profile), old_bearer)
        S.ATTEMPT.mkdir(mode=0o700)
        release_gateway_guard()
        halted_health()
        S.driver_stopped()
        U.run(['systemctl', 'start', 'velocita-swap-gateway.service'])
        exit_state = wait_for_exit()
        S.restore_guards()
        after = producer_observation()
        if exit_state.get('ExecMainStatus') != '0':
            raise ValueError('gateway did not complete recovery: '+json.dumps(exit_state))
        resolved_effect25(after)
        result = dict(report, recovered_to=COMMIT, binary_sha256=binary, credential=installed, gateway_exit=exit_state,
                      observation_after=after, signer_started=False, start_guards_retained=True, driver_started=False)
        U.fresh_write(ARCHIVE/'result.json', (json.dumps(result, indent=2)+'\n').encode(), 0o400)
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
    p.add_argument('--bearer-sha256', required=True)
    p.add_argument('--expires-at-ms', type=int, required=True)
    p.add_argument('--staged', type=Path, required=True)
    p.add_argument('--run', action='store_true')
    a = p.parse_args()
    configure(a.binary_sha256)
    lock = U.directory(U.PUBLIC)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if a.run:
            result = recover(a.bundle, a.binary_sha256, a.bearer_sha256, a.expires_at_ms, a.staged)
        else:
            data, profile = U.payload(a.bundle)
            result = dict(preflight(data, profile, a.bearer_sha256, a.expires_at_ms, a.staged)[7], applied=False)
        print(json.dumps(result, sort_keys=True))
    finally:
        os.close(lock)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps(dict(error_type=type(error).__name__, error=str(error)[:300], stop_and_inspect=True, do_not_retry=True)))
        raise SystemExit(1)
