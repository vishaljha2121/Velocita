#!/usr/bin/env python3
"""Pinned startup mapping; checks by default, launch only with operator authorization.

No install, token refresh, funding initialization, retry, or admission-gate bypass.
Install this bundle root-owned alongside the exact pinned release. Do not run it
from a user-writable checkout as a production service.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fd_handoff as FD
import service_profile as PROFILE


REVIEW_LIFETIME_MS = 86_400_000

def strict_json(raw):
    def pairs(items):
        result = {}
        for name, value in items:
            if name in result: raise FD.Refused('duplicate JSON field')
            result[name] = value
        return result
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(FD.Refused('nonfinite JSON')))


def pinned_bytes(path, digest, limit):
    fd = FD.open_pinned_public(path, digest, limit=limit)
    try:
        raw = os.read(fd, limit + 1)
        if not raw or len(raw) > limit or hashlib.sha256(raw).hexdigest() != digest:
            raise FD.Refused('public artifact changed while reading')
        return raw
    finally:
        os.close(fd)


def fields(raw):
    if not raw or len(raw) > 8192: raise FD.Refused('bounded profile required')
    result = {}
    for line in raw.decode('ascii').splitlines():
        name, equal, value = line.partition('=')
        if not equal or not name or not value or name in result or '\x00' in line:
            raise FD.Refused('profile field shape')
        result[name] = value
    return result


def unsigned(value):
    if not isinstance(value, str) or not value.isascii() or not value.isdecimal() or int(value) > 2**64-1:
        raise FD.Refused('unsigned decimal required')
    return int(value)


def check_bindings(manifest, profile_raw, review_raw, history_raw, now_ms, role):
    """Cross-file deployment pins; C++ remains authoritative for economics/recovery."""
    profile, review, history = fields(profile_raw), fields(review_raw), strict_json(history_raw)
    same = {'schema': '1', 'incarnation': hashlib.sha256(bytes.fromhex(manifest['incarnation'])).hexdigest(),
            'gateway_uid': str(manifest['gateway_uid']), 'signer_uid': str(manifest['signer_uid']),
            'gateway_caller': str(manifest['gateway_caller']), 'signer_caller': str(manifest['signer_caller']),
            'socket': '/run/velocita-swap-signer/sign.sock',
            'account_review_digest': manifest['account_review_sha256']}
    if any(profile.get(k) != v for k, v in same.items()):
        raise FD.Refused('runtime profile disagrees with service identity')
    if review.get('schema') != '1' or review.get('history_evidence') != manifest['account_history_sha256'] or review.get('user_id') != profile.get('user_id'):
        raise FD.Refused('account review/history/user mismatch')
    owner = bytes.fromhex(profile['owner_wallet'])
    if len(owner) != 32 or not any(owner) or not isinstance(history, dict) or history.get('owner_sha256') != hashlib.sha256(owner).hexdigest() or history.get('user_id_sha256') != hashlib.sha256(profile['user_id'].encode()).hexdigest():
        raise FD.Refused('history account identity mismatch')
    start, end = unsigned(review['reviewed_at_ms']), unsigned(review['expires_at_ms'])
    captured = history.get('capture_finished_ms')
    if not 0 < start < end or end-start > REVIEW_LIFETIME_MS or type(captured) is not int or not 0 < captured <= start or start-captured > REVIEW_LIFETIME_MS:
        raise FD.Refused('review/capture time bounds')
    review_current = start <= now_ms < end and captured <= now_ms <= captured+REVIEW_LIFETIME_MS
    if role == 'signer' and not review_current:
        raise FD.Refused('signer account review expired')
    if role == 'gateway' and not now_ms < unsigned(profile['expires_at_ms']):
        raise FD.Refused('gateway bearer expired')
    return {'review_current': review_current, 'economic_admission_checked': False}


def directory(path, uid, gid, mode):
    fd = FD._open_components(path, uid)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISDIR(meta.st_mode) or meta.st_uid != uid or meta.st_gid != gid or stat.S_IMODE(meta.st_mode) != mode:
            raise FD.Refused('private state/socket directory ownership or mode mismatch')
    finally: os.close(fd)


def identity(manifest, role):
    uid = manifest[role + '_uid']
    gid = manifest['socket_gid'] if role == 'signer' else manifest['gateway_gid']
    allowed_groups = {gid, manifest['socket_gid']}
    if os.getuid() != uid or os.geteuid() != uid or os.getgid() != gid or os.getegid() != gid or not set(os.getgroups()) <= allowed_groups:
        raise FD.Refused('service UID/GID/groups mismatch')
    if role == 'gateway' and manifest['socket_gid'] not in set(os.getgroups()) | {gid}:
        raise FD.Refused('gateway lacks signer socket group')
    return uid, gid


def service_context(role):
    if sys.platform != 'linux': raise FD.Refused('production launch requires Linux')
    expected = '/system.slice/velocita-swap-' + role + '.service'
    if ('0::' + expected) not in Path('/proc/self/cgroup').read_text().splitlines():
        raise FD.Refused('launch must run in its configured system service')
    status = dict(line.split(':', 1) for line in Path('/proc/self/status').read_text().splitlines() if ':' in line)
    if int(status['CapEff'].strip(), 16) or int(status['CapBnd'].strip(), 16) or status['NoNewPrivs'].strip() != '1':
        raise FD.Refused('service privilege restrictions missing')


def runtime_identity(manifest):
    """The immutable image userspace must match the one selected by the unit."""
    marker = (manifest['runtime_image_sha256'] + '\n').encode('ascii')
    actual = pinned_bytes('/etc/velocita-runtime-image', hashlib.sha256(marker).hexdigest(), 65)
    if actual != marker:
        raise FD.Refused('runtime image identity mismatch')


def prepare(manifest, role, read=pinned_bytes, now_ms=None):
    PROFILE.validate(manifest)
    if role not in ('gateway', 'signer'): raise FD.Refused('fixed role required')
    identity(manifest, role)
    runtime_identity(manifest)
    contract = PROFILE.launch_contract(manifest, role)
    data = {name: read(PROFILE.PUBLIC + '/' + name, manifest[field], limit)
            for name, field, limit in (
                ('runtime.profile', 'runtime_profile_sha256', 8192),
                ('account.review', 'account_review_sha256', 8192),
                ('account.history', 'account_history_sha256', 4096))}
    result = check_bindings(manifest, data['runtime.profile'], data['account.review'], data['account.history'],
                            time.time_ns()//1000000 if now_ms is None else now_ms, role)
    transaction_digest = fields(data['runtime.profile']).get('transaction_review_digest')
    if transaction_digest is not None:
        if len(transaction_digest) != 64 or any(c not in '0123456789abcdef' for c in transaction_digest) or transaction_digest == '0'*64:
            raise FD.Refused('transaction review digest shape')
        path = PROFILE.PUBLIC + '/transaction.review'
        read(path, transaction_digest, 32768)
        contract['public_files_sha256'][path] = transaction_digest
    binary_fd = FD.open_pinned_public(manifest['binary'], manifest['binary_sha256'], True)
    try:
        if os.read(binary_fd, 4) != b'\x7fELF': raise FD.Refused('native ELF release required')
    finally: os.close(binary_fd)
    catalog_fd = FD.open_pinned_public(PROFILE.PUBLIC + '/catalog.bin', manifest['catalog_sha256'])
    os.close(catalog_fd)
    uid, gid = identity(manifest, role)
    state = contract['state_directory']['path']
    for path in (state, state+'/recovery', state+'/metrics'):
        directory(path, uid, gid, 0o700)
    directory('/run/velocita-swap-'+role, uid, gid, 0o750 if role == 'signer' else 0o700)
    result.update(role=role, source_commit=manifest['source_commit'], launch_authorized=manifest['launch_authorized'],
                  secrets_opened=False, live_admitted=False)
    return contract, result


def launch(manifest, role):
    PROFILE.validate(manifest)
    if role not in ('gateway', 'signer'): raise FD.Refused('fixed role required')
    if manifest['launch_authorized'] is not True:
        raise FD.Refused('operator has not authorized launch; no credential opened')
    if role=='signer' and manifest.get('recovery_only',False):
        raise FD.Refused('recovery cannot start signer')
    service_context(role)
    contract, _ = prepare(manifest, role)
    transaction_pin = contract['public_files_sha256'].get(PROFILE.PUBLIC+'/transaction.review')
    if (not isinstance(transaction_pin, str) or re.fullmatch('[0-9a-f]{64}', transaction_pin) is None or
        transaction_pin == '0'*64):
        raise FD.Refused('reviewed transaction review required before credential access')
    caller = FD.open_secret('/etc/velocita-swap/'+role+'/caller.key', manifest[role+'_uid'])
    os.close(caller)
    secret = FD.open_secret(contract['secret_fd']['path'], manifest[role+'_uid'])
    try:
        FD.exec_handoff(manifest['binary'], contract['argv'], secret,
            {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'AERON_TERM_BUFFER_LENGTH': '1048576',
             'TMPDIR': '/run/velocita-swap-'+role})
    finally:
        try: os.close(secret)
        except OSError: pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--role', choices=('gateway','signer'), required=True)
    parser.add_argument('--launch', action='store_true')
    args = parser.parse_args()
    manifest = strict_json(pinned_bytes(args.manifest, args.manifest_sha256, 16384))
    if args.launch: launch(manifest, args.role)
    else: print(json.dumps(prepare(manifest, args.role)[1]))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, KeyError, TypeError):
        print('REFUSED: startup pins, authorization or protected resources failed validation', file=sys.stderr)
        raise SystemExit(64)
