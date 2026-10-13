import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import signal
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('history', ROOT / 'tools/tm-account-history-check.py')
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)


def b58(byte, n):
    number, out = int.from_bytes(bytes([byte])*n, 'big'), ''
    while number:
        number, rem = divmod(number, 58)
        out = M.ALPHABET[rem]+out
    return out


OWNER, ACCOUNT, SIG = b58(1, 32), b58(2, 32), b58(3, 64)
SETUP = {'id': '11111111-2222-3333-4444-555555555555', 'trading': {'defi': {'enabled': True}},
         'wallets': [{'name': 'main', 'provider': 'turnkey', 'addresses': [{'chain': 'solana', 'address': OWNER}]}]}
BALANCES = {'data': [{'symbol': symbol, 'venue': 'defi', 'chain': 'solana', 'total': amount, 'available': amount, 'held': '0'} for symbol, amount in [('SOL', '0.04'), ('USDC', '1.00')]]}


def token_row():
    return {'pubkey': ACCOUNT, 'account': {'owner': M.TOKEN, 'executable': False, 'lamports': 2039280,
        'data': {'parsed': {'type': 'account', 'info': {'owner': OWNER, 'mint': M.USDC,
                 'state': 'initialized', 'tokenAmount': {'amount': '1000000', 'decimals': 6}}}}}}


def tx():
    return {'slot': 99, 'version': 'legacy', 'transaction': {'signatures': [SIG],
        'message': {'accountKeys': [{'pubkey': ACCOUNT}, {'pubkey': b58(4, 32)}]}},
        'meta': {'err': None, 'fee': 5000, 'preBalances': [2039280, 10000], 'postBalances': [2039280, 5000],
        'preTokenBalances': [{'accountIndex': 0, 'owner': OWNER, 'mint': M.USDC, 'uiTokenAmount': {'amount': '0', 'decimals': 6}}],
        'postTokenBalances': [{'accountIndex': 0, 'owner': OWNER, 'mint': M.USDC, 'uiTokenAmount': {'amount': '1000000', 'decimals': 6}}]}}


class Fake:
    def __init__(self, order_rows=None):
        self.calls = []
        self.order_rows = order_rows or []

    def get(self, path):
        self.calls.append(('GET', path))
        if path.endswith('/trading'):
            return SETUP
        if path.endswith('/balances'):
            return BALANCES
        return {'data': self.order_rows, 'pagination': {'next_cursor': None}}

    def rpc(self, method, params):
        self.calls.append((method, params))
        if method == 'getTokenAccountsByOwner':
            return {'context': {'slot': 100}, 'value': [token_row()] if params[1]['programId'] == M.TOKEN else []}
        if method == 'getSignaturesForAddress':
            return [{'signature': SIG, 'slot': 99, 'confirmationStatus': 'finalized', 'err': None}] if params[0] == ACCOUNT else []
        if method == 'getTransaction':
            return tx()
        if method == 'getMultipleAccounts':
            return {'context': {'slot': 100}, 'value': [{'owner': '11111111111111111111111111111111', 'executable': False, 'data': ['', 'base64'], 'lamports': 40000000}]}
        raise AssertionError(method)


