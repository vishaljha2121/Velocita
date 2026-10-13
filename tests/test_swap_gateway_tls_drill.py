import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_swap_https import FakeSwapTlsServer


@unittest.skipUnless(os.environ.get('VELOCITA_SWAP_GATEWAY_TLS_DRILL'), 'requires isolated test executable')
class CombinedDrill(unittest.TestCase):
    def test_lost_ack_restart_and_authenticated_reconcile(self):
        root = Path.home()/'.cache/v2-swaptls'
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            work = Path(directory)
            subprocess.run([os.environ.get('VELOCITA_TEST_OPENSSL','openssl'), 'req','-x509','-newkey','rsa:2048','-nodes',
                '-keyout',str(work/'server.key'),'-out',str(work/'server.pem'),'-days','1','-subj','/CN=localhost',
                '-addext','subjectAltName=DNS:localhost'], check=True, capture_output=True)
            server = FakeSwapTlsServer(work)
            try:
                server.mode = 'swap_drill'
                server.balance_body = b'{"data":[{"symbol":"SOL","chain":"solana","venue":"defi","total":"0.009259259","available":"0.009259259","held":"0"},{"symbol":"USDC","chain":"solana","venue":"defi","total":"3.998","available":"3.998","held":"0"}]}'
                env = dict(os.environ, GATEWAY_TEST='swap_tls_unknown_restart_reconcile',
                    VELOCITA_SWAP_DRILL_ORIGIN=f'https://localhost:{server.port}',VELOCITA_SWAP_DRILL_CA=str(work/'server.pem'),
                    TMPDIR=str(root))
                result = subprocess.run([env['VELOCITA_SWAP_GATEWAY_TLS_DRILL']],env=env,capture_output=True,text=True,timeout=60)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('PASS swap_tls_unknown_restart_reconcile',result.stdout)
                self.assertEqual([(r[0],r[1]) for r in server.requests],
                    [('POST','/fixture/swap'),('GET','/v1/gateway/balances')], 'exactly one POST and one post-settlement GET; no replay')
                self.assertEqual(server.requests[0][2],b'{"fixture":true}')
                print(result.stdout, end='')
            finally:
                server.close()


if __name__ == '__main__': unittest.main()
