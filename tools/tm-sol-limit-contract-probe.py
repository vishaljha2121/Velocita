"""One unsigned SOL limit request, gated by SOL_LIMIT_PROBE_PROTOCOL.md. Default is a dry run."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import time
import urllib.error
import urllib.request


spec = importlib.util.spec_from_file_location('unsigned_probe', Path(__file__).with_name('tm-unsigned-contract-probe.py'))
PROBE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PROBE)
ACCOUNT = PROBE.ACCOUNT
LIMIT = 4_000_000
PINNED_CATALOG_SHA256 = '98f9f84a3ef162ea014b09bc4d925502d88d9a56d88e7c8b9e9202ba8424c895'


def body():
    return dict(asset_id=PROBE.ASSET, chain='solana', side='buy', type='limit',
                qty='0.01', qty_unit='base', price='100.00')


def exchange(method, path, token=None, document=None):
    if (method, path, document) == ('GET', '/v1/gateway/assets', None) and token is None:
        headers = {'Accept': 'application/json'}
        data = None
    elif method == 'POST' and path == '/v1/gateway/orders' and document == body():
        ACCOUNT.MINT._validate_token(token)
        headers = {'Accept': 'application/json', 'Content-Type': 'application/json',
                   'Authorization': 'Bearer ' + token}
        data = json.dumps(document, separators=(',', ':')).encode()
    else:
        raise ValueError('outside fixed request protocol')
    headers['User-Agent'] = 'velocita-contract-read/1'
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), ACCOUNT.NoRedirect())
    request = urllib.request.Request(PROBE.ORIGIN + path, data=data, headers=headers, method=method)
    try:
        response = opener.open(request, timeout=20)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read(LIMIT + 1)
        if len(raw) > LIMIT:
            raise ValueError('response bound exceeded')
        return response.code, raw


def summarize(status, raw):
    result = dict(http_status=status, response_sha256=hashlib.sha256(raw).hexdigest(),
                  signed=False, execute_request_sent=False, do_not_retry=True)
    if status != 201:
        return dict(result, verdict='http_response_requires_private_review')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('order response shape')
    state = value.get('status')
    if state not in {'initialized', 'pending', 'cancel_pending', 'complete', 'canceled', 'active', 'failed'}:
        raise ValueError('unknown order status; reconcile before another attempt')
    result['status'] = state
    if state != 'initialized':
        return dict(result, verdict='unexpected_status_requires_reconciliation')
    payloads = value.get('payloads', [])
    if not isinstance(payloads, list) or len(payloads) > 8:
        raise ValueError('payload bound or shape')
    hashes = []
    for item in payloads:
        if not isinstance(item, dict) or not isinstance(item.get('payload'), str) or not item['payload']:
            raise ValueError('payload shape')
        digest = hashlib.sha256(item['payload'].encode()).hexdigest()
        if item.get('digest') != digest:
            raise ValueError('payload digest mismatch')
        hashes.append(digest)
    result.update(payload_count=len(hashes), payload_sha256=hashes)
    quote = value.get('quote')
    if quote is not None and not isinstance(quote, dict):
        raise ValueError('quote shape')
    result['issues_present'] = bool(value.get('issues') or (quote or {}).get('issues'))
    result['quote'] = {}
    for key in ('qty', 'qty_out', 'fee', 'price'):
        field = (quote or {}).get(key)
        if field is not None:
            if not isinstance(field, str) or not re.fullmatch(r'\d{1,40}(?:\.\d{1,40})?', field):
                raise ValueError('quote decimal shape')
            result['quote'][key] = field
    has_order = isinstance(value.get('order_id'), str) and bool(value['order_id'])
    accepted = has_order and bool(hashes) and not result['issues_present']
    return dict(result, verdict='initialized_unsigned_requires_semantic_review' if accepted
                else 'inconclusive_response_requires_private_review')


def verify_pinned_catalog(raw):
    if len(raw) > LIMIT or hashlib.sha256(raw).hexdigest() != PINNED_CATALOG_SHA256:
        raise ValueError('pinned catalog digest mismatch')
    document = json.loads(raw)
    PROBE.verify_asset(document)


def load_pinned_catalog(path):
    parent, normalized = ACCOUNT.MINT._open_private_parent(path)
    try:
        fd = os.open(normalized.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd, 'rb') as stream:
        raw = stream.read(LIMIT + 1)
    verify_pinned_catalog(raw)
    return raw


def run(session, output, send=exchange, *, catalog_raw=None):
    if catalog_raw is not None:
        verify_pinned_catalog(catalog_raw)
    parent, normalized = ACCOUNT.MINT._open_private_parent(session)
    try:
        fd = os.open(normalized.name + '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd, 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        pair, expiry = ACCOUNT.token_pair(ACCOUNT.read_private(session))
        if expiry <= time.time() + 120:
            raise ValueError('session needs separate renewal; no network request made')
        output.mkdir(mode=0o700, exist_ok=True)
        parent, _ = ACCOUNT.MINT._open_private_parent(output / 'attempt.json')
        os.close(parent)
        parent = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
        PROBE.private_new(output / 'attempt.json', json.dumps({'request': body()}).encode())
        if catalog_raw is None:
            code, raw = send('GET', '/v1/gateway/assets')
            PROBE.private_new(output / 'catalog-http-status.json', json.dumps({'phase': 'catalog', 'http_status': code}).encode())
            PROBE.private_new(output / 'catalog.json', raw)
            if code != 200:
                raise ValueError('catalog request refused')
            PROBE.verify_asset(json.loads(raw))
        else:
            PROBE.private_new(output / 'catalog.json', catalog_raw)
        code, raw = send('POST', '/v1/gateway/orders', pair['access_token'], body())
        PROBE.private_new(output / 'http-status.json', json.dumps({'phase': 'create_order', 'http_status': code}).encode())
        PROBE.private_new(output / 'response.json', raw)
        result = summarize(code, raw)
        PROBE.private_new(output / 'summary.json', json.dumps(result).encode())
        return result


def expired(signum, frame):
    raise TimeoutError('bounded capture expired; do not retry')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--use-pinned-sol-catalog', action='store_true')
    args = parser.parse_args()
    if not args.run:
        print(json.dumps(dict(proposed_only=True, request=body(), refresh=False, execute=False,
                             catalog='pinned-20261009' if args.use_pinned_sol_catalog else 'public-get'), indent=2))
        return 0
    signal.signal(signal.SIGALRM, expired)
    signal.alarm(60)
    try:
        root = Path.home() / '.velocita-credentials/truemarkets'
        raw = load_pinned_catalog(root / 'unsigned-contract-20261009/catalog.json') if args.use_pinned_sol_catalog else None
        directory = 'sol-limit-pinned-20261012' if args.use_pinned_sol_catalog else 'sol-limit-contract-20261011'
        print(json.dumps(run(root / 'session.json', root / directory, catalog_raw=raw), indent=2))
        return 0
    except Exception as error:
        print(json.dumps(dict(stopped=True, error_type=type(error).__name__, do_not_retry=True)))
        return 1
    finally:
        signal.alarm(0)


if __name__ == '__main__':
    raise SystemExit(main())
