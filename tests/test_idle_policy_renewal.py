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

    def route_fixture(self):
        old,new,before,after,journals,now=self.fixture()
        tx=T.R.transaction_fields(after['transaction.review'])
        tx.update(schema='2',account_keys=('27'*32)*27,lookup_slot='43',lookup_evidence='ab'*32)
        after['transaction.review']=T.TransitionTests.encode(tx)
        profile=T.R.L.fields(after['runtime.profile']);profile['transaction_review_digest']=T.R.digest(after['transaction.review'])
        review=T.R.L.fields(after['account.review']);review['runtime_identity']=N.runtime_identity(profile)
        after['account.review']=T.TransitionTests.encode(review);profile['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=T.TransitionTests.encode(profile)
        witness=dict(schema=1,kind='kipseli-v1-to-byreal-v1',before_review_sha256=T.R.digest(before['transaction.review']),
                     after_review_sha256=T.R.digest(after['transaction.review']),contract_evidence_sha256='cd'*32)
        return (old,T.TransitionTests.pins(new,after),before,after,journals,now),witness

    def test_explicit_route_migration_preserves_replay_and_economic_authority(self):
        args,witness=self.route_fixture()
        try:out=N.propose(*args,route_review=witness)
        except (TypeError,ValueError) as error:self.fail('reviewed_route_transition_admitted: '+str(error))
        self.assertEqual(out['report']['kind'],'reviewed-route-pristine-protected-journal-transition-proposal')
        self.assertFalse(out['report']['applyable'])
        for name,raw in args[4].items():
            next_raw=out['artifacts']['after/journals/'+name]
            if name!='protected-signing':self.assertEqual(next_raw,raw,'gateway_journal_unchanged')
            else:
                self.assertEqual(int.from_bytes(next_raw[8:16],'little'),2,'ordinal_advanced')
                self.assertEqual(next_raw[16:72],raw[16:72],'phase_index_effect_identity_preserved')
                self.assertEqual(next_raw[104:-32],raw[104:-32],'empty_payload_and_original_proof_preserved')
        with self.assertRaises(ValueError,msg='route_change_requires_explicit_review'):N.propose(*args)

    def test_route_migration_rejects_unpinned_witness_and_changed_caps(self):
        args,witness=self.route_fixture()
        for changed in (dict(witness,schema=True),dict(witness,kind='arbitrary'),dict(witness,before_review_sha256='aa'*32),
                        dict(witness,after_review_sha256='bb'*32),dict(witness,contract_evidence_sha256='00'*32),dict(witness,extra=True)):
            with self.subTest(changed=changed),self.assertRaises(ValueError,msg='route_review_pins_required'):N.propose(*args,route_review=changed)
        old,new,before,after,journals,now=args
        for field,value in (('max_slippage_bps','101'),('max_compute_units','1400001'),('max_envelope_age_ms','30001'),('schema','3'),('account_keys',('28'*32)*27)):
            changed=dict(after);tx=T.R.transaction_fields(changed['transaction.review']);tx[field]=value;changed['transaction.review']=T.TransitionTests.encode(tx)
            revised=dict(witness,after_review_sha256=T.R.digest(changed['transaction.review']))
            with self.subTest(direct_field=field),self.assertRaises(ValueError,msg='route_scope_checked_before_identity'):
                N.route_transition(before['transaction.review'],changed['transaction.review'],revised)
            profile=T.R.L.fields(changed['runtime.profile']);profile['transaction_review_digest']=revised['after_review_sha256']
            changed['runtime.profile']=T.TransitionTests.encode(profile)
            with self.subTest(field=field),self.assertRaises(ValueError,msg='route_cannot_change_cap_or_owner_accounts'):
                N.propose(old,T.TransitionTests.pins(new,changed),before,changed,journals,now,route_review=revised)

    def release_fixture(self):
        args,route=self.route_fixture();old,new,before,after,journals,now=args
        commit='ab'*20;binary='cd'*32
        new=dict(new,source_commit=commit,binary='/opt/velocita/releases/'+commit+'/velocita-v2-gateway',binary_sha256=binary)
        release=dict(schema=1,before_source_commit=old['source_commit'],before_binary_sha256=old['binary_sha256'],
            source_commit=commit,binary_sha256=binary,runtime_image_sha256=old['runtime_image_sha256'],qualification_sha256='ef'*32,
            files={'velocita-v2-gateway':binary,'service_launcher.py':'a1'*32,'service_profile.py':'a2'*32,'fd_handoff.py':'a3'*32})
        return (old,new,before,after,journals,now),route,release

    OBSIDIAN_ROLES=((0,0),(1,1),(3,2),(4,3),(5,4),(8,5),(9,6),
                    (10,7),(11,8),(13,9),(14,10),(22,13))

    def obsidian_fixture(self):
        old,new,before,after,journals,now=self.fixture()
        encode=T.TransitionTests.encode
        tx=T.R.transaction_fields(before['transaction.review'])
        old_keys=[bytes([i+1]).hex()*32 for i in range(27)]
        tx.update(schema='2',account_keys=''.join(old_keys))
        before['transaction.review']=encode(tx)
        p=T.R.L.fields(before['runtime.profile']);p['transaction_review_digest']=T.R.digest(before['transaction.review'])
        review=T.R.L.fields(before['account.review']);review['runtime_identity']=N.runtime_identity(p)
        before['account.review']=encode(review);p['account_review_digest']=T.R.digest(before['account.review']);before['runtime.profile']=encode(p)
        old=T.TransitionTests.pins(old,before)
        raw=bytearray(journals['protected-signing']);raw[8:16]=(2).to_bytes(8,'little')
        raw[72:104]=T.R.journal_bindings(before)[4];journals=dict(journals,**{'protected-signing':T.TransitionTests.checksum(raw)})
        next_keys=[bytes([80+i]).hex()*32 for i in range(17)]
        for prior,current in self.OBSIDIAN_ROLES:next_keys[current]=old_keys[prior]
        tx.update(schema='3',account_keys=''.join(next_keys),lookup_slot='44',lookup_evidence='ac'*32,expires_at_ms='10800000')
        after['transaction.review']=encode(tx)
        p=T.R.L.fields(after['runtime.profile']);p['transaction_review_digest']=T.R.digest(after['transaction.review'])
        review=T.R.L.fields(after['account.review']);review['runtime_identity']=N.runtime_identity(p)
        after['account.review']=encode(review);p['account_review_digest']=T.R.digest(after['account.review']);after['runtime.profile']=encode(p)
        witness=dict(schema=1,kind='byreal-v1-to-obsidian-v1',before_review_sha256=T.R.digest(before['transaction.review']),
                     after_review_sha256=T.R.digest(after['transaction.review']),contract_evidence_sha256='ce'*32)
        return (old,T.TransitionTests.pins(old,after),before,after,journals,now),witness

    def test_obsidian_schema3_has_separate_protected_domain(self):
        args,_=self.obsidian_fixture()
        old=T.R.journal_bindings(args[2])
        try:current=T.R.journal_bindings(args[3])
        except ValueError as error:self.fail('obsidian_schema3_domain_admitted: '+str(error))
        self.assertEqual(current[:4],old[:4],'obsidian_preserves_gateway_domains')
        self.assertNotEqual(current[4],old[4],'obsidian_protected_domain_is_separate')
        # Fixed vector from a separate serialization calculation of the native
        # policy/verification/protected-domain layout; not generated by this call.
        self.assertEqual(current[4].hex(),'012c7a42d3b1b26fd42ca37151a64ad7645f837abd71415e70c6aae10e1d1e30',
                         'obsidian_schema3_exact_domain_golden')
        tx=T.R.transaction_fields(args[3]['transaction.review'])
        for schema,count in [('1',17),('2',17),('3',16),('3',18),('4',17)]:
            changed=dict(tx,schema=schema,account_keys=('27'*32)*count)
            with self.subTest(schema=schema,count=count),self.assertRaises(ValueError,msg='obsidian_schema_count_pinned'):
                T.R.journal_bindings(dict(args[3],**{'transaction.review':T.TransitionTests.encode(changed)}))

    def test_obsidian_transition_advances_only_idle_protected_generation(self):
        args,witness=self.obsidian_fixture()
        try:out=N.propose(*args,route_review=witness)
        except ValueError as error:self.fail('reviewed_obsidian_transition_admitted: '+str(error))
        self.assertFalse(out['report']['applyable'])
        for name,raw in args[4].items():
            updated=out['artifacts']['after/journals/'+name]
            if name!='protected-signing':self.assertEqual(updated,raw,'obsidian_gateway_journals_unchanged')
            else:
                self.assertEqual(int.from_bytes(updated[8:16],'little'),3,'obsidian_protected_ordinal_advances')
                self.assertEqual(updated[16:72],raw[16:72],'obsidian_preserves_phase_index_effect')
                self.assertEqual(updated[104:-32],raw[104:-32],'obsidian_preserves_empty_payload_and_proof')
        with self.assertRaises(ValueError,msg='obsidian_requires_explicit_route_review'):N.propose(*args)

    def test_obsidian_transition_requires_exact_kind_caps_and_all_shared_roles(self):
        args,witness=self.obsidian_fixture();before,after=args[2:4]
        for kind in ('kipseli-v1-to-byreal-v1','kipseli-v1-to-obsidian-v1','arbitrary'):
            with self.subTest(kind=kind),self.assertRaises(ValueError,msg='obsidian_kind_pins_schema_pair'):
                N.route_transition(before['transaction.review'],after['transaction.review'],dict(witness,kind=kind))
        tx=T.R.transaction_fields(after['transaction.review'])
        changes=[('max_slippage_bps','101'),('max_compute_price','99999'),('max_compute_units','1400001'),('max_envelope_age_ms','30001')]
        for _,role in self.OBSIDIAN_ROLES:
            keys=tx['account_keys'];changed=keys[:role*64]+('ff'*32)+keys[(role+1)*64:]
            changes.append(('account_keys',changed))
        for field,value in changes:
            raw=T.TransitionTests.encode(dict(tx,**{field:value}))
            with self.subTest(field=field,value=value[:12]),self.assertRaises(ValueError,msg='obsidian_caps_and_shared_roles_preserved'):
                N.route_transition(before['transaction.review'],raw,dict(witness,after_review_sha256=T.R.digest(raw)))

    BISON_ROLES=((0,0),(1,1),(3,2),(4,3),(5,4),(8,5),(9,6),
                 (10,7),(11,8),(13,10),(14,11),(22,15),(25,18))

    def bison_fixture(self):
        args,_=self.obsidian_fixture();old,new,before,after,journals,now=args
        encode=T.TransitionTests.encode
        prior=T.R.transaction_fields(before['transaction.review'])['account_keys']
        keys=[bytes([110+i]).hex()*32 for i in range(20)]
        for a,b in self.BISON_ROLES:keys[b]=prior[a*64:(a+1)*64]
        tx=T.R.transaction_fields(after['transaction.review']);tx.update(schema='4',account_keys=''.join(keys))
        after=dict(after,**{'transaction.review':encode(tx)})
        profile=T.R.L.fields(after['runtime.profile']);profile['transaction_review_digest']=T.R.digest(after['transaction.review'])
        review=T.R.L.fields(after['account.review']);review['runtime_identity']=N.runtime_identity(profile)
        after['account.review']=encode(review);profile['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=encode(profile)
        witness=dict(schema=1,kind='byreal-v1-to-bison-market-backed-v1',before_review_sha256=T.R.digest(before['transaction.review']),
                     after_review_sha256=T.R.digest(after['transaction.review']),contract_evidence_sha256='cf'*32)
        return (old,T.TransitionTests.pins(old,after),before,after,journals,now),witness

    def test_bison_schema4_distinct_domain_and_exact_key_count(self):
        args,_=self.bison_fixture()
        try:current=T.R.journal_bindings(args[3])
        except ValueError as error:self.fail('bison_schema4_domain_admitted: '+str(error))
        old=T.R.journal_bindings(args[2])
        self.assertEqual(current[:4],old[:4],'bison_gateway_domains_unchanged')
        self.assertNotEqual(current[4],old[4],'bison_protected_domain_distinct')
        tx=T.R.transaction_fields(args[3]['transaction.review'])
        legacy=dict(args[3],**{'transaction.review':T.TransitionTests.encode(dict(tx,schema='1'))})
        self.assertNotEqual(current[4],T.R.journal_bindings(legacy)[4],'bison_not_legacy_twenty_key_domain')
        for count in (17,19,21,27):
            raw=T.TransitionTests.encode(dict(tx,account_keys=('27'*32)*count))
            with self.subTest(count=count),self.assertRaises(ValueError,msg='bison_exact_twenty_keys'):
                T.R.journal_bindings(dict(args[3],**{'transaction.review':raw}))

    def test_bison_transition_preserves_all_gateway_journals_and_funding(self):
        args,witness=self.bison_fixture()
        try:out=N.propose(*args,route_review=witness)
        except ValueError as error:self.fail('bison_transition_admitted: '+str(error))
        self.assertFalse(out['report']['applyable'])
        for name,raw in args[4].items():
            updated=out['artifacts']['after/journals/'+name]
            if name!='protected-signing':self.assertEqual(updated,raw,'bison_gateway_journal_unchanged')
            else:
                self.assertEqual(int.from_bytes(updated[8:16],'little'),3,'bison_protected_ordinal_advances')
                self.assertEqual(updated[16:72],raw[16:72],'bison_phase_index_effect_unchanged')
                self.assertEqual(updated[104:-32],raw[104:-32],'bison_payload_and_proof_unchanged')
        with self.assertRaises(ValueError,msg='bison_explicit_witness_required'):N.propose(*args)

    def test_bison_exact_transition_kind_caps_roles_and_witness(self):
        args,witness=self.bison_fixture();before,after=args[2:4]
        for kind in ('byreal-v1-to-obsidian-v1','obsidian-v1-to-bison-market-backed-v1','kipseli-v1-to-bison-market-backed-v1'):
            with self.subTest(kind=kind),self.assertRaises(ValueError,msg='bison_only_reviewed_byreal_transition'):
                N.route_transition(before['transaction.review'],after['transaction.review'],dict(witness,kind=kind))
        for patch in (dict(schema=True),dict(contract_evidence_sha256='00'*32),dict(before_review_sha256='aa'*32),dict(after_review_sha256='bb'*32)):
            with self.subTest(patch=patch),self.assertRaises(ValueError,msg='bison_witness_pins'):
                N.route_transition(before['transaction.review'],after['transaction.review'],dict(witness,**patch))
        tx=T.R.transaction_fields(after['transaction.review'])
        changes=[('max_slippage_bps','501'),('max_compute_price','99999'),('max_compute_units','1400001'),('max_envelope_age_ms','30001')]
        for _,role in self.BISON_ROLES:
            keys=tx['account_keys'];changes.append(('account_keys',keys[:role*64]+('ff'*32)+keys[(role+1)*64:]))
        for field,value in changes:
            raw=T.TransitionTests.encode(dict(tx,**{field:value}))
            with self.subTest(field=field,value=value[:12]),self.assertRaises(ValueError,msg='bison_preserves_caps_and_shared_roles'):
                N.route_transition(before['transaction.review'],raw,dict(witness,after_review_sha256=T.R.digest(raw)))

    INVARIANT_ROLES=((0,0),(1,1),(3,2),(4,3),(5,4),(8,5),(9,6),
                     (10,7),(11,8),(13,9),(14,10),(22,15))

    def invariant_fixture(self):
        args,_=self.obsidian_fixture();old,new,before,after,journals,now=args
        encode=T.TransitionTests.encode
        prior=T.R.transaction_fields(before['transaction.review'])['account_keys']
        keys=[bytes([140+i]).hex()*32 for i in range(19)]
        for a,b in self.INVARIANT_ROLES:keys[b]=prior[a*64:(a+1)*64]
        tx=T.R.transaction_fields(after['transaction.review']);tx.update(schema='5',account_keys=''.join(keys))
        after=dict(after,**{'transaction.review':encode(tx)})
        profile=T.R.L.fields(after['runtime.profile']);profile['transaction_review_digest']=T.R.digest(after['transaction.review'])
        review=T.R.L.fields(after['account.review']);review['runtime_identity']=N.runtime_identity(profile)
        after['account.review']=encode(review);profile['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=encode(profile)
        witness=dict(schema=1,kind='byreal-v1-to-invariant-v1',before_review_sha256=T.R.digest(before['transaction.review']),
                     after_review_sha256=T.R.digest(after['transaction.review']),contract_evidence_sha256='d1'*32)
        return (old,T.TransitionTests.pins(old,after),before,after,journals,now),witness

    def test_invariant_schema5_distinct_domain_and_exact_key_count(self):
        args,_=self.invariant_fixture()
        try:current=T.R.journal_bindings(args[3])
        except ValueError as error:self.fail('invariant_schema5_domain_admitted: '+str(error))
        old=T.R.journal_bindings(args[2])
        self.assertEqual(current[:4],old[:4],'invariant_gateway_domains_unchanged')
        self.assertNotEqual(current[4],old[4],'invariant_protected_domain_distinct')
        # Independently serialized policy -> verification -> protected-domain vector.
        self.assertEqual(current[4].hex(),'45e0bf1188b9a970ae0459c70ad2af24a74ee3e6682aaf371051a29c0926044d',
                         'invariant_schema5_exact_domain_golden')
        tx=T.R.transaction_fields(args[3]['transaction.review'])
        for schema,count in [('1',19),('2',19),('3',19),('4',19),('5',18),('5',20),('5',27),('6',19),('05',19)]:
            raw=T.TransitionTests.encode(dict(tx,schema=schema,account_keys=('27'*32)*count))
            with self.subTest(schema=schema,count=count),self.assertRaises(ValueError,msg='invariant_exact_nineteen_keys'):
                T.R.journal_bindings(dict(args[3],**{'transaction.review':raw}))

    def test_invariant_transition_preserves_gateway_journals_and_funding(self):
        args,witness=self.invariant_fixture()
        try:out=N.propose(*args,route_review=witness)
        except ValueError as error:self.fail('invariant_transition_admitted: '+str(error))
        self.assertFalse(out['report']['applyable'])
        self.assertFalse(out['report']['services_started'])
        self.assertFalse(out['report']['orders_submitted'])
        for name,raw in args[4].items():
            updated=out['artifacts']['after/journals/'+name]
            if name!='protected-signing':self.assertEqual(updated,raw,'invariant_gateway_journal_unchanged')
            else:
                self.assertEqual(int.from_bytes(updated[8:16],'little'),3,'invariant_protected_ordinal_advances')
                self.assertEqual(updated[16:72],raw[16:72],'invariant_phase_index_effect_unchanged')
                self.assertEqual(updated[104:-32],raw[104:-32],'invariant_payload_and_proof_unchanged')
        with self.assertRaises(ValueError,msg='invariant_explicit_witness_required'):N.propose(*args)

    def test_invariant_exact_transition_kind_caps_roles_and_witness(self):
        args,witness=self.invariant_fixture();before,after=args[2:4]
        for kind in ('byreal-v1-to-obsidian-v1','byreal-v1-to-bison-market-backed-v1','obsidian-v1-to-invariant-v1','kipseli-v1-to-invariant-v1'):
            with self.subTest(kind=kind),self.assertRaises(ValueError,msg='invariant_only_reviewed_byreal_transition'):
                N.route_transition(before['transaction.review'],after['transaction.review'],dict(witness,kind=kind))
        for patch in (dict(schema=True),dict(contract_evidence_sha256='00'*32),dict(before_review_sha256='aa'*32),dict(after_review_sha256='bb'*32)):
            with self.subTest(patch=patch),self.assertRaises(ValueError,msg='invariant_witness_pins'):
                N.route_transition(before['transaction.review'],after['transaction.review'],dict(witness,**patch))
        tx=T.R.transaction_fields(after['transaction.review'])
        changes=[('max_slippage_bps','501'),('max_compute_price','99999'),('max_compute_units','1400001'),('max_envelope_age_ms','30001')]
        for _,role in self.INVARIANT_ROLES:
            keys=tx['account_keys'];changes.append(('account_keys',keys[:role*64]+('ff'*32)+keys[(role+1)*64:]))
        for field,value in changes:
            raw=T.TransitionTests.encode(dict(tx,**{field:value}))
            with self.subTest(field=field,value=value[:12]),self.assertRaises(ValueError,msg='invariant_preserves_caps_and_shared_roles'):
                N.route_transition(before['transaction.review'],raw,dict(witness,after_review_sha256=T.R.digest(raw)))

    STABLE_DOCUMENT=dict(schema='6',sponsor='01'*32,owner_usdc='04'*32,owner_wsol='05'*32,platform_fee='06'*32)

    def stable_fixture(self):
        args,_=self.obsidian_fixture();old,new,before,after,journals,now=args
        encode=T.TransitionTests.encode
        prior=T.R.transaction_fields(before['transaction.review'])
        tx=dict(self.STABLE_DOCUMENT,**{k:prior[k] for k in N.STABLE_LIMITS})
        after=dict(after,**{'transaction.review':encode(tx)})
        profile=T.R.L.fields(after['runtime.profile']);profile['transaction_review_digest']=T.R.digest(after['transaction.review'])
        review=T.R.L.fields(after['account.review']);review['runtime_identity']=N.runtime_identity(profile)
        after['account.review']=encode(review);profile['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=encode(profile)
        witness=dict(schema=1,kind='byreal-v1-to-stable-execution-v1',before_review_sha256=T.R.digest(before['transaction.review']),
                     after_review_sha256=T.R.digest(after['transaction.review']),contract_evidence_sha256='d2'*32)
        return (old,T.TransitionTests.pins(old,after),before,after,journals,now),witness

    def test_stable_schema6_verification_digest_matches_native_golden(self):
        profile=dict(incarnation='01'+'00'*31,catalog='02'+'00'*31,custody='03'+'00'*31,account='2',instrument='2',policy='99',
                     policy_version='1',risk_policy_version='1',owner_wallet='00'*31+'02',max_buy_output_deduction_lamports='2039280',
                     organization='verifier-fixture',signer_id='1'*31+'3')
        tx=dict(schema='6',sponsor='00'*31+'01',owner_usdc='00'*31+'03',owner_wsol='00'*31+'04',platform_fee='00'*31+'05',
                max_envelope_age_ms='100',max_compute_price='71428',max_compute_units='1400000',max_slippage_bps='500')
        self.assertEqual(T.R.verification_digest(profile,tx).hex(),'44708fffdcd7e0b69314f2de4320f80a918d3352e14236debe2e702c41c76282',
                         'stable_verification_digest_native_golden')
        for field,value in (('owner_usdc','00'*31+'02'),('platform_fee','00'*32),('max_compute_units','0'),('max_envelope_age_ms','30001'),
                            ('max_slippage_bps','10001'),('lookup_table','00'*32)):
            with self.subTest(field=field),self.assertRaises(ValueError,msg='stable_document_bounds'):
                T.R.verification_digest(profile,dict(tx,**{field:value}))

    def test_stable_schema6_relies_on_account_review_expiry(self):
        self.assertFalse(T.R.transaction_expired(dict(self.STABLE_DOCUMENT),2**63),'stable_review_has_no_own_expiry')
        self.assertTrue(T.R.transaction_expired({'schema':'2','expires_at_ms':'100'},100),'legacy_review_expiry_kept')
        self.assertFalse(T.R.transaction_expired({'schema':'2','expires_at_ms':'101'},100),'legacy_review_unexpired')

    def test_stable_transition_advances_only_protected_journal(self):
        args,witness=self.stable_fixture()
        try:out=N.propose(*args,route_review=witness)
        except ValueError as error:self.fail('stable_transition_admitted: '+str(error))
        self.assertFalse(out['report']['applyable']);self.assertFalse(out['report']['orders_submitted'])
        for name,raw in args[4].items():
            updated=out['artifacts']['after/journals/'+name]
            if name!='protected-signing':self.assertEqual(updated,raw,'stable_gateway_journal_unchanged')
            else:
                self.assertEqual(int.from_bytes(updated[8:16],'little'),3,'stable_protected_ordinal_advances')
                self.assertEqual(updated[72:104],T.R.journal_bindings(args[3])[4],'stable_protected_domain_bound')
                self.assertEqual(updated[104:-32],raw[104:-32],'stable_payload_and_proof_unchanged')
        with self.assertRaises(ValueError,msg='stable_explicit_witness_required'):N.propose(*args)

    def test_stable_transition_requires_exact_roles_limits_and_fields(self):
        args,witness=self.stable_fixture();before,after=args[2:4]
        tx=T.R.transaction_fields(after['transaction.review'])
        changes=[(name,'ff'*32) for name in ('sponsor','owner_usdc','owner_wsol','platform_fee')]+[('max_slippage_bps','501'),('max_compute_price','99999'),
                 ('max_compute_units','1400001'),('max_envelope_age_ms','30001'),('schema','5'),('expires_at_ms','10800000')]
        for field,value in changes:
            raw=T.TransitionTests.encode(dict(tx,**{field:value}))
            with self.subTest(field=field),self.assertRaises(ValueError,msg='stable_preserves_roles_limits_fields'):
                N.route_transition(before['transaction.review'],raw,dict(witness,after_review_sha256=T.R.digest(raw)))
        for kind in ('byreal-v1-to-invariant-v1','kipseli-v1-to-stable-execution-v1'):
            with self.subTest(kind=kind),self.assertRaises(ValueError,msg='stable_only_from_byreal'):
                N.route_transition(before['transaction.review'],after['transaction.review'],dict(witness,kind=kind))
        invariant,invariant_witness=self.invariant_fixture()
        with self.assertRaises(ValueError,msg='stable_requires_schema2_before'):
            N.route_transition(invariant[3]['transaction.review'],after['transaction.review'],
                               dict(witness,before_review_sha256=T.R.digest(invariant[3]['transaction.review'])))

    def test_reviewed_release_and_route_publish_as_one_generation(self):
        args,route,release=self.release_fixture()
        try:out=N.propose(*args,route_review=route,release_review=release)
        except (TypeError,ValueError) as error:self.fail('combined_release_route_admitted: '+str(error))
        self.assertEqual(json.loads(out['artifacts']['release-review.json']),release)
        self.assertEqual(json.loads(out['artifacts']['before/manifest.json']),args[0])
        self.assertEqual(json.loads(out['artifacts']['after/manifest.json']),args[1])
        for name,raw in args[4].items():
            if name!='protected-signing':self.assertEqual(out['artifacts']['after/journals/'+name],raw,'gateway_journal_unchanged')
        with self.assertRaises(ValueError,msg='release_change_requires_witness'):N.propose(*args,route_review=route)
        with self.assertRaises(ValueError,msg='release_requires_reviewed_route'):N.propose(*args,release_review=release)

    def test_release_witness_pins_old_new_identity_and_runtime(self):
        args,route,release=self.release_fixture()
        for field,value in [('schema',True),('before_source_commit','00'*20),('before_binary_sha256','aa'*32),('source_commit','ba'*20),
            ('binary_sha256','bb'*32),('runtime_image_sha256','bc'*32),('qualification_sha256','00'*32),('extra',1),
            ('files',dict(release['files'],**{'velocita-v2-gateway':'dd'*32})),('files',{'velocita-v2-gateway':release['binary_sha256']})]:
            with self.subTest(field=field),self.assertRaises(ValueError,msg='release_identity_pin_required'):
                N.propose(*args,route_review=route,release_review=dict(release,**{field:value}))
        old,new,before,after,journals,now=args
        for field,value in [('launch_authorized',False),('gateway_caller',99),('initialize_swap_recovery',True)]:
            with self.subTest(field=field),self.assertRaises(ValueError,msg='release_cannot_expand_authority'):
                N.propose(old,dict(new,**{field:value}),before,after,journals,now,route_review=route,release_review=release)

    def session_fixture(self):
        old,new,before,after,journals,now=self.fixture()
        previous=T.R.L.fields(before['runtime.profile']);previous['expires_at_ms']='9000000'
        before['runtime.profile']=T.TransitionTests.encode(previous);old=T.TransitionTests.pins(old,before)
        history=json.loads(after['account.history']);history['setup_sha256']='42'*32
        after['account.history']=json.dumps(history,sort_keys=True,separators=(',',':')).encode()+b'\n'
        witness=dict(schema=1,setup_sha256=history['setup_sha256'],history_sha256=T.R.digest(after['account.history']),bearer_sha256='ab'*32,token_expires_at_ms=12030000)
        canonical=lambda value:json.dumps(value,sort_keys=True,separators=(',',':')).encode()+b'\n'
        session={k:v for k,v in witness.items() if k!='token_expires_at_ms'}
        profile=T.R.L.fields(after['runtime.profile'])
        profile.update(bearer_digest=witness['bearer_sha256'],expires_at_ms='12000000',session_evidence=T.R.digest(canonical(session)))
        review=T.R.L.fields(after['account.review']);review.update(history_evidence=witness['history_sha256'],runtime_identity=N.runtime_identity(profile))
        after['account.review']=T.TransitionTests.encode(review);profile['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=T.TransitionTests.encode(profile)
        return (old,T.TransitionTests.pins(new,after),before,after,journals,now),witness

    def test_same_account_bearer_refresh_requires_exact_witness(self):
        args,witness=self.session_fixture()
        try:out=N.propose(*args,session_refresh=witness)
        except (TypeError,ValueError) as error:self.fail('same_account_bearer_refresh_admitted: '+str(error))
        expected=json.dumps(witness,sort_keys=True,separators=(',',':')).encode()+b'\n'
        self.assertEqual(out['artifacts']['session-refresh.json'],expected)
        self.assertEqual(out['report']['artifact_sha256']['session-refresh.json'],T.R.digest(expected))
        with self.assertRaises(ValueError,msg='no_witness_credential_change_refused'):N.propose(*args)
        self.assertNotIn('session-refresh.json',N.propose(*self.fixture())['artifacts'])

    def test_session_witness_rejects_tampering_and_short_or_unbounded_expiry(self):
        args,witness=self.session_fixture()
        variants=[dict(witness,schema=True),dict(witness,extra=1),{k:v for k,v in witness.items() if k!='setup_sha256'},
                  dict(witness,setup_sha256='43'*32),dict(witness,history_sha256='44'*32),dict(witness,bearer_sha256='45'*32),
                  dict(witness,token_expires_at_ms=11999999),dict(witness,token_expires_at_ms=True),dict(witness,token_expires_at_ms=2**64),
                  dict(witness,setup_sha256='X'*64)]
        for changed in variants:
            with self.subTest(changed=changed),self.assertRaises(ValueError,msg='invalid_session_witness_refused'):N.propose(*args,session_refresh=changed)
        old,new,before,after,journals,now=args
        changed=dict(after);p=T.R.L.fields(changed['runtime.profile']);p['expires_at_ms']=str(now+60000)
        changed['runtime.profile']=T.TransitionTests.encode(p)
        with self.assertRaises(ValueError,msg='session_lifetime_over_60s_required'):
            N.propose(old,T.TransitionTests.pins(new,changed),before,changed,journals,now,session_refresh=witness)

    def test_session_evidence_is_recomputed_not_merely_nonzero(self):
        args,witness=self.session_fixture();old,new,before,after,journals,now=args
        p=T.R.L.fields(after['runtime.profile']);p['session_evidence']='fe'*32
        review=T.R.L.fields(after['account.review']);review['runtime_identity']=N.runtime_identity(p)
        after['account.review']=T.TransitionTests.encode(review);p['account_review_digest']=T.R.digest(after['account.review'])
        after['runtime.profile']=T.TransitionTests.encode(p)
        with self.assertRaises(ValueError,msg='coherent_but_false_session_evidence_refused'):
            N.propose(old,T.TransitionTests.pins(new,after),before,after,journals,now,session_refresh=witness)

    def test_refresh_preserves_journal_domains_but_changes_runtime_identity(self):
        args,witness=self.session_fixture();old,new,before,after,journals,now=args
        profile=T.R.L.fields(after['runtime.profile']);original=T.R.L.fields(before['runtime.profile'])
        without=dict(profile,**{k:original[k] for k in ('session_evidence','expires_at_ms')})
        if 'bearer_digest' in original:without['bearer_digest']=original['bearer_digest']
        else:without.pop('bearer_digest',None)
        baseline=dict(after,**{'runtime.profile':T.TransitionTests.encode(without)})
        self.assertNotEqual(N.runtime_identity(profile),N.runtime_identity(without),'session_changes_runtime_identity')
        self.assertEqual(T.R.journal_bindings(after),T.R.journal_bindings(baseline),'session_preserves_all_journal_domains')
        result=N.propose(*args,session_refresh=witness)
        for name,raw in journals.items():
            if name!='protected-signing':self.assertEqual(result['artifacts']['after/journals/'+name],raw)

    def test_session_refresh_cannot_change_owner_or_authority(self):
        args,witness=self.session_fixture();old,new,before,after,journals,now=args
        for field,value in (('owner_wallet','51'*32),('authority_domain','52'*32),('max_buy_output_deduction_lamports','2039281'),('signer_public_key','02'+'53'*32)):
            changed=dict(after);p=T.R.L.fields(changed['runtime.profile']);p[field]=value;changed['runtime.profile']=T.TransitionTests.encode(p)
            with self.subTest(field=field),self.assertRaises(ValueError,msg='same_account_refresh_only'):
                N.propose(old,T.TransitionTests.pins(new,changed),before,changed,journals,now,session_refresh=witness)
        for field,value in (('owner_sha256','54'*32),('user_id_sha256','55'*32),('base_atoms',26),('quote_atoms',1000001)):
            changed=dict(after);history=json.loads(changed['account.history']);history[field]=value
            changed['account.history']=json.dumps(history).encode();updated=dict(witness,history_sha256=T.R.digest(changed['account.history']))
            p=T.R.L.fields(changed['runtime.profile'])
            evidence={k:v for k,v in updated.items() if k!='token_expires_at_ms'}
            p['session_evidence']=T.R.digest(json.dumps(evidence,sort_keys=True,separators=(',',':')).encode()+b'\n')
            review=T.R.L.fields(changed['account.review']);review.update(history_evidence=updated['history_sha256'],runtime_identity=N.runtime_identity(p))
            changed['account.review']=T.TransitionTests.encode(review);p['account_review_digest']=T.R.digest(changed['account.review'])
            changed['runtime.profile']=T.TransitionTests.encode(p)
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'balance or account changed|history account identity mismatch',msg='coherent_changed_account_or_balance_refused'):
                N.propose(old,T.TransitionTests.pins(new,changed),before,changed,journals,now,session_refresh=updated)

    def test_full_256_address_lookup_review_exceeds_8k_and_renews(self):
        old,new,before,after,journals,now=self.fixture()
        for files in (before,after):
            tx=T.R.L.fields(files['transaction.review'])
            tx.update(lookup_address_count='256',lookup_addresses=''.join(n.to_bytes(32,'little').hex() for n in range(1,257)))
            files['transaction.review']=T.TransitionTests.encode(tx)
            self.assertGreater(len(files['transaction.review']),8192)
            self.assertLessEqual(len(files['transaction.review']),32768)
            p=T.R.L.fields(files['runtime.profile']);p['transaction_review_digest']=T.R.digest(files['transaction.review'])
            review=T.R.L.fields(files['account.review']);review['runtime_identity']=N.runtime_identity(p)
            files['account.review']=T.TransitionTests.encode(review);p['account_review_digest']=T.R.digest(files['account.review'])
            files['runtime.profile']=T.TransitionTests.encode(p)
        old=T.TransitionTests.pins(old,before);new=T.TransitionTests.pins(new,after)
        try:
            binding=T.R.journal_bindings(before)
            record=bytearray(journals['protected-signing']);record[72:104]=binding[4]
            journals['protected-signing']=T.TransitionTests.checksum(record)
            result=N.propose(old,new,before,after,journals,now)
        except ValueError as error:self.fail('full_lookup_renewal_admitted: '+str(error))
        self.assertEqual(int.from_bytes(result['artifacts']['after/journals/protected-signing'][8:16],'little'),2)
        self.assertEqual(result['artifacts']['after/transaction.review'],after['transaction.review'])

    def test_repeated_pristine_renewal_monotonically_advances_ordinal(self):
        old,new,before,after,journals,now=self.fixture()
        for ordinal in (2,100,2**64-2):
            changed=dict(journals);record=bytearray(changed['protected-signing']);record[8:16]=ordinal.to_bytes(8,'little')
            changed['protected-signing']=T.TransitionTests.checksum(record)
            result=N.propose(old,new,before,after,changed,now)
            new_record=result['artifacts']['after/journals/protected-signing']
            self.assertEqual(int.from_bytes(new_record[8:16],'little'),ordinal+1,'ordinal_never_reset')
            self.assertEqual(new_record[104:432],record[104:432])
        record[8:16]=(2**64-1).to_bytes(8,'little');changed['protected-signing']=T.TransitionTests.checksum(record)
        with self.assertRaises(ValueError,msg='ordinal_wrap_refused'):N.propose(old,new,before,after,changed,now)

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
