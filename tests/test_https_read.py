import http.server
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import time
import unittest


@unittest.skipUnless(os.environ.get('VELOCITA_HTTPS_PROBE'), 'requires the isolated HTTPS test executable')
class HttpsReadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = Path.home() / '.vc-test'
        scratch.mkdir(parents=True, exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(dir=scratch)
        cls.root = Path(cls.scratch.name)
        cls.binary = os.environ['VELOCITA_HTTPS_PROBE']
        openssl = os.environ.get('VELOCITA_TEST_OPENSSL', 'openssl')
        for name in ('server', 'untrusted'):
            subprocess.run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                            '-keyout', str(cls.root / f'{name}.key'), '-out', str(cls.root / f'{name}.pem'),
                            '-days', '1', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                           check=True, capture_output=True)
        (cls.root / 'index').write_text('')
        (cls.root / 'serial').write_text('01\n')
        (cls.root / 'ca.cnf').write_text(
            '[ca]\ndefault_ca=fixture\n[fixture]\n'
            'database=index\nserial=serial\nnew_certs_dir=.\n'
            'certificate=server.pem\nprivate_key=server.key\n'
            'default_md=sha256\npolicy=subject\nx509_extensions=extensions\n'
            '[subject]\ncommonName=supplied\n'
            '[extensions]\nsubjectAltName=DNS:localhost\n')
        subprocess.run([openssl, 'req', '-new', '-key', 'server.key', '-subj', '/CN=localhost',
                        '-out', 'expired.csr'], cwd=cls.root, check=True, capture_output=True)
        subprocess.run([openssl, 'ca', '-batch', '-selfsign', '-config', 'ca.cnf',
                        '-in', 'expired.csr', '-out', 'expired.pem',
                        '-startdate', '20000101000000Z', '-enddate', '20010101000000Z'],
                       cwd=cls.root, check=True, capture_output=True)
        cls.requests = []
        cls.client_identifiers = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                cls.requests.append((self.path, self.headers.get('Authorization')))
                cls.client_identifiers.append(self.headers.get('User-Agent'))
                if self.path == '/v1/gateway/balances':
                    self.send_response(302)
                    self.send_header('Location', '/escape')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                data = b'{"ok":true}'
                if self.path.endswith('/large'):
                    data = b'x' * 1025
                if self.path.endswith('/exact'):
                    data = b'x' * 1024
                self.send_response(404 if self.path.endswith('/missing') else 200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                if self.path.endswith('/slow'):
                    self.wfile.write(data[:1])
                    self.wfile.flush()
                    data = data[1:]
                    time.sleep(0.6)
                try:
                    self.wfile.write(data)
                except (BrokenPipeError, ssl.SSLError):
                    pass

        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cls.root / 'server.pem', cls.root / 'server.key')
        cls.context = context
        cls.server.socket = context.wrap_socket(cls.server.socket, server_side=True)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.scratch.cleanup()

    def call(self, path='/v1/gateway/assets', host='localhost', ca='server', timeout=2000, scheme='https'):
        result = subprocess.run([self.binary, f'{scheme}://{host}:{self.server.server_port}',
                                 str(self.root / f'{ca}.pem'), path, str(timeout)],
                                text=True, capture_output=True, timeout=4, check=True)
        return result.stdout.strip()

    def test_trusted_host_and_exact_body_bound(self):
        self.assertEqual(self.call(), 'complete 200 11')
        self.assertEqual(self.requests[-1], ('/v1/gateway/assets', 'Bearer fixture-token'))
        self.assertEqual(self.call('/v1/gateway/orders/exact'), 'complete 200 1024')

    def test_truthful_client_identification(self):
        self.assertEqual(self.call(), 'complete 200 11')
        self.assertEqual(self.client_identifiers[-1], 'velocita-contract-read/1',
                         'HTTPS reader must identify the actual Velocita client')

    def test_wrong_hostname_is_refused(self):
        before = len(self.requests)
        self.assertEqual(self.call(host='127.0.0.1'), 'tls_failure 0 0', 'hostname guard must reject before HTTP')
        self.assertEqual(len(self.requests), before)

    def test_untrusted_certificate_is_refused(self):
        before = len(self.requests)
        self.assertEqual(self.call(ca='untrusted'), 'tls_failure 0 0', 'CA guard must reject before HTTP')
        self.assertEqual(len(self.requests), before)

    def test_expired_certificate_is_refused(self):
        before = len(self.requests)
        try:
            self.context.load_cert_chain(self.root / 'expired.pem', self.root / 'server.key')
            self.assertEqual(self.call(ca='expired'), 'tls_failure 0 0')
            self.assertEqual(len(self.requests), before)
        finally:
            self.context.load_cert_chain(self.root / 'server.pem', self.root / 'server.key')

    def test_redirect_is_not_followed(self):
        before = len(self.requests)
        self.assertEqual(self.call('/v1/gateway/balances'), 'refused 302 0', 'redirect guard must refuse')
        self.assertEqual(len(self.requests), before + 1)

    def test_response_bound_and_whole_request_timeout(self):
        self.assertEqual(self.call('/v1/gateway/orders/large'), 'oversized 0 0')
        start = time.monotonic()
        self.assertEqual(self.call('/v1/gateway/orders/slow', timeout=150), 'timeout 0 0', 'total deadline must bound body reads')
        self.assertLess(time.monotonic() - start, 1.0)

    def test_no_cleartext_or_write_routes(self):
        before = len(self.requests)
        self.assertEqual(self.call(scheme='http'), 'unavailable 0 0')
        self.assertEqual(self.call('/v1/gateway/orders/a/execute'), 'refused 0 0')
        self.assertEqual(self.call('//evil.invalid/'), 'refused 0 0')
        self.assertEqual(len(self.requests), before)

    def test_http_error_is_distinct_from_transport_failure(self):
        self.assertEqual(self.call('/v1/gateway/orders/missing'), 'complete 404 11')


if __name__ == '__main__':
    unittest.main()
