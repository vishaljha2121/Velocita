"""Bounded isolation drill tests; no network, systemd or sudo execution."""
import errno
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'deploy/v2/swap-runtime'
spec = importlib.util.spec_from_file_location('service_profile', HERE / 'service_profile.py')
PROFILE = importlib.util.module_from_spec(spec); spec.loader.exec_module(PROFILE)
with patch.dict(sys.modules, service_profile=PROFILE):
    spec = importlib.util.spec_from_file_location('network_probe', HERE / 'network_probe.py')
    PROBE = importlib.util.module_from_spec(spec); spec.loader.exec_module(PROBE)


def result(role):
    pairs = [('tcp', a) for a in PROBE.VOTERS + [PROBE.EXTERNAL, '127.0.0.1']] + [('udp', a) for a in ['127.0.0.1', PROBE.EXTERNAL]]
    data = {'role': role, 'uid': 61234, 'passed': True, 'checks': [
        {'protocol': protocol, 'address': address, 'passed': True,
         'error': ('timeout' if protocol == 'tcp' else errno.EPERM) if role == 'restricted' and address not in PROBE.VOTERS else None}
        for protocol, address in pairs]}
    return subprocess.CompletedProcess([], 0, json.dumps(data), '')


class Connection:
    def __init__(self, payload): self.payload = payload
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def settimeout(self, value): pass
    def recv(self, size):
        out, self.payload = self.payload[:size], self.payload[size:]
        return out


class Listener:
    def __init__(self): self.pending = []
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def bind(self, address): pass
    def listen(self, backlog): pass
    def getsockname(self): return ('127.0.0.1', 40000)
    def settimeout(self, value): pass
    def accept(self):
        if not self.pending: raise PROBE.socket.timeout()
        return self.pending.pop(0), ('127.0.0.1', 55555)


