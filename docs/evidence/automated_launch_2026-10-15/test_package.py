"""Packaging copies only pinned ELF binaries over the previous qualified image and never replaces a tag."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package', Path(__file__).with_name('package.py'))
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
COMMIT = '54a5fcb05821c5d10120b32ff6e1dccdb7f3e831'


class Package(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/package-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.root = Path(self.tmp.name)
        self.source = self.root/'build'
        self.source.mkdir()
        self.pins = {}
        for name in P.NAMES:
            raw = b'\x7fELF' + name.encode()
            (self.source/name).write_bytes(raw)
            self.pins[name] = hashlib.sha256(raw).hexdigest()
        self.exists, self.calls = 1, []

    def tearDown(self):
        self.tmp.cleanup()

    def run_(self, argv, **kwargs):
        self.calls.append(argv)
        if argv[:3] == ['podman', 'image', 'exists']:
            return subprocess.CompletedProcess(argv, self.exists, '', '')
        return subprocess.CompletedProcess(argv, 0, 'sha256:base\n' if P.BASE_IMAGE in argv else 'sha256:new\n', '')

    def test_builds_over_the_previous_image_from_pinned_binaries(self):
        result = P.package(COMMIT, self.source, self.pins, self.root/'out', run=self.run_)
        self.assertEqual((result['image'], result['base_image'], result['image_id']), ('localhost/velocita-v2:qualified-54a5fcb', 'sha256:base', 'sha256:new'))
        containerfile = (self.root/'out/image/Containerfile').read_text()
        self.assertTrue(containerfile.startswith('FROM sha256:base\n'))
        self.assertIn(f'org.velocita.source_commit="{COMMIT}"', containerfile)
        build = [c for c in self.calls if c[:2] == ['podman', 'build']][0]
        self.assertIn('--network=none', build)
        self.assertIn('--pull-never', build)

    def test_refuses_changed_binary_existing_tag_or_bad_pins(self):
        (self.source/'velocita-v2-client').write_bytes(b'\x7fELFchanged')
        with self.assertRaisesRegex(ValueError, 'velocita-v2-client'):
            P.package(COMMIT, self.source, self.pins, self.root/'a', run=self.run_)
        (self.source/'velocita-v2-client').write_bytes(b'\x7fELFvelocita-v2-client')
        self.exists = 0
        with self.assertRaisesRegex(ValueError, 'preserve'):
            P.package(COMMIT, self.source, self.pins, self.root/'b', run=self.run_)
        self.exists = 1
        for pins in ({k: v for k, v in self.pins.items() if k != 'velocita-v2-node'}, dict(self.pins, extra='0' * 64)):
            with self.assertRaisesRegex(ValueError, 'pins'):
                P.package(COMMIT, self.source, pins, self.root/'c', run=self.run_)
        with self.assertRaisesRegex(ValueError, 'commit'):
            P.package(COMMIT[:7], self.source, self.pins, self.root/'d', run=self.run_)
        script = b'#!/bin/sh\n'
        (self.source/'velocita-v2-node').write_bytes(script)
        with self.assertRaisesRegex(ValueError, 'velocita-v2-node'):
            P.package(COMMIT, self.source, dict(self.pins, **{'velocita-v2-node': hashlib.sha256(script).hexdigest()}), self.root/'e', run=self.run_)
        (self.root/'f').mkdir()
        with self.assertRaisesRegex(ValueError, 'fresh output'):
            P.package(COMMIT, self.source, self.pins, self.root/'f', run=self.run_)
        self.assertFalse(any(c[:2] == ['podman', 'build'] for c in self.calls))


if __name__ == '__main__':
    unittest.main()
