import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class SwapPaperDrillTests(unittest.TestCase):
    def setUp(self):
        self.binary = os.environ['VELOCITA_SWAP_PAPER_DRILL']
        scratch = Path.home() / '.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'paper.journal'

    def run_drill(self, mode, path=None):
        return subprocess.run([self.binary, mode, str(path or self.path)], capture_output=True,
                              text=True, timeout=10, check=False)

    def create(self):
        result = self.run_drill('--create')
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_process_restart_preserves_exact_money_and_unknown_reservation(self):
        original = self.create()
        result = self.run_drill('--replay')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), original)
        self.assertEqual(original['base_atoms'], 0)
        self.assertEqual(original['quote_atoms'], 5_014_480)
        self.assertEqual(original['cost_quote_atoms'], 0)
        self.assertEqual(original['fees_quote_atoms'], 4_038)
        self.assertEqual(original['realized_pnl_quote_atoms'], 14_480)
        self.assertEqual(original['purchase_debits_quote_atoms'], 1_002_000)
        self.assertEqual(original['reserved_quote_atoms'], 1_002_000)
        self.assertEqual(original['pending_phase'], 4)
        self.assertEqual(original['records'], 14)
        self.assertEqual(original['settlements'], 2)

    def test_existing_journal_is_never_overwritten(self):
        self.create()
        before = self.path.read_bytes()
        result = self.run_drill('--create')
        self.assertEqual(result.returncode, 1)
        self.assertIn('journal open refused', result.stderr)
        self.assertEqual(self.path.read_bytes(), before)

    def test_torn_or_whole_record_tail_loss_refuses_replay(self):
        self.create()
        raw = self.path.read_bytes()
        first_size = int.from_bytes(raw[8:12], 'little')
        for length in [0, 12, first_size, len(raw) - 40, len(raw) - 1]:
            with self.subTest(length=length):
                broken = self.path.parent / f'torn-{length}'
                broken.write_bytes(raw[:length])
                result = self.run_drill('--replay', broken)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, '', 'unverified replay must not publish balances')

    def test_changed_journal_bytes_refuse_replay(self):
        self.create()
        raw = bytearray(self.path.read_bytes())
        raw[24] ^= 1
        broken = self.path.parent / 'changed'
        broken.write_bytes(raw)
        result = self.run_drill('--replay', broken)
        self.assertEqual(result.returncode, 1)
        self.assertIn('journal digest mismatch', result.stderr)
        self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    unittest.main()
