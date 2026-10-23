"""The route-fix upgrade changes only the executable pins of the sealed aae119b9 manifest and refuses any other starting point."""
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('owner_upgrade_restart', Path(__file__).with_name('owner_upgrade_restart.py'))
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)
BINARY = 'b' * 64


class Upgrade(unittest.TestCase):
    def setUp(self):
        M.configure(BINARY)
        self.old = json.loads((Path(__file__).parents[1]/'dormant-manifest.json').read_text())
        self.old.update(launch_authorized=True, initialize_swap_recovery=False, source_commit=M.OLD_COMMIT,
                        binary=f'/opt/velocita/releases/{M.OLD_COMMIT}/velocita-v2-gateway', binary_sha256='8d0fab33246d85628cac6471c3926205fd5710f96a55731106324a101cc9abcf',
                        runtime_profile_sha256='1' * 64, account_review_sha256='2' * 64, account_history_sha256='3' * 64)
        self.profile = M.F.T.P

    def test_only_executable_pins_change(self):
        new = M.next_manifest(self.old, self.profile)
        self.assertEqual({k for k in self.old if self.old[k] != new[k]}, {'source_commit', 'binary', 'binary_sha256'})
        self.assertEqual((new['source_commit'], new['binary_sha256'], new['binary']),
                         (M.COMMIT, BINARY, f'/opt/velocita/releases/{M.COMMIT}/velocita-v2-gateway'))
        self.assertEqual(M.U.PINS['velocita-v2-gateway'], BINARY)
        self.assertEqual(M.U.PINS['service_launcher.py'], '5f1123d9093cd75c69cfe57b1c19239e05d4420d7f787a6d383ccaa84a49fe38')

    def test_refuses_anything_but_the_sealed_1f8d7b6_install(self):
        for change in (dict(initialize_swap_recovery=True), dict(launch_authorized=False), dict(recovery_only=True),
                       dict(source_commit='0' * 40), dict(incarnation='0' * 32), dict(runtime_image_sha256='0' * 64)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                M.next_manifest(dict(self.old, **change), self.profile)

    def test_binary_pin_must_be_exact(self):
        for bad in ('', 'B' * 64, 'b' * 63):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, 'SHA-256'):
                M.configure(bad)


if __name__ == '__main__':
    unittest.main()
