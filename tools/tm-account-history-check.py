"""Bounded, private, read-only SOL/USDC account-history evidence capture."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import stat
import time
import urllib.parse
import urllib.request

spec = importlib.util.spec_from_file_location('bootstrap', Path(__file__).with_name('tm-account-bootstrap-check.py'))
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)
TM = B.TM
RPC = 'https://api.mainnet-beta.solana.com'
TOKEN = 'TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA'
TOKEN22 = 'TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb'
USDC = 'EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v'
RPC_METHODS = {'getTokenAccountsByOwner', 'getSignaturesForAddress', 'getTransaction', 'getMultipleAccounts'}
MAX_BYTES, PAGE, MAX_PAGES, MAX_ACCOUNTS, MAX_TX = 1048576, 20, 3, 8, 40
ALPHABET = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'


def key(value, size=32, allow_zero=False):
    if not isinstance(value, str) or not value or len(value) > 90:
        raise ValueError('invalid base58 identity')
    number = 0
    for ch in value:
        if ch not in ALPHABET:
            raise ValueError('invalid base58 identity')
        number = number * 58 + ALPHABET.index(ch)
    raw = b'\0' * (len(value) - len(value.lstrip('1'))) + (number.to_bytes((number.bit_length()+7)//8, 'big') if number else b'')
    if len(raw) != size or (not allow_zero and not any(raw)):
        raise ValueError('invalid base58 size')
    return value


def key_bytes(value, allow_zero=False):
    key(value, allow_zero=allow_zero)
    number = 0
    for ch in value:
        number = number * 58 + ALPHABET.index(ch)
    return number.to_bytes(32, 'big')


def strict(raw):
    def pairs(items):
        result = {}
        for k, v in items:
            if k in result:
                raise ValueError('duplicate JSON member')
            result[k] = v
        return result
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON')))


def integer(value):
    if type(value) is not int or not 0 <= value <= 2**64-1:
        raise ValueError('integer out of bounds')
    return value


def atoms(value, decimals):
    if not isinstance(value, str) or not re.fullmatch(r'(0|[1-9][0-9]{0,19})(\.[0-9]{1,'+str(decimals)+r'})?', value):
        raise ValueError('invalid decimal amount')
    whole, _, fraction = value.partition('.')
    result = int(whole) * (10**decimals) + int(fraction.ljust(decimals, '0'))
    if result > 2**64-1:
        raise ValueError('amount exceeds bound')
    return int(result)


def identity(document):
    user = document.get('id')
    if not isinstance(user, str) or not re.fullmatch(r'[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}', user) or not user.replace('-', '').strip('0'):
        raise ValueError('invalid user identity')
    if document.get('trading', {}).get('defi', {}).get('enabled') is not True:
        raise ValueError('DeFi disabled')
    wallets = document.get('wallets')
    if not isinstance(wallets, list) or not 1 <= len(wallets) <= 2:
        raise ValueError('wallet bound')
    main = [w for w in wallets if w.get('name') == 'main']
    if len(main) != 1 or main[0].get('provider') != 'turnkey':
        raise ValueError('ambiguous main wallet')
    addresses = main[0].get('addresses')
    if not isinstance(addresses, list) or not 1 <= len(addresses) <= 2:
        raise ValueError('address bound')
    owners = [a.get('address') for a in addresses if a.get('chain') == 'solana']
    if len(owners) != 1:
        raise ValueError('ambiguous SOL owner')
    return user, key(owners[0])


def balances(document):
    rows = TM.balances(document)
    result = {}
    for row in rows:
        if row['chain'] != 'solana' or row['venue'] != 'defi':
            continue
        symbol = row['symbol']
        if symbol not in ('SOL', 'USDC'):
            raise ValueError('unsupported Solana balance asset')
        if symbol in result:
            raise ValueError('duplicate balance asset')
        decimals = 9 if symbol == 'SOL' else 6
        total, available, held = (atoms(row[k], decimals) for k in ('total', 'available', 'held'))
        if available + held != total or held:
            raise ValueError('balance held or inconsistent')
        result[symbol] = total
    if set(result) != {'SOL', 'USDC'}:
        raise ValueError('missing SOL/USDC balance')
    return result


def transport(method, target, bearer=None, body=None, timeout=20):
    if method == 'GET':
        if body is not None:
            raise ValueError('GET body refused')
        parts = urllib.parse.urlsplit(target)
        if parts.path not in ('/v1/account/me/trading', '/v1/gateway/balances', '/v1/gateway/orders') or parts.scheme or parts.netloc or parts.fragment:
            raise ValueError('endpoint refused')
        query = urllib.parse.parse_qs(parts.query, strict_parsing=True)
        if parts.path.endswith('/orders'):
            if set(query) - {'limit', 'cursor'} or query.get('limit') != [str(PAGE)] or any(len(v) != 1 for v in query.values()):
                raise ValueError('order query refused')
        elif parts.query:
            raise ValueError('query refused')
        TM.MINT._validate_token(bearer)
        headers = {'Authorization': 'Bearer ' + bearer}
        url = TM.ORIGIN + target
    elif method == 'POST' and target == RPC and isinstance(body, dict) and body.get('method') in RPC_METHODS:
        if bearer is not None:
            raise ValueError('RPC credentials refused')
        headers, url = {}, RPC
    else:
        raise ValueError('endpoint refused')
    headers.update({'Accept': 'application/json', 'Content-Type': 'application/json', 'User-Agent': 'velocita-account-history/1'})
    req = urllib.request.Request(url, data=json.dumps(body).encode() if body else None, method=method, headers=headers)
    with urllib.request.build_opener(urllib.request.ProxyHandler({}), TM.NoRedirect()).open(req, timeout=min(20, timeout)) as response:
        raw = response.read(MAX_BYTES + 1)
        if response.status != 200 or len(raw) > MAX_BYTES:
            raise ValueError('response refused')
    return raw


class Capture:
    def __init__(self, output, bearer, deadline=None):
        self.output, self.bearer, self.hashes = output, bearer, {}
        self.deadline = time.monotonic() + 300 if deadline is None else deadline
        self.count = 0
        self.requests = []

    def call(self, method, target, body=None):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise ValueError('capture deadline exceeded')
        self.count += 1
        if self.count > 110:
            raise ValueError('request budget exceeded')
        self.requests.append({'method': method, 'target': target, 'body': body})
        raw = transport(method, target, self.bearer if method == 'GET' else None, body, timeout=min(20, remaining))
        name = 'response-%03d.json' % self.count
        B.private_write(self.output, name, raw)
        self.hashes[name] = hashlib.sha256(raw).hexdigest()
        if time.monotonic() > self.deadline:
            raise ValueError('capture deadline exceeded')
        return strict(raw)

    def get(self, path):
        return self.call('GET', path)

    def rpc(self, method, params):
        document = self.call('POST', RPC, {'jsonrpc': '2.0', 'id': self.count + 1, 'method': method, 'params': params})
        if document.get('jsonrpc') != '2.0' or document.get('id') != self.count or 'error' in document or 'result' not in document:
            raise ValueError('invalid RPC response')
        return document['result']


def orders(capture):
    result, cursor, cursors = {}, None, set()
    for _ in range(MAX_PAGES):
        path = '/v1/gateway/orders?' + urllib.parse.urlencode({'limit': PAGE, **({'cursor': cursor} if cursor else {})})
        page = capture.get(path)
        rows = page.get('data')
        if not isinstance(rows, list) or len(rows) > PAGE:
            raise ValueError('order page bound')
        for row in rows:
            oid, status = row.get('order_id'), row.get('status')
            if not isinstance(oid, str) or not 1 <= len(oid) <= 128 or oid in result:
                raise ValueError('duplicate or invalid order identity')
            if status not in ('initialized', 'pending', 'cancel_pending', 'complete', 'canceled', 'active', 'failed'):
                raise ValueError('unknown order status')
            if row.get('venue') not in ('defi', 'cefi'):
                raise ValueError('unknown order venue')
            tx = row.get('tx_hash')
            result[oid] = {'status': status, 'venue': row['venue'], 'tx_hash': tx}
        pagination = page.get('pagination')
        if not isinstance(pagination, dict) or 'next_cursor' not in pagination:
            raise ValueError('missing pagination')
        cursor = pagination['next_cursor']
        if cursor is None:
            return result
        if not isinstance(cursor, str) or not 1 <= len(cursor) <= 1024 or cursor in cursors or not rows:
            raise ValueError('invalid order cursor')
        cursors.add(cursor)
    raise ValueError('order history truncated')


def token_accounts(capture, owner):
    result, slots = {}, []
    for program in (TOKEN, TOKEN22):
        response = capture.rpc('getTokenAccountsByOwner', [owner, {'programId': program}, {'encoding': 'jsonParsed', 'commitment': 'finalized'}])
        slots.append(integer(response['context']['slot']))
        rows = response['value']
        if not isinstance(rows, list) or len(rows) > MAX_ACCOUNTS:
            raise ValueError('token account bound')
        for row in rows:
            address, account = key(row['pubkey']), row['account']
            parsed = account['data']['parsed']
            info = parsed['info']
            if program == TOKEN22:
                raise ValueError('Token-2022 account unsupported')
            if address in result or account['owner'] != program or account.get('executable') is not False or parsed['type'] != 'account' or info['owner'] != owner or info['mint'] != USDC:
                raise ValueError('token account identity unsupported')
            amount = info['tokenAmount']
            if type(amount['decimals']) is not int or amount['decimals'] != 6 or info.get('state') != 'initialized' or info.get('delegate') is not None or info.get('isNative', False) is not False:
                raise ValueError('token account configuration unsupported')
            if not isinstance(amount['amount'], str) or not re.fullmatch(r'(0|[1-9][0-9]{0,19})', amount['amount']):
                raise ValueError('token amount invalid')
            close_authority = key(info.get('closeAuthority', owner), allow_zero=True)
            result[address] = {'amount': integer(int(amount['amount'])), 'program': program,
                               'mint': USDC, 'owner': owner, 'state': info['state'],
                               'is_native': False, 'delegate': None,
                               'close_authority': close_authority,
                               'close_authority_sha256': hashlib.sha256(key_bytes(close_authority, allow_zero=True)).hexdigest(),
                               'rent_lamports_excluded': integer(account['lamports'])}
    if len(result) > MAX_ACCOUNTS:
        raise ValueError('token account bound')
    return result, slots


def signatures(capture, address, head_only=False):
    result, before, last_slot = [], None, 2**64-1
    for _ in range(1 if head_only else MAX_PAGES):
        options = {'limit': 1 if head_only else PAGE, 'commitment': 'finalized'}
        if before:
            options['before'] = before
        rows = capture.rpc('getSignaturesForAddress', [address, options])
        if not isinstance(rows, list) or len(rows) > options['limit']:
            raise ValueError('signature page bound')
        for row in rows:
            signature, slot = key(row['signature'], 64), integer(row['slot'])
            if signature in result or slot > last_slot or row.get('confirmationStatus') != 'finalized':
                raise ValueError('signature history inconsistent')
            result.append(signature)
            last_slot = slot
        if head_only or len(rows) < PAGE:
            return result
        before = result[-1]
    raise ValueError('signature history truncated')


def transaction(capture, signature, owner, accounts):
    document = capture.rpc('getTransaction', [signature, {'encoding': 'jsonParsed', 'commitment': 'finalized', 'maxSupportedTransactionVersion': 0}])
    if not isinstance(document, dict) or not (document.get('version') == 'legacy' or (type(document.get('version')) is int and document['version'] == 0)):
        raise ValueError('missing or unsupported transaction')
    integer(document['slot'])
    tx, meta = document['transaction'], document['meta']
    if not tx.get('signatures') or tx['signatures'][0] != signature or not isinstance(meta, dict) or 'err' not in meta:
        raise ValueError('transaction identity mismatch')
    keys = [key(k['pubkey'], allow_zero=True) for k in tx['message']['accountKeys']]
    if len(set(keys)) != len(keys) or not 1 <= len(keys) <= 256:
        raise ValueError('transaction account bound')
    pre, post = meta['preBalances'], meta['postBalances']
    if len(pre) != len(keys) or len(post) != len(keys):
        raise ValueError('native balance shape')
    pre, post = list(map(integer, pre)), list(map(integer, post))
    token = []
    for field in ('preTokenBalances', 'postTokenBalances'):
        rows = meta.get(field)
        if not isinstance(rows, list) or len(rows) > len(keys):
            raise ValueError('token balance metadata missing')
        values = {}
        for row in rows:
            index = integer(row['accountIndex'])
            if index >= len(keys) or index in values:
                raise ValueError('duplicate token balance index')
            amount = row['uiTokenAmount']
            if keys[index] in accounts:
                if row.get('owner') != owner or row.get('mint') != USDC or amount.get('decimals') != 6:
                    raise ValueError('token history identity mismatch')
                if not isinstance(amount.get('amount'), str) or not re.fullmatch(r'(0|[1-9][0-9]{0,19})', amount['amount']):
                    raise ValueError('token history amount invalid')
                values[index] = integer(int(amount['amount']))
            else:
                values[index] = None
        token.append(values)
    relevant = [i for i, address in enumerate(keys) if address in accounts]
    deltas = {keys[i]: (token[1].get(i) or 0) - (token[0].get(i) or 0) for i in relevant}
    return {'signature': signature, 'slot': document['slot'], 'failed': meta.get('err') is not None,
            'network_fee_lamports': integer(meta['fee']),
            'owner_native_delta': post[keys.index(owner)] - pre[keys.index(owner)] if owner in keys else 0,
            'current_usdc_account_deltas': deltas,
            'attribution': 'observation_requires_review'}


def native_balance(capture, owner):
    response = capture.rpc('getMultipleAccounts', [[owner], {'encoding': 'base64', 'commitment': 'finalized'}])
    slot = integer(response['context']['slot'])
    rows = response['value']
    if not isinstance(rows, list) or len(rows) != 1 or not isinstance(rows[0], dict):
        raise ValueError('native account missing')
    row = rows[0]
    if row.get('owner') != '11111111111111111111111111111111' or row.get('executable') is not False or row.get('data') != ['', 'base64']:
        raise ValueError('native account unsupported')
    return integer(row['lamports']), slot


def gather(capture, pinned):
    expected = identity(pinned)
    if identity(capture.get('/v1/account/me/trading')) != expected:
        raise ValueError('account pin mismatch')
    owner = expected[1]
    initial = balances(capture.get('/v1/gateway/balances'))
    known_orders = orders(capture)
    accounts, slots = token_accounts(capture, owner)
    if sum(account['amount'] for account in accounts.values()) != initial['USDC']:
        raise ValueError('TM/on-chain USDC mismatch')
    native, start_slot = native_balance(capture, owner)
    if native != initial['SOL']:
        raise ValueError('TM/on-chain SOL mismatch')
    histories = {address: signatures(capture, address) for address in (owner, *sorted(accounts))}
    unique = sorted({s for history in histories.values() for s in history})
    if len(unique) > MAX_TX:
        raise ValueError('transaction budget exceeded')
    observed = [transaction(capture, sig, owner, accounts) for sig in unique]
    by_tx = {row['tx_hash'] for row in known_orders.values() if row['tx_hash']}
    for row in observed:
        row['attribution'] = 'TM_order_link_requires_review' if row['signature'] in by_tx else 'external_or_unlinked_requires_review'
    final_accounts, final_slots = token_accounts(capture, owner)
    if final_accounts != accounts or any(end < start for start, end in zip(slots, final_slots)):
        raise ValueError('token accounts changed during capture')
    if identity(capture.get('/v1/account/me/trading')) != expected or balances(capture.get('/v1/gateway/balances')) != initial:
        raise ValueError('account changed during capture')
    final_orders = orders(capture)
    if final_orders != known_orders:
        raise ValueError('orders changed during capture')
    final_native, end_slot = native_balance(capture, owner)
    if final_native != native or end_slot < start_slot:
        raise ValueError('native account changed during capture')
    for address, history in histories.items():
        if signatures(capture, address, head_only=True) != history[:1]:
            raise ValueError('history changed during capture')
    unresolved = sum(row['status'] not in ('complete', 'canceled', 'failed') for row in known_orders.values())
    status_counts = {status: sum(row['status'] == status for row in known_orders.values())
                     for status in ('initialized', 'pending', 'cancel_pending', 'complete', 'canceled', 'active', 'failed')}
    return {'schema': 1, 'scope': 'current main-wallet SOL and legacy USDC accounts',
            'history_boundary': 'RPC returned short final pages; provider archival completeness is not proven',
            'closed_historical_token_accounts_covered': False, 'live_admitted': False,
            'requires_reviewed_initial_cut': True, 'unresolved_tm_orders': unresolved,
            'status_counts': status_counts, 'start_slot': start_slot, 'end_slot': end_slot,
            'stable_window': True, 'pagination_exhausted': True, 'unsupported_accounts': 0,
            'owner_sha256': hashlib.sha256(key_bytes(owner)).hexdigest(),
            'user_id_sha256': hashlib.sha256(expected[0].encode()).hexdigest(),
            'tm_orders': len(known_orders), 'token_accounts': len(accounts), 'transactions': len(observed),
            'finalized_discovery_slots': slots, 'finalized_recheck_slots': final_slots,
            'balances_atoms': initial, 'owner': owner, 'token_account_configs': accounts,
            'token_account_rent_in_managed_funding': False, 'histories': histories, 'observations': observed}


def collect(session, setup_pin, setup_sha256, output):
    deadline = time.monotonic() + 300
    if not re.fullmatch('[0-9a-f]{64}', setup_sha256):
        raise ValueError('setup digest required')
    parent, normalized = TM.MINT._open_private_parent(setup_pin)
    try:
        fd = os.open(normalized.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('unsafe setup pin')
        raw = stream.read(32769)
    if len(raw) > 32768 or hashlib.sha256(raw).hexdigest() != setup_sha256:
        raise ValueError('setup pin digest mismatch')
    pinned = strict(raw)
    identity(pinned)
    parent, output = TM.MINT._open_private_parent(output)
    try:
        os.mkdir(output.name, mode=0o700, dir_fd=parent)
        os.fsync(parent)
    finally:
        os.close(parent)
    parent, session = TM.MINT._open_private_parent(session)
    try:
        lock = os.open(session.name + '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(lock, 'w') as handle:
        info = os.fstat(handle.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('unsafe session lock')
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        B.private_write(output, 'attempt.json', b'{"orders_sent":false,"retries":false}\n')
        pair, operation = TM.session(session, None, False)
        TM.token_pair(pair)
        capture = Capture(output, pair['access_token'], deadline)
        started = time.time_ns()
        try:
            result = gather(capture, pinned)
            TM.token_pair(pair)
        except Exception:
            B.private_write(output, 'stopped.json', json.dumps({'complete': False, 'raw_sha256': capture.hashes, 'requests': capture.count}).encode())
            raise
        result.update({'authentication': operation, 'setup_pin_sha256': setup_sha256,
                       'started_ns': started, 'finished_ns': time.time_ns(), 'raw_sha256': capture.hashes})
        B.private_write(output, 'requests.private.json', (json.dumps(capture.requests, indent=2)+'\n').encode())
        result['capture_started_ms'] = started // 1000000
        result['capture_finished_ms'] = result['finished_ns'] // 1000000
        result['setup_sha256'] = capture.hashes['response-001.json']
        result['balances_sha256'] = capture.hashes['response-002.json']
        manifest = (json.dumps(capture.hashes, sort_keys=True, separators=(',', ':'))+'\n').encode()
        B.private_write(output, 'manifest.json', manifest)
        result['evidence_manifest_sha256'] = hashlib.sha256(manifest).hexdigest()
        if not result['unresolved_tm_orders']:
            cut = {k: result[k] for k in ('schema', 'owner_sha256', 'user_id_sha256', 'setup_sha256',
                   'balances_sha256', 'evidence_manifest_sha256', 'capture_started_ms', 'capture_finished_ms',
                   'start_slot', 'end_slot', 'stable_window', 'pagination_exhausted', 'unsupported_accounts')}
            cut.update({'scope': 'sol-usdc-current-accounts', 'base_atoms': result['balances_atoms']['SOL'],
                        'quote_atoms': result['balances_atoms']['USDC'], 'blocking_orders': 0})
            B.private_write(output, 'baseline-cut.json', (json.dumps(cut, sort_keys=True, separators=(',', ':'))+'\n').encode())
        B.private_write(output, 'result.private.json', (json.dumps(result, indent=2)+'\n').encode())
        public = {k: result[k] for k in ('scope', 'history_boundary', 'closed_historical_token_accounts_covered',
                  'live_admitted', 'requires_reviewed_initial_cut', 'unresolved_tm_orders', 'tm_orders', 'token_accounts', 'transactions')}
        public.update({'requests': capture.count, 'result_sha256': hashlib.sha256((output/'result.private.json').read_bytes()).hexdigest()})
        B.private_write(output, 'summary.json', (json.dumps(public, indent=2)+'\n').encode())
        return public


@contextmanager
def hard_deadline(seconds=300):
    if seconds <= 0 or signal.getitimer(signal.ITIMER_REAL) != (0.0, 0.0):
        raise ValueError('invalid or already-active process timer')
    prior = signal.getsignal(signal.SIGALRM)
    def timeout(_signum, _frame):
        raise TimeoutError('capture hard deadline exceeded')
    signal.signal(signal.SIGALRM, timeout)
    try:
        signal.setitimer(signal.ITIMER_REAL, seconds)
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, prior)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--setup-pin', type=Path, required=True)
    parser.add_argument('--setup-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        print('Prepared: <=110 read requests, <=1 session refresh; no retries or fund movement; private evidence only.')
        return
    with hard_deadline():
        report = collect(args.session, args.setup_pin, args.setup_sha256, args.output)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('History capture stopped; preserve partial evidence. Error type: ' + type(error).__name__)
        raise SystemExit(1)
