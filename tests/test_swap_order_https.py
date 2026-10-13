import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_swap_https import FakeSwapTlsServer


@unittest.skipUnless(os.environ.get('VELOCITA_SWAP_ORDER_HTTPS_PROBE'), 'requires isolated order probe')
class OrderHttpsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = Path.home() / '.cache/velocita-build/order-https-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(dir=scratch)
        cls.root = Path(cls.temp.name)
        cls.binary = os.environ['VELOCITA_SWAP_ORDER_HTTPS_PROBE']
        for name in ('server', 'untrusted'):
            subprocess.run([os.environ.get('VELOCITA_TEST_OPENSSL', 'openssl'), 'req', '-x509',
                            '-newkey', 'rsa:2048', '-nodes', '-keyout', str(cls.root / f'{name}.key'),
                            '-out', str(cls.root / f'{name}.pem'), '-days', '1', '-subj', '/CN=localhost',
                            '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)
        cls.server = FakeSwapTlsServer(cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.server.close()
        cls.temp.cleanup()

    def setUp(self):
        self.server.mode = 'swap_drill'
        self.order = dict(order_id='abc', asset_id='495e07ac-fa3e-4179-85b7-b1e8cece3dc4',
                          base_asset='SOL', quote_asset='USDC', asset_type='spot', venue='defi',
                          status='complete', type='market', side='buy', qty_unit='quote', qty='1',
                          executed_qty='0.01', leaves_qty='0', executed_vwap='100', fee='0.002',
                          tx_hash='1GMkH3brNXiNNs1tiFZHu4yZSRrzJwxi5wB9bHFtMinfCXNnR1adh8Vo8NTheK4evneedH4qmvjeqcBBNAefgS')

    def call(self, mode='normal', host='localhost', ca='server'):
        self.server.balance_body = json.dumps(self.order).encode()
        with tempfile.TemporaryDirectory(dir=self.root) as store:
            result = subprocess.run([self.binary, f'https://{host}:{self.server.port}',
                                     str(self.root / f'{ca}.pem'), store, mode], check=True,
                                    capture_output=True, text=True, timeout=6)
        self.assertNotIn('fixture-token', result.stdout + result.stderr)
        return result.stdout.strip()

    def test_exact_authenticated_get_only_retains_identity(self):
        before = len(self.server.requests)
        self.assertEqual(self.call(), 'terminal=1 retained=1 unresolved=1')
        self.assertEqual(len(self.server.requests), before + 1)
        method, path, body, agent, authorization = self.server.requests[-1]
        self.assertEqual((method, path, body, authorization),
                         ('GET', '/v1/gateway/orders/abc', b'', 'Bearer fixture-token'))
        self.assertTrue(agent.startswith('velocita-'))

    def test_tls_identity_failures_send_no_http(self):
        before = len(self.server.requests)
        self.assertEqual(self.call(ca='untrusted'), 'terminal=0 retained=0 unresolved=1')
        self.assertEqual(self.call(host='127.0.0.1'), 'terminal=0 retained=0 unresolved=1')
        self.assertEqual(len(self.server.requests), before)

    def test_no_redirect_refresh_or_lost_reply_retry(self):
        for mode in ('unauthorized', 'redirect', 'lostack'):
            with self.subTest(mode=mode):
                self.server.mode = mode
                before = len(self.server.requests)
                self.assertEqual(self.call(), 'terminal=0 retained=0 unresolved=1')
                self.assertEqual(len(self.server.requests), before + 1)

    def test_wrong_order_cannot_bind_signature(self):
        self.order['order_id'] = 'another-order'
        self.assertEqual(self.call(), 'terminal=0 retained=0 unresolved=1')

    def test_wrong_route_refuses_before_http(self):
        before = len(self.server.requests)
        self.assertEqual(self.call(mode='wrong-path'), 'local_refusal=1')
        self.assertEqual(len(self.server.requests), before)


if __name__ == '__main__':
    unittest.main()
