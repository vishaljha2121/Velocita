import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import unittest

INCARNATION = '00000000000000000000000000000001'


def u64(*values):
    return struct.pack('<' + 'Q' * len(values), *values)


def record(kind, role, body, event=100):
    prefix = struct.pack('<IHBBIQ', 0x31505356, 1, kind, role, 52 + len(body), event) + body
    return prefix + hashlib.sha256(prefix).digest()


def opening(incarnation=INCARNATION):
    return (hashlib.sha256(bytes.fromhex(incarnation)).digest() + bytes([2])*32 + bytes([3])*32 +
            u64(2, 2, 99, 1, 1, 1000000, 1000000, 2000, 500000, 40000000, 0,
                146635156, 100000000, 30000, 10000, 40000000, 25000000, 4000000) + bytes([4])*32 + u64(1, 1))


def grant(body=None, caller=21, sequence=3, evidence=bytes([97])*32, kill=1, executor=1):
    original = opening() if body is None else body
    return hashlib.sha256(record(1, 1, original, 0)).digest() + evidence + u64(caller, sequence, kill, executor)


class GrantTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root.mkdir(mode=0o700, parents=True, exist_ok=False)
        cls.records = {'opening': record(1, 1, opening()), 'grant': record(10, 1, grant(), 101),
                       'wrong-role': record(10, 2, grant(), 101), 'wrong-opening-role': record(1, 2, opening()),
                       'different-opening': record(1, 1, opening('00000000000000000000000000000002')),
                       'other-cluster-grant': record(10, 1, grant(opening('00000000000000000000000000000002')), 101),
                       'operator-target': record(10, 1, grant(caller=9), 101),
                       'zero-sequence': record(10, 1, grant(sequence=0), 101),
                       'empty-evidence': record(10, 1, grant(evidence=bytes(32)), 101),
                       'zero-kill': record(10, 1, grant(kill=0), 101),
                       'zero-executor': record(10, 1, grant(executor=0), 101),
                       'zero-time': record(10, 1, grant(), 0),
                       'trailing': record(10, 1, grant(), 101)+b'x'}
        for name, data in cls.records.items():
            (cls.root/name).write_bytes(data)

    @classmethod
    def tearDownClass(cls):
        for name, data in cls.records.items():
            if (cls.root/name).read_bytes() != data:
                raise AssertionError('grant inputs must remain unchanged')

    def invoke(self, *names):
        return subprocess.run([str(self.binary), '--dry-run', '--incarnation', INCARNATION,
                               'spot-swap-record', *(str(self.root/name) for name in names)],
                              capture_output=True, text=True, timeout=10)

    def test_grant_summary(self):
        result = self.invoke('grant', 'opening')
        self.assertEqual(result.returncode, 0, result.stderr)
        for text in ('SWAP STRATEGY GRANT caller=21 first_sequence=3', 'kill_epoch=1 executor_generation=1',
                     'opening_digest='+hashlib.sha256(record(1, 1, opening(), 0)).hexdigest(),
                     'review_evidence='+bytes([97]*32).hex(), 'DRY-RUN not sent'):
            self.assertIn(text, result.stdout)

    def test_identity_and_role(self):
        for names in (('grant',), ('grant', 'different-opening'), ('grant', 'wrong-opening-role'),
                      ('wrong-role', 'opening'), ('other-cluster-grant', 'different-opening')):
            with self.subTest(names=names):
                result=self.invoke(*names)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('DRY-RUN not sent', result.stdout)

    def test_values_and_canonical_bytes(self):
        for name in ('operator-target','zero-sequence','empty-evidence','zero-kill','zero-executor','zero-time','trailing'):
            with self.subTest(name=name):
                result=self.invoke(name,'opening')
                self.assertNotEqual(result.returncode,0)
                self.assertNotIn('DRY-RUN not sent',result.stdout)


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--client',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    GrantTests.binary=args.client.resolve()
    GrantTests.root=args.output.resolve()
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(GrantTests))
    (args.output/'result.json').write_text(json.dumps(dict(tests=result.testsRun,failures=len(result.failures),
        errors=len(result.errors),network_used=False,funds_moved=False),indent=2)+'\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
