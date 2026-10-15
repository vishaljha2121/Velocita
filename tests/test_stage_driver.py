"""Driver staging writes a pinned, manual-start unit and refuses anything it cannot pin; no host access."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('stage_driver', Path(__file__).resolve().parents[1]/'deploy/v2/strategy/stage_driver.py')
S = importlib.util.module_from_spec(spec)
spec.loader.exec_module(S)
RUN = 'a' * 32
EXPIRY = 2_000_000_000_000
IMAGE = 'localhost/velocita-v2:qualified-54a5fcb'


class Stage(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/stage-driver-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.root = Path(self.tmp.name)
        self.home = self.root/'home'
        self.home.mkdir()
        self.archive = self.root/'model.tar'
        self.tar({'model/src/velocita_model/__init__.py': b'', 'model/src/velocita_model/shadow/live_swap_driver.py': b'x=1\n'})
        self.opening = self.root/'opening.record'
        self.opening.write_bytes(b'opening-bytes')
        self.calls = []
        self.active = 'inactive'

    def tearDown(self):
        self.tmp.cleanup()

    def tar(self, files):
        with tarfile.open(self.archive, 'w') as out:
            for name, body in files.items():
                info = tarfile.TarInfo(name)
                info.size = len(body)
                out.addfile(info, io.BytesIO(body))

    def run_(self, argv, **kwargs):
        self.calls.append(argv)
        return subprocess.CompletedProcess(argv, 0, self.active + '\n', '')

    def stage(self, **change):
        args = dict(archive=self.archive, archive_sha=hashlib.sha256(self.archive.read_bytes()).hexdigest(), opening=self.opening,
                    opening_sha=hashlib.sha256(b'opening-bytes').hexdigest(), image=IMAGE, incarnation=RUN, kill_epoch=1, executor_generation=1, max_orders=6, review_expires_ms=EXPIRY)
        args.update(change)
        return S.stage(self.home, 1002, args['archive'], args['archive_sha'], args['opening'], args['opening_sha'], args['image'],
                       args['incarnation'], args['kill_epoch'], args['executor_generation'], args['max_orders'], args['review_expires_ms'], run=self.run_)

    def test_stages_pinned_driver_without_starting_it(self):
        result = self.stage()
        root = self.home/'velocita-strategy'/RUN
        self.assertEqual((result['started'], result['staged']), (False, str(root)))
        self.assertEqual((root/'opening.record').read_bytes(), b'opening-bytes')
        self.assertEqual((root/'model/src/velocita_model/shadow/live_swap_driver.py').read_text(), 'x=1\n')
        config = json.loads((root/'driver.json').read_text())
        self.assertEqual((config['incarnation'], config['opening_record'], config['kill_epoch'], config['executor_generation'], config['max_orders']),
                         (RUN, '/config/opening.record', 1, 1, 6))
        self.assertEqual(config['observation'], str(self.home/'velocita-v2/textfile/strategy-settlements.json'))
        self.assertEqual(config.get('review_expires_ms'), EXPIRY, 'staging preserves the exact reviewed cutoff')
        client = config['client']
        self.assertEqual(client[client.index(IMAGE) + 1:client.index(IMAGE) + 5], ['--caller-key-id', '12', '--caller-id', '21'], 'strategy caller 21 with key slot 12')
        self.assertIn('/run/user/1002/velocita-secrets/caller-12.key:/run/velocita-secrets/caller-12.key:ro', client)
        self.assertEqual(client[client.index('--response-endpoint') + 1], '100.72.127.102:44412')
        self.assertEqual(client.count('--node'), 3)
        unit = (self.home/'.config/systemd/user/velocita-strategy-driver.service').read_text()
        self.assertIn('Restart=no', unit)
        self.assertNotIn('[Install]', unit, 'manual start only')
        self.assertIn(f'--config {root}/driver.json', unit)
        self.assertEqual(self.calls[-1], ['systemctl', '--user', 'daemon-reload'])
        self.assertFalse(any('start' in call for call in self.calls), 'never started')

    def test_refuses_unpinned_inputs_and_wrong_identity(self):
        for change, reason in ((dict(opening_sha='0' * 64), 'pin'), (dict(archive_sha='0' * 64), 'pin'), (dict(incarnation='A' * 32), 'incarnation'),
                               (dict(image='docker.io/velocita:latest'), 'image'), (dict(kill_epoch=0), 'epochs'), (dict(executor_generation=0), 'epochs'),
                               (dict(max_orders=0), 'order limit'), (dict(max_orders=241), 'order limit'), (dict(max_orders='6'), 'order limit'),
                               (dict(review_expires_ms=0), 'review expiry'), (dict(review_expires_ms='2000000000000'), 'review expiry'),
                               (dict(review_expires_ms=True), 'review expiry')):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, reason):
                self.stage(**change)
        self.assertFalse((self.home/'velocita-strategy').exists())

    def test_review_cutoff_must_leave_the_entire_entry_drain_window(self):
        with patch.object(S.time, 'time_ns', return_value=1_000_000_000):
            with self.assertRaisesRegex(ValueError, 'review expiry'):
                self.stage(review_expires_ms=421_000)
            result = self.stage(review_expires_ms=421_001)
        self.assertEqual(result['review_expires_ms'], 421_001)
        config = json.loads((self.home/'velocita-strategy'/RUN/'driver.json').read_text())
        self.assertEqual(config['review_expires_ms'], 421_001)

    def test_refuses_a_running_driver_or_an_existing_stage(self):
        self.active = 'active'
        with self.assertRaisesRegex(ValueError, 'running'):
            self.stage()
        self.active = 'inactive'
        self.stage()
        with self.assertRaisesRegex(ValueError, 'already staged'):
            self.stage()

    def test_archive_limited_to_the_model_package(self):
        for name in ('../escape.py', 'model/src/other/x.py', 'etc/passwd'):
            with self.subTest(name=name):
                self.tar({name: b'x'})
                with self.assertRaisesRegex(ValueError, 'archive'):
                    self.stage()
                self.assertFalse((self.home/'velocita-strategy').exists(), 'nothing staged from a refused archive')
        with tarfile.open(self.archive, 'w') as out:
            link = tarfile.TarInfo('model/src/velocita_model/link.py')
            link.type, link.linkname = tarfile.SYMTYPE, '/etc/passwd'
            out.addfile(link)
        with self.assertRaisesRegex(ValueError, 'archive'):
            self.stage()


if __name__ == '__main__':
    unittest.main()
