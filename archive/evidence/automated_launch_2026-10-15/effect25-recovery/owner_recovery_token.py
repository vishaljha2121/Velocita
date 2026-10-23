"""Owner Mac: stage the access token of a private TM session into NUC1 owner tmpfs and print only its digest and expiry."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SESSION = load('recovery_session', ROOT/'tools/tm-account-session.py')
STAGE = load('recovery_stage', ROOT/'tools/tm-stage-swap-credential.py')
MARGIN_MS = 120_000


def token(path):
    pair, expiry = SESSION.token_pair(SESSION.read_private(path))
    raw = pair['access_token'].encode('ascii')
    return raw, int(expiry*1000)-MARGIN_MS


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--session', type=Path, required=True)
    p.add_argument('--receiver', required=True)
    p.add_argument('--receiver-sha256', required=True)
    p.add_argument('--send', action='store_true')
    a = p.parse_args()
    raw, expires_at_ms = token(a.session)
    report = dict(bearer_sha256=hashlib.sha256(raw).hexdigest(), expires_at_ms=expires_at_ms, staged=None)
    if a.send:
        report['staged'] = STAGE.send('gateway', raw, a.receiver, a.receiver_sha256)['staged_path']
    print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