class NetworkProbeTests(unittest.TestCase):
    def setUp(self):
        self.listener = Listener()
        self.patch = patch.object(PROBE.socket, 'socket', return_value=self.listener)
        self.patch.start(); self.addCleanup(self.patch.stop)

    def test_shared_policy_and_bounded_nonroot_services(self):
        control = PROBE.command('control', '123', 40000, 'a'*32, 'before')
        restricted = PROBE.command('restricted', '123', 40000, 'a'*32, 'restricted')
        self.assertEqual(control[:3], ['/usr/bin/sudo', '-n', '/usr/bin/systemd-run'])
        for args in (control, restricted):
            for value in ('--collect', '--wait', '--property=DynamicUser=yes',
                          '--property=RuntimeMaxSec=35', '--property=Restart=no',
                          '--property=CapabilityBoundingSet='):
                self.assertIn(value, args)
            self.assertNotIn('--user', args)
        for prop in PROFILE.signer_network_properties(PROBE.VOTERS):
            self.assertIn('--property=' + prop, restricted)
        self.assertNotIn('--property=IPAddressDeny=any', control)
        with self.assertRaises(ValueError): PROBE.command('signer', '123', 40000, 'a'*32, 'before')
        with self.assertRaises(ValueError): PROBE.command('control', '123;bad', 40000, 'a'*32, 'before')

    def test_timeout_refusal_missing_coverage_and_allowed_external_do_not_pass(self):
        for error in (None, errno.ECONNREFUSED, True):
            sample = result('restricted'); data = json.loads(sample.stdout)
            data['checks'][3]['error'] = error
            sample.stdout = json.dumps(data)
            with self.subTest(error=error), self.assertRaises(ValueError): PROBE.evaluate('restricted', sample)
        for change in ('root', 'missing', 'voter_denied', 'failed_unit'):
            sample = result('restricted'); data = json.loads(sample.stdout)
            if change == 'root': data['uid'] = 0
            elif change == 'missing': data['checks'].pop()
            elif change == 'voter_denied': data['checks'][0]['error'] = errno.EPERM
            else: sample.returncode = 1
            sample.stdout = json.dumps(data)
            with self.subTest(change=change), self.assertRaises(ValueError): PROBE.evaluate('restricted', sample)

    def test_valid_control_and_restricted_results(self):
        for role in ('control', 'restricted'):
            self.assertTrue(PROBE.evaluate(role, result(role))['passed'])

    def test_stop_after_failed_control_and_preserve_evidence(self):
        base = Path.home() / '.cache/velocita-build/network-probe-tests'
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            for timeout in (False, True):
                calls = []
                def runner(args, **kwargs):
                    calls.append(args)
                    self.assertEqual(kwargs['timeout'], 45)
                    if timeout: raise subprocess.TimeoutExpired(args, 45)
                    return subprocess.CompletedProcess(args, 1, '', 'sudo: a password is required')
                output = Path(tmp) / str(timeout)
                with patch.object(PROBE.sys, 'platform', 'linux'), patch.object(PROBE.socket, 'gethostname', return_value='bulbazar-NUC'), patch.object(PROBE.os, 'geteuid', return_value=1003):
                    report = PROBE.run(output, runner=runner)
                self.assertEqual(len(calls), 1)
                self.assertFalse(report['network_policy_passed'])
                self.assertFalse(report['live_admitted'])
                self.assertTrue((output / 'result.json').is_file())
                self.assertEqual((output / 'result.json').stat().st_mode & 0o777, 0o600)

    def test_success_stays_narrow_and_existing_output_refuses(self):
        base = Path.home() / '.cache/velocita-build/network-probe-tests'
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            calls = []
            def runner(args, **kwargs):
                calls.append(args[-3])
                if args[-3] == 'control': self.listener.pending.append(Connection(args[-1].encode()))
                return result(args[-3])
            output = Path(tmp) / 'result'
            with patch.object(PROBE.sys, 'platform', 'linux'), patch.object(PROBE.socket, 'gethostname', return_value='bulbazar-NUC'), patch.object(PROBE.os, 'geteuid', return_value=1003):
                report = PROBE.run(output, runner=runner)
                with self.assertRaises(FileExistsError): PROBE.run(output, runner=runner)
            self.assertEqual(calls, ['control', 'restricted', 'control'])
            self.assertTrue(report['network_policy_passed'])
            self.assertFalse(report['aeron_qualified'])
            self.assertFalse(report['production_uids_qualified'])

    def test_wrong_host_or_root_refuses_before_output_or_commands(self):
        for hostname, uid in [('nuc2', 1003), ('bulbazar-NUC', 0)]:
            with patch.object(PROBE.sys, 'platform', 'linux'), patch.object(PROBE.socket, 'gethostname', return_value=hostname), patch.object(PROBE.os, 'geteuid', return_value=uid):
                with self.assertRaises(ValueError): PROBE.run(Path('/never-created'))

    def test_timeout_cannot_pass_without_brackets_or_when_connection_arrives(self):
        base = Path.home() / '.cache/velocita-build/network-probe-tests'
        base.mkdir(parents=True, exist_ok=True)
        for failure in ('before_missing', 'restricted_connected', 'after_missing', 'after_unreachable'):
            with tempfile.TemporaryDirectory(dir=base) as tmp:
                calls = []
                def runner(args, **kwargs):
                    role, nonce = args[-3], args[-1]
                    phase = ('before', 'restricted', 'after')[len(calls)]; calls.append(phase)
                    sample = result(role)
                    if role == 'control' and failure != phase + '_missing':
                        self.listener.pending.append(Connection(nonce.encode()))
                    if role == 'restricted' and failure == 'restricted_connected':
                        self.listener.pending.append(Connection(b''))
                    if phase == 'after' and failure == 'after_unreachable':
                        data = json.loads(sample.stdout); data['checks'][3]['error'] = 'timeout'
                        sample.stdout = json.dumps(data)
                    return sample
                with patch.object(PROBE.sys, 'platform', 'linux'), patch.object(PROBE.socket, 'gethostname', return_value='bulbazar-NUC'), patch.object(PROBE.os, 'geteuid', return_value=1002):
                    report = PROBE.run(Path(tmp)/'result', runner=runner)
                with self.subTest(failure=failure): self.assertFalse(report['network_policy_passed'])
                self.listener.pending.clear()

    def test_udp_timeout_still_refuses_and_nonce_must_be_exact(self):
        sample = result('restricted'); data = json.loads(sample.stdout)
        data['checks'][-1]['error'] = 'timeout'; sample.stdout = json.dumps(data)
        with self.assertRaises(ValueError): PROBE.evaluate('restricted', sample)
        self.listener.pending.append(Connection(b'wrong-nonce'))
        with self.assertRaises(ValueError): PROBE.observe_listener(self.listener, 'a'*32)


if __name__ == '__main__': unittest.main()
