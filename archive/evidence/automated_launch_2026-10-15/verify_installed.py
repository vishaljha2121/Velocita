"""Owner sudo, read-only: the dormant new-incarnation install matches its pins and each credential is readable only by its own role."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import time

PUBLIC = Path('/etc/velocita-swap/public')


def read_caller_key(H, path, uid, expected_gid=None):
    """Use the launcher's no-symlink walk; return bytes only to in-process callers."""
    gid = uid if expected_gid is None else expected_gid
    H.tmpfs(path.parent)
    directory = H.FD._open_components(str(path.parent), uid)
    try:
        meta = os.fstat(directory)
        if not stat.S_ISDIR(meta.st_mode) or (meta.st_uid, meta.st_gid, stat.S_IMODE(meta.st_mode)) != (uid, gid, 0o700):
            raise ValueError('caller directory metadata refused')
    finally:
        os.close(directory)
    fd = H.FD._open_components(str(path), uid)
    try:
        before = os.fstat(fd)
        if (not stat.S_ISREG(before.st_mode) or
                (before.st_uid, before.st_gid, stat.S_IMODE(before.st_mode), before.st_nlink, before.st_size) != (uid, gid, 0o600, 1, 32)):
            raise ValueError('caller key metadata refused')
        raw = os.read(fd, 33)
        after = os.fstat(fd)
        if len(raw) != 32 or (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise ValueError('caller key changed during read')
        return raw
    finally:
        os.close(fd)


def verify_caller_key(H, path, role, run=subprocess.run, expected_gid=None):
    uid, _ = H.ROLES[role]
    read_caller_key(H, path, uid, expected_gid)
    other = 'signer' if role == 'gateway' else 'gateway'
    for reader, expected in ((role, 0), (other, 1)):
        probe = run(['/usr/sbin/runuser', '-u', 'velocita-swap-' + reader, '--', '/usr/bin/test', '-r', str(path)],
                    capture_output=True, timeout=10)
        if probe.returncode != expected:
            kind = 'own-role' if reader == role else 'cross-role'
            raise ValueError(kind + ' readability or probe error')
    return dict(caller_key_validated=True, own_role_can_read=True, other_role_cannot_read=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receiver', type=Path, required=True)
    for name in ('profile-sha256', 'manifest-sha256', 'signer-sha256'):
        p.add_argument('--' + name, required=True)
    a = p.parse_args()
    spec = importlib.util.spec_from_file_location('handoff', a.receiver/'credential_handoff.py')
    H = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(H)
    H.harden()
    H.PROVISION.require_host_root()
    profile = H.LAUNCH.fields(H.LAUNCH.pinned_bytes(str(PUBLIC/'runtime.profile'), a.profile_sha256, 8192))
    m = json.loads(H.LAUNCH.pinned_bytes(str(PUBLIC/'manifest.json'), a.manifest_sha256, 8192))
    if m['launch_authorized'] is not False or m['incarnation'] != 'aae119b95ddcea5a206f03ab5ca12c15':
        raise ValueError('dormant new-incarnation manifest required')
    files = {name: H.LAUNCH.pinned_bytes(str(PUBLIC/name), m[field], limit) for name, field, limit in
             [('runtime.profile', 'runtime_profile_sha256', 8192), ('account.review', 'account_review_sha256', 8192), ('account.history', 'account_history_sha256', 4096)]}
    H.LAUNCH.pinned_bytes(str(PUBLIC/'transaction.review'), profile['transaction_review_digest'], 32768)
    now = time.time_ns()//1_000_000
    for role in H.ROLES:
        H.LAUNCH.check_bindings(m, files['runtime.profile'], files['account.review'], files['account.history'], now, role)
    H.LAUNCH.pinned_bytes(m['binary'], m['binary_sha256'], 64*1024**2)
    pins = {'gateway': profile['bearer_digest'], 'signer': a.signer_sha256}
    results = {}
    for role, (uid, leaf) in H.ROLES.items():
        path = Path('/run/velocita-swap-credentials')/role/leaf
        caller = verify_caller_key(H, path.with_name('caller.key'), role)
        H.tmpfs(path.parent)
        raw = H.private_read(path, uid)
        H.pin(raw, pins[role])
        H.validate_secret(role, raw, profile, now)
        meta = path.stat()
        if meta.st_gid != uid:
            raise ValueError('credential group mismatch')
        other = 'signer' if role == 'gateway' else 'gateway'
        probe = subprocess.run(['/usr/sbin/runuser', '-u', 'velocita-swap-' + other, '--', '/usr/bin/test', '-r', str(path)], capture_output=True, timeout=10)
        if probe.returncode != 1:
            raise ValueError('cross-role readability or probe error')
        unit = 'velocita-swap-' + role + '.service'
        output = subprocess.check_output(['/usr/bin/systemctl', 'show', unit, '-p', 'ActiveState', '-p', 'SubState', '-p', 'UnitFileState'], text=True)
        state = dict(line.split('=', 1) for line in output.splitlines())
        if state != {'ActiveState': 'inactive', 'SubState': 'dead', 'UnitFileState': 'static'}:
            raise ValueError('service state changed')
        H.LAUNCH.pinned_bytes('/etc/systemd/system/' + unit, hashlib.sha256(H.LAUNCH.PROFILE.render(m)[unit + '.review'].encode()).hexdigest(), 32768)
        if list(Path('/var/lib/velocita-swap-' + role + '/recovery').iterdir()):
            raise ValueError('unexpected recovery initialization')
        results[role] = dict(caller=caller, mode=oct(stat.S_IMODE(meta.st_mode)), credential_matches_pin=True, other_role_cannot_read=True, unit_matches_manifest=True,
                             recovery_directory_empty=True, service=state)
    print(json.dumps(dict(verified=True, incarnation=m['incarnation'], source_commit=m['source_commit'], profile_sha256=a.profile_sha256,
                          manifest_sha256=a.manifest_sha256, roles=results, account_review_expires_ms=int(H.LAUNCH.fields(files['account.review'])['expires_at_ms']),
                          launch_authorized=False, services_started=False), indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print(json.dumps(dict(verified=False, error_type=type(e).__name__, error=str(e)[:200])))
        raise SystemExit(1)
