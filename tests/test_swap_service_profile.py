"""Offline service review: synthetic credentials and harmless child processes only."""
import importlib.util
import json
import sys
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('swap_profile', ROOT / 'deploy/v2/swap-runtime/service_profile.py')
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)


def fixture():
    return dict(schema=1, launch_authorized=False, source_commit='a' * 40, binary_sha256='b' * 64,
                runtime_image_sha256='2' * 64,
                binary='/opt/velocita/releases/' + 'a' * 40 + '/velocita-v2-gateway',
                incarnation='c' * 32, gateway_uid=2001, gateway_gid=2001,
                signer_uid=2002, socket_gid=2002, gateway_caller=22,
                signer_caller=23, gateway_key_id=13, signer_key_id=14,
                gateway_response_port=42413, signer_response_port=42414,
                host_ip='100.72.127.102',
                nodes=['1=100.72.127.102:44301', '2=100.84.151.116:44302', '3=100.85.36.22:44303'],
                runtime_profile_sha256='d' * 64, account_review_sha256='e' * 64,
                account_history_sha256='f' * 64, catalog_sha256='1' * 64)


class ServiceProfileTests(unittest.TestCase):
    def test_disabled_units_and_actual_role_arguments(self):
        result = PROFILE.render(fixture())
        for role in ('gateway', 'signer'):
            unit = result['velocita-swap-' + role + '.service.review']
            self.assertIn('ExecStart=/usr/bin/python3 -I -B /opt/velocita/releases/', unit)
            self.assertIn('service_launcher.py --manifest ', unit)
            self.assertIn('--manifest-sha256 ', unit)
            self.assertNotIn('[Install]', unit)
            self.assertIn('Restart=no', unit)
            self.assertIn('UMask=0077', unit)
            self.assertIn('LimitCORE=0', unit)
            self.assertNotIn('EnvironmentFile', unit)
            args = json.loads(result[role + '-launch-contract.json'])['argv']
            self.assertEqual(args[args.index('--swap-process') + 1], role)
            self.assertIn('--swap-secret-fd', args)
            self.assertNotIn('--initialize-swap-recovery', args)
            self.assertNotIn('--mode', args)
            self.assertEqual(args[args.index('--venue') + 1], 'truemarkets')
            self.assertEqual(args[args.index('--execution-contract') + 1], 'exact-input-swap')
        source = (ROOT / 'src/v2_gateway_main.cpp').read_text()
        for flag in ('--swap-runtime-profile', '--swap-account-review', '--swap-account-history',
                     '--swap-process', '--swap-secret-fd', '--arm-live-venue'):
            self.assertIn('"' + flag + '"', source)
        gate = (ROOT / 'include/velocita/v2/swap_runtime_production.hpp').read_text()
        self.assertIn('swap_runtime_live_contract_admitted() noexcept { return false; }', gate)

    def test_pinned_runtime_and_role_only_mounts(self):
        m = fixture()
        for role in ('gateway', 'signer'):
            unit = PROFILE.render(m)['velocita-swap-' + role + '.service.review']
            self.assertIn('RootDirectory=/opt/velocita/runtimes/' + '2'*64 + '/rootfs', unit)
            self.assertIn('MountAPIVFS=yes', unit)
            self.assertIn('BindReadOnlyPaths=/etc/velocita-swap/runtime-roots/'+role+':/run\n', unit)
            self.assertNotIn('TemporaryFileSystem=/run', unit)
            self.assertNotIn('BindPaths=/run:/run', unit)
            self.assertIn('BindReadOnlyPaths=/opt/velocita/releases/' + 'a'*40, unit)
            self.assertIn('BindReadOnlyPaths=/run/velocita-swap-credentials/' + role +
                          ':/etc/velocita-swap/' + role + '\n', unit)
            other = 'signer' if role == 'gateway' else 'gateway'
            self.assertNotIn('BindReadOnlyPaths=/etc/velocita-swap/' + other, unit)
            self.assertNotIn('BindReadOnlyPaths=/run/velocita-swap-credentials/' + other, unit)
            self.assertNotIn('BindReadOnlyPaths=/etc/velocita-swap/' + role + '\n', unit)
            self.assertNotIn('BindReadOnlyPaths=/etc/velocita-swap\n', unit)
            if role == 'gateway':
                self.assertIn('BindReadOnlyPaths=/run/velocita-swap-signer', unit)
                self.assertIn('BindReadOnlyPaths=/etc/resolv.conf', unit)
            else:
                self.assertNotIn('BindReadOnlyPaths=/etc/resolv.conf', unit)
        del m['runtime_image_sha256']
        with self.assertRaises(ValueError): PROFILE.render(m)

    def test_unreviewed_account_pins_only_in_disabled_manifest(self):
        pending = ('runtime_profile_sha256', 'account_review_sha256', 'account_history_sha256')
        for name in pending:
            m = fixture(); m[name] = None
            files = PROFILE.render(m)
            self.assertIsNone(json.loads(files['manifest.json'])[name])
            m['launch_authorized'] = True
            with self.subTest(pin=name), self.assertRaises(ValueError): PROFILE.render(m)
        m = fixture(); m.update({name: None for name in pending})
        files = PROFILE.render(m)
        for role in ('gateway', 'signer'):
            contract = json.loads(files[role + '-launch-contract.json'])
            for filename in ('runtime.profile', 'account.review', 'account.history'):
                self.assertIsNone(contract['public_files_sha256']['/etc/velocita-swap/public/' + filename])
            self.assertEqual(contract['public_files_sha256']['/etc/velocita-swap/public/catalog.bin'], '1'*64)
        for name in ('source_commit', 'binary_sha256', 'runtime_image_sha256', 'incarnation', 'catalog_sha256'):
            broken = dict(m); broken[name] = None
            with self.subTest(required_pin=name), self.assertRaises(ValueError): PROFILE.render(broken)

    def test_disabled_pending_profile_refuses_before_context_or_credentials(self):
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('pending_launcher', ROOT / 'deploy/v2/swap-runtime/service_launcher.py')
        launcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(launcher)
        m = fixture()
        m.update(runtime_profile_sha256=None, account_review_sha256=None, account_history_sha256=None)
        for role in ('gateway', 'signer'):
            with patch.object(launcher, 'prepare') as prepare, patch.object(launcher, 'service_context') as context, \
                 patch.object(launcher.FD, 'open_secret') as secret:
                with self.assertRaisesRegex(ValueError, 'operator has not authorized launch'):
                    launcher.launch(m, role)
                prepare.assert_not_called(); context.assert_not_called(); secret.assert_not_called()

    def test_network_proposal_and_peer_identity(self):
        files = PROFILE.render(fixture())
        signer = files['velocita-swap-signer.service.review']
        self.assertIn('User=2002', signer)
        self.assertIn('Group=2002', signer)
        self.assertIn('RuntimeDirectoryMode=0750', signer)
        self.assertIn('IPAddressDeny=any', signer)
        self.assertIn('RestrictAddressFamilies=AF_UNIX AF_INET', signer)
        self.assertEqual(signer.count('IPAddressAllow='), 3)
        self.assertIn('IPAddressAllow=100.72.127.102/32', signer)
        gateway = files['velocita-swap-gateway.service.review']
        self.assertIn('SupplementaryGroups=2002', gateway)
        contract = json.loads(files['signer-launch-contract.json'])
        self.assertEqual(contract['secret_fd']['mode'], '0600')
        self.assertEqual(contract['secret_fd']['uid'], 2002)
        self.assertEqual(contract['peer_uid'], 2001)
        self.assertEqual(contract['secret_fd']['hard_links'], 1)
        self.assertFalse(contract['deployable'])
        self.assertNotIn('signer-network-adr-review', contract['blockers'])
        self.assertIn('host-identity-and-network-enforcement', contract['blockers'])
        self.assertNotIn('inherited-descriptor-launcher', contract['blockers'])

    def test_rejects_identity_collision_root_bool_and_out_of_range(self):
        for patch in ({'launch_authorized': 'true'}, {'gateway_uid': 0}, {'gateway_uid': True}, {'signer_uid': 2001},
                      {'signer_caller': 22}, {'signer_key_id': 13},
                      {'gateway_response_port': 42414}, {'gateway_response_port': 70000}, {'gateway_response_port': 80},
                      {'gateway_gid': 2002}, {'socket_gid': 0}):
            value = fixture(); value.update(patch)
            with self.subTest(patch=patch), self.assertRaises(ValueError): PROFILE.render(value)

    def test_rejects_injection_unknown_fields_and_nonfixed_addresses(self):
        for patch in ({'binary': '/usr/bin/false\nExecStart=oops'}, {'token': 'secret'},
                      {'source_commit': 'HEAD'}, {'binary_sha256': '0' * 64},
                      {'host_ip': 'example.com'}, {'host_ip': '255.255.255.255'}, {'host_ip': '240.0.0.1'},
                      {'nodes': ['1=example.com:44301', '2=100.84.151.116:44302', '3=100.85.36.22:44303']},
                      {'nodes': ['1=100.72.127.102:44301'] * 3},
                      {'nodes': ['1=100.72.127.102:44301', '2=100.84.151.116:44302', '3=127.0.0.1:44303']}):
            value = fixture(); value.update(patch)
            with self.subTest(patch=patch), self.assertRaises(ValueError): PROFILE.render(value)

    def test_fresh_output_only_and_no_secret_contents(self):
        base = Path.home() / '.cache/velocita-service-profile-tests-v2'
        base.mkdir(mode=0o700, parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            out = Path(tmp) / 'review'
            PROFILE.write_review(fixture(), out)
            self.assertEqual(len(list(out.iterdir())), 5)
            with self.assertRaises(FileExistsError): PROFILE.write_review(fixture(), out)
            contract = json.loads((out / 'gateway-launch-contract.json').read_text())
            self.assertEqual(contract['secret_fd']['path'], '/etc/velocita-swap/gateway/session.bearer')
            self.assertNotIn('secret_contents', contract)




class DescriptorHandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('handoff', ROOT / 'deploy/v2/swap-runtime/fd_handoff.py')
        cls.handoff = importlib.util.module_from_spec(spec); spec.loader.exec_module(cls.handoff)

    def test_private_descriptor_rejects_mode_hardlink_and_symlinks(self):
        import os
        base = Path.home() / '.cache/velocita-service-profile-tests-v2'
        base.mkdir(mode=0o700, parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            directory = Path(tmp); secret = directory / 'synthetic'
            secret.write_text('SYNTHETIC-NOT-A-VENUE-KEY'); secret.chmod(0o600)
            fd = self.handoff.open_secret(str(secret), os.getuid()); os.close(fd)
            secret.chmod(0o640)
            with self.assertRaises(ValueError): self.handoff.open_secret(str(secret), os.getuid())
            secret.chmod(0o600); os.link(secret, directory / 'hardlink')
            with self.assertRaises(ValueError): self.handoff.open_secret(str(secret), os.getuid())
            (directory / 'hardlink').unlink(); (directory / 'link').symlink_to(secret)
            with self.assertRaises(OSError): self.handoff.open_secret(str(directory / 'link'), os.getuid())
            directory.chmod(0o770)
            with self.assertRaises(ValueError): self.handoff.open_secret(str(secret), os.getuid())
            directory.chmod(0o700)
            os.mkfifo(directory / 'fifo', 0o600)
            with self.assertRaises(ValueError): self.handoff.open_secret(str(directory / 'fifo'), os.getuid())
            with self.assertRaises(ValueError): self.handoff.open_pinned_public(str(secret), '0' * 64)

    def _assert_harmless_exec_handoff(self, source_fd):
        import os
        base = Path.home() / '.cache/velocita-service-profile-tests-v2'
        base.mkdir(mode=0o700, parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            secret = Path(tmp) / 'synthetic'; secret.write_text('synthetic-handoff'); secret.chmod(0o600)
            read_end, write_end = os.pipe()
            child = os.fork()
            if child == 0:
                try:
                    os.close(read_end); os.dup2(write_end, 1); os.close(write_end)
                    extra_fd = os.open('/dev/null', os.O_RDONLY)
                    os.dup2(extra_fd, 100, inheritable=True)
                    os.close(extra_fd)
                    fd = self.handoff.open_secret(str(secret), os.getuid())
                    # Force both the no-op dup2 case and a distinct source,
                    # independently of unittest's ambient descriptor allocation.
                    if fd != source_fd:
                        os.dup2(fd, source_fd, inheritable=False)
                        os.close(fd)
                    os.set_inheritable(source_fd, False)
                    assert not os.get_inheritable(source_fd)
                    self.handoff.exec_handoff(sys.executable, [sys.executable, '-B', '-c',
                        'import os, errno; assert os.environ.get("SYNTHETIC") == "1"; '
                        'assert "HOME" not in os.environ; '
                        'print(os.read(3,128).decode()); '
                        '\ntry: os.fstat(100)\nexcept OSError as e: assert e.errno == errno.EBADF\n'
                        'else: raise AssertionError("unrelated descriptor leaked")'], source_fd,
                        {'SYNTHETIC': '1', 'PATH': '/usr/bin:/bin', 'TMPDIR': tmp})
                except BaseException: os._exit(91)
            os.close(write_end)
            import select
            try:
                if not select.select([read_end], [], [], 10)[0]:
                    os.kill(child, 9)
                    os.waitpid(child, 0)
                    self.fail('descriptor child timed out')
                result = os.read(read_end, 128)
            finally:
                os.close(read_end)
            _, status = os.waitpid(child, 0)
            self.assertEqual(os.waitstatus_to_exitcode(status), 0)
            self.assertEqual(result, b'synthetic-handoff\n')

    def test_cloexec_fd3_survives_exec_without_unrelated_descriptors(self):
        self._assert_harmless_exec_handoff(3)

    def test_cloexec_fd7_moves_to_fd3_without_unrelated_descriptors(self):
        self._assert_harmless_exec_handoff(7)

    def test_binary_hash_mismatch_and_unconditional_cli_refusal(self):
        import hashlib
        import os
        import subprocess
        executable = Path('/usr/bin/true')
        digest = hashlib.sha256(executable.read_bytes()).hexdigest()
        fd = self.handoff.open_pinned_public(str(executable), digest, True); os.close(fd)
        with self.assertRaises(ValueError): self.handoff.open_pinned_public(str(executable), '0' * 64, True)
        result = subprocess.run([__import__('sys').executable, '-B', str(ROOT / 'deploy/v2/swap-runtime/fd_handoff.py'),
                                 '--deployable', 'true', '--secret', '/does-not-exist'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 64)
        self.assertIn('production descriptor launch is not admitted', result.stderr)


if __name__ == '__main__': unittest.main()
