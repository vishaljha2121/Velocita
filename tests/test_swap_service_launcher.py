"""Startup mapping tests with synthetic public documents; no real credentials."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('launcher', ROOT/'deploy/v2/swap-runtime/service_launcher.py')
L = importlib.util.module_from_spec(spec); spec.loader.exec_module(L)
spec = importlib.util.spec_from_file_location('profile_tests', ROOT/'tests/test_swap_service_profile.py')
T = importlib.util.module_from_spec(spec); spec.loader.exec_module(T)


def documents():
    manifest = T.fixture()
    profile = dict(schema='1', incarnation=hashlib.sha256(bytes.fromhex(manifest['incarnation'])).hexdigest(), gateway_uid='2001', signer_uid='2002',
        gateway_caller='22', signer_caller='23', socket='/run/velocita-swap-signer/sign.sock',
        owner_wallet='12'*32, user_id='11111111-1111-1111-1111-111111111111', expires_at_ms='9999999')
    history = {'owner_sha256':hashlib.sha256(bytes.fromhex(profile['owner_wallet'])).hexdigest(),
        'user_id_sha256':hashlib.sha256(profile['user_id'].encode()).hexdigest(), 'capture_finished_ms':1000000}
    history_raw = json.dumps(history).encode()
    manifest['account_history_sha256'] = hashlib.sha256(history_raw).hexdigest()
    review = dict(schema='1', user_id=profile['user_id'], history_evidence=manifest['account_history_sha256'],
                  reviewed_at_ms='1000001', expires_at_ms='1900001')
    encode = lambda d: ''.join(k+'='+v+'\n' for k,v in d.items()).encode()
    review_raw = encode(review)
    manifest['account_review_sha256'] = hashlib.sha256(review_raw).hexdigest()
    profile['account_review_digest'] = manifest['account_review_sha256']
    return manifest, profile, review, history, encode


class LauncherTests(unittest.TestCase):
    def test_profile_uses_cpp_incarnation_digest_not_raw_cluster_id(self):
        m,p,r,h,encode=documents()
        self.assertEqual(len(m['incarnation']),32)
        self.assertEqual(len(p['incarnation']),64)
        try:L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),1001000,'signer')
        except ValueError as e:self.fail('cpp_incarnation_digest_admitted: '+str(e))
        p['incarnation']=m['incarnation']
        with self.assertRaises(ValueError,msg='raw_incarnation_not_a_swap_digest'):
            L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),1001000,'signer')

    def test_recovery_manifest_never_opens_signer_credentials(self):
        m=dict(T.fixture(),launch_authorized=True,recovery_only=True)
        with patch.object(L,'service_context') as context, patch.object(L.FD,'open_secret') as secret:
            with self.assertRaisesRegex(ValueError,'recovery.*signer',msg='recovery_signer_refused_before_resources'):
                L.launch(m,'signer')
            context.assert_not_called();secret.assert_not_called()

    def test_runtime_identity_marker_required_before_account_access(self):
        m = T.fixture()
        marker = (m['runtime_image_sha256']+'\n').encode()
        with patch.object(L, 'pinned_bytes', return_value=marker) as read:
            L.runtime_identity(m)
            read.assert_called_once_with('/etc/velocita-runtime-image', hashlib.sha256(marker).hexdigest(), 65)
        with patch.object(L, 'pinned_bytes', return_value=b'wrong'):
            with self.assertRaises(ValueError): L.runtime_identity(m)
        with patch.object(L, 'identity'), patch.object(L, 'runtime_identity', side_effect=ValueError('wrong runtime')), patch.object(L.FD, 'open_secret') as secret:
            with self.assertRaises(ValueError): L.prepare(m, 'gateway')
            secret.assert_not_called()

    def test_identity_and_hash_links_and_freshness(self):
        m,p,r,h,encode = documents()
        result = L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),1001000,'signer')
        self.assertTrue(result['review_current'])
        for key in ('gateway_uid','signer_caller','socket','incarnation','account_review_digest'):
            wrong = dict(p); wrong[key] = 'wrong'
            with self.subTest(key=key), self.assertRaises(ValueError):
                L.check_bindings(m,encode(wrong),encode(r),json.dumps(h).encode(),1001000,'signer')
        for key in ('history_evidence','user_id'):
            wrong = dict(r); wrong[key] = 'wrong'
            with self.subTest(key=key), self.assertRaises(ValueError):
                L.check_bindings(m,encode(p),encode(wrong),json.dumps(h).encode(),1001000,'signer')

    def test_expiry_keeps_gateway_recovery_but_never_signer(self):
        m,p,r,h,encode = documents()
        with self.assertRaises(ValueError): L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),1900001,'signer')
        result = L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),1900001,'gateway')
        self.assertFalse(result['review_current'])
        self.assertFalse(result['economic_admission_checked'])
        with self.assertRaises(ValueError): L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),9999999,'gateway')
        for changed in (True,2000000,-1):
            h['capture_finished_ms'] = changed
            with self.assertRaises(ValueError): L.check_bindings(m,encode(p),encode(r),json.dumps(h).encode(),1001000,'gateway')

    def test_review_and_capture_may_last_one_day(self):
        def bound(review_end, captured):
            m,p,r,h,encode = documents()
            start = 100_000_001
            h['capture_finished_ms'] = captured
            history = json.dumps(h).encode()
            m['account_history_sha256'] = hashlib.sha256(history).hexdigest()
            r.update(history_evidence=m['account_history_sha256'], reviewed_at_ms=str(start), expires_at_ms=str(review_end))
            m['account_review_sha256'] = p['account_review_digest'] = hashlib.sha256(encode(r)).hexdigest()
            p['expires_at_ms'] = str(review_end)
            return L.check_bindings(m,encode(p),encode(r),history,start,'signer')
        self.assertTrue(bound(100_000_001 + 86_400_000, 100_000_001 - 86_400_000)['review_current'], 'day-long review over a day-old capture')
        for review_end, captured in ((100_000_001 + 86_400_001, 100_000_001 - 86_400_000), (100_000_001 + 86_400_000, 100_000_001 - 86_400_001)):
            with self.subTest(review_end=review_end, captured=captured), self.assertRaises(ValueError):
                bound(review_end, captured)

    def test_duplicate_and_ambiguous_input_refuses(self):
        for raw in (b'a=1\na=2\n',b'a=1\n\n',b'a=\n',b'a=1\x00\n'):
            with self.assertRaises(ValueError): L.fields(raw)
        with self.assertRaises(ValueError): L.strict_json('{"a":1,"a":2}')
        with self.assertRaises(ValueError): L.strict_json('{"a":NaN}')
        for value in ('-1','1.0','\u0661',str(2**64)):
            with self.assertRaises(ValueError): L.unsigned(value)

    def test_false_authorization_refuses_before_secret_or_service_access(self):
        with patch.object(L.FD,'open_secret') as secret, patch.object(L,'prepare') as prepare, patch.object(L,'service_context') as context:
            with self.assertRaises(ValueError): L.launch(T.fixture(),'gateway')
            secret.assert_not_called(); prepare.assert_not_called(); context.assert_not_called()

    def test_wrong_user_or_extra_groups_refuse(self):
        m = T.fixture()
        with patch.object(L.os,'getuid',return_value=2001), patch.object(L.os,'geteuid',return_value=2001), patch.object(L.os,'getgid',return_value=2001), patch.object(L.os,'getegid',return_value=2001), patch.object(L.os,'getgroups',return_value=[2001,2002]):
            self.assertEqual(L.identity(m,'gateway'),(2001,2001))
            with patch.object(L.os,'getgroups',return_value=[2001,2002,27]):
                with self.assertRaises(ValueError): L.identity(m,'gateway')
            with patch.object(L.os,'getgroups',return_value=[2001]):
                with self.assertRaises(ValueError): L.identity(m,'gateway')
            with patch.object(L.os,'geteuid',return_value=0):
                with self.assertRaises(ValueError): L.identity(m,'gateway')

    def test_launch_mapping_transfers_only_expected_secret_and_environment(self):
        m = T.fixture(); m['launch_authorized']=True
        contract=L.PROFILE.launch_contract(m,'gateway')
        contract['public_files_sha256'][L.PROFILE.PUBLIC+'/transaction.review']='4'*64
        with patch.object(L,'service_context') as context, patch.object(L,'prepare',return_value=(contract,{})), patch.object(L.FD,'open_secret',side_effect=[41,42]) as opened, patch.object(L.os,'close') as close, patch.object(L.FD,'exec_handoff',side_effect=OSError('synthetic exec failure')) as handoff:
            with self.assertRaises(OSError): L.launch(m,'gateway')
            context.assert_called_once_with('gateway')
            self.assertEqual([c.args[0] for c in opened.call_args_list],['/etc/velocita-swap/gateway/caller.key','/etc/velocita-swap/gateway/session.bearer'])
            self.assertEqual(handoff.call_args.args[:3],(m['binary'],contract['argv'],42))
            self.assertEqual(set(handoff.call_args.args[3]),{'PATH','LANG','AERON_TERM_BUFFER_LENGTH','TMPDIR'})
            close.assert_any_call(41); close.assert_any_call(42)

    def test_legacy_profile_refused_before_secret(self):
        m=T.fixture();m['launch_authorized']=True
        for pin in (None, '', '0'*64, 'z'*64):
            contract=L.PROFILE.launch_contract(m,'gateway')
            if pin is not None: contract['public_files_sha256'][L.PROFILE.PUBLIC+'/transaction.review']=pin
            with self.subTest(pin=pin), patch.object(L,'service_context'), \
                 patch.object(L,'prepare',return_value=(contract,{})), \
                 patch.object(L.FD,'open_secret',side_effect=AssertionError('legacy_profile_refused_before_secret')) as opened:
                with self.assertRaisesRegex(ValueError,'transaction review'):
                    L.launch(m,'gateway')
                opened.assert_not_called()

    def test_initialization_exec_failure_never_retries(self):
        m=dict(T.fixture(),launch_authorized=True,initialize_swap_recovery=True)
        try:
            L.PROFILE.validate(m)
        except ValueError as error:
            self.fail('explicit_initialization_manifest_supported: '+str(error))
        contract=L.PROFILE.launch_contract(m,'signer')
        contract['public_files_sha256'][L.PROFILE.PUBLIC+'/transaction.review']='4'*64
        with patch.object(L,'service_context'),patch.object(L,'prepare',return_value=(contract,{})), \
             patch.object(L.FD,'open_secret',side_effect=[41,42]),patch.object(L.os,'close'), \
             patch.object(L.FD,'exec_handoff',side_effect=OSError('synthetic refusal')) as handoff:
            with self.assertRaisesRegex(OSError,'synthetic refusal'): L.launch(m,'signer')
            self.assertEqual(handoff.call_count,1,'initialization_exec_never_retried')
            self.assertEqual(handoff.call_args.args[1].count('--initialize-swap-recovery'),1)

    def test_configuration_failure_never_opens_credentials(self):
        m=T.fixture();m['launch_authorized']=True
        with patch.object(L,'service_context'),patch.object(L,'prepare',side_effect=ValueError('mismatch')),patch.object(L.FD,'open_secret') as opened:
            with self.assertRaises(ValueError): L.launch(m,'signer')
            opened.assert_not_called()

    def test_private_directory_owner_group_mode_and_type(self):
        import stat
        from types import SimpleNamespace
        safe = dict(st_uid=2001,st_gid=2001,st_mode=stat.S_IFDIR|0o700)
        with patch.object(L.FD,'_open_components',return_value=71),patch.object(L.os,'close') as close:
            for change in ({'st_uid':2002},{'st_gid':2002},{'st_mode':stat.S_IFDIR|0o750},{'st_mode':stat.S_IFREG|0o700}):
                meta=dict(safe);meta.update(change)
                with patch.object(L.os,'fstat',return_value=SimpleNamespace(**meta)):
                    with self.assertRaises(ValueError): L.directory('/var/lib/velocita-swap-gateway',2001,2001,0o700)
            with patch.object(L.os,'fstat',return_value=SimpleNamespace(**safe)):
                L.directory('/var/lib/velocita-swap-gateway',2001,2001,0o700)
            self.assertEqual(close.call_count,5)

    def test_prepare_maps_exact_pinned_documents_without_secrets(self):
        m,p,r,h,encode=documents()
        raw={'runtime.profile':encode(p),'account.review':encode(r),'account.history':json.dumps(h).encode()}
        calls=[]
        def read(path,digest,limit):
            name=path.rsplit('/',1)[-1];calls.append((name,digest,limit));return raw[name]
        with patch.object(L,'runtime_identity'),patch.object(L,'identity',return_value=(2001,2001)),patch.object(L.FD,'open_pinned_public',side_effect=[40,41]),patch.object(L.os,'read',return_value=b'\x7fELF'),patch.object(L.os,'close'),patch.object(L,'directory') as directory,patch.object(L.FD,'open_secret') as secret:
            contract,report=L.prepare(m,'gateway',read=read,now_ms=1001000)
            self.assertEqual(calls,[('runtime.profile',m['runtime_profile_sha256'],8192),('account.review',m['account_review_sha256'],8192),('account.history',m['account_history_sha256'],4096)])
            self.assertEqual(directory.call_count,4)
            self.assertFalse(report['live_admitted']);self.assertFalse(report['secrets_opened'])
            self.assertFalse(report['launch_authorized']);secret.assert_not_called()
            self.assertIn('--swap-secret-fd',contract['argv'])

    def test_transaction_review_is_pinned_before_credentials(self):
        m,p,r,h,encode=documents()
        document=b'synthetic transaction policy\n'
        digest=hashlib.sha256(document).hexdigest()
        p['transaction_review_digest']=digest
        raw={'runtime.profile':encode(p),'account.review':encode(r),'account.history':json.dumps(h).encode(),
             'transaction.review':document}
        calls=[]
        def read(path,pin,limit):
            calls.append((path,pin,limit));return raw[path.rsplit('/',1)[-1]]
        with patch.object(L,'runtime_identity'),patch.object(L,'identity',return_value=(2001,2001)),patch.object(L.FD,'open_pinned_public',side_effect=[40,41]),patch.object(L.os,'read',return_value=b'\x7fELF'),patch.object(L.os,'close'),patch.object(L,'directory'),patch.object(L.FD,'open_secret') as secret:
            contract,_=L.prepare(m,'gateway',read=read,now_ms=1001000)
            expected=(L.PROFILE.PUBLIC+'/transaction.review',digest,32768)
            self.assertIn(expected,calls,'transaction_review_checked_before_secret')
            self.assertEqual(contract['public_files_sha256'][expected[0]],digest)
            secret.assert_not_called()

    def test_service_context_refuses_privilege_or_wrong_cgroup(self):
        good='CapEff:\t00000000\nCapBnd:\t00000000\nNoNewPrivs:\t1\n'
        with patch.object(L.sys,'platform','linux'):
            for cgroup,status in [('0::/user.slice/other.service\n',good),('0::/system.slice/velocita-swap-signer.service\n',good.replace('CapBnd:\t00000000','CapBnd:\t00000001'))]:
                with patch.object(L.Path,'read_text',side_effect=[cgroup,status]):
                    with self.assertRaises(ValueError): L.service_context('signer')
            with patch.object(L.Path,'read_text',side_effect=['0::/system.slice/velocita-swap-signer.service\n',good]):
                L.service_context('signer')


if __name__ == '__main__': unittest.main()
