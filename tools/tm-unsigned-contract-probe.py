"""Fixed unsigned DeFi contract probe. Review the October 9 protocol before --run.

No execute route, payload signing, private-key argument, retries or size changes.
Raw responses are private; an exclusive attempt marker prevents replay.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.request

spec = importlib.util.spec_from_file_location('account', Path(__file__).with_name('tm-account-session.py'))
ACCOUNT = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ACCOUNT)
ASSET = '495e07ac-fa3e-4179-85b7-b1e8cece3dc4'
MINT = 'So11111111111111111111111111111111111111112'
ORIGIN = 'https://api.truemarkets.co'


def bodies():
    return [dict(asset_id=ASSET, side=side, type='market', qty=qty, qty_unit=unit)
            for side, qty, unit in [('buy', '1.00', 'quote'), ('sell', '0.0084', 'base')]]


def verify_asset(document):
    rows = document.get('data') if isinstance(document, dict) else None
    if not isinstance(rows, list):
        raise ValueError('catalog shape')
    matches = [r for r in rows if isinstance(r, dict) and r.get('id') == ASSET]
    expected = dict(chain='solana', address=MINT, symbol='SOL', decimals=9,
                    is_active=True, status='active', tradeable=True, venue='defi', type='spot')
    if len(matches) != 1 or any(type(matches[0].get(k)) is not type(v) or matches[0].get(k) != v
                                for k, v in expected.items()):
        raise ValueError('SOL identity is not verified')


def response_summary(raw):
    d = json.loads(raw)
    if not isinstance(d, dict) or d.get('status') != 'initialized':
        raise ValueError('unexpected order status')
    if d.get('issues'):
        raise ValueError('quote reports issues')
    if not isinstance(d.get('order_id'), str) or not d['order_id']:
        raise ValueError('missing initialized order')
    payloads = d.get('payloads')
    if not isinstance(payloads, list) or not 1 <= len(payloads) <= 8:
        raise ValueError('unsigned payloads unavailable or over bound')
    hashes = []
    for p in payloads:
        if not isinstance(p, dict) or not isinstance(p.get('payload'), str) or not p['payload']:
            raise ValueError('payload shape')
        hashes.append(hashlib.sha256(p['payload'].encode()).hexdigest())
    quote = d.get('quote')
    if not isinstance(quote, dict) or quote.get('issues'):
        raise ValueError('quote unavailable or has issues')
    safe = {}
    for key in ('qty', 'qty_out', 'fee'):
        value = quote.get(key)
        if value is not None:
            if not isinstance(value, str) or not re.fullmatch(r'\d{1,40}(?:\.\d{1,40})?', value):
                raise ValueError('quote decimal shape')
            safe[key] = value
    return dict(response_sha256=hashlib.sha256(raw).hexdigest(), status='initialized',
                quote=safe, payload_count=len(payloads), payload_sha256=hashes,
                signed=False, executed=False)


def exchange(body=None, token=None):
    if body is not None and body not in bodies():
        raise ValueError('request outside protocol')
    path = '/v1/gateway/assets' if body is None else '/v1/gateway/orders'
    headers = {'Accept': 'application/json', 'Content-Type': 'application/json',
               'User-Agent': 'velocita-contract-read/1'}
    if body is not None:
        ACCOUNT.MINT._validate_token(token)
        headers['Authorization'] = 'Bearer ' + token
    req = urllib.request.Request(ORIGIN + path, method='GET' if body is None else 'POST',
                                 headers=headers, data=None if body is None else json.dumps(body).encode())
    with urllib.request.build_opener(ACCOUNT.NoRedirect()).open(req, timeout=25) as reply:
        if reply.status != (200 if body is None else 201):
            raise ValueError('unexpected HTTP status')
        raw = reply.read(4_000_001)
    if len(raw) > 4_000_000:
        raise ValueError('response over bound')
    return raw


def private_new(path, raw):
    parent, normalized = ACCOUNT.MINT._open_private_parent(path)
    try:
        fd = os.open(normalized.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=parent)
        with os.fdopen(fd, 'wb') as out:
            out.write(raw)
            out.flush()
            os.fsync(out.fileno())
        os.fsync(parent)
    finally:
        os.close(parent)


def run(session, output):
    parent, normalized = ACCOUNT.MINT._open_private_parent(session)
    try:
        fd = os.open(normalized.name + '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW,
                     0o600, dir_fd=parent)
    finally:
        os.close(parent)
    output.mkdir(mode=0o700, exist_ok=True)
    directory_parent, _ = ACCOUNT.MINT._open_private_parent(output / 'attempt.json')
    os.close(directory_parent)
    parent_fd = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(parent_fd)
    finally:
        os.close(parent_fd)
    with os.fdopen(fd, 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        private_new(output / 'attempt.json', json.dumps({'requests': bodies()}).encode())
        pair, _ = ACCOUNT.session(session, None, False)
        catalog = exchange()
        verify_asset(json.loads(catalog))
        private_new(output / 'catalog.json', catalog)
        summaries = []
        for body in bodies():
            raw = exchange(body, pair['access_token'])
            private_new(output / (body['side'] + '.json'), raw)
            summary = response_summary(raw)
            summary['side'] = body['side']
            summaries.append(summary)
        return summaries


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        print(json.dumps({'proposed_only': True, 'requests': bodies()}, indent=2))
    else:
        try:
            root = Path.home() / '.velocita-credentials/truemarkets'
            print(json.dumps(run(root / 'session.json', root / 'unsigned-contract-20261009'), indent=2))
        except Exception as error:
            code = error.code if isinstance(error, urllib.error.HTTPError) else None
            print(json.dumps({'stopped': True, 'error_type': type(error).__name__, 'http_status': code,
                              'do_not_retry': True}))
            raise SystemExit(1)
