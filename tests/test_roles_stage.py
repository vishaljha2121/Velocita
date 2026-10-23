"""Exercise the cutover in disposable directories with inert host commands."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = Path(os.environ.get('STAGE_SCRIPT', ROOT / 'archive/evidence/roles_incarnation_2026-10-09/stage-host.sh'))
OLD = 'abd1e149a26d4062ba5bbba2d90b3305'
NAMES = ['velocita-paper-stochastic-' + name for name in ('node', 'exporter', 'edge', 'gateway')]
NAMES.append('velocita-paper-indicator-observer')


class StageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'archive/evidence/live_promotion_review_2026-10-09')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.live = self.root / 'velocita-indicator-paper/stochastic'
        self.retired = self.root / 'velocita-indicator-paper/retired/stochastic-abd1e149-20261009'
        (self.live / 'config').mkdir(parents=True)
        (self.live / 'config/node-1.conf').write_text(f'cluster.incarnation = {OLD}\nstartup = recover\n')
        (self.live / 'config/catalog.bin').write_bytes(b'catalog fixture')
        (self.live / 'sentinel').write_text('original state')
        (self.live.parent / 'observer.json').write_text('[]')
        self.units = self.root / '.config/systemd/user'
        self.units.mkdir(parents=True)
        for name in NAMES:
            image = 'localhost/velocita-v2-shadow:paper-ledger-f55209e' if name.endswith(('edge', 'observer')) else 'localhost/velocita-v2:ledger-f55209e'
            (self.units / (name + '.service')).write_text(f'ExecStart=podman run {image} --incarnation {OLD} --allow-new-risk\n')
        commands = self.root / 'bin'
        commands.mkdir()
        for name, body in {
            'systemctl': 'case "$*" in *daemon-reload*) exit 0;; esac\n[ "${STAGE_TEST_QUERY_ERROR:-0}" = 0 ] || exit 1\ncase "$*" in *"${STAGE_TEST_ACTIVE:-never-match}"*) state=active;; *) state=inactive;; esac\ncase "$*" in *is-active*) [ "$state" = active ];; *) printf "%s\\n" "$state";; esac',
            'podman': 'case "$*" in *"${STAGE_TEST_MISSING_IMAGE:-never-match}"*) exit 1;; esac\nexit 0',
        }.items():
            path = commands / name
            path.write_text('#!/bin/sh\n' + body + '\n')
            path.chmod(0o700)
        self.script = self.root / 'stage.sh'
        self.script.write_text(SCRIPT.read_text().replace('$HOME/', str(self.root) + '/'))
        self.env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'])

    def run_stage(self, **env):
        return subprocess.run(['/bin/sh', str(self.script), '1'], env=dict(self.env, **env), capture_output=True, text=True, timeout=10)

    def assert_untouched(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.live / 'sentinel').exists(), 'live state moved despite failed preflight')
        self.assertEqual((self.live / 'sentinel').read_text(), 'original state')
        self.assertFalse(self.retired.exists())
        for name in NAMES:
            path = self.units / (name + '.service')
            if path.exists():
                self.assertIn('ledger-f55209e', path.read_text())

    def test_every_running_service_blocks_before_state_moves(self):
        for name in NAMES:
            self.setUp()
            with self.subTest(service=name):
                self.assert_untouched(self.run_stage(STAGE_TEST_ACTIVE=name))

    def test_failed_service_query_blocks_before_state_moves(self):
        self.assert_untouched(self.run_stage(STAGE_TEST_QUERY_ERROR='1'))

    def test_missing_shadow_image_blocks_before_state_moves(self):
        self.assert_untouched(self.run_stage(STAGE_TEST_MISSING_IMAGE='paper-roles-2f5d405'))

    def test_missing_unit_blocks_before_state_moves(self):
        (self.units / (NAMES[0] + '.service')).unlink()
        self.assert_untouched(self.run_stage())

    def test_existing_ingress_config_blocks_before_state_moves(self):
        with (self.live / 'config/node-1.conf').open('a') as file:
            file.write('ingress.burst = 40\n')
        self.assert_untouched(self.run_stage())

    def test_stopped_complete_host_stages_once_and_preserves_old_state(self):
        result = self.run_stage()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.retired / 'sentinel').read_text(), 'original state')
        self.assertIn('startup = bootstrap', (self.live / 'config/node-1.conf').read_text())
        self.assertIn('--require-loss-limit --allow-new-risk', (self.units / 'velocita-paper-stochastic-gateway.service').read_text())
        self.assertNotEqual(self.run_stage().returncode, 0)


if __name__ == '__main__':
    unittest.main()
