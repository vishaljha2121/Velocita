import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('probe', ROOT / 'tools/tm-unsigned-contract-probe.py')
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)


def catalog():
    return {'data': [dict(id=P.ASSET, chain='solana', address=P.MINT, symbol='SOL',
                         decimals=9, is_active=True, status='active', tradeable=True,
                         venue='defi', type='spot')]}


def quote():
    return dict(order_id='private-order', status='initialized',
                payloads=[dict(payload='PRIVATE_PAYLOAD', digest='unused')],
                quote=dict(qty='1.00', qty_out='0.008', fee='0.002', address='PRIVATE_ADDRESS'))


class ProbeTests(unittest.TestCase):
    def test_fixed_amounts_and_no_other_requests(self):
        self.assertEqual([(x['side'], x['qty'], x['qty_unit']) for x in P.bodies()],
                         [('buy', '1.00', 'quote'), ('sell', '0.0084', 'base')])
        with patch.object(P.urllib.request, 'build_opener') as net:
            for body in [dict(P.bodies()[0], qty='2'), {'execute': True}, {}]:
                with self.assertRaisesRegex(ValueError, 'outside protocol'):
                    P.exchange(body, 'test')
            net.assert_not_called()

    def test_wrong_route_mint_or_ambiguous_identity_refused(self):
        P.verify_asset(catalog())
        for key, value in [('venue', 'cefi'), ('chain', 'hypercore-perp'),
                           ('address', 'wrong'), ('decimals', 8), ('tradeable', 1)]:
            d = catalog(); d['data'][0][key] = value
            with self.assertRaisesRegex(ValueError, 'identity'):
                P.verify_asset(d)
        d = catalog(); d['data'].append(d['data'][0])
        with self.assertRaisesRegex(ValueError, 'identity'):
            P.verify_asset(d)

    def test_projection_excludes_payload_identity_and_extra_fields(self):
        s = P.response_summary(json.dumps(quote()).encode())
        self.assertEqual(s['quote'], {'qty': '1.00', 'qty_out': '0.008', 'fee': '0.002'})
        self.assertEqual(s['payload_count'], 1)
        self.assertFalse(s['signed']); self.assertFalse(s['executed'])
        self.assertNotIn('PRIVATE', json.dumps(s)); self.assertNotIn('private-order', json.dumps(s))

    def test_noninitialized_or_problem_quotes_stop(self):
        for key, value in [('status', 'pending'), ('status', 'complete'), ('order_id', ''),
                           ('payloads', []), ('issues', ['insufficient_balance'])]:
            d = quote(); d[key] = value
            with self.assertRaises(ValueError):
                P.response_summary(json.dumps(d).encode())
        d = quote(); d['quote']['issues'] = ['minimum']
        with self.assertRaises(ValueError):
            P.response_summary(json.dumps(d).encode())

    def exercise(self, responses, check):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            with patch.object(P.ACCOUNT, 'session', return_value=({'access_token':'aaa.bbb.ccc'}, 'reuse')) as auth:
                with patch.object(P, 'exchange', side_effect=responses) as net:
                    check(root, auth, net)

    def test_exact_batch_private_capture_and_replay_refusal(self):
        def check(root, auth, net):
            result = P.run(root/'session.json', root/'capture')
            self.assertEqual(len(result), 2)
            self.assertEqual(net.call_count, 3)
            self.assertEqual(net.call_args_list[1].args, (P.bodies()[0], 'aaa.bbb.ccc'))
            self.assertEqual(net.call_args_list[2].args, (P.bodies()[1], 'aaa.bbb.ccc'))
            auth.assert_called_once_with(root/'session.json', None, False)
            for f in (root/'capture').iterdir():
                self.assertEqual(f.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):
                P.run(root/'session.json', root/'capture')
            self.assertEqual(net.call_count, 3, 'replay reached network')
        self.exercise([json.dumps(catalog()).encode()] + [json.dumps(quote()).encode()]*2
                      + [json.dumps(catalog()).encode()], check)

    def test_uncertain_first_post_stops_without_second_or_retry(self):
        def check(root, auth, net):
            with self.assertRaises(TimeoutError):
                P.run(root/'session.json', root/'capture')
            self.assertEqual(net.call_count, 2, 'uncertain create retried or second create sent')
            self.assertTrue((root/'capture/attempt.json').is_file())
            with self.assertRaises(FileExistsError):
                P.run(root/'session.json', root/'capture')
            self.assertEqual(net.call_count, 2)
        self.exercise([json.dumps(catalog()).encode(), TimeoutError()], check)

    def test_metadata_failure_never_creates(self):
        def check(root, auth, net):
            with self.assertRaises(ValueError):
                P.run(root/'session.json', root/'capture')
            self.assertEqual(net.call_count, 1, 'create sent without verified DeFi identity')
        self.exercise([b'{"data":[]}'], check)


if __name__ == '__main__':
    unittest.main()
