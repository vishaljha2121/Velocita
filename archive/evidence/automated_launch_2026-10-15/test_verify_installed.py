"""Caller-key startup preflight: real file checks with synthetic role probes."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO/'deploy/v2/swap-runtime'))
import credential_handoff as H
spec = importlib.util.spec_from_file_location('verify_installed', HERE/'verify_installed.py')
V = importlib.util.module_from_spec(spec)
spec.loader.exec_module(V)


class CallerPreflight(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/focused-mvp-20261015/credentials'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.path = Path(self.tmp.name)/'gateway/caller.key'
        self.path.parent.mkdir(mode=0o700)
        self.path.write_bytes(bytes(range(32)))
        self.path.chmod(0o600)
        self.uid, self.gid = os.getuid(), os.getgid()
        self.roles = patch.dict(H.ROLES, {'gateway': (self.uid, 'session.bearer')})
        self.roles.start()
        self.filesystem = patch.object(H, 'tmpfs')
        self.filesystem.start()
        self.probes = []

    def tearDown(self):
        self.filesystem.stop()
        self.roles.stop()
        self.tmp.cleanup()

    def probe(self, argv, **kwargs):
        self.probes.append(argv)
        return subprocess.CompletedProcess(argv, 0 if argv[2]=='velocita-swap-gateway' else 1)

    def check(self):
        # macOS has different primary GID; only the synthetic expected group changes.
        return V.verify_caller_key(H, self.path, 'gateway', run=self.probe, expected_gid=self.gid)

    def test_caller_key_preflight_exists(self):
        self.assertTrue(callable(getattr(V, 'verify_caller_key', None)), 'installer must preflight caller keys')

    def test_complete_key_checks_both_role_accesses(self):
        result = self.check()
        self.assertTrue(result['caller_key_validated'])
        self.assertEqual(len(self.probes), 2)
        self.assertTrue(result['own_role_can_read'])
        self.assertTrue(result['other_role_cannot_read'])
        self.assertNotIn('key', result)

    def test_main_requires_caller_preflight_before_secret_read(self):
        manifest = dict(launch_authorized=False, incarnation='aae119b95ddcea5a206f03ab5ca12c15',
                        runtime_profile_sha256='a', account_review_sha256='b', account_history_sha256='c',
                        binary='/unused', binary_sha256='d')
        launch = SimpleNamespace(pinned_bytes=lambda *a: b'{}',
                                 fields=lambda *a: dict(transaction_review_digest='e', bearer_digest='f'),
                                 check_bindings=lambda *a: None)
        fake = SimpleNamespace(harden=lambda: None,
                               PROVISION=SimpleNamespace(require_host_root=lambda: None),
                               LAUNCH=launch, ROLES={'gateway': (29001, 'session.bearer')})
        args = SimpleNamespace(receiver=Path('/unused'), profile_sha256='a', manifest_sha256='b', signer_sha256='c')
        spec = SimpleNamespace(loader=SimpleNamespace(exec_module=lambda *a: None))
        with patch.object(V.argparse.ArgumentParser, 'parse_args', return_value=args), \
             patch.object(V.importlib.util, 'spec_from_file_location', return_value=spec), \
             patch.object(V.importlib.util, 'module_from_spec', return_value=fake), \
             patch.object(V.json, 'loads', return_value=manifest), \
             patch.object(V, 'verify_caller_key', side_effect=ValueError('missing caller key')) as checked:
            with self.assertRaisesRegex(ValueError, 'missing caller key'):
                V.main()
            self.assertEqual(checked.call_args.args[1], Path('/run/velocita-swap-credentials/gateway/caller.key'))

    def test_missing_key_refused(self):
        self.path.unlink()
        with self.assertRaises(OSError): self.check()

    def test_wrong_mode_refused(self):
        self.path.chmod(0o640)
        with self.assertRaisesRegex(ValueError, 'caller key metadata'): self.check()

    def test_wrong_role_owner_refused(self):
        with patch.dict(H.ROLES, {'gateway': (self.uid+1, 'session.bearer')}):
            with self.assertRaises(ValueError): self.check()

    def test_wrong_group_refused(self):
        with self.assertRaisesRegex(ValueError, 'directory metadata|caller key metadata'):
            V.verify_caller_key(H, self.path, 'gateway', run=self.probe, expected_gid=self.gid+1)

    def test_hard_link_refused(self):
        os.link(self.path, self.path.with_name('extra'))
        with self.assertRaisesRegex(ValueError, 'caller key metadata'): self.check()

    def test_non32_byte_key_refused(self):
        for size in (0, 31, 33, 64):
            with self.subTest(size=size):
                self.path.write_bytes(b'x'*size)
                with self.assertRaisesRegex(ValueError, 'caller key metadata'): self.check()

    def test_symlink_refused(self):
        target = self.path.with_name('real')
        self.path.rename(target)
        self.path.symlink_to(target)
        with self.assertRaises(OSError): self.check()

    def test_mutable_directory_refused(self):
        self.path.parent.chmod(0o750)
        with self.assertRaisesRegex(ValueError, 'directory metadata'): self.check()

    def test_own_role_unreadable_refused(self):
        with self.assertRaisesRegex(ValueError, 'own-role readability'):
            V.verify_caller_key(H, self.path, 'gateway', expected_gid=self.gid,
                               run=lambda *a, **k: subprocess.CompletedProcess(a, 1))

    def test_other_role_readable_refused(self):
        with self.assertRaisesRegex(ValueError, 'cross-role readability'):
            V.verify_caller_key(H, self.path, 'gateway', expected_gid=self.gid,
                               run=lambda *a, **k: subprocess.CompletedProcess(a, 0))

    def test_probe_error_refused(self):
        with self.assertRaisesRegex(ValueError, 'readability'):
            V.verify_caller_key(H, self.path, 'gateway', expected_gid=self.gid,
                               run=lambda *a, **k: subprocess.CompletedProcess(a, 2))


if __name__ == '__main__': unittest.main()
