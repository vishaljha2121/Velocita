"""Offline request-boundary tests; every token and response here is synthetic."""
import datetime
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('route_probe', ROOT / 'tools/tm-route-a-probe.py')
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class RouteAProbeTests(unittest.TestCase):
    def setUp(self):
        scratch = Path.home() / '.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.session = self.root / 'session.json'
        self.output = self.root / 'capture'
        self.pair = self.tokens(600)
        self.session.write_text(json.dumps(self.pair))
        self.session.chmod(0o600)
        self.catalog = b'catalog fixture checked separately'
        payload = '{"fixture":"no transaction"}\n'
        self.response = dict(status='initialized', order_id='private-fixture-id',
                             payloads=[dict(payload=payload, digest=hashlib.sha256(payload.encode()).hexdigest())],
                             quote=dict(qty='0.0084', qty_out='1.0', fee='0.002'))
        self.calls = []

    def tokens(self, seconds):
        expiry = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(seconds=seconds)
        return dict(access_token='fixture.access.token', refresh_token='fixture.refresh.token',
                    expires_in=expiry.isoformat(), token_type='Bearer')

    def send(self, path, document, token=None):
        self.calls.append((path, document, token))
        if path == '/v1/auth/token/refresh':
            return 200, json.dumps(self.tokens(600)).encode()
        return 201, json.dumps(self.response).encode()

    def run_probe(self, send=None):
        with patch.object(PROBE.CONTRACT, 'verify_pinned_catalog') as verify:
            result = PROBE.run(self.session, self.output, self.catalog, send or self.send)
            verify.assert_called_once_with(self.catalog)
            return result

    def test_exact_single_sell_and_private_capture(self):
        result = self.run_probe()
        self.assertEqual(self.calls, [('/v1/gateway/orders', {
            'base_asset': 'So11111111111111111111111111111111111111112',
            'chain': 'solana', 'side': 'sell', 'type': 'market', 'qty': '0.0084', 'qty_unit': 'base'
        }, 'fixture.access.token')])
        self.assertEqual(result['verdict'], 'initialized_unsigned_requires_semantic_review')
        for name in ('attempt.json', 'catalog.json', 'response.json', 'summary.json'):
            self.assertEqual((self.output / name).stat().st_mode & 0o777, 0o600)
        self.assertNotIn('private-fixture-id', json.dumps(result))
        self.assertNotIn('fixture.access', json.dumps(result))
        with self.assertRaises(FileExistsError):
            self.run_probe()
        self.assertEqual(len(self.calls), 1, 'exclusive attempt must prevent replay')

    def test_expired_session_refreshes_once_then_quotes(self):
        self.session.write_text(json.dumps(self.tokens(-600)))
        self.run_probe()
        self.assertEqual([x[0] for x in self.calls], ['/v1/auth/token/refresh', '/v1/gateway/orders'])
        self.assertEqual(self.calls[0][1], {'refresh_token': 'fixture.refresh.token'})
        self.assertIsNone(self.calls[0][2])
        self.assertEqual(self.session.stat().st_mode & 0o777, 0o600)
        self.assertGreater(PROBE.ACCOUNT.token_pair(PROBE.ACCOUNT.read_private(self.session))[1], 0)

    def test_buy_capture_uses_exact_quote_input_without_sell_fallback(self):
        with patch.object(PROBE.CONTRACT, 'verify_pinned_catalog'):
            PROBE.run(self.session, self.output, self.catalog, self.send, buy=True)
        self.assertEqual(self.calls, [('/v1/gateway/orders', {
            'base_asset': 'So11111111111111111111111111111111111111112',
            'chain': 'solana', 'side': 'buy', 'type': 'market', 'qty': '1.00', 'qty_unit': 'quote'
        }, 'fixture.access.token')])
        marker = json.loads((self.output / 'attempt.json').read_text())
        self.assertEqual(marker['request'], self.calls[0][1])
        with patch.object(PROBE.CONTRACT, 'verify_pinned_catalog'):
            with self.assertRaises(FileExistsError):
                PROBE.run(self.session, self.output, self.catalog, self.send, buy=True)
        self.assertEqual(len(self.calls), 1, 'buy capture cannot repeat its create request')

    def test_changed_buy_amount_or_quantity_unit_never_opens_socket(self):
        with patch.object(PROBE.urllib.request, 'build_opener', side_effect=AssertionError('network reached')):
            for overrides in ({'qty': '1.01'}, {'qty_unit': 'base'}, {'chain': 'base'}, {'side': 'sell'}):
                with self.subTest(overrides=overrides):
                    with self.assertRaisesRegex(ValueError, 'outside fixed protocol'):
                        PROBE.exchange('/v1/gateway/orders', dict(PROBE.body(buy=True), **overrides),
                                       'fixture.access.token')

    def test_refresh_failure_never_reaches_order_or_retries(self):
        self.session.write_text(json.dumps(self.tokens(-600)))
        def refuse(path, document, token=None):
            self.calls.append(path)
            return 401, b'{"private":"error"}'
        with self.assertRaisesRegex(ValueError, 'renewal refused'):
            self.run_probe(refuse)
        with self.assertRaises(FileExistsError):
            self.run_probe(refuse)
        self.assertEqual(self.calls, ['/v1/auth/token/refresh'])

    def test_quote_timeout_retains_attempt_and_prevents_resend(self):
        def timeout(*args):
            self.calls.append(args)
            raise TimeoutError('synthetic timeout')
        with self.assertRaises(TimeoutError):
            self.run_probe(timeout)
        with self.assertRaises(FileExistsError):
            self.run_probe(timeout)
        self.assertEqual(len(self.calls), 1, 'ambiguous quote timeout cannot send again')

    def test_changed_catalog_refused_before_network(self):
        with self.assertRaisesRegex(ValueError, 'pinned catalog digest mismatch'):
            PROBE.run(self.session, self.output, b'{}', self.send)
        self.assertEqual(self.calls, [])

    def test_quote_rejection_is_preserved_without_followup(self):
        def reject(path, document, token=None):
            self.calls.append(path)
            return 400, b'{"private":"minimum not met"}'
        result = self.run_probe(reject)
        self.assertEqual(result['http_status'], 400)
        self.assertEqual(self.calls, ['/v1/gateway/orders'])
        self.assertEqual((self.output / 'response.json').read_bytes(), b'{"private":"minimum not met"}')
        self.assertNotIn('minimum', json.dumps(result))

    def test_fixed_endpoint_body_and_amount_boundary(self):
        cases = [('/v1/gateway/orders/x/execute', PROBE.body()),
                 ('/v1/gateway/orders', dict(PROBE.body(), qty='0.009')),
                 ('/v1/gateway/orders', dict(PROBE.body(), side='buy')),
                 ('/v1/gateway/orders', dict(PROBE.body(), chain='base')),
                 ('/v1/auth/api-key/token', {}),
                 ('/v1/auth/token/refresh', {'refresh_token': 'fixture.refresh.token', 'extra': 1})]
        with patch.object(PROBE.urllib.request, 'build_opener', side_effect=AssertionError('network reached')):
            for path, body in cases:
                with self.subTest(path=path, body=body):
                    with self.assertRaisesRegex(ValueError, 'outside fixed protocol'):
                        PROBE.exchange(path, body, 'fixture.access.token')

    def test_wire_request_fixed_origin_truthful_identity_no_proxy_or_redirect(self):
        response = io.BytesIO(b'{}')
        response.code = 201
        opener = Mock()
        opener.open.return_value = response
        with patch.object(PROBE.urllib.request, 'build_opener', return_value=opener) as build:
            self.assertEqual(PROBE.exchange('/v1/gateway/orders', PROBE.body(), 'fixture.access.token'), (201, b'{}'))
        request = opener.open.call_args.args[0]
        self.assertEqual(request.full_url, 'https://api.truemarkets.co/v1/gateway/orders')
        self.assertEqual(request.method, 'POST')
        self.assertEqual(json.loads(request.data), PROBE.body())
        self.assertEqual(request.get_header('User-agent'), 'velocita-contract-read/1')
        self.assertEqual(build.call_args.args[0].proxies, {})
        self.assertIsInstance(build.call_args.args[1], PROBE.ACCOUNT.NoRedirect)
        self.assertEqual(opener.open.call_args.kwargs['timeout'], 20)

    def test_oversized_response_refused(self):
        response = io.BytesIO(b'x' * (PROBE.LIMIT + 1))
        response.code = 201
        opener = Mock()
        opener.open.return_value = response
        with patch.object(PROBE.urllib.request, 'build_opener', return_value=opener):
            with self.assertRaisesRegex(ValueError, 'response bound'):
                PROBE.exchange('/v1/gateway/orders', PROBE.body(), 'fixture.access.token')


if __name__ == '__main__':
    unittest.main()
