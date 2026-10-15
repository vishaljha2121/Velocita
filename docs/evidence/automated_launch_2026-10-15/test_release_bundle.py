"""The new-incarnation bundle passes the reviewed dormant installer's own payload checks and changes only release and incarnation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2]/'deploy/v2/swap-runtime'
spec = importlib.util.spec_from_file_location('release_bundle', HERE/'release_bundle.py')
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)
sys.path.insert(0, str(SOURCE))
import install_disabled as INSTALL

COMMIT = '54a5fcb05821c5d10120b32ff6e1dccdb7f3e831'
NEW = '0123456789abcdef0123456789abcdef'
PREVIOUS = {'account_history_sha256': 'bc6dbf4ef8450b971a2f577f3fcbc30bc5410814ba4435f318df8fe85e5102e4',
            'account_review_sha256': '80b9aeb43fdb9445a25c4c1a5b80b51e1b0efc1d654c6c388fd4a3f6c8d8efdf',
            'binary': '/opt/velocita/releases/6c54f3f52fdf862bbd0761e5d9d900eac025d5fe/velocita-v2-gateway',
            'binary_sha256': '638b419f4c29da30046d78cd84bd210992cfb3a9ffbcdab40183b1c6e1b5f5ff', 'catalog_sha256': B.CATALOG_SHA256,
            'gateway_caller': 22, 'gateway_gid': 29001, 'gateway_key_id': 13, 'gateway_response_port': 44413, 'gateway_uid': 29001,
            'host_ip': '100.72.127.102', 'incarnation': 'b79e65be1ea14e219016ec48ea02efef', 'initialize_swap_recovery': False,
            'launch_authorized': True, 'nodes': ['1=100.72.127.102:44301', '2=100.84.151.116:44302', '3=100.85.36.22:44303'],
            'runtime_image_sha256': '076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f',
            'runtime_profile_sha256': 'db1388aec6e6bd9c8c7741390477e0bc82d3eb6013f8f48ec2618f964a0b44e6', 'schema': 1, 'signer_caller': 23,
            'signer_key_id': 14, 'signer_response_port': 44414, 'signer_uid': 29002, 'socket_gid': 29002,
            'source_commit': '6c54f3f52fdf862bbd0761e5d9d900eac025d5fe'}


class Bundle(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/release-bundle-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.root = Path(self.tmp.name)
        (self.root/'previous.json').write_text(json.dumps(PREVIOUS))
        self.binary = self.root/'velocita-v2-gateway'
        self.binary.write_bytes(b'\x7fELFgateway')
        self.catalog = self.root/'catalog.bin'
        self.catalog.write_bytes(b'catalog')
        self.original = B.CATALOG_SHA256
        B.CATALOG_SHA256 = hashlib.sha256(b'catalog').hexdigest()
        PREVIOUS['catalog_sha256'] = B.CATALOG_SHA256

    def tearDown(self):
        B.CATALOG_SHA256 = self.original
        PREVIOUS['catalog_sha256'] = self.original
        (self.root/'previous.json').unlink()
        self.tmp.cleanup()

    def build(self, **change):
        (self.root/'previous.json').write_text(json.dumps(PREVIOUS))
        args = dict(binary_sha256=hashlib.sha256(b'\x7fELFgateway').hexdigest(), commit=COMMIT, incarnation=NEW, output=self.root/'out')
        args.update(change)
        return B.build(self.root/'previous.json', SOURCE, self.binary, args['binary_sha256'], self.catalog, args['commit'], args['incarnation'], args['output'])

    def test_bundle_passes_the_reviewed_installer_payload_checks(self):
        result = self.build()
        m, data = INSTALL.payload(self.root/'out', result['bundle_sha256'])
        self.assertEqual((m['incarnation'], m['source_commit'], m['launch_authorized']), (NEW, COMMIT, False))
        self.assertEqual(m['binary'], f'/opt/velocita/releases/{COMMIT}/velocita-v2-gateway')
        self.assertTrue(all(m[k] is None for k in INSTALL.PROFILE.PENDING_ACCOUNT_PINS))
        self.assertNotIn('initialize_swap_recovery', m)
        changed = {k for k in set(m) | set(PREVIOUS) if m.get(k) != PREVIOUS.get(k)}
        self.assertEqual(changed, {'incarnation', 'source_commit', 'binary', 'binary_sha256', 'launch_authorized', 'initialize_swap_recovery',
                                   'runtime_profile_sha256', 'account_review_sha256', 'account_history_sha256'})
        self.assertEqual(data['velocita-v2-gateway'], b'\x7fELFgateway')

    def test_refuses_unpinned_inputs_and_unchanged_identity(self):
        for change, reason in ((dict(binary_sha256='0' * 64), 'binary'), (dict(commit=PREVIOUS['source_commit']), 'commit'),
                               (dict(incarnation=PREVIOUS['incarnation']), 'incarnation'), (dict(incarnation='X' * 32), 'incarnation')):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, reason):
                self.build(output=self.root/'refused', **change)
        self.catalog.write_bytes(b'other')
        with self.assertRaisesRegex(ValueError, 'catalog'):
            self.build(output=self.root/'refused')
        self.assertFalse((self.root/'refused').exists())


if __name__ == '__main__':
    unittest.main()
