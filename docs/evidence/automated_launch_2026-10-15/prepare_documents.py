"""Owner Mac: bind the reviewed profile to the new incarnation and build the cash-only opening and adoption bundle; no network."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
PRIVATE = Path.home()/'.velocita-credentials/truemarkets'
OLD = 'b79e65be1ea14e219016ec48ea02efef'
DAY_MS = 86_400_000


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


S = load('stage', ROOT/'tools/tm-stage-swap-credential.py')
A = load('adopt', ROOT/'deploy/v2/swap-runtime/adopt_account.py')


def canonical(d):
    return (json.dumps(d, sort_keys=True, separators=(',', ':')) + '\n').encode()


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def document(d):
    return ''.join(f'{k}={d[k]}\n' for k in sorted(d)).encode()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def write(path, raw):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'wb') as out:
        out.write(raw)
        out.flush()
        os.fsync(out.fileno())


def profile_for(previous, incarnation, authority_domain, key_id, setup, history_bytes, end):
    p = dict(previous)
    require(p['incarnation'] == sha(bytes.fromhex(OLD)), 'previous profile must be the b79e65be profile')
    session = dict(schema=1, setup_sha256=sha(setup), history_sha256=sha(history_bytes), bearer_sha256=sha(key_id.encode()))
    signer = dict(schema=1, incarnation=incarnation, uid=29002, caller=23, custody=p['custody'], key_version=1)
    p.update(incarnation=sha(bytes.fromhex(incarnation)), authority_domain=authority_domain, session_evidence=sha(canonical(session)),
             bearer_digest=sha(key_id.encode()), expires_at_ms=str(end), signer_instance=sha(canonical(signer)),
             account_review_digest=sha(b'input-template-only-builder-replaces-this'))
    return p, session, signer


def opening_parameters(now, end):
    return document(dict(schema=1, now_ms=now, review_expires_ms=end, managed_quote_atoms=25_000_000, kill_epoch=1, executor_generation=1,
                         max_buy_price_per_sol=200_000_000, min_sell_price_per_sol=20_000_000))


def main():
    a = argparse.ArgumentParser(description=__doc__)
    for name in ('history', 'previous-profile', 'transaction-review', 'dormant-manifest', 'builder', 'output'):
        a.add_argument('--' + name, type=Path, required=True)
    a.add_argument('--incarnation', required=True)
    a.add_argument('--authority-domain', required=True)
    args = a.parse_args()
    S.H.harden()
    now = time.time_ns()//1_000_000
    require(len(args.incarnation) == 32 and args.incarnation != OLD and len(args.authority_domain) == 64, 'new incarnation and domain required')
    history_bytes = (args.history/'baseline-cut.json').read_bytes()
    cut = S.SESSION.read_private(args.history/'baseline-cut.json')
    require(cut['stable_window'] and cut['pagination_exhausted'] and not cut['blocking_orders'] and not cut['unsupported_accounts'], 'history refused')
    setup = (args.history/'response-001.json').read_bytes()
    user, owner = S.HISTORY.identity(json.loads(setup))
    require(sha(setup) == cut['setup_sha256'] and sha(S.HISTORY.key_bytes(owner)) == cut['owner_sha256'], 'setup evidence')
    previous = S.H.LAUNCH.fields(args.previous_profile.read_bytes())
    require(previous['user_id'] == user and previous['owner_wallet'] == S.HISTORY.key_bytes(owner).hex(), 'history is for another account')
    transaction = args.transaction_review.read_bytes()
    require(sha(transaction) == previous['transaction_review_digest'], 'transaction review differs from the profile')
    key_id = S.api_key_id(S.SESSION.read_private(PRIVATE/'api-key.json'))
    end = min(cut['capture_finished_ms'] + DAY_MS - 60_000, now + DAY_MS - 60_000)
    require(end > now + 3_600_000, 'history too old for a useful window; capture a fresh cut')
    profile, session, signer = profile_for(previous, args.incarnation, args.authority_domain, key_id, setup, history_bytes, end)
    dormant = json.loads(args.dormant_manifest.read_bytes())
    require(dormant['incarnation'] == args.incarnation and dormant['launch_authorized'] is False, 'dormant manifest for the new incarnation required')
    args.output.mkdir(mode=0o700)
    for name, raw in {'profile.template': document(profile), 'transaction.review': transaction, 'opening.parameters': opening_parameters(now, end),
                      'session.evidence': canonical(session), 'signer-instance.evidence': canonical(signer)}.items():
        write(args.output/name, raw)
    subprocess.run([str(args.builder), 'live-opening', '--runtime-profile', str(args.output/'profile.template'), '--account-history', str(args.history/'baseline-cut.json'),
                    '--transaction-review', str(args.output/'transaction.review'), '--parameters', str(args.output/'opening.parameters'),
                    '--output', str(args.output/'opening')], check=True)
    files = {name: (args.output/'opening'/name).read_bytes() for name in A.DOCUMENTS}
    manifest = dict(dormant)
    for name, key in A.DOCUMENTS.items():
        if key:
            manifest[key] = sha(files[name])
    A.validate(dormant, manifest, files, time.time_ns()//1_000_000)
    bundle = args.output/'adoption'
    bundle.mkdir(mode=0o700)
    files.update({'previous-manifest.json': A.PROFILE.manifest_bytes(dormant), 'manifest.json': A.PROFILE.manifest_bytes(manifest)})
    for name, raw in files.items():
        write(bundle/name, raw)
    spec = canonical(dict(schema=1, files={name: sha(raw) for name, raw in files.items()}))
    write(bundle/'adoption.json', spec)
    reviewed = S.H.LAUNCH.fields(files['runtime.profile'])
    for role in ('gateway', 'signer'):
        S.material(role, PRIVATE/'api-key.json', reviewed)
    summary = dict(adoption_sha256=sha(spec), runtime_profile_sha256=manifest['runtime_profile_sha256'], review_expires_ms=end,
                   opening_record_sha256=sha((args.output/'opening/opening.record').read_bytes()), managed_quote_atoms=25_000_000,
                   history_base_atoms=cut['base_atoms'], history_quote_atoms=cut['quote_atoms'], gateway_credential='api key id',
                   ledger_initialized=False, services_started=False)
    write(args.output/'summary.json', canonical(summary))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
