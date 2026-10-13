import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

M = module('candidate', ROOT/'tools/tm-account-review-candidate.py')
F = module('history_fixtures', ROOT/'tests/test_tm_account_history.py')


class CandidateTests(unittest.TestCase):
    def fixture(self):
        scratch = Path.home()/'.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        temp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(temp.cleanup)
        directory = Path(temp.name)
        pin = directory/'setup.json'
        raw = json.dumps(F.SETUP).encode()
        pin.write_bytes(raw); pin.chmod(0o600)
        session = directory/'session.json'
        session.write_text(json.dumps(dict(access_token='aaa.bbb.ccc', refresh_token='ddd.eee.fff', expires_in='2099-01-01T00:00:00Z', token_type='Bearer')))
        session.chmod(0o600)
        fake = F.Fake()
        def wire(method, target, bearer=None, body=None, timeout=20):
            if method == 'GET': return json.dumps(fake.get(target)).encode()
            return json.dumps(dict(jsonrpc='2.0', id=body['id'], result=fake.rpc(body['method'],body['params']))).encode()
        capture = directory/'capture'
        with patch.object(M.H, 'transport', side_effect=wire):
            M.H.collect(session, pin, hashlib.sha256(raw).hexdigest(), capture)
        return capture, directory/'proposal'

    def update(self, capture, name, action):
        path = capture/name
        value = json.loads(path.read_text())
        action(value)
        path.write_text(json.dumps(value))

    def test_cash_partition_exact_nonexecutable_and_private(self):
        capture, output = self.fixture()
        with patch.object(M.H, 'transport', side_effect=AssertionError('network forbidden')):
            summary = M.prepare(capture, output)
        private = json.loads((output/'funding-proposal.private.json').read_text())
        self.assertEqual(summary['proposed_funding'], dict(managed_base_atoms=0, managed_quote_atoms=1000000, managed_base_cost_atoms=0, outside_base_atoms=40000000, outside_quote_atoms=0, token_account_rent_included=False))
        self.assertEqual(summary['status'], 'proposal_requires_owner_review')
        self.assertIs(summary['live_admitted'], False)
        self.assertIs(summary['ledger_initialized'], False)
        self.assertIs(private['observations_require_owner_review'], True)
        self.assertEqual(private['observations'][0]['current_usdc_account_deltas'][F.ACCOUNT], 1000000)
        self.assertEqual(private['token_account_configs'][F.ACCOUNT]['rent_lamports_excluded'], 2039280)
        for forbidden in (F.OWNER, F.ACCOUNT, F.SETUP['id'], 'aaa.bbb.ccc', 'custody', 'incarnation'):
            self.assertNotIn(forbidden, json.dumps(summary))
        self.assertNotIn('incarnation', private)
        self.assertNotIn('signer_public_key', private)
        self.assertEqual(output.stat().st_mode & 0o777, 0o700)
        self.assertTrue(all(p.stat().st_mode & 0o777 == 0o600 for p in output.iterdir()))

    def test_boolean_zero_eligibility_rejected(self):
        for name, field in [('baseline-cut.json','blocking_orders'), ('baseline-cut.json','unsupported_accounts'), ('result.private.json','unresolved_tm_orders')]:
            capture, output = self.fixture()
            self.update(capture, name, lambda d: d.update({field:False}))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'eligibility|ineligible'):
                M.prepare(capture, output)
            self.assertFalse(output.exists())

    def test_manifest_must_cover_setup_and_balances(self):
        for name in ('response-001.json', 'response-002.json'):
            capture, output = self.fixture()
            self.update(capture, 'manifest.json', lambda d: d.pop(name))
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'required response coverage'):
                M.prepare(capture, output)

    def test_raw_hash_tampering_rejected(self):
        capture, output = self.fixture()
        path = capture/'response-001.json'
        path.write_bytes(path.read_bytes()+b' ')
        with self.assertRaisesRegex(ValueError, 'raw evidence hash mismatch'): M.prepare(capture, output)

    def test_cut_hash_binding_rejected(self):
        capture, output = self.fixture()
        self.update(capture, 'baseline-cut.json', lambda d: d.update(evidence_manifest_sha256='a'*64))
        with self.assertRaisesRegex(ValueError, 'cut identity'): M.prepare(capture, output)

    def test_partition_balance_mismatch_rejected(self):
        capture, output = self.fixture()
        self.update(capture, 'baseline-cut.json', lambda d: d.update(quote_atoms=1000001))
        with self.assertRaisesRegex(ValueError, 'cut identity'): M.prepare(capture, output)

    def test_unreviewed_observations_cannot_be_removed_or_changed(self):
        for action in (lambda d: d.update(observations=[]), lambda d: d['observations'][0].update(owner_native_delta=1), lambda d: d.update(requires_reviewed_initial_cut=False), lambda d: d.update(token_account_rent_in_managed_funding=True)):
            capture, output = self.fixture()
            self.update(capture, 'result.private.json', action)
            with self.assertRaisesRegex(ValueError, 'observations|review or rent'): M.prepare(capture, output)

    def test_private_trace_must_match_readonly_replay(self):
        capture, output = self.fixture()
        self.update(capture, 'requests.private.json', lambda rows: rows[0].update(method='POST'))
        with self.assertRaisesRegex(ValueError, 'trace mismatch'): M.prepare(capture, output)

    def test_time_boolean_and_capture_bounds_rejected(self):
        for field, value in [('start_slot', True), ('capture_finished_ms', 1), ('capture_finished_ms', 2**63)]:
            capture, output = self.fixture()
            self.update(capture, 'baseline-cut.json', lambda d: d.update({field:value}))
            with self.subTest(field=field), self.assertRaises(ValueError): M.prepare(capture, output)

    def test_symlink_and_hardlink_evidence_rejected(self):
        for link in ('symlink', 'hardlink'):
            capture, output = self.fixture()
            source = capture/'result.private.json'
            alias = capture/'alias.json'
            source.rename(alias)
            if link == 'symlink': source.symlink_to(alias)
            else: os.link(alias, source)
            with self.subTest(link=link), self.assertRaises((ValueError,RuntimeError,OSError)): M.prepare(capture, output)

    def test_second_output_refuses_overwrite(self):
        capture, output = self.fixture()
        M.prepare(capture, output)
        before = (output/'funding-proposal.private.json').read_bytes()
        with self.assertRaises((ValueError,RuntimeError,OSError)): M.prepare(capture, output)
        self.assertEqual((output/'funding-proposal.private.json').read_bytes(), before)

    def test_duplicate_json_and_private_permissions_rejected(self):
        capture, output = self.fixture()
        (capture/'baseline-cut.json').write_text('{"schema":1,"schema":1}')
        with self.assertRaisesRegex(ValueError, 'duplicate JSON'): M.prepare(capture, output)
        capture, output = self.fixture()
        (capture/'result.private.json').chmod(0o644)
        with self.assertRaisesRegex(ValueError, 'unsafe evidence'): M.prepare(capture, output)


if __name__ == '__main__': unittest.main()
