import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tm_session', ROOT / 'tools/tm-account-session.py')
TM = importlib.util.module_from_spec(spec)
spec.loader.exec_module(TM)


class AccountSessionTests(unittest.TestCase):
    def setUp(self):
        self.scratch = Path.home() / '.cache/velocita-build/test-scratch'
        self.scratch.mkdir(parents=True, exist_ok=True)

    def pair(self, expiry='2099-01-01T00:00:00Z'):
        return dict(access_token='aaa.bbb.ccc', refresh_token='ddd.eee.fff',
                    expires_in=expiry, token_type='Bearer')

    def test_order_and_foreign_paths_refused_before_network(self):
        for method, path in [('POST', '/v1/gateway/orders'), ('GET', 'https://other.example'),
                             ('POST', '/v1/gateway/transfers')]:
            with patch.object(TM.urllib.request, 'build_opener') as network:
                with self.assertRaisesRegex(ValueError, 'endpoint refused'):
                    TM.request(method, path)
                network.assert_not_called()

    def test_refresh_keeps_rotated_pair_and_private_permissions(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as d:
            path = Path(d) / 'session.json'
            path.write_text(json.dumps(self.pair('2000-01-01T00:00:00Z')))
            path.chmod(0o600)
            with patch.object(TM, 'request', return_value=self.pair()) as request:
                result, operation = TM.session(path, None, False)
            self.assertEqual(operation, 'refresh')
            self.assertEqual(result, self.pair())
            self.assertEqual(json.loads(path.read_text()), self.pair())
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            request.assert_called_once_with('POST', '/v1/auth/token/refresh',
                                            {'refresh_token': 'ddd.eee.fff'})

    def test_bad_refresh_preserves_previous_session(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as d:
            path = Path(d) / 'session.json'
            previous = json.dumps(self.pair('2000-01-01T00:00:00Z'))
            path.write_text(previous)
            path.chmod(0o600)
            with patch.object(TM, 'request', return_value={'access_token': 'SECRET'}):
                with self.assertRaises(ValueError):
                    TM.session(path, None, False)
            self.assertEqual(path.read_text(), previous)

    def test_valid_session_reused_without_auth_network(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as d:
            path = Path(d) / 'session.json'
            path.write_text(json.dumps(self.pair()))
            path.chmod(0o600)
            with patch.object(TM, 'request') as request:
                self.assertEqual(TM.session(path, None, False)[1], 'reuse')
            request.assert_not_called()

    def test_balance_projection_drops_secrets_and_rejects_inconsistent_cash(self):
        row = dict(symbol='SOL', chain='solana', venue='defi', total='0.040297',
                   available='0.040297', held='0', private_key='SECRET', address='SECRET')
        projected = TM.balances({'data': [row], 'access_token': 'SECRET'})
        self.assertNotIn('SECRET', json.dumps(projected))
        self.assertEqual(projected[0]['available'], '0.040297')
        row['held'] = '1'
        with self.assertRaisesRegex(ValueError, 'inconsistent balance'):
            TM.balances({'data': [row]})

    def test_session_symlink_refused(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as d:
            path = Path(d) / 'session.json'
            path.symlink_to(Path(d) / 'elsewhere')
            with self.assertRaisesRegex(RuntimeError, 'not a regular file'):
                TM.read_private(path)


if __name__ == '__main__':
    unittest.main()
