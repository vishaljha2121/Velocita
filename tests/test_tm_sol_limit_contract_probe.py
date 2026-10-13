import datetime
import hashlib
import io
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('limit_probe', ROOT / 'tools/tm-sol-limit-contract-probe.py')
PROBE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PROBE)


class SolLimitProbeTests(unittest.TestCase):
    def setUp(self):
        scratch = Path.home() / '.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.session = self.root / 'session.json'
        self.output = self.root / 'attempt'
        self.write_session(600)
        self.calls = []
        self.catalog = {'data': [dict(id=PROBE.PROBE.ASSET, chain='solana', address=PROBE.PROBE.MINT,
                                    symbol='SOL', decimals=9, is_active=True, status='active',
                                    tradeable=True, venue='defi', type='spot')]}
        payload = '{"fixture":"unsigned"}\n'
        self.response = dict(status='initialized', order_id='private-order-id',
                             payloads=[dict(payload=payload, digest=hashlib.sha256(payload.encode()).hexdigest())],
                             quote=dict(qty='0.01', qty_out='1.00', fee='0.002'))
        self.code = 201

    def write_session(self, remaining):
        expiry = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(seconds=remaining)
        self.session.write_text(json.dumps(dict(access_token='fixture.token.unsigned', refresh_token='fixture.refresh.unsigned',
                                              expires_in=expiry.isoformat(), token_type='Bearer')))
        self.session.chmod(0o600)

    def send(self, method, path, token=None, document=None):
        self.calls.append((method, path, token, document))
        if method == 'GET':
            return 200, json.dumps(self.catalog).encode()
        return self.code, json.dumps(self.response).encode()

    def run_probe(self, send=None):
        with patch.object(PROBE.ACCOUNT, 'session', side_effect=AssertionError('no mint or refresh')):
            return PROBE.run(self.session, self.output, send or self.send)

    def test_exact_request_original_payload_hash_and_replay_refusal(self):
        result = self.run_probe()
        self.assertEqual(self.calls, [
            ('GET', '/v1/gateway/assets', None, None),
            ('POST', '/v1/gateway/orders', 'fixture.token.unsigned',
             {'asset_id': '495e07ac-fa3e-4179-85b7-b1e8cece3dc4', 'chain': 'solana', 'side': 'buy',
              'type': 'limit', 'qty': '0.01', 'qty_unit': 'base', 'price': '100.00'})])
        self.assertEqual(result['verdict'], 'initialized_unsigned_requires_semantic_review')
        self.assertEqual(result['payload_sha256'], [hashlib.sha256(b'{"fixture":"unsigned"}\n').hexdigest()])
        for name in ['attempt.json', 'catalog.json', 'response.json', 'summary.json']:
            self.assertEqual((self.output / name).stat().st_mode & 0o777, 0o600)
        with self.assertRaises(FileExistsError):
            self.run_probe()
        self.assertEqual(len(self.calls), 2, 'the attempt marker must block every repeated network call')
        public = json.dumps(result)
        for private in ['fixture.token.unsigned', 'fixture.refresh.unsigned', 'private-order-id', 'unsigned\\"']:
            self.assertNotIn(private, public)

    def test_expiring_session_stops_before_network_without_refresh(self):
        self.write_session(90)
        with self.assertRaisesRegex(ValueError, 'separate renewal'):
            self.run_probe()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.output.exists())

    def test_catalog_identity_prevents_a_cefi_request(self):
        self.catalog['data'][0]['venue'] = 'cefi'
        with self.assertRaisesRegex(ValueError, 'SOL identity'):
            self.run_probe()
        self.assertEqual(len(self.calls), 1, 'a catalog mismatch must prevent the create POST')

    def test_timeout_is_never_retried(self):
        def timeout(method, path, token=None, document=None):
            if method == 'GET':
                return self.send(method, path)
            self.calls.append((method, path, token, document))
            raise TimeoutError('fixture timeout')
        with self.assertRaises(TimeoutError):
            self.run_probe(timeout)
        with self.assertRaises(FileExistsError):
            self.run_probe(timeout)
        self.assertEqual(len(self.calls), 2, 'an ambiguous timeout must not send another request')

    def test_rejection_is_retained_without_printing_private_error(self):
        self.code, self.response = 400, {'error': 'private-wallet-marker unsupported route'}
        result = self.run_probe()
        self.assertEqual(result['http_status'], 400)
        self.assertEqual(json.loads((self.output / 'response.json').read_text()), self.response)
        self.assertNotIn('private-wallet-marker', json.dumps(result))

    def test_unexpected_status_requires_reconciliation(self):
        for status in ['active', 'pending', 'complete', 'failed']:
            with self.subTest(status=status):
                self.response['status'] = status
                result = PROBE.summarize(201, json.dumps(self.response).encode())
                self.assertEqual(result['verdict'], 'unexpected_status_requires_reconciliation')
                self.assertFalse(result['execute_request_sent'])

    def test_missing_payload_or_issue_is_inconclusive(self):
        for field, value in [('payloads', []), ('issues', ['insufficient balance']), ('order_id', '')]:
            with self.subTest(field=field):
                response = dict(self.response, **{field: value})
                result = PROBE.summarize(201, json.dumps(response).encode())
                self.assertEqual(result['verdict'], 'inconclusive_response_requires_private_review')

    def test_payload_digest_must_match_original_bytes(self):
        self.response['payloads'][0]['payload'] = '{"fixture":"unsigned"}'
        with self.assertRaisesRegex(ValueError, 'payload digest mismatch'):
            PROBE.summarize(201, json.dumps(self.response).encode())

    def test_other_routes_and_changed_amounts_never_open_a_socket(self):
        with patch.object(PROBE.urllib.request, 'build_opener', side_effect=AssertionError('network reached')):
            for path, document in [('/v1/gateway/orders', dict(PROBE.body(), qty='0.02')),
                                   ('/v1/gateway/orders/a/execute', PROBE.body()),
                                   ('/v1/auth/token/refresh', PROBE.body())]:
                with self.subTest(path=path):
                    with self.assertRaisesRegex(ValueError, 'outside fixed request protocol'):
                        PROBE.exchange('POST', path, 'fixture.token.unsigned', document)

    def test_http_request_has_fixed_origin_and_body(self):
        response = io.BytesIO(b'{"status":"initialized"}')
        response.code = 201
        opener = Mock()
        opener.open.return_value = response
        with patch.object(PROBE.urllib.request, 'build_opener', return_value=opener) as build:
            code, raw = PROBE.exchange('POST', '/v1/gateway/orders', 'fixture.token.unsigned', PROBE.body())
        request = opener.open.call_args.args[0]
        self.assertEqual(request.full_url, 'https://api.truemarkets.co/v1/gateway/orders')
        self.assertEqual(request.method, 'POST')
        self.assertEqual(json.loads(request.data), PROBE.body())
        self.assertEqual(request.get_header('Authorization'), 'Bearer fixture.token.unsigned')
        self.assertEqual(opener.open.call_args.kwargs, {'timeout': 20})
        self.assertEqual(build.call_args.args[0].proxies, {})
        self.assertIsInstance(build.call_args.args[1], PROBE.ACCOUNT.NoRedirect)
        self.assertEqual((code, raw), (201, b'{"status":"initialized"}'))

    def test_http_response_cap(self):
        response = io.BytesIO(b'x' * (PROBE.LIMIT + 1))
        response.code = 201
        opener = Mock()
        opener.open.return_value = response
        with patch.object(PROBE.urllib.request, 'build_opener', return_value=opener):
            with self.assertRaisesRegex(ValueError, 'response bound exceeded'):
                PROBE.exchange('POST', '/v1/gateway/orders', 'fixture.token.unsigned', PROBE.body())

    def test_truthful_client_identity_reaches_http_handler(self):
        received = []
        def capture(handler, request):
            received.append(dict(request.header_items()))
            response = io.BytesIO(b'{}')
            response.code = response.status = 200 if request.method == 'GET' else 201
            response.msg = 'OK'
            response.info = lambda: {}
            return response
        with patch.object(PROBE.urllib.request.HTTPSHandler, 'https_open', capture):
            PROBE.exchange('GET', '/v1/gateway/assets')
            PROBE.exchange('POST', '/v1/gateway/orders', 'fixture.token.unsigned', PROBE.body())
        self.assertEqual(len(received), 2)
        for headers in received:
            self.assertEqual(headers.get('User-agent'), 'velocita-contract-read/1',
                             'requests must identify the actual Velocita client')
        self.assertNotIn('Authorization', received[0])
        self.assertEqual(received[1]['Authorization'], 'Bearer fixture.token.unsigned')

    def test_pinned_catalog_sends_only_the_fixed_post(self):
        raw = json.dumps(self.catalog).encode()
        with patch.object(PROBE, 'PINNED_CATALOG_SHA256', hashlib.sha256(raw).hexdigest()):
            result = PROBE.run(self.session, self.output, self.send, catalog_raw=raw)
        self.assertEqual(len(self.calls), 1, 'pinned catalog must avoid any catalog GET')
        self.assertEqual(self.calls[0], ('POST', '/v1/gateway/orders', 'fixture.token.unsigned',
            dict(asset_id='495e07ac-fa3e-4179-85b7-b1e8cece3dc4', chain='solana', side='buy',
                 type='limit', qty='0.01', qty_unit='base', price='100.00')))
        self.assertEqual(result['verdict'], 'initialized_unsigned_requires_semantic_review')
        status = json.loads((self.output / 'http-status.json').read_text())
        self.assertEqual(status, {'phase': 'create_order', 'http_status': 201})

    def test_changed_pinned_catalog_refused_before_network(self):
        with self.assertRaisesRegex(ValueError, 'pinned catalog digest mismatch'):
            PROBE.run(self.session, self.output, self.send, catalog_raw=b'{}')
        self.assertEqual(self.calls, [])

    def test_pinned_catalog_identity_is_still_checked(self):
        self.catalog['data'][0]['chain'] = 'hypercore-perp'
        raw = json.dumps(self.catalog).encode()
        with patch.object(PROBE, 'PINNED_CATALOG_SHA256', hashlib.sha256(raw).hexdigest()):
            with self.assertRaisesRegex(ValueError, 'SOL identity'):
                PROBE.run(self.session, self.output, self.send, catalog_raw=raw)
        self.assertEqual(self.calls, [])

    def test_pinned_timeout_blocks_another_post(self):
        raw = json.dumps(self.catalog).encode()
        def timeout(*args):
            self.calls.append(args)
            raise TimeoutError('fixture timeout')
        with patch.object(PROBE, 'PINNED_CATALOG_SHA256', hashlib.sha256(raw).hexdigest()):
            with self.assertRaises(TimeoutError):
                PROBE.run(self.session, self.output, timeout, catalog_raw=raw)
            self.assertTrue((self.output / 'attempt.json').exists(), 'pinned attempt marker must survive timeout')
            with self.assertRaises(FileExistsError):
                PROBE.run(self.session, self.output, timeout, catalog_raw=raw)
        self.assertEqual(len(self.calls), 1, 'pinned timeout must not cause a second POST')


if __name__ == '__main__':
    unittest.main()
