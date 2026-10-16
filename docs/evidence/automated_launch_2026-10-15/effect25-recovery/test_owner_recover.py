"""The effect 25 recovery changes only the executable pins, the profile pin and recovery_only, and the profile only in its gateway session."""
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('owner_recover', Path(__file__).with_name('owner_recover.py'))
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)
BINARY = 'b' * 64
NOW = 1_792_160_000_000
PROFILE = b'schema=1\nbearer_digest=' + b'a' * 64 + b'\nexpires_at_ms=1792158514459\nsocket=/run/velocita-swap-signer/sign.sock\n'


class Recover(unittest.TestCase):
    def setUp(self):
        M.configure(BINARY)
        self.old = json.loads((Path(__file__).parents[1]/'dormant-manifest.json').read_text())
        self.old.update(launch_authorized=True, initialize_swap_recovery=False, source_commit=M.OLD_COMMIT,
                        binary=f'/opt/velocita/releases/{M.OLD_COMMIT}/velocita-v2-gateway', binary_sha256=M.OLD_BINARY,
                        runtime_profile_sha256='1' * 64, account_review_sha256='2' * 64, account_history_sha256='3' * 64)
        self.profile = M.F.T.P

    def test_manifest_changes_only_executable_profile_and_recovery(self):
        new = M.next_manifest(self.old, self.profile, '4' * 64)
        self.assertEqual({k for k in set(self.old) | set(new) if self.old.get(k) != new.get(k)},
                         {'source_commit', 'binary', 'binary_sha256', 'runtime_profile_sha256', 'recovery_only'})
        self.assertEqual((new['source_commit'], new['binary_sha256'], new['recovery_only'], new['initialize_swap_recovery']),
                         (M.COMMIT, BINARY, True, False))
        self.assertEqual(M.U.RELEASE, Path('/opt/velocita/releases')/M.COMMIT)
        self.assertEqual((M.S.ARCHIVE, M.S.ATTEMPT), (M.ARCHIVE, M.ARCHIVE/'recovery'))
        self.assertIn('--swap-recovery-only', self.profile.launch_contract(new, 'gateway')['argv'])
        self.assertNotIn('--swap-recovery-only', self.profile.launch_contract(self.old, 'gateway')['argv'])
        self.assertNotIn('BindsTo=velocita-swap-signer.service', self.profile.render(new)['velocita-swap-gateway.service.review'])

    def test_refuses_anything_but_the_installed_live_manifest(self):
        for change in (dict(initialize_swap_recovery=True), dict(launch_authorized=False), dict(recovery_only=True),
                       dict(source_commit='0' * 40), dict(binary_sha256='1' * 64), dict(incarnation='0' * 32), dict(runtime_image_sha256='0' * 64)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                M.next_manifest(dict(self.old, **change), self.profile, '4' * 64)

    def test_profile_changes_only_the_gateway_session(self):
        new = M.next_profile(PROFILE, 'c' * 64, NOW + 3_600_000, NOW)
        old_lines, new_lines = PROFILE.decode().splitlines(), new.decode().splitlines()
        self.assertEqual(len(old_lines), len(new_lines))
        changed = {a.split('=')[0] for a, b in zip(old_lines, new_lines) if a != b}
        self.assertEqual(changed, {'bearer_digest', 'expires_at_ms'})
        self.assertIn('bearer_digest=' + 'c' * 64, new_lines)
        self.assertIn('expires_at_ms=' + str(NOW + 3_600_000), new_lines)

    def test_profile_refuses_bad_session_or_shape(self):
        for args in (('C' * 64, NOW + 3_600_000), ('c' * 63, NOW + 3_600_000), ('c' * 64, NOW + 599_999), ('c' * 64, NOW + 86_400_001), ('c' * 64, str(NOW))):
            with self.subTest(args=args), self.assertRaises(ValueError):
                M.next_profile(PROFILE, args[0], args[1], NOW)
        for raw in (PROFILE.rstrip(b'\n'), PROFILE + b'bearer_digest=' + b'd' * 64 + b'\n', b'schema=1\nbearer_digest=' + b'a' * 64 + b'\nsocket=x\n', PROFILE + b'noequals\n'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                M.next_profile(raw, 'c' * 64, NOW + 3_600_000, NOW)
        same = PROFILE.replace(b'1792158514459', str(NOW + 3_600_000).encode()).replace(b'a' * 64, b'c' * 64)
        with self.assertRaises(ValueError):
            M.next_profile(same, 'c' * 64, NOW + 3_600_000, NOW)

    def test_effect25_shapes(self):
        pending = dict(pending_effect=25, last_effect=25, base_atoms=0, quote_atoms=24_047_860)
        M.pending_effect25(pending)
        with self.assertRaises(ValueError):
            M.resolved_effect25(pending)
        resolved = dict(pending, pending_effect=0)
        M.resolved_effect25(resolved)
        for bad in (dict(resolved, quote_atoms=24_047_859), dict(resolved, base_atoms=1), dict(resolved, last_effect=26)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                M.resolved_effect25(bad)
        for bad in (dict(pending, pending_effect=0), dict(pending, quote_atoms=11_337_860), dict(pending, last_effect=26)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                M.pending_effect25(bad)

    def test_binary_pin_must_be_exact(self):
        for bad in ('', 'B' * 64, 'b' * 63):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, 'SHA-256'):
                M.configure(bad)


if __name__ == '__main__':
    unittest.main()
