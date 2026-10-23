"""One fixed unsigned SOL capture. Review the protocol for the selected side.

Default prints the proposal only. No signing, execution, key loading or retries.
At most one session renewal followed by one fixed DeFi create-order request.
Sell: ROUTE_A_CAPTURE_PROTOCOL_2026-10-12.md.
Buy (--buy): SPONSORED_BUY_PROTOCOL_2026-10-12.md; separate approval required.
"""
import argparse
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import signal
import time
import urllib.error
import urllib.request

SPEC = importlib.util.spec_from_file_location('limit_contract', Path(__file__).with_name('tm-sol-limit-contract-probe.py'))
CONTRACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTRACT)
ACCOUNT = CONTRACT.ACCOUNT
LIMIT = CONTRACT.LIMIT


def body(*, buy=False):
    return dict(base_asset='So11111111111111111111111111111111111111112',
                chain='solana', side='buy' if buy else 'sell', type='market',
                qty='1.00' if buy else '0.0084', qty_unit='quote' if buy else 'base')


def exchange(path, document, token=None):
    headers = {'Accept': 'application/json', 'Content-Type': 'application/json',
               'User-Agent': 'velocita-contract-read/1'}
    if path == '/v1/gateway/orders' and document in (body(), body(buy=True)):
        ACCOUNT.MINT._validate_token(token)
        headers['Authorization'] = 'Bearer ' + token
    elif (path == '/v1/auth/token/refresh' and token is None and isinstance(document, dict)
          and set(document) == {'refresh_token'}):
        ACCOUNT.MINT._validate_token(document['refresh_token'])
    else:
        raise ValueError('outside fixed protocol')
    request = urllib.request.Request(CONTRACT.PROBE.ORIGIN + path, method='POST', headers=headers,
                                     data=json.dumps(document, separators=(',', ':')).encode())
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), ACCOUNT.NoRedirect())
    try:
        response = opener.open(request, timeout=20)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read(LIMIT + 1)
        if len(raw) > LIMIT:
            raise ValueError('response bound exceeded')
        return response.code, raw


def run(session, output, catalog_raw, send=exchange, *, buy=False):
    CONTRACT.verify_pinned_catalog(catalog_raw)
    parent, normalized = ACCOUNT.MINT._open_private_parent(session)
    try:
        fd = os.open(normalized.name + '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd, 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        pair, expiry = ACCOUNT.token_pair(ACCOUNT.read_private(session), require_future=False)
        output.mkdir(mode=0o700, exist_ok=True)
        parent, _ = ACCOUNT.MINT._open_private_parent(output / 'attempt.json')
        os.close(parent)
        parent = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
        write = CONTRACT.PROBE.private_new
        request_body = body(buy=buy)
        write(output / 'attempt.json', json.dumps(dict(request=request_body, max_refresh=1, max_create=1)).encode())
        write(output / 'catalog.json', catalog_raw)
        if expiry <= time.time() + 120:
            status, raw = send('/v1/auth/token/refresh', {'refresh_token': pair['refresh_token']})
            write(output / 'renewal-http-status.json', json.dumps(dict(http_status=status)).encode())
            write(output / 'renewal-response.json', raw)
            if status != 200:
                raise ValueError('session renewal refused; stop without quote')
            pair, expiry = ACCOUNT.token_pair(json.loads(raw))
            if expiry <= time.time() + 120:
                raise ValueError('renewed session too short; stop without quote')
            ACCOUNT.MINT._atomic_write_token(session, json.dumps(pair))
        status, raw = send('/v1/gateway/orders', request_body, pair['access_token'])
        write(output / 'http-status.json', json.dumps(dict(http_status=status)).encode())
        write(output / 'response.json', raw)
        result = CONTRACT.summarize(status, raw)
        write(output / 'summary.json', json.dumps(result).encode())
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--buy', action='store_true', help='fixed 1.00 USDC unsigned buy; separate protocol approval')
    args = parser.parse_args()
    if not args.run:
        print(json.dumps(dict(proposed_only=True, request=body(buy=args.buy), max_session_refresh=1,
                              max_create_order=1, signed=False, execute=False, retries=0), indent=2))
        return 0
    signal.signal(signal.SIGALRM, CONTRACT.expired)
    signal.alarm(60)
    try:
        root = Path.home() / '.velocita-credentials/truemarkets'
        catalog = CONTRACT.load_pinned_catalog(root / 'unsigned-contract-20261009/catalog.json')
        directory = 'route-a-buy-20261012' if args.buy else 'route-a-sell-20261012'
        result = run(root / 'session.json', root / directory, catalog, buy=args.buy)
        print(json.dumps(result, indent=2))
        return 0
    except Exception as error:
        print(json.dumps(dict(stopped=True, error_type=type(error).__name__, do_not_retry=True)))
        return 1
    finally:
        signal.alarm(0)


if __name__ == '__main__':
    raise SystemExit(main())
