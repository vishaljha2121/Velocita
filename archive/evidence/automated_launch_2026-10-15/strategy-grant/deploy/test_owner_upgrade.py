"""Exact release transition and halted-state requirements for the existing cluster upgrade."""
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('grant_owner_upgrade', HERE/'owner_upgrade.py')
W = importlib.util.module_from_spec(spec)
spec.loader.exec_module(W)
M = W.M


class Upgrade(unittest.TestCase):
    def setUp(self):
        W.configure(W.BINARY)
        self.old = json.loads((HERE.parents[1]/'dormant-manifest.json').read_text())
        self.old.update(launch_authorized=True, initialize_swap_recovery=False, source_commit=M.OLD_COMMIT,
                        binary=f'/opt/velocita/releases/{M.OLD_COMMIT}/velocita-v2-gateway',
                        binary_sha256='60f27026d1fdacd376f235acebbc53849b324e5724b4737f38a4d80b97159969',
                        runtime_profile_sha256='1'*64, account_review_sha256='2'*64, account_history_sha256='3'*64)

    def test_release_and_archive_are_exact(self):
        self.assertEqual(M.U.COMMIT, 'ba299ff6c89392177cfd4702fda11dabb7ee1719')
        self.assertEqual(M.U.PINS['velocita-v2-gateway'], W.BINARY)
        self.assertEqual(M.U.RELEASE, Path('/opt/velocita/releases')/W.COMMIT)
        self.assertEqual(M.S.ARCHIVE, Path('/var/lib/velocita-strategy-grant-upgrade-ba299ff'))
        self.assertEqual(M.OLD_MANIFEST, 'e328a6e388f9b1e532edeb62dd41af8866905505bb586c051dcf22b15ded880d')
        self.assertIs(M.health, W.health)
        self.assertIs(M.configure, W.configure)

    def test_only_executable_fields_change(self):
        new = M.next_manifest(self.old, M.F.T.P)
        self.assertEqual({k for k in self.old if self.old[k] != new[k]}, {'source_commit', 'binary', 'binary_sha256'})
        self.assertEqual(new['source_commit'], W.COMMIT)
        self.assertEqual(new['binary_sha256'], W.BINARY)

    def test_wrong_installation_refuses(self):
        for change in (dict(source_commit='1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc'), dict(incarnation='0'*32),
                       dict(launch_authorized=False), dict(initialize_swap_recovery=True), dict(recovery_only=True)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                M.next_manifest(dict(self.old, **change), M.F.T.P)

    def test_other_binary_refuses(self):
        with self.assertRaisesRegex(ValueError, 'native-qualified'):
            W.configure('b'*64)

    def test_all_halted_passes(self):
        rows = [dict(strategy_mode=5) for _ in range(3)]
        with patch.object(W, 'base_health', return_value=rows):
            self.assertEqual(W.health(), rows)

    def test_any_normal_voter_refuses(self):
        for index in range(3):
            rows = [dict(strategy_mode=5) for _ in range(3)]
            rows[index]['strategy_mode'] = 1
            with self.subTest(index=index), patch.object(W, 'base_health', return_value=rows), self.assertRaisesRegex(ValueError, 'owner-halted'):
                W.health()

    def test_missing_voter_refuses(self):
        with patch.object(W, 'base_health', return_value=[dict(strategy_mode=5)]*2), self.assertRaises(ValueError):
            W.health()

    def test_base_health_failure_propagates(self):
        with patch.object(W, 'base_health', side_effect=ValueError('not ready')), self.assertRaisesRegex(ValueError, 'not ready'):
            W.health()


if __name__ == '__main__':
    unittest.main()
