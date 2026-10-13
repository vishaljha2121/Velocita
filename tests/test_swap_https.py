import http.server
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
import warnings


class FakeSwapTlsServer:
    def __init__(self, root):
        self.requests = []
        self.mode = 'ok'
        self.root = root
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def log_message(self, *args):
                pass

            def do_GET(self):
                self.respond()

            def do_POST(self):
                self.respond()

            def respond(self):
                data = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                owner.requests.append((self.command, self.path, data,
                                       self.headers.get('User-Agent'),
                                       self.headers.get('Authorization')))
                mode = owner.mode
                if mode == 'lostack' or (mode == 'swap_drill' and self.command == 'POST'):
                    self.connection.shutdown(socket.SHUT_RDWR)
                    self.connection.close()
                    return
                body = owner.balance_body if mode in ('swap_drill', 'prepare') else b'{"ok":true}'
                if mode == 'exact':
                    body = b'x' * 1024
                elif mode == 'large':
                    body = b'x' * 1025
                code = {'prepare': 201, 'redirect': 307, 'rate': 429, 'error': 500, 'unauthorized': 401}.get(mode, 200)
                self.send_response(code)
                if mode == 'redirect':
                    self.send_header('Location', '/v1/gateway/orders/escape')
                if mode == 'headers':
                    self.send_header('X-Large', 'x' * 8193)
                if mode == 'unauthorized':
                    self.send_header('WWW-Authenticate', 'Basic realm="fixture"')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                try:
                    if mode == 'slow':
                        self.wfile.write(body[:1])
                        self.wfile.flush()
                        time.sleep(0.6)
                        body = body[1:]
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                    pass

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.server.daemon_threads = True
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(root / 'server.pem', root / 'server.key')
        self.server.socket = self.context.wrap_socket(self.server.socket, server_side=True)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    @property
    def port(self):
        return self.server.server_port

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


