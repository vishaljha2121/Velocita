import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import unittest


INCARNATION = '00000000000000000000000000000001'
PRICE = 146635156
NEW_PRICE = 147537213
EVIDENCE = hashlib.sha256(b'owner-reviewed-price-amendment').digest()


def u64(*values):
    return struct.pack('<' + 'Q' * len(values), *values)


def record(kind, role, body, event=100):
    prefix = struct.pack('<IHBBIQ', 0x31505356, 1, kind, role, 52 + len(body), event) + body
    return prefix + hashlib.sha256(prefix).digest()


def binding(incarnation=INCARNATION):
    return hashlib.sha256(bytes.fromhex(incarnation)).digest() + bytes([2]) * 32 + bytes([3]) * 32 + u64(2, 2, 99, 1, 1)


def opening(incarnation=INCARNATION, quote=25000000):
    return (binding(incarnation) + u64(1000000, 1000000, 2000, 500000, 40000000, 0,
            PRICE, 100000000, 30000, 10000) + u64(40000000, quote, 4000000) + bytes([4]) * 32 + u64(1, 1))


def amendment(body=None, price=PRICE):
    body = opening() if body is None else body
    return hashlib.sha256(record(1, 1, body, 0)).digest() + EVIDENCE + u64(price, NEW_PRICE, 0, 1, 1)


class ClientAmendmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root.mkdir(mode=0o700, parents=True, exist_ok=False)
        cls.fixtures = {
            'opening': record(1, 1, opening()),
            'amendment': record(9, 1, amendment(), 120),
            'intent': record(2, 2, binding() + u64(1) + bytes([1]) +
                             u64(998000, 1000000, 0, 2000, 0, 6777951, 0, 130, 200, 1, 1), 130),
            'different-opening': record(1, 1, opening(quote=25000001)),
            'wrong-price': record(9, 1, amendment(price=PRICE-1), 120),
            'wrong-amendment-role': record(9, 2, amendment(), 120),
            'wrong-opening-role': record(1, 2, opening()),
            'other-incarnation-opening': record(1, 1, opening('00000000000000000000000000000002')),
            'other-incarnation-amendment': record(9, 1, amendment(opening('00000000000000000000000000000002')), 120),
            'trailing-amendment': record(9, 1, amendment(), 120) + b'x',
            'trailing-opening': record(1, 1, opening()) + b'x',
            'oversized': bytes(513),
        }
        for name, value in cls.fixtures.items():
            (cls.root / name).write_bytes(value)

    @classmethod
    def tearDownClass(cls):
        if any((cls.root / name).read_bytes() != value for name, value in cls.fixtures.items()):
            raise AssertionError('client_must_preserve_all_input_records')

    def invoke(self, *names, incarnation=INCARNATION):
        command = [str(self.binary), '--dry-run', '--incarnation', incarnation, 'spot-swap-record']
        command += [str(self.root / name) for name in names]
        return subprocess.run(command, capture_output=True, text=True, timeout=10)

    def refused(self, *names, assertion):
        result = self.invoke(*names)
        self.assertNotEqual(result.returncode, 0, assertion)
        self.assertNotIn('DRY-RUN not sent', result.stdout, assertion)

    def test_00_amendment_accepts_bound_opening(self):
        result = self.invoke('amendment', 'opening')
        self.assertEqual(result.returncode, 0, 'amendment_with_original_opening_must_parse: ' + result.stderr)
        for expected in ('SWAP BUY PRICE AMENDMENT', 'expected_price=146635156', 'replacement_price=147537213',
                         'opening_digest=' + hashlib.sha256(record(1, 1, opening(), 0)).hexdigest(),
                         'review_evidence=' + EVIDENCE.hex(), 'DRY-RUN not sent'):
            self.assertIn(expected, result.stdout, 'amendment_summary_must_show_reviewed_economics')

    def test_legacy_records(self):
        for name, summary in (('opening', 'SWAP FUNDING'), ('intent', 'SWAP INTENT')):
            result = self.invoke(name)
            self.assertEqual(result.returncode, 0, 'legacy_record_syntax_preserved: ' + result.stderr)
            self.assertIn(summary, result.stdout)
            self.assertIn('DRY-RUN not sent', result.stdout)

    def test_opening_identity(self):
        self.refused('amendment', 'different-opening', assertion='wrong_opening_digest_must_refuse')
        self.refused('wrong-price', 'opening', assertion='wrong_expected_price_must_refuse')
        self.refused('other-incarnation-amendment', 'other-incarnation-opening', assertion='matching_pair_wrong_cluster_must_refuse')
        self.refused('amendment', 'intent', assertion='opening_must_be_initialize')

    def test_roles_and_arity(self):
        self.refused('wrong-amendment-role', 'opening', assertion='amendment_requires_operator_role')
        self.refused('amendment', 'wrong-opening-role', assertion='opening_requires_operator_role')
        self.refused('amendment', assertion='amendment_requires_original_opening')
        self.refused('opening', 'opening', assertion='initialize_cannot_take_amendment_syntax')
        self.refused('intent', 'opening', assertion='intent_cannot_take_amendment_syntax')

    def test_bounded_canonical_records(self):
        for first, second in (('trailing-amendment', 'opening'), ('amendment', 'trailing-opening'),
                              ('oversized', 'opening'), ('amendment', 'oversized')):
            self.refused(first, second, assertion='both_records_must_be_bounded_canonical')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    ClientAmendmentTests.binary = args.client.resolve()
    ClientAmendmentTests.root = args.output.resolve()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ClientAmendmentTests))
    args.output.joinpath('result.json').write_text(json.dumps({'tests': result.testsRun, 'failures': len(result.failures),
        'errors': len(result.errors), 'network_used': False, 'funds_moved': False}, indent=2) + '\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
