"""Synthetic create-only restoration; UID/GID normalization is local-test-only."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[2]/'deploy/v2/swap-runtime'))
import credential_handoff as H
import restore_caller_keys as R


class Restore(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/focused-mvp-20261015/credentials'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.root = Path(self.tmp.name)
        self.uid = os.getuid()
        self.old = self.root/'run/velocita-retired-credentials-b79e65be-20261015'
        self.new = self.root/'run/velocita-swap-credentials'
        for role in ('gateway', 'signer'):
            for parent in (self.old, self.new):
                (parent/role).mkdir(parents=True, mode=0o700)
            key = self.old/role/'caller.key'
            key.write_bytes(bytes(range(32)))
            key.chmod(0o600)
            (self.root/f'var/lib/velocita-swap-{role}/recovery').mkdir(parents=True)
        self.active, self.pid = 'inactive', '0'
        self.calls = []
        fstat = os.fstat
        # Production fixed groups equal UIDs; macOS test account's GID differs.
        def metadata(fd):
            m = fstat(fd)
            values = {n: getattr(m, n) for n in dir(m) if n.startswith('st_')}
            values['st_gid'] = self.uid
            return SimpleNamespace(**values)
        exclusive = H.exclusive
        self.patches = [patch.object(H, 'tmpfs'), patch.object(os, 'fstat', side_effect=metadata),
                        patch.dict(H.ROLES, {r: (self.uid, leaf) for r, (_, leaf) in H.ROLES.items()}),
                        patch.object(H, 'exclusive', side_effect=lambda d, n, b, u, g: exclusive(d, n, b, u, os.getgid()))]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in reversed(self.patches): p.stop()
        self.tmp.cleanup()

    def system(self, argv, **kwargs):
        self.calls.append(argv)
        if argv[0]=='/usr/bin/systemctl':
            return subprocess.CompletedProcess(argv, 0, f'ActiveState={self.active}\nMainPID={self.pid}\n')
        expected_role = Path(argv[-1]).parent.name
        return subprocess.CompletedProcess(argv, 0 if argv[2]=='velocita-swap-'+expected_role else 1)

    def restore(self):
        return R.restore(H, self.root, run=self.system, uid_check=False)

    def assert_no_active_keys(self):
        for role in ('gateway', 'signer'):
            self.assertFalse((self.new/role/'caller.key').exists())

    def test_restores_two_keys_keeps_sources_and_never_starts_services(self):
        result = self.restore()
        for role in ('gateway', 'signer'):
            self.assertEqual((self.new/role/'caller.key').read_bytes(), bytes(range(32)))
            self.assertEqual((self.old/role/'caller.key').read_bytes(), bytes(range(32)))
            self.assertEqual((self.new/role/'caller.key').stat().st_nlink, 1)
        self.assertFalse(result['services_started'])
        self.assertFalse(any('start' in argv for argv in self.calls))

    def test_failed_service_without_pid_is_stopped(self):
        self.active = 'failed'
        self.restore()
        self.assertTrue((self.new/'signer/caller.key').exists())

    def test_active_service_or_pid_refuses_before_creating_keys(self):
        for state, pid in (('active', '0'), ('inactive', '123')):
            self.active, self.pid = state, pid
            with self.assertRaisesRegex(ValueError, 'stopped'): self.restore()
            self.assert_no_active_keys()

    def test_second_recovery_nonempty_refuses_before_creating_either_key(self):
        (self.root/'var/lib/velocita-swap-signer/recovery/journal').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError, 'empty'): self.restore()
        self.assert_no_active_keys()

    def test_second_bad_source_refuses_before_creating_first_key(self):
        (self.old/'signer/caller.key').write_bytes(b'x'*31)
        with self.assertRaisesRegex(ValueError, 'metadata'): self.restore()
        self.assert_no_active_keys()

    def test_existing_second_destination_refuses_without_overwrite(self):
        key = self.new/'signer/caller.key'
        key.write_bytes(b'preserve')
        with self.assertRaisesRegex(ValueError, 'already exists'): self.restore()
        self.assertEqual(key.read_bytes(), b'preserve')
        self.assertFalse((self.new/'gateway/caller.key').exists())

    def test_dangling_destination_link_refused(self):
        key = self.new/'signer/caller.key'
        key.symlink_to(self.new/'absent')
        with self.assertRaisesRegex(ValueError, 'already exists'): self.restore()
        self.assertFalse((self.new/'gateway/caller.key').exists())
        self.assertTrue(key.is_symlink())

    def test_active_directory_wrong_mode_refused(self):
        (self.new/'signer').chmod(0o750)
        with self.assertRaisesRegex(ValueError, 'directory metadata'): self.restore()
        self.assert_no_active_keys()

    def test_partial_write_failure_does_not_delete_or_retry(self):
        original = H.exclusive
        def fail_second(directory, name, raw, uid, gid):
            if (self.new/'gateway/caller.key').exists():
                raise OSError('synthetic stop')
            return original(directory, name, raw, uid, gid)
        with patch.object(H, 'exclusive', side_effect=fail_second) as writes:
            with self.assertRaisesRegex(OSError, 'synthetic stop'): self.restore()
            self.assertEqual(writes.call_count, 2)
        self.assertTrue((self.new/'gateway/caller.key').exists())
        self.assertFalse((self.new/'signer/caller.key').exists())
        for role in ('gateway', 'signer'):
            self.assertTrue((self.old/role/'caller.key').exists())


if __name__ == '__main__': unittest.main()