@unittest.skipUnless(os.environ.get('VELOCITA_SWAP_HTTPS_PROBE'), 'requires opt-in isolated probe')
class SwapHttpsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = Path.home() / '.cache/velocita-build/abc-transport-20261012'
        scratch.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(dir=scratch)
        cls.root = Path(cls.temporary.name)
        cls.binary = os.environ['VELOCITA_SWAP_HTTPS_PROBE']
        openssl = os.environ.get('VELOCITA_TEST_OPENSSL', 'openssl')
        for name in ('server', 'untrusted'):
            subprocess.run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                            '-keyout', str(cls.root / f'{name}.key'), '-out', str(cls.root / f'{name}.pem'),
                            '-days', '1', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                           check=True, capture_output=True)
        (cls.root / 'index').write_text('')
        (cls.root / 'serial').write_text('01\n')
        (cls.root / 'ca.cnf').write_text(
            '[ca]\ndefault_ca=fixture\n[fixture]\ndatabase=index\nserial=serial\nnew_certs_dir=.\n'
            'certificate=server.pem\nprivate_key=server.key\ndefault_md=sha256\n'
            'policy=subject\nx509_extensions=extensions\n[subject]\ncommonName=supplied\n'
            '[extensions]\nsubjectAltName=DNS:localhost\n')
        subprocess.run([openssl, 'req', '-new', '-key', 'server.key', '-subj', '/CN=localhost',
                        '-out', 'expired.csr'], cwd=cls.root, check=True, capture_output=True)
        subprocess.run([openssl, 'ca', '-batch', '-selfsign', '-config', 'ca.cnf', '-in', 'expired.csr',
                        '-out', 'expired.pem', '-startdate', '20000101000000Z', '-enddate', '20010101000000Z'],
                       cwd=cls.root, check=True, capture_output=True)
        cls.fixture = FakeSwapTlsServer(cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.fixture.close()
        cls.temporary.cleanup()

    def setUp(self):
        self.fixture.mode = 'ok'

    def call(self, method='GET', path='/v1/gateway/orders/fixture', host='localhost',
             ca='server', timeout=2000, mode='normal', scheme='https', limit=1024):
        process = subprocess.run([self.binary, f'{scheme}://{host}:{self.fixture.port}',
                                  str(self.root / f'{ca}.pem'), method, path, str(timeout),
                                  str(limit), mode], text=True, capture_output=True, timeout=5, check=True)
        self.assertNotIn('fixture-token', process.stdout + process.stderr, 'token_leaked_to_diagnostics')
        return process.stdout.strip()

    def test_get_and_post_exact_owned_bytes(self):
        self.assertEqual(self.call(), 'complete response_received 200 11', 'trusted_get_failed')
        self.assertEqual(self.call(method='POST', path='/v1/gateway/orders'),
                         'complete response_received 200 11', 'trusted_post_failed')
        self.assertEqual(self.fixture.requests[-1], ('POST', '/v1/gateway/orders', b'{"fixture":true}',
                         'velocita-swap-fixture/1', 'Bearer fixture-token'), 'request_contract_changed')
        self.assertEqual(self.call(mode='owned'), 'owned_response_preserved', 'response_lifetime_not_owned')

    def test_wrong_hostname(self):
        before = len(self.fixture.requests)
        self.assertEqual(self.call(host='127.0.0.1'), 'tls_failure possibly_sent 0 0', 'hostname_verification_required')
        self.assertEqual(len(self.fixture.requests), before, 'hostname_failure_sent_http')

    def test_untrusted_and_expired_certificates(self):
        before = len(self.fixture.requests)
        self.assertEqual(self.call(ca='untrusted'), 'tls_failure possibly_sent 0 0', 'peer_verification_required')
        try:
            self.fixture.context.load_cert_chain(self.root / 'expired.pem', self.root / 'server.key')
            self.assertEqual(self.call(ca='expired'), 'tls_failure possibly_sent 0 0', 'certificate_expiry_required')
        finally:
            self.fixture.context.load_cert_chain(self.root / 'server.pem', self.root / 'server.key')
        self.assertEqual(len(self.fixture.requests), before)

    def test_obsolete_tls_is_rejected(self):
        before = len(self.fixture.requests)
        try:
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', DeprecationWarning)
                self.fixture.context.minimum_version = ssl.TLSVersion.TLSv1_1
                self.fixture.context.maximum_version = ssl.TLSVersion.TLSv1_1
                self.fixture.context.set_ciphers('DEFAULT:@SECLEVEL=0')
            self.assertEqual(self.call(), 'tls_failure possibly_sent 0 0', 'tls12_minimum_required')
            self.assertEqual(len(self.fixture.requests), before)
        finally:
            self.fixture.context.minimum_version = ssl.TLSVersion.TLSv1_2
            self.fixture.context.maximum_version = ssl.TLSVersion.MAXIMUM_SUPPORTED
            self.fixture.context.set_ciphers('DEFAULT:@SECLEVEL=2')

    def test_redirect_never_resends_post(self):
        self.fixture.mode = 'redirect'
        before = len(self.fixture.requests)
        self.assertEqual(self.call(method='POST', path='/v1/gateway/orders'),
                         'redirect response_received 307 0', 'redirect_must_be_refused')
        self.assertEqual(len(self.fixture.requests), before + 1, 'redirect_must_not_resend')

    def test_ambiguous_timeout_and_lost_ack_never_retry(self):
        self.fixture.mode = 'slow'
        before = len(self.fixture.requests)
        start = time.monotonic()
        self.assertEqual(self.call(method='POST', path='/v1/gateway/orders', timeout=150),
                         'timeout possibly_sent 0 0', 'timeout_is_ambiguous')
        self.assertLess(time.monotonic() - start, 1, 'whole_request_deadline_required')
        self.assertEqual(len(self.fixture.requests), before + 1, 'timeout_must_not_retry')
        self.fixture.mode = 'lostack'
        before = len(self.fixture.requests)
        self.assertEqual(self.call(method='POST', path='/v1/gateway/orders'),
                         'transport_failure possibly_sent 0 0', 'lost_ack_is_ambiguous')
        self.assertEqual(len(self.fixture.requests), before + 1, 'lost_ack_must_not_retry')

    def test_body_and_header_bounds(self):
        self.fixture.mode = 'exact'
        self.assertEqual(self.call(), 'complete response_received 200 1024', 'exact_body_capacity_allowed')
        for mode in ('large', 'headers'):
            self.fixture.mode = mode
            self.assertEqual(self.call(), 'oversized possibly_sent 0 0', 'oversized_response_refused')

    def test_http_errors_are_responses_not_terminal_trade_evidence(self):
        for mode, status in [('rate',429),('error',500),('unauthorized',401)]:
            self.fixture.mode = mode
            before = len(self.fixture.requests)
            self.assertEqual(self.call(method='POST', path='/v1/gateway/orders'),
                             f'complete response_received {status} 11', 'http_status_is_not_transport_failure')
            self.assertEqual(len(self.fixture.requests), before + 1, 'http_error_must_not_retry')

    def test_local_refusals_never_touch_server(self):
        before = len(self.fixture.requests)
        cases = [dict(scheme='http'), dict(host='api.truemarkets.co'),
                 dict(path='//escape'), dict(path='/v1/gateway/orders/a?x=1'),
                 dict(method='POST'), dict(method='DELETE'), dict(mode='bad-token'),
                 dict(mode='invalid-origin'), dict(mode='invalid-route'), dict(mode='empty-routes'),
                 dict(mode='too-many-routes'), dict(mode='duplicate-route'), dict(mode='get-with-body'),
                 dict(mode='empty-post',method='POST',path='/v1/gateway/orders'),
                 dict(mode='large-request',method='POST',path='/v1/gateway/orders'),
                 dict(timeout=0), dict(timeout=30001), dict(limit=0), dict(limit=65537)]
        for case in cases:
            self.assertEqual(self.call(**case), 'local_refusal not_attempted 0 0', 'local_refusal_must_precede_send')
        self.assertEqual(len(self.fixture.requests), before)

    def test_fingerprint_is_actual_runtime_metadata(self):
        result = subprocess.run([self.binary, '--fingerprint'], check=True, capture_output=True, text=True)
        data = json.loads(result.stdout)
        for key in ('curl_headers', 'curl_runtime', 'tls_backend', 'curl_host'):
            self.assertTrue(data[key], 'runtime_fingerprint_required')
        self.assertTrue(data['ssl'] and data['async_dns'])


if __name__ == '__main__':
    unittest.main()
