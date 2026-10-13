"""Operator API authentication and balance reads; no trading endpoints.

Use --key once to initialize a private --session file. Later balance checks
reuse it or rotate its refresh token. This is not the live gateway transport.
"""
import argparse
import base64
from datetime import datetime, timezone
from decimal import Decimal, localcontext
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

spec = importlib.util.spec_from_file_location('mint', Path(__file__).with_name('mint-token.py'))
MINT = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MINT)
ORIGIN = 'https://api.truemarkets.co'
ALLOWED = {('POST', '/v1/auth/api-key/token'), ('POST', '/v1/auth/token/refresh'),
           ('GET', '/v1/gateway/balances')}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('redirect refused')


def request(method, path, body=None, token=None):
    if (method, path) not in ALLOWED:
        raise ValueError('endpoint refused')
    headers = {'Accept': 'application/json', 'Content-Type': 'application/json',
               'User-Agent': 'velocita-account-read/1'}
    if token is not None:
        MINT._validate_token(token)
        headers['Authorization'] = 'Bearer ' + token
    req = urllib.request.Request(ORIGIN + path, method=method, headers=headers,
                                 data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.build_opener(NoRedirect()).open(req, timeout=25) as response:
            if response.status != 200:
                raise ValueError('unexpected HTTP status')
            raw = response.read(1048577)
    except urllib.error.HTTPError as error:
        raise ValueError('API HTTP ' + str(error.code)) from None
    if len(raw) > 1048576:
        raise ValueError('response exceeds bound')
    return json.loads(raw)


def read_private(path):
    parent, normalized = MINT._open_private_parent(path)
    try:
        fd = os.open(normalized.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd) as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('private file ownership or permissions invalid')
        raw = stream.read(16385)
        if len(raw) > 16384:
            raise ValueError('private file exceeds bound')
        return json.loads(raw)


def token_pair(document, require_future=True):
    if not isinstance(document, dict):
        raise ValueError('invalid token response')
    keys = ('access_token', 'refresh_token', 'expires_in', 'token_type')
    if any(not isinstance(document.get(k), str) for k in keys):
        raise ValueError('missing token fields')
    if document['token_type'] != 'Bearer':
        raise ValueError('invalid token type')
    for name in ('access_token', 'refresh_token'):
        MINT._validate_token(document[name])
    expiry = datetime.fromisoformat(document['expires_in'].replace('Z', '+00:00'))
    if expiry.tzinfo is None or (require_future and expiry.timestamp() <= time.time() + 60):
        raise ValueError('invalid token expiry')
    return {k: document[k] for k in keys}, expiry.timestamp()


def mint(key_path, scratch_parent):
    material = read_private(key_path)
    jwk = material.get('private_key', {})
    if material.get('algorithm') != 'ES256' or jwk.get('kty') != 'EC' or jwk.get('crv') != 'P-256':
        raise ValueError('unsupported key format')
    key_id = material.get('key_id')
    if not isinstance(key_id, str) or not re.fullmatch(r'[0-9a-fA-F-]{36}', key_id):
        raise ValueError('invalid key id')
    timestamp = int(time.time())
    with tempfile.TemporaryDirectory(dir=scratch_parent) as directory:
        pem = Path(directory) / 'key.pem'
        fd = os.open(pem, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(MINT.jwk_to_pem(jwk))
        signed = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(pem)],
                                input=f'{key_id}.{timestamp}'.encode(),
                                capture_output=True, check=True, timeout=10)
    signature = base64.urlsafe_b64encode(MINT.der_signature_to_raw(signed.stdout)).decode().rstrip('=')
    return request('POST', '/v1/auth/api-key/token',
                   dict(key_id=key_id, timestamp=timestamp, signature=signature))


def session(path, key_path, force_refresh):
    try:
        pair, expiry = token_pair(read_private(path), require_future=False)
    except FileNotFoundError:
        if key_path is None:
            raise ValueError('initialization requires --key') from None
        pair, _ = token_pair(mint(key_path, path.parent))
        MINT._atomic_write_token(path, json.dumps(pair))
        return pair, 'mint'
    if force_refresh or expiry <= time.time() + 300:
        pair, _ = token_pair(request('POST', '/v1/auth/token/refresh',
                                     {'refresh_token': pair['refresh_token']}))
        MINT._atomic_write_token(path, json.dumps(pair))
        return pair, 'refresh'
    return pair, 'reuse'


def balances(document):
    if not isinstance(document, dict) or not isinstance(document.get('data'), list):
        raise ValueError('unrecognized balance response')
    rows = []
    for row in document['data']:
        projected = {}
        for k in ('symbol', 'chain', 'venue'):
            v = row.get(k)
            if v is not None and (not isinstance(v, str) or not re.fullmatch(r'[A-Za-z0-9._-]{1,40}', v)):
                raise ValueError('invalid balance label')
            projected[k] = v
        for k in ('total', 'available', 'held'):
            v = row.get(k)
            if not isinstance(v, str) or not re.fullmatch(r'\d{1,40}(?:\.\d{1,40})?', v):
                raise ValueError('invalid balance amount')
            projected[k] = v
        with localcontext() as context:
            context.prec = 100
            if Decimal(projected['available']) + Decimal(projected['held']) != Decimal(projected['total']):
                raise ValueError('inconsistent balance')
        rows.append(projected)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--key', type=Path)
    parser.add_argument('--refresh', action='store_true')
    args = parser.parse_args()
    parent, normalized = MINT._open_private_parent(args.session)
    try:
        lock = os.open(normalized.name + '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(lock, 'w') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        pair, operation = session(args.session, args.key, args.refresh)
        summary = balances(request('GET', '/v1/gateway/balances', token=pair['access_token']))
        print(json.dumps(dict(checked_at=datetime.now(timezone.utc).isoformat(),
                              authentication=operation, expires_at=pair['expires_in'],
                              balances=summary, orders_sent=False), indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('Account read stopped; error type: ' + type(error).__name__)
        raise SystemExit(1)
