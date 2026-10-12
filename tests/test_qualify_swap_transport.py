import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('qualifier', ROOT / 'tools/qualify-swap-transport.py')
Q = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(Q)


class QualificationTests(unittest.TestCase):
    def setUp(self):
        scratch = Path.home() / '.cache/velocita-build/native-transport-20261012'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def manifest(self, data):
        path = self.root / 'manifest.json'
        path.write_text(json.dumps({'schema': 1, 'fingerprint': data}))
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_manifest_content_must_match(self):
        path, sha = self.manifest({'compiler': 'approved'})
        with self.assertRaises(Q.Refusal, msg='dependency_mismatch_refused_before_compile'):
            Q.verify_manifest(path, sha, {'compiler': 'different'})
        with self.assertRaises(Q.Refusal, msg='incomplete_manifest_refused'):
            Q.verify_manifest(path, sha, {'compiler': 'approved', 'curl': 'required'})
        Q.verify_manifest(path, sha, {'compiler': 'approved'})

    def test_system_include_flags_are_bounded_pairs(self):
        actual = '-I/usr/include/aarch64-linux-gnu -isystem /usr/include/mit-krb5 -lcurl'
        self.assertEqual(Q.flags(actual), actual.split(), 'paired_system_include_accepted')
        for value in ('-isystem', '-isystem -lcurl', '-isystem relative',
                      '-isystem /usr/include;echo', '-include /etc/passwd',
                      '-Wl,-rpath,/unreviewed', '@/unreviewed', '-DUNREVIEWED=1'):
            with self.subTest(value=value):
                with self.assertRaises(Q.Refusal, msg='unsafe_pkg_flags_refused'):
                    Q.flags(value)

    def test_manifest_hash_must_match(self):
        path, _ = self.manifest({'compiler': 'approved'})
        with self.assertRaises(Q.Refusal, msg='manifest_hash_must_match'):
            Q.verify_manifest(path, '0' * 64, {'compiler': 'approved'})

    def test_runtime_requirements(self):
        good = {'curl_headers': '8.18.0', 'curl_runtime': '8.18.0',
                'tls_backend': 'OpenSSL/3.5.5', 'ssl': True, 'async_dns': True}
        Q.verify_runtime(good, '8.18.0')
        for key, value in [('curl_runtime', '7.81.0'), ('curl_headers', '8.17.0'),
                           ('tls_backend', ''), ('ssl', False), ('async_dns', False)]:
            with self.subTest(key=key):
                with self.assertRaises(Q.Refusal, msg='runtime_dependencies_must_match'):
                    Q.verify_runtime(dict(good, **{key: value}), '8.18.0')

    def test_run_gates_precede_compile(self):
        actual = {'arch': 'x86_64', 'compiler': 'installed'}
        manifest, sha = self.manifest(actual)
        args = SimpleNamespace(mode='run', source=self.root / 'repo', output=self.root / 'out',
                               commit='a' * 40, expected_arch='aarch64',
                               toolchain_manifest=manifest, toolchain_manifest_sha256=sha)
        with patch.object(Q, 'fingerprint', return_value=actual), patch.object(Q, 'verify_source') as source:
            with patch.object(Q.subprocess, 'run') as compile_call:
                with self.assertRaises(Q.Refusal, msg='arch_mismatch_precedes_compile'):
                    Q.run(args)
                compile_call.assert_not_called()
                source.assert_not_called()
        args.output = self.root / 'out2'
        args.expected_arch = 'x86_64'
        args.toolchain_manifest_sha256 = '0' * 64
        with patch.object(Q, 'fingerprint', return_value=actual), patch.object(Q, 'verify_source') as source:
            with patch.object(Q.subprocess, 'run') as compile_call:
                with self.assertRaises(Q.Refusal, msg='manifest_mismatch_precedes_compile'):
                    Q.run(args)
                compile_call.assert_not_called()
                source.assert_not_called()
        args.output = self.root / 'out3'
        args.toolchain_manifest_sha256 = sha
        with patch.object(Q, 'fingerprint', return_value=actual):
            with patch.object(Q, 'verify_source', side_effect=Q.Refusal('dirty')):
                with patch.object(Q.subprocess, 'run') as compile_call:
                    with self.assertRaises(Q.Refusal, msg='dirty_source_precedes_compile'):
                        Q.run(args)
                    compile_call.assert_not_called()

    def test_source_rejects_wrong_commit_and_dirty_files(self):
        source = self.root / 'source'
        source.mkdir()
        env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL='/dev/null')
        def git(*args):
            return subprocess.run(['git', '-c', 'core.hooksPath=/dev/null', '-C', str(source), *args],
                                  check=True, capture_output=True, text=True, env=env).stdout.strip()
        git('init', '-q')
        for rel in Q.SOURCE_FILES:
            target = source / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / rel).read_bytes())
        git('add', '--', *Q.SOURCE_FILES)
        git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            '-c', 'commit.gpgsign=false', 'commit', '-qm', 'isolated fixture')
        commit = git('rev-parse', 'HEAD')
        self.assertEqual(set(Q.verify_source(source, commit)), set(Q.SOURCE_FILES), 'exact_commit_export_required')
        with self.assertRaises(Q.Refusal, msg='wrong_commit_refused'):
            Q.verify_source(source, 'f' * 40)
        target = source / Q.SOURCE_FILES[0]
        original = target.read_bytes()
        target.write_bytes(original + b'\n')
        with self.assertRaises(Q.Refusal, msg='dirty_tracked_source_refused'):
            Q.verify_source(source, commit)
        target.write_bytes(original)
        (source / 'untracked.hpp').write_text('override')
        with self.assertRaises(Q.Refusal, msg='untracked_source_refused'):
            Q.verify_source(source, commit)
        (source / 'untracked.hpp').unlink()
        (source / '.git/info/exclude').write_text('ignored.hpp\n')
        (source / 'ignored.hpp').write_text('override')
        with self.assertRaises(Q.Refusal, msg='ignored_source_refused'):
            Q.verify_source(source, commit)


if __name__ == '__main__':
    unittest.main()
