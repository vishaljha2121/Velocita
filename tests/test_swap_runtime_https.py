import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import test_swap_order_https as fixtures

@unittest.skipUnless(os.environ.get('VELOCITA_SWAP_RUNTIME_HTTPS_PROBE'), 'requires isolated runtime probe')
class RuntimeHttpsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        old = os.environ.get('VELOCITA_SWAP_ORDER_HTTPS_PROBE')
        os.environ['VELOCITA_SWAP_ORDER_HTTPS_PROBE'] = os.environ['VELOCITA_SWAP_RUNTIME_HTTPS_PROBE']
        try:
            fixtures.OrderHttpsTests.setUpClass.__func__(cls)
        finally:
            if old is None:
                os.environ.pop('VELOCITA_SWAP_ORDER_HTTPS_PROBE', None)
            else:
                os.environ['VELOCITA_SWAP_ORDER_HTTPS_PROBE'] = old
        cls.binary = os.environ['VELOCITA_SWAP_RUNTIME_HTTPS_PROBE']

    @classmethod
    def tearDownClass(cls):
        fixtures.OrderHttpsTests.tearDownClass.__func__(cls)

    def setUp(self):
        self.server.mode = 'ok'

    def call(self, mode='normal', host='localhost', ca='server'):
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            token = Path(directory) / 'token'
            token.write_text('fixture-token')
            token.chmod(0o600)
            fd = os.open(token, os.O_RDONLY)
            try:
                result = subprocess.run([self.binary, f'https://{host}:{self.server.port}',
                    str(self.root / f'{ca}.pem'), str(fd), mode], pass_fds=(fd,),
                    capture_output=True, text=True, timeout=6, check=True)
            finally:
                os.close(fd)
        self.assertNotIn('fixture-token', result.stdout + result.stderr)
        return result.stdout.strip()

    def test_exact_execute_once(self):
        before = len(self.server.requests)
        self.assertEqual(self.call(), 'complete=1 local=0 status=200 used=1')
        self.assertEqual(len(self.server.requests), before + 1)
        method, path, body, _, auth = self.server.requests[-1]
        self.assertEqual((method,path,body,auth), ('POST','/v1/gateway/orders/abc/execute',
            b'{"auth_type":"api_key","signatures":["fixture"]}', 'Bearer fixture-token'))

    def test_tls_identity_failures_send_no_http(self):
        before = len(self.server.requests)
        self.assertEqual(self.call(ca='untrusted'), 'complete=0 local=0 status=0 used=1')
        self.assertEqual(self.call(host='127.0.0.1'), 'complete=0 local=0 status=0 used=1')
        self.assertEqual(len(self.server.requests), before)

    def test_no_redirect_refresh_or_lost_reply_retry(self):
        for mode, status, complete in [('unauthorized',401,1),('redirect',307,0),('lostack',0,0)]:
            with self.subTest(mode=mode):
                self.server.mode=mode
                before=len(self.server.requests)
                self.assertEqual(self.call(),f'complete={complete} local=0 status={status} used=1')
                self.assertEqual(len(self.server.requests),before+1)

    def test_execution_bounds_before_io(self):
        for mode in ('wrong-identity','wrong-body','wrong-path','large-body','large-path','large-handle','bad-hash','expired'):
            with self.subTest(mode=mode):
                before=len(self.server.requests)
                self.assertEqual(self.call(mode),'complete=0 local=1 status=0 used=1')
                self.assertEqual(len(self.server.requests),before)

    def test_keyless_finalized_rpc(self):
        before=len(self.server.requests)
        self.assertEqual(self.call('rpc'),'complete=1 status=200 size=11 provider=1')
        self.assertEqual(len(self.server.requests),before+1)
        method,path,body,_,auth=self.server.requests[-1]
        self.assertEqual((method,path,auth),('POST','/',None))
        self.assertIn(b'"method":"getTransaction"',body)
        self.assertIn(b'"commitment":"finalized"',body)

    def test_rpc_rejects_other_methods_and_unbounded_timeout(self):
        for mode in ('rpc-method','rpc-commitment','rpc-signature','rpc-id','rpc-extra','rpc-timeout'):
            with self.subTest(mode=mode):
                before=len(self.server.requests)
                self.assertEqual(self.call(mode),'complete=0 status=0 size=0 provider=1')
                self.assertEqual(len(self.server.requests),before)

    def test_rpc_response_bound(self):
        self.server.mode='prepare'
        self.server.balance_body=b'x'*32768
        self.assertEqual(self.call('rpc'),'complete=1 status=201 size=32768 provider=1')
        self.server.balance_body=b'x'*32769
        self.assertEqual(self.call('rpc'),'complete=0 status=0 size=0 provider=1')

    def test_rpc_tls_and_ambiguous_response(self):
        before=len(self.server.requests)
        self.assertEqual(self.call('rpc',ca='untrusted'),'complete=0 status=0 size=0 provider=1')
        self.assertEqual(len(self.server.requests),before)
        self.server.mode='lostack'
        self.assertEqual(self.call('rpc'),'complete=0 status=0 size=0 provider=1')
        self.assertEqual(len(self.server.requests),before+1)

if __name__=='__main__':unittest.main()
