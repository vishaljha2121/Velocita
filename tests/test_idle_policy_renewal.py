"""Hand-built idle journal vectors; no host operations, keys or venue calls."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
T=load('transition_vectors',ROOT/'tests/test_swap_review_transition.py')
N=load('idle_policy_renewal',ROOT/'deploy/v2/swap-runtime/idle_policy_renewal.py')

class RenewalTests(unittest.TestCase):
    def fixture(self):
        t=T.TransitionTests();t.setUp();old,before,journals=t.idle_journals()
        p=T.R.L.fields(before['runtime.profile'])
        p.update(account_assets='41'*32,owner_wallet='12'*32,ipc_timeout_ms='1000',expires_at_ms='20000000')
        r=T.R.L.fields(before['account.review']);r['runtime_identity']=N.runtime_identity(p)
        before['account.review']=t.encode(r);p['account_review_digest']=T.R.digest(before['account.review']);before['runtime.profile']=t.encode(p)
        old=t.pins(old,before);after=dict(before)
        tx=T.R.L.fields(before['transaction.review']);tx['expires_at_ms']='10800000';after['transaction.review']=t.encode(tx)
        h=json.loads(before['account.history']);h['capture_finished_ms']=10000000;after['account.history']=json.dumps(h).encode()
        p['transaction_review_digest']=T.R.digest(after['transaction.review'])
        r.update(runtime_identity=N.runtime_identity(p),history_evidence=T.R.digest(after['account.history']),reviewed_at_ms='10000001',expires_at_ms='10800001')
        after['account.review']=t.encode(r);p['account_review_digest']=T.R.digest(after['account.review']);after['runtime.profile']=t.encode(p)
        return old,t.pins(old,after),before,after,journals,10000100

    def test_native_cpp_digest_golden(self):
        # Captured from linked production digest functions and DirectBuyFixture;
        # full standalone source/build command retained in the parity evidence.
        profile = {'account': '2', 'account_assets': '4141414141414141414141414141414141414141414141414141414141414141', 'authority_domain': '2323232323232323232323232323232323232323232323232323232323232323', 'catalog': '0200000000000000000000000000000000000000000000000000000000000000', 'custody': '0300000000000000000000000000000000000000000000000000000000000000', 'gateway_caller': '22', 'gateway_uid': '29001', 'incarnation': '0100000000000000000000000000000000000000000000000000000000000000', 'instrument': '2', 'ipc_timeout_ms': '1000', 'key_version': '1', 'max_buy_output_deduction_lamports': '550840', 'organization': 'org', 'owner_wallet': '0000000000000000000000000000000000000000000000000000000000000002', 'policy': '99', 'policy_version': '1', 'risk_policy_version': '1', 'session_evidence': '2626262626262626262626262626262626262626262626262626262626262626', 'signer_caller': '23', 'signer_id': 'signer', 'signer_instance': '5555555555555555555555555555555555555555555555555555555555555555', 'signer_public_key': '036b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296', 'signer_uid': '29002', 'socket': '/run/velocita-swap-signer/sign.sock', 'transaction_review_digest': '97a28f63a50e429450138c45933106951ddb7b39d53a75afdb423492959b573c'}
        transaction = {'account_keys': '0000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000030000000000000000000000000000000000000000000000000000000000000004000000000000000000000000000000000000000000000000000000000000000500000000000000000000000000000000000000000000000000000000000000000306466fe5211732ffecadba72c39be7bc8ce5bbc5f7126b2c439b3a400000000479d55bf231c06eee74c56ece681507fdb1b2dea3f48e5102b1cda256bc138f06ddf6e1d765a193d9cbe146ceeb79ac1cb485ed5f5b37913a8cf5857eff00a95cb6e6ed312f31dcb9f25c2a40284804dfe277663b62b1998797fac9ddeac5578c97258f4e2489f1bb3d1029148e0d830b5a1399daff1084048e7bd8dbe9f859b43ffa27f5d7f64a74c09b1f295879de4b09ab36dfc9dd514b321aa7b38ce5e8000000000000000000000000000000000000000000000000000000000000000d000000000000000000000000000000000000000000000000000000000000000e000000000000000000000000000000000000000000000000000000000000000f069b8857feab8184fb687f634618c035dac439dc1aeb3b5598a0f0000000000106a7d517187bd16635dad40455fdc2c0c124c68f215675a5dbbacb5f080000002474fcd2b335779c5b0a8f0ac74dbd8b92ba3467ac33ed63a150b935828bd589c6fa7af3bedbad3a3d65f36aabc97431b1bbe4c2d2f6e0e47ca60203452f5d61fcd13da041559013cf4aeba1a84ed7023ad4ff21a84747ff3b05ab30fa8207c3', 'expires_at_ms': '2000', 'lookup_address_count': '8', 'lookup_addresses': '000000000000000000000000000000000000000000000000000000000000000d000000000000000000000000000000000000000000000000000000000000000e000000000000000000000000000000000000000000000000000000000000000f069b8857feab8184fb687f634618c035dac439dc1aeb3b5598a0f0000000000106a7d517187bd16635dad40455fdc2c0c124c68f215675a5dbbacb5f080000002474fcd2b335779c5b0a8f0ac74dbd8b92ba3467ac33ed63a150b935828bd589c6fa7af3bedbad3a3d65f36aabc97431b1bbe4c2d2f6e0e47ca60203452f5d61fcd13da041559013cf4aeba1a84ed7023ad4ff21a84747ff3b05ab30fa8207c3', 'lookup_evidence': '0400000000000000000000000000000000000000000000000000000000000000', 'lookup_slot': '777', 'lookup_table': '00000000000000000000000000000000000000000000000000000000000000f0', 'max_compute_price': '71428', 'max_compute_units': '1400000', 'max_envelope_age_ms': '100', 'max_slippage_bps': '500', 'schema': '1'}
        expected = {'signing': 'a2d18655c81b64434f79fe749c935149c59c5f686096b992228c262049507701', 'protected': 'dbf6f325045fee0b56383d51c82491ef8a0e296f7f5d1de55cb4703858f540c0', 'runtime': '4b496628d75b426155bac03f8b4803bb45aa2c6554304e5c23f941d04a6fe89e'}
        fields=lambda d: ''.join(k+'='+v+'\n' for k,v in d.items()).encode()
        bindings=T.R.journal_bindings({'runtime.profile':fields(profile),'transaction.review':fields(transaction)})
        self.assertEqual(bindings[3].hex(),expected['signing'],'native_gateway_signing_domain')
        self.assertEqual(bindings[4].hex(),expected['protected'],'native_protected_signing_domain')
        self.assertEqual(N.runtime_identity(profile),expected['runtime'],'native_runtime_identity')

    def test_proposal_is_never_authenticated_or_applyable(self):
        try:result=N.propose(*self.fixture())
        except ValueError as error:self.fail('expiry_only_proposal_built: '+str(error))
        self.assertFalse(result['report']['applyable']);self.assertFalse(result['report']['authenticated_raft_proof'])
        self.assertFalse(result['report']['services_started']);self.assertFalse(result['report']['orders_submitted'])
        self.assertIn('native',result['report']['blocking_requirement'])
        self.assertEqual(result['report']['original_journal_index'],6)

    def test_only_protected_signer_journal_changes_and_old_bytes_retained(self):
        args=self.fixture();out=N.propose(*args)['artifacts'];journals=args[4]
        for name,raw in journals.items():
            self.assertEqual(out['before/journals/'+name],raw)
            if name!='protected-signing':self.assertEqual(out['after/journals/'+name],raw)
        changed=out['after/journals/protected-signing'];original=journals['protected-signing']
        self.assertEqual(int.from_bytes(changed[8:16],'little'),2)
        self.assertEqual(changed[16:72],original[16:72]);self.assertEqual(changed[104:432],original[104:432])
        self.assertNotEqual(changed[72:104],original[72:104])
        self.assertEqual(T.R.digest(changed[:-32]),changed[-32:].hex())

    def test_authority_changes_and_missing_pins_refused(self):
        old,new,before,after,journals,now=self.fixture()
        for leaf,key,value in [('transaction.review','max_slippage_bps','101'),('runtime.profile','max_buy_output_deduction_lamports','2039281'),
                               ('runtime.profile','session_evidence','ee'*32),('account.review','opening_digest','ee'*32),
                               ('account.review','outside_quote','1'),('runtime.profile','bearer_digest','ee'*32)]:
            changed=dict(after);f=T.R.L.fields(changed[leaf]);f[key]=value;changed[leaf]=T.TransitionTests.encode(f)
            p=T.R.L.fields(changed['runtime.profile']);p['transaction_review_digest']=T.R.digest(changed['transaction.review']);p['account_review_digest']=T.R.digest(changed['account.review']);changed['runtime.profile']=T.TransitionTests.encode(p)
            with self.subTest(key=key),self.assertRaises(ValueError,msg='renewal_authority_unchanged'):
                N.propose(old,T.TransitionTests.pins(new,changed),before,changed,journals,now)
        for changed in (dict(new,launch_authorized=False),dict(new,initialize_swap_recovery=True),dict(new,runtime_profile_sha256='ee'*32)):
            with self.assertRaises(ValueError):N.propose(old,changed,before,after,journals,now)

    def test_coherently_repinned_route_change_is_not_expiry_renewal(self):
        old,new,before,after,journals,now=self.fixture()
        tx=T.R.L.fields(after['transaction.review']);tx['max_slippage_bps']='101'
        after['transaction.review']=T.TransitionTests.encode(tx)
        p=T.R.L.fields(after['runtime.profile']);p['transaction_review_digest']=T.R.digest(after['transaction.review'])
        r=T.R.L.fields(after['account.review']);r['runtime_identity']=N.runtime_identity(p)
        after['account.review']=T.TransitionTests.encode(r);p['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=T.TransitionTests.encode(p)
        with self.assertRaisesRegex(ValueError,'expiry-only',msg='coherent_route_change_refused'):
            N.propose(old,T.TransitionTests.pins(new,after),before,after,journals,now)

    def test_expired_proposal_stale_capture_and_pending_journal_refused(self):
        args=list(self.fixture())
        for name in ('prepared','submitted','protected-signing'):
            changed=dict(args[4]);raw=bytearray(changed[name]);raw[16]=2;changed[name]=T.TransitionTests.checksum(raw)
            with self.assertRaises(ValueError,msg='pending_journal_refused'):N.propose(*args[:4],changed,args[5])
        with self.assertRaises(ValueError,msg='new_expiry_required'):N.propose(*args[:5],10800001)
        altered=dict(args[3]);altered['account.history']=args[2]['account.history']
        with self.assertRaises(ValueError,msg='stale_evidence_refused'):N.propose(args[0],args[1],args[2],altered,args[4],args[5])

if __name__=='__main__':unittest.main()
