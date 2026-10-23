"""Offline immutable-runtime adoption; only synthetic files and temporary dirs."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('runtime_install', ROOT / 'deploy/v2/swap-runtime/install_runtime.py')
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)


class RuntimeInstallTests(unittest.TestCase):
    def setUp(self):
        base = Path.home() / '.cache/velocita-build/runtime-install-tests'
        base.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        prior_umask = os.umask(0)
        self.addCleanup(os.umask, prior_umask)
        self.source = self.base / 'source'; self.source.mkdir(mode=0o755)
        for name in ('etc', 'usr', 'usr/bin', 'usr/lib'):
            (self.source / name).mkdir(mode=0o755)
        (self.source / 'usr/bin/example').write_bytes(b'synthetic executable\n')
        (self.source / 'usr/bin/example').chmod(0o755)
        (self.source / 'etc/example').write_bytes(b'synthetic configuration\n')
        (self.source / 'etc/example').chmod(0o644)
        (self.source / 'lib').symlink_to('usr/lib')
        (self.source / 'etc/mtab').symlink_to('/proc/mounts')

    def manifest(self):
        value = P.build_manifest(self.source)
        return value, hashlib.sha256(P.manifest_bytes(value)).hexdigest()

    def test_manifest_hashes_content_and_preserves_chroot_symlinks(self):
        value, digest = self.manifest()
        entries = {e['path']: e for e in value['entries']}
        self.assertEqual(value['image'], P.IMAGE)
        self.assertEqual(entries['etc/mtab']['target'], '/proc/mounts')
        self.assertEqual(entries['usr/bin/example']['sha256'], hashlib.sha256(b'synthetic executable\n').hexdigest())
        self.assertEqual(entries['usr/bin/example']['mode'], 0o755)
        self.assertTrue(P.review(self.source, value, digest)['source_verified'])

    def test_rejects_special_setid_and_writable_source_modes(self):
        value, _ = self.manifest()
        for mode in (0o4755, 0o6755):
            changed = json.loads(json.dumps(value))
            next(e for e in changed['entries'] if e['type'] == 'file')['mode'] = mode
            with self.subTest(mode=mode), self.assertRaises(P.Refused): P.validate_manifest(changed)
        for mode in (0o777, 0o600):
            (self.source / 'usr/bin/example').chmod(mode)
            with self.subTest(mode=mode), self.assertRaises(P.Refused): P.build_manifest(self.source)
        (self.source / 'usr/bin/example').chmod(0o755)
        os.mkfifo(self.source / 'pipe', 0o600)
        with self.assertRaisesRegex(P.Refused, 'special'): P.build_manifest(self.source)

    def test_manifest_rejects_path_escape_duplicate_bad_image_and_link_parent(self):
        value, _ = self.manifest()
        variants = []
        for path in ('../outside', '/absolute', 'usr//bin', 'usr/./bin', 'usr/../bin'):
            changed = json.loads(json.dumps(value)); changed['entries'][0]['path'] = path; variants.append(changed)
        changed = json.loads(json.dumps(value)); changed['entries'].append(changed['entries'][0]); variants.append(changed)
        changed = json.loads(json.dumps(value)); changed['image'] = 'a' * 64; variants.append(changed)
        changed = json.loads(json.dumps(value)); changed['entries'].append(dict(path='lib/injected', type='file', mode=0o644, size=0, sha256=hashlib.sha256(b'').hexdigest()))
        changed['entries'].sort(key=lambda e: e['path']); variants.append(changed)
        for changed in variants:
            with self.subTest(value=changed), self.assertRaises(P.Refused): P.validate_manifest(changed)

    def test_changes_and_unexpected_files_fail_review(self):
        value, digest = self.manifest()
        (self.source / 'etc/example').write_bytes(b'Synthetic configuration\n')
        with self.assertRaises(P.Refused): P.review(self.source, value, digest)
        (self.source / 'etc/example').write_bytes(b'synthetic configuration\n')
        (self.source / 'unexpected').write_bytes(b'extra'); (self.source / 'unexpected').chmod(0o644)
        with self.assertRaises(P.Refused): P.review(self.source, value, digest)

    def test_marker_in_source_and_symlinked_source_ancestor_refused(self):
        (self.source / 'etc/velocita-runtime-image').write_text(P.IMAGE + '\n')
        with self.assertRaises(P.Refused): P.build_manifest(self.source)
        (self.source / 'etc/velocita-runtime-image').unlink()
        alias = self.base / 'alias'; alias.symlink_to(self.source)
        with self.assertRaises(OSError): P.build_manifest(alias)

    def test_wrong_manifest_pin_refused_before_source_open(self):
        value, _ = self.manifest()
        with patch.object(P, 'open_source') as opened, patch.object(P, 'build_manifest', return_value=value):
            with self.assertRaisesRegex(P.Refused, 'hash'): P.review(self.source, value, '0' * 64)
            opened.assert_not_called()

    def test_copy_creates_regular_data_before_links_and_never_follows_link_parent(self):
        value, _ = self.manifest()
        dest = self.base / 'dest'; dest.mkdir()
        srcfd = P.open_source(self.source); dstfd = os.open(dest, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with patch.object(P.os, 'fchown') as chown:
                P.copy_tree(srcfd, dstfd, value)
                self.assertTrue(chown.call_count)
                self.assertTrue(all(c.args[1:] == (0, 0) for c in chown.call_args_list))
            self.assertEqual((dest / 'etc/example').read_bytes(), b'synthetic configuration\n')
            self.assertTrue((dest / 'etc/mtab').is_symlink())
            self.assertEqual(os.readlink(dest / 'etc/mtab'), '/proc/mounts')
            self.assertEqual(P.scan_tree(dstfd), value['entries'])
        finally: os.close(srcfd); os.close(dstfd)

    def test_copy_refuses_source_replacement_and_existing_destination(self):
        value, _ = self.manifest()
        (self.source / 'etc/example').write_bytes(b'changed\n')
        dest = self.base / 'dest'; dest.mkdir()
        srcfd = P.open_source(self.source); dstfd = os.open(dest, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with patch.object(P.os, 'fchown'):
                with self.assertRaises(P.Refused): P.copy_tree(srcfd, dstfd, value)
            with patch.object(P.os, 'fchown'):
                with self.assertRaises(FileExistsError): P.copy_tree(srcfd, dstfd, value)
        finally: os.close(srcfd); os.close(dstfd)

    def test_root_host_guard_and_pin_required_before_install(self):
        value, digest = self.manifest()
        with patch.object(P.os, 'getuid', return_value=1002), patch.object(P.os, 'geteuid', return_value=1002), \
             patch.object(P, 'open_source', return_value=13) as source, \
             patch.object(P, 'review', return_value={}), patch.object(P, 'open_destination', return_value=(11, 12)), \
             patch.object(P, 'copy_tree'), patch.object(P, 'complete_install'), patch.object(P.os, 'close'):
            with self.assertRaises(P.Refused): P.install(self.source, value, digest)
            source.assert_not_called()

    def test_full_install_completion_follows_verified_copy(self):
        value, digest = self.manifest()
        release = self.base / 'release'; release.mkdir(mode=0o700)
        dest = release / 'rootfs'; dest.mkdir(mode=0o755)
        release_fd = os.open(release, os.O_RDONLY | os.O_DIRECTORY)
        dest_fd = os.open(dest, os.O_RDONLY | os.O_DIRECTORY)
        with patch.object(P, 'require_root_host'), patch.object(P, 'open_destination', return_value=(release_fd, dest_fd)), \
             patch.object(P.os, 'fchown'):
            result = P.install(self.source, value, digest)
        self.assertTrue(result['installed'])
        self.assertFalse(result['live_admitted'])
        self.assertEqual((dest / P.MARKER).read_text(), P.IMAGE + '\n')
        self.assertEqual((dest / P.MARKER).stat().st_mode & 0o777, 0o444)
        completed = json.loads((release / 'installation.json').read_text())
        self.assertEqual(completed['manifest_sha256'], digest)
        self.assertTrue(completed['complete'])
        self.assertEqual((release / 'rootfs-manifest.json').read_bytes(), P.manifest_bytes(value))
        self.assertEqual(release.stat().st_mode & 0o777, 0o755)

    def test_default_cli_reviews_without_install(self):
        value, digest = self.manifest()
        manifest = self.base / 'manifest.json'; manifest.write_bytes(P.manifest_bytes(value))
        with patch.object(P.sys, 'argv', ['install_runtime.py', '--source', str(self.source), '--manifest', str(manifest), '--manifest-sha256', digest]), \
             patch.object(P, 'install') as install, patch('builtins.print') as output:
            P.main()
            install.assert_not_called()
            self.assertIn('"installed": false', output.call_args.args[0])

    def test_destination_creation_under_umask077_and_existing_directory_refusal(self):
        fake_root = self.base / 'host'; fake_root.mkdir(mode=0o755)
        actual_open, actual_fstat = os.open, os.fstat

        def anchored_open(path, flags, *args, **kwargs):
            return actual_open(fake_root if path == '/' else path, flags, *args, **kwargs)

        class RootMeta:
            st_uid = st_gid = 0
            def __init__(self, meta): self.meta = meta
            def __getattr__(self, name): return getattr(self.meta, name)

        previous = os.umask(0o077)
        try:
            with patch.object(P.os, 'open', side_effect=anchored_open), \
                 patch.object(P.os, 'fstat', side_effect=lambda fd: RootMeta(actual_fstat(fd))), \
                 patch.object(P.os, 'fchown'):
                release, rootfs = P.open_destination()
                os.close(rootfs); os.close(release)
                for path in ('opt', 'opt/velocita', 'opt/velocita/runtimes',
                             'opt/velocita/runtimes/' + P.IMAGE + '/rootfs'):
                    self.assertEqual((fake_root / path).stat().st_mode & 0o777, 0o755)
                self.assertEqual((fake_root / 'opt/velocita/runtimes' / P.IMAGE).stat().st_mode & 0o777, 0o700)
                (fake_root / 'opt').chmod(0o700)
                with self.assertRaisesRegex(P.Refused, 'destination ancestor'):
                    P.open_destination()
                self.assertEqual((fake_root / 'opt').stat().st_mode & 0o777, 0o700)
        finally: os.umask(previous)

    def test_failed_copy_never_writes_completion(self):
        value, digest = self.manifest()
        with patch.object(P, 'require_root_host'), patch.object(P, 'review'), \
             patch.object(P, 'open_destination', return_value=(11, 12)), \
             patch.object(P, 'open_source', return_value=13), patch.object(P.os, 'close'), \
             patch.object(P, 'copy_tree', side_effect=P.Refused('copy failed')), \
             patch.object(P, 'complete_install') as complete:
            with self.assertRaisesRegex(P.Refused, 'copy failed'): P.install(self.source, value, digest)
            complete.assert_not_called()


if __name__ == '__main__': unittest.main()
