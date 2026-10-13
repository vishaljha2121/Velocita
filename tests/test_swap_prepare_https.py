import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_swap_https import FakeSwapTlsServer


@unittest.skipUnless(os.environ.get('VELOCITA_SWAP_PREPARE_HTTPS_TESTS'), 'requires isolated prepare tests')
class PrepareHttpsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = Path.home()/'.cache/velocita-build/prepare-https-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(dir=scratch)
        cls.root = Path(cls.temp.name)
        for name in ('server', 'untrusted'):
            subprocess.run([os.environ.get('VELOCITA_TEST_OPENSSL', 'openssl'), 'req', '-x509',
                            '-newkey', 'rsa:2048', '-nodes', '-keyout', str(cls.root/f'{name}.key'),
                            '-out', str(cls.root/f'{name}.pem'), '-days', '1', '-subj', '/CN=localhost',
                            '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)
        cls.server = FakeSwapTlsServer(cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.server.close()
        cls.temp.cleanup()

    def setUp(self):
        self.server.mode = 'prepare'
        payload = '{"activity":"synthetic"}\n'
        self.document = dict(order_id='order-1', status='initialized', payloads=[dict(
            payload=payload, digest=hashlib.sha256(payload.encode()).hexdigest())], quote=dict(
            base_asset='So11111111111111111111111111111111111111112',
            quote_asset='EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v',
            fee_asset='EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v',
            qty='1', qty_out='0.009191912', fee='0.002'))

    def call(self, mode='success', host='localhost', ca='server'):
        self.server.balance_body = json.dumps(self.document).encode()
        env = dict(os.environ, PREPARE_FIXTURE_ORIGIN=f'https://{host}:{self.server.port}',
                   PREPARE_FIXTURE_CA=str(self.root/f'{ca}.pem'), PREPARE_FIXTURE_MODE=mode)
        result = subprocess.run([env['VELOCITA_SWAP_PREPARE_HTTPS_TESTS'], 'https_prepare_and_recovery'],
                                env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn('PASS https_prepare_and_recovery', result.stdout)
        self.assertNotIn('fixture-token', result.stdout+result.stderr)

    def test_reserved_principal_reaches_one_unsigned_create_and_recovers(self):
        before = len(self.server.requests)
        self.call()
        self.assertEqual(len(self.server.requests), before+1, 'restart must not re-create')
        method, path, body, agent, authorization = self.server.requests[-1]
        self.assertEqual((method, path, authorization), ('POST', '/v1/gateway/orders', 'Bearer fixture-token'))
        self.assertEqual(json.loads(body), dict(base_asset='So11111111111111111111111111111111111111112',
            chain='solana', side='buy', type='market', qty='1.000000', qty_unit='quote'))
        self.assertTrue(agent.startswith('velocita-'))

    def test_lost_reply_http_errors_and_redirect_never_retry(self):
        for mode in ('lostack', 'unauthorized', 'redirect', 'error', 'rate'):
            with self.subTest(mode=mode):
                self.server.mode = mode
                before = len(self.server.requests)
                self.call(mode='unresolved')
                self.assertEqual(len(self.server.requests), before+1)

    def test_fee_or_payload_mismatch_stays_unresolved(self):
        for mode in ('fee', 'digest'):
            with self.subTest(mode=mode):
                self.setUp()
                if mode == 'fee':
                    self.document['quote']['fee'] = '0.001'
                else:
                    self.document['payloads'][0]['digest'] = '00'*32
                before = len(self.server.requests)
                self.call(mode='unresolved')
                self.assertEqual(len(self.server.requests), before+1)

    def test_tls_identity_failure_sends_no_http(self):
        before = len(self.server.requests)
        self.call(mode='unresolved', ca='untrusted')
        self.call(mode='unresolved', host='127.0.0.1')
        self.assertEqual(len(self.server.requests), before)

    def test_wrong_path_quantity_and_lengths_send_no_http(self):
        before = len(self.server.requests)
        self.call(mode='invalid')
        self.assertEqual(len(self.server.requests), before)


if __name__ == '__main__':
    unittest.main()