class HistoryTests(unittest.TestCase):
    def test_inbound_token_transfer_without_owner_signature_is_collected(self):
        fake = Fake()
        result = M.gather(fake, SETUP)
        self.assertEqual(result['transactions'], 1)
        self.assertEqual(result['observations'][0]['current_usdc_account_deltas'][ACCOUNT], 1000000)
        self.assertEqual(result['observations'][0]['owner_native_delta'], 0)
        self.assertEqual(result['observations'][0]['attribution'], 'external_or_unlinked_requires_review')
        self.assertFalse(result['closed_historical_token_accounts_covered'])
        self.assertFalse(result['live_admitted'])
        self.assertEqual(result['owner_sha256'], hashlib.sha256(bytes([1])*32).hexdigest())

    def test_initialized_quote_blocks_cut(self):
        result = M.gather(Fake([{'order_id': 'id1', 'venue': 'defi', 'status': 'initialized'}]), SETUP)
        self.assertEqual(result['unresolved_tm_orders'], 1)
        self.assertEqual(result['status_counts']['initialized'], 1)

    def test_alternate_close_authority_recorded_without_rent_adoption(self):
        fake = Fake()
        original = fake.rpc
        authority = b58(11, 32)
        def call(method, params):
            result = original(method, params)
            if method == 'getTokenAccountsByOwner' and result['value']:
                result['value'][0]['account']['data']['parsed']['info']['closeAuthority'] = authority
            return result
        fake.rpc = call
        result = M.gather(fake, SETUP)
        config = result['token_account_configs'][ACCOUNT]
        self.assertEqual(config['close_authority'], authority)
        self.assertEqual(config['close_authority_sha256'], hashlib.sha256(bytes([11])*32).hexdigest())
        self.assertEqual(config['rent_lamports_excluded'], 2039280)
        self.assertFalse(result['token_account_rent_in_managed_funding'])
        self.assertEqual(result['balances_atoms']['USDC'], 1000000)
        self.assertEqual(result['balances_atoms']['SOL'], 40000000)

    def test_close_authority_change_blocks_even_same_balance(self):
        fake = Fake()
        original = fake.rpc
        legacy_reads = 0
        def call(method, params):
            nonlocal legacy_reads
            result = original(method, params)
            if method == 'getTokenAccountsByOwner' and result['value']:
                legacy_reads += 1
                result['value'][0]['account']['data']['parsed']['info']['closeAuthority'] = b58(10+legacy_reads, 32)
            return result
        fake.rpc = call
        with self.assertRaisesRegex(ValueError, 'accounts changed'): M.gather(fake, SETUP)

    def test_bad_close_authority_delegation_native_frozen_refused(self):
        for field, value in [('closeAuthority', 'not-a-key'), ('delegate', b58(12,32)), ('isNative', True), ('state', 'frozen')]:
            row = token_row()
            row['account']['data']['parsed']['info'][field] = value
            with patch.object(Fake, 'rpc', return_value={'context':{'slot':100},'value':[row]}):
                with self.subTest(field=field), self.assertRaises(ValueError): M.token_accounts(Fake(), OWNER)

    def test_hard_alarm_interrupts_and_restores_handler(self):
        prior = signal.getsignal(signal.SIGALRM)
        with self.assertRaisesRegex(TimeoutError, 'hard deadline'):
            with M.hard_deadline(0.02):
                time.sleep(0.2)
        self.assertEqual(signal.getsignal(signal.SIGALRM), prior)
        self.assertEqual(signal.getitimer(signal.ITIMER_REAL), (0.0, 0.0))
        with M.hard_deadline(1):
            with self.assertRaisesRegex(ValueError, 'already-active'):
                with M.hard_deadline(1): pass

    def test_native_balance_disagreement_refused(self):
        fake = Fake()
        original = fake.rpc
        def call(method, params):
            result = original(method, params)
            if method == 'getMultipleAccounts':
                result['value'][0]['lamports'] += 1
            return result
        fake.rpc = call
        with self.assertRaisesRegex(ValueError, 'SOL mismatch'):
            M.gather(fake, SETUP)

    def test_zero_net_activity_recheck_refused(self):
        fake = Fake()
        original = fake.rpc
        def call(method, params):
            result = original(method, params)
            if method == 'getSignaturesForAddress' and params[0] == ACCOUNT and params[1]['limit'] == 1:
                result[0]['signature'] = b58(5, 64)
            return result
        fake.rpc = call
        with self.assertRaisesRegex(ValueError, 'history changed'):
            M.gather(fake, SETUP)

    def test_duplicate_identity_amount_json_refused(self):
        for raw in ('{"id":"a","id":"b"}', '{"total":"1","total":"2"}', '{"x":NaN}'):
            with self.assertRaises(ValueError): M.strict(raw)
        for amount in ('1e3', '-1', '1.0000001', '01', '18446744073709551616'):
            with self.assertRaises(ValueError): M.atoms(amount, 6)
        bad = {'data': BALANCES['data'] + [BALANCES['data'][0]]}
        with self.assertRaisesRegex(ValueError, 'duplicate balance'): M.balances(bad)

    def test_order_cursor_exhaustion_and_cycles(self):
        fake = Fake()
        with patch.object(fake, 'get', side_effect=[{'data': [{'order_id':'a','venue':'defi','status':'complete'}], 'pagination':{'next_cursor':'opaque/+='}}, {'data': [], 'pagination':{'next_cursor':None}}]) as read:
            self.assertEqual(len(M.orders(fake)), 1)
            self.assertIn('cursor=opaque%2F%2B%3D', read.call_args_list[1].args[0])
        pages = [{'data':[{'order_id':str(n),'venue':'defi','status':'complete'}], 'pagination':{'next_cursor':'same'}} for n in range(2)]
        with patch.object(fake, 'get', side_effect=pages):
            with self.assertRaisesRegex(ValueError, 'cursor'): M.orders(fake)

    def test_order_pagination_bound(self):
        fake = Fake()
        pages = [{'data':[{'order_id':str(n),'venue':'defi','status':'complete'}], 'pagination':{'next_cursor':str(n)}} for n in range(M.MAX_PAGES)]
        with patch.object(fake, 'get', side_effect=pages) as read:
            with self.assertRaisesRegex(ValueError, 'truncated'): M.orders(fake)
            self.assertEqual(read.call_count, M.MAX_PAGES)

    def test_signature_pagination_truncation(self):
        fake = Fake()
        pages = [[{'signature':b58(1+n*M.PAGE+i,64),'slot':100-n,'confirmationStatus':'finalized'} for i in range(M.PAGE)] for n in range(M.MAX_PAGES)]
        with patch.object(fake, 'rpc', side_effect=pages) as read:
            with self.assertRaisesRegex(ValueError, 'truncated'): M.signatures(fake, OWNER)
            self.assertEqual(read.call_count, M.MAX_PAGES)

    def test_token2022_and_wrong_mint_refused(self):
        fake = Fake()
        for program in (M.TOKEN, M.TOKEN22):
            row = token_row()
            row['account']['data']['parsed']['info']['mint'] = b58(8,32)
            with patch.object(fake, 'rpc', side_effect=[{'context':{'slot':100},'value':[row]}] if program == M.TOKEN else [{'context':{'slot':100},'value':[]}, {'context':{'slot':100},'value':[row]}]):
                with self.assertRaises(ValueError): M.token_accounts(fake, OWNER)

    def test_wrong_transaction_identity_and_missing_metadata_refused(self):
        fake = Fake()
        for field in ('signatures', 'preTokenBalances'):
            document = tx()
            if field == 'signatures': document['transaction'][field] = [b58(9,64)]
            else: del document['meta'][field]
            with patch.object(fake, 'rpc', return_value=document):
                with self.assertRaises(ValueError): M.transaction(fake, SIG, OWNER, {ACCOUNT: 1000000})

    def test_transport_disallows_mutation_redirect_auth_rpc(self):
        with patch.object(M.urllib.request, 'build_opener') as opener:
            for args in [('POST', '/v1/gateway/orders', 'secret', {}), ('GET', 'https://evil.test/v1/gateway/balances', 'secret', None), ('POST', M.RPC, 'secret', {'method':'getTransaction'}), ('POST', M.RPC, None, {'method':'sendTransaction'})]:
                with self.assertRaises(ValueError): M.transport(*args)
            opener.assert_not_called()

    def test_transport_caps_response_and_no_retry(self):
        class Response:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def read(self, count): return b'x'*count
        with patch.object(M.urllib.request, 'build_opener') as opener:
            opener.return_value.open.return_value = Response()
            with self.assertRaisesRegex(ValueError, 'response refused'): M.transport('GET', '/v1/gateway/balances', 'aaa.bbb.ccc')
            self.assertEqual(opener.return_value.open.call_count, 1)

    def test_system_program_key_valid_only_inside_transaction(self):
        document = tx()
        document['transaction']['message']['accountKeys'][1]['pubkey'] = '1'*32
        with patch.object(Fake, 'rpc', return_value=document):
            self.assertEqual(M.transaction(Fake(), SIG, OWNER, {ACCOUNT: 1000000})['network_fee_lamports'], 5000)
        with self.assertRaises(ValueError): M.key('1'*32)

    def test_integer_decimal_upper_boundary(self):
        self.assertEqual(M.atoms('18446744073.709551615', 9), 2**64-1)
        with self.assertRaises(ValueError): M.atoms('18446744073.709551616', 9)

    def test_false_version_and_missing_err_refused(self):
        for field in ('version', 'err'):
            document = tx()
            if field == 'version': document['version'] = False
            else: del document['meta']['err']
            with patch.object(Fake, 'rpc', return_value=document):
                with self.assertRaises(ValueError): M.transaction(Fake(), SIG, OWNER, {ACCOUNT: 1000000})

    def test_deadline_prevents_network_and_bounds_timeout(self):
        with patch.object(M.time, 'monotonic', return_value=20), patch.object(M, 'transport') as network:
            capture = M.Capture(Path('/unused'), 'aaa.bbb.ccc', deadline=19)
            with self.assertRaisesRegex(ValueError, 'deadline'): capture.get('/v1/gateway/balances')
            network.assert_not_called()
            capture.deadline = 22
            network.side_effect = ValueError('stop')
            with self.assertRaises(ValueError): capture.get('/v1/gateway/balances')
            self.assertEqual(network.call_args.kwargs['timeout'], 2)

    def test_individual_program_slot_regression_refused(self):
        fake = Fake()
        original = fake.rpc
        discoveries = 0
        def call(method, params):
            nonlocal discoveries
            result = original(method, params)
            if method == 'getTokenAccountsByOwner':
                result['context']['slot'] = [100, 120, 105, 119][discoveries]
                discoveries += 1
            return result
        fake.rpc = call
        with self.assertRaisesRegex(ValueError, 'accounts changed'): M.gather(fake, SETUP)

    def test_private_cut_emission_and_redacted_public_output(self):
        scratch = Path.home()/'.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            directory = Path(directory)
            pin = directory/'setup.json'
            raw = json.dumps(SETUP).encode()
            pin.write_bytes(raw); pin.chmod(0o600)
            session = directory/'session.json'
            session.write_text(json.dumps(dict(access_token='aaa.bbb.ccc', refresh_token='ddd.eee.fff', expires_in='2099-01-01T00:00:00Z', token_type='Bearer')))
            session.chmod(0o600)
            fake = Fake()
            def wire(method, target, bearer=None, body=None, timeout=20):
                if method == 'GET': return json.dumps(fake.get(target)).encode()
                return json.dumps({'jsonrpc':'2.0','id':body['id'],'result':fake.rpc(body['method'],body['params'])}).encode()
            with patch.object(M, 'transport', side_effect=wire):
                report = M.collect(session, pin, hashlib.sha256(raw).hexdigest(), directory/'capture')
            cut = json.loads((directory/'capture/baseline-cut.json').read_text())
            self.assertEqual(cut['quote_atoms'], 1000000)
            self.assertEqual(cut['blocking_orders'], 0)
            self.assertNotIn(OWNER, json.dumps(report))
            self.assertNotIn('aaa.bbb.ccc', ''.join(p.read_text() for p in (directory/'capture').iterdir()))
            self.assertTrue(all(p.stat().st_mode & 0o777 == 0o600 for p in (directory/'capture').iterdir()))
            with patch.object(M, 'transport', side_effect=ValueError('network')) as transport:
                with self.assertRaises(ValueError): M.collect(session, pin, hashlib.sha256(raw).hexdigest(), directory/'failed')
                self.assertEqual(transport.call_count, 1)
                self.assertTrue((directory/'failed/stopped.json').exists())
                self.assertFalse((directory/'failed/baseline-cut.json').exists())


if __name__ == '__main__': unittest.main()
