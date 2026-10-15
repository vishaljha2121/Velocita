"""Retirement moves every installed piece into one archive, keeps credentials in tmpfs and refuses a running or incomplete install."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('retire', Path(__file__).with_name('retire_installation.py'))
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)


class Retire(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/retire-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.root = Path(self.tmp.name)
        for d in ('etc/velocita-swap/public', 'var/lib/velocita-swap-gateway/recovery', 'var/lib/velocita-swap-gateway/metrics', 'var/lib/velocita-swap-signer/recovery',
                  'var/lib/velocita-swap-signer/metrics', 'var/lib/velocita-live-observer',
                  'run/velocita-swap-credentials/gateway', 'etc/systemd/system'):
            (self.root/d).mkdir(parents=True)
        (self.root/'etc/velocita-swap/public').chmod(0o750)
        for role in ('gateway', 'signer'):
            for sub in ('', '/recovery', '/metrics'):
                (self.root/f'var/lib/velocita-swap-{role}{sub}').chmod(0o700)
        (self.root/'etc/velocita-swap/public/manifest.json').write_text('{}')
        (self.root/'var/lib/velocita-swap-gateway/recovery/prepare.journal').write_text('journal')
        (self.root/'var/lib/velocita-live-observer/settlements.json').write_text('cache')
        (self.root/'run/velocita-swap-credentials/gateway/session.bearer').write_text('secret')
        for unit in R.UNITS:
            (self.root/'etc/systemd/system'/unit).write_text('[Service]\n')
            (self.root/'etc/systemd/system'/(unit + '.d')).mkdir()
            (self.root/'etc/systemd/system'/(unit + '.d')/'90-idle-renewal.conf').write_text('guard')
        self.active, self.load, self.calls = 'inactive', 'not-found', []

    def tearDown(self):
        self.tmp.cleanup()

    def run_(self, argv, **kwargs):
        self.calls.append(argv)
        value = self.active if '--property=ActiveState' in argv else self.load
        return subprocess.CompletedProcess(argv, 0, value + '\n', '')

    def retire(self):
        return R.retire(self.root, '20261015', run=self.run_, uid_check=False)

    def test_moves_everything_into_one_archive_and_keeps_credentials_in_tmpfs(self):
        result = self.retire()
        archive = self.root/'var/lib/velocita-retired-b79e65be-20261015'
        self.assertEqual((archive/'public/manifest.json').read_text(), '{}')
        self.assertEqual((archive/'state/velocita-swap-gateway/recovery/prepare.journal').read_text(), 'journal')
        self.assertEqual((archive/'units/velocita-swap-signer.service.d/90-idle-renewal.conf').read_text(), 'guard')
        self.assertEqual((archive/'observer-settlements.json').read_text(), 'cache')
        self.assertFalse(any('session.bearer' in str(p) for p in archive.rglob('*')), 'no credential reaches the disk archive')
        self.assertEqual((self.root/'run/velocita-retired-credentials-b79e65be-20261015/gateway/session.bearer').read_text(), 'secret')
        public = self.root/'etc/velocita-swap/public'
        self.assertEqual((list(public.iterdir()), public.stat().st_mode & 0o777), ([], 0o750))
        for role in ('gateway', 'signer'):
            for sub in ('', '/recovery', '/metrics'):
                fresh = self.root/f'var/lib/velocita-swap-{role}{sub}'
                self.assertEqual(fresh.stat().st_mode & 0o777, 0o700, 'provisioned state directory recreated with its mode')
            self.assertEqual(list((self.root/f'var/lib/velocita-swap-{role}/recovery').iterdir()), [], 'recreated empty')
        for unit in R.UNITS:
            self.assertFalse((self.root/'etc/systemd/system'/unit).exists())
        self.assertIn(['/usr/bin/systemctl', 'daemon-reload'], self.calls)
        self.assertEqual((result['deleted'], result['started']), (False, False))

    def test_refusals_move_nothing(self):
        self.active = 'active'
        with self.assertRaisesRegex(ValueError, 'inactive'):
            self.retire()
        self.active = 'inactive'
        (self.root/'var/lib/velocita-swap-signer/metrics').rmdir()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.retire()
        (self.root/'var/lib/velocita-swap-signer/metrics').mkdir()
        (self.root/'var/lib/velocita-retired-b79e65be-20261015').mkdir()
        with self.assertRaisesRegex(ValueError, 'preserve'):
            self.retire()
        (self.root/'var/lib/velocita-retired-b79e65be-20261015').rmdir()
        with self.assertRaisesRegex(ValueError, 'YYYYMMDD'):
            R.retire(self.root, '2026-10-15', run=self.run_, uid_check=False)
        self.assertTrue((self.root/'etc/velocita-swap/public/manifest.json').exists())
        self.assertTrue((self.root/'run/velocita-swap-credentials/gateway/session.bearer').exists())

    def test_missing_credential_directory_refused_before_any_move(self):
        credentials = self.root/'run/velocita-swap-credentials'
        credentials.rename(self.root/'run/elsewhere')
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.retire()
        self.assertTrue((self.root/'etc/velocita-swap/public/manifest.json').exists())

    def test_absent_observer_cache_is_not_required(self):
        (self.root/'var/lib/velocita-live-observer/settlements.json').unlink()
        self.retire()
        self.assertFalse((self.root/'var/lib/velocita-retired-b79e65be-20261015/observer-settlements.json').exists())

    def test_symlinked_piece_refused(self):
        signer = self.root/'var/lib/velocita-swap-signer'
        signer.rename(self.root/'var/lib/elsewhere')
        signer.symlink_to(self.root/'var/lib/elsewhere')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.retire()

    def test_unit_still_loaded_after_reload_is_reported(self):
        self.load = 'loaded'
        with self.assertRaisesRegex(ValueError, 'still loaded'):
            self.retire()


if __name__ == '__main__':
    unittest.main()
