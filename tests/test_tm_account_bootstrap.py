import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bootstrap', ROOT / 'tools/tm-account-bootstrap-check.py')
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)


class BootstrapTests(unittest.TestCase):
    def fixture(self):
        scratch = Path.home() / '.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(tmp.cleanup)
        directory = Path(tmp.name)
        pair = dict(access_token='aaa.bbb.ccc', refresh_token='ddd.eee.fff',
                    expires_in='2099-01-01T00:00:00Z', token_type='Bearer')
        session = directory / 'session.json'
        session.write_text(json.dumps(pair))
        session.chmod(0o600)
        return session, directory / 'capture'

    def test_fixed_two_reads_and_private_no_retry_capture(self):
        session, output = self.fixture()
        with patch.object(M, 'read', side_effect=[b'{"id":"fixture"}', b'{"data":[]}']) as read:
            report = M.collect(session, output)
        self.assertEqual([c.args[0] for c in read.call_args_list], list(M.PATHS))
        self.assertFalse(report['orders_sent'])
        self.assertFalse(report['live_admitted'])
        self.assertNotIn('aaa.bbb.ccc', json.dumps(report))
        self.assertEqual((output / 'setup.json').stat().st_mode & 0o777, 0o600)
        with patch.object(M, 'read') as read:
            with self.assertRaisesRegex(RuntimeError, "not a regular file"):
                M.collect(session, output)
            read.assert_not_called()

    def test_first_error_stops_second_request(self):
        session, output = self.fixture()
        with patch.object(M, 'read', side_effect=ValueError('refused')) as read:
            with self.assertRaises(ValueError):
                M.collect(session, output)
            self.assertEqual(read.call_count, 1)
        self.assertTrue((output / 'attempt.json').exists())
        self.assertFalse((output / 'result.json').exists())

    def test_order_endpoint_cannot_be_used(self):
        with patch.object(M.urllib.request, 'build_opener') as network:
            with self.assertRaisesRegex(ValueError, 'endpoint refused'):
                M.read('/v1/gateway/orders', 'aaa.bbb.ccc')
            network.assert_not_called()

    def test_missing_session_does_not_mint(self):
        session, output = self.fixture()
        session.unlink()
        with patch.object(M.TM, 'mint') as mint:
            with self.assertRaisesRegex(ValueError, 'initialization requires'):
                M.collect(session, output)
            mint.assert_not_called()


if __name__ == '__main__':
    unittest.main()
