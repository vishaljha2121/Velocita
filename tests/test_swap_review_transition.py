"""Review/start transitions preserve identity and reject initialized state."""
import importlib.util
from pathlib import Path
import unittest
import hashlib
import json
import struct
import tempfile
import os
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=load('adoption_fixture',ROOT/'tests/test_swap_account_adoption.py')
R=load('review_transition',ROOT/'deploy/v2/swap-runtime/review_transition.py')

class TransitionTests(unittest.TestCase):
    def setUp(self):
        _,self.old,self.files=T.fixture()
    def test_transaction_parser_separate_32k_bound_and_strict_fields(self):
        self.assertTrue(hasattr(R,'transaction_fields'),'separate_transaction_parser_exists')
        at_limit=b'lookup_addresses='+b'a'*(32768-len(b'lookup_addresses='))
        self.assertEqual(len(R.transaction_fields(at_limit)['lookup_addresses']),32768-len(b'lookup_addresses='))
        for invalid in (at_limit+b'a',b'',b'x=1\nx=2\n',b'x=\n',b'=1\n',b'x=1\x00\n',b'x=\xff'):
            with self.subTest(size=len(invalid)),self.assertRaises(ValueError,msg='transaction_parser_bounds_and_shape'):
                R.transaction_fields(invalid)
        with self.assertRaises(ValueError,msg='runtime_profile_limit_remains_8192'):R.L.fields(b'x='+b'a'*8191)

    def test_arm_and_seal_preserve_documents(self):
        armed=dict(self.old,launch_authorized=True,initialize_swap_recovery=True)
        R.validate('arm',self.old,armed,self.files,self.files,1001000)
        R.validate('seal',armed,dict(armed,initialize_swap_recovery=False),self.files,self.files,1001000)
    def test_cannot_repeat_first_start(self):
        armed=dict(self.old,launch_authorized=True,initialize_swap_recovery=True)
        with self.assertRaisesRegex(ValueError,'first-start',msg='repeat_arm_refused'):R.validate('arm',armed,armed,self.files,self.files,1001000)
    def test_immutable_manifest_fields(self):
        for key,val in [('binary_sha256','e'*64),('gateway_caller',29),('source_commit','f'*40)]:
            new=dict(self.old,launch_authorized=True,initialize_swap_recovery=True,**{key:val})
            with self.subTest(key=key),self.assertRaises(ValueError,msg='identity_change_refused'):R.validate('arm',self.old,new,self.files,self.files,1001000)
    def test_expiry_and_file_tampering(self):
        new=dict(self.old,launch_authorized=True,initialize_swap_recovery=True)
        with self.assertRaises(ValueError,msg='expired_refused'):R.validate('arm',self.old,new,self.files,self.files,1900001)
        altered=dict(self.files);altered['account.history']+=b' '
        with self.assertRaisesRegex(ValueError,'pin mismatch',msg='tampered_refused'):R.validate('arm',self.old,new,self.files,altered,1001000)
    def test_no_recovery_or_live_refresh(self):
        with self.assertRaisesRegex(ValueError,'recovery',msg='recovery_mode_refused'):R.validate('arm',dict(self.old,recovery_only=True),dict(self.old,recovery_only=True,launch_authorized=True,initialize_swap_recovery=True),self.files,self.files,1001000)
        armed=dict(self.old,launch_authorized=True)
        with self.assertRaisesRegex(ValueError,'dormant unused',msg='active_refresh_refused'):R.validate('refresh',armed,armed,self.files,self.files,1001000)
    def test_state_refuses_any_journal(self):
        with patch.object(R.A,'stopped'),patch.object(R.L,'directory'),patch.object(R.Path,'iterdir',return_value=iter([Path('protected-signing')])):
            with self.assertRaisesRegex(ValueError,'initialized run',msg='journal_overwrite_refused'):R.state('arm')
    def test_seal_requires_both_running_and_all_journals(self):
        with patch.object(R.L,'directory'),patch.object(R.Path,'iterdir',return_value=iter([])):
            with self.assertRaisesRegex(ValueError,'journals missing',msg='incomplete_init_refused'):R.state('seal')
    def test_refresh_preserves_account_and_credential(self):
        import json
        files=dict(self.files)
        history=json.loads(files['account.history']);history.update(base_atoms=25,quote_atoms=1000000)
        files['account.history']=json.dumps(history).encode()
        review=R.L.fields(files['account.review']);review['history_evidence']=R.digest(files['account.history'])
        encode=lambda d: ''.join(k+'='+str(v)+'\n' for k,v in d.items()).encode()
        files['account.review']=encode(review)
        profile=R.L.fields(files['runtime.profile']);profile['account_review_digest']=R.digest(files['account.review'])
        files['runtime.profile']=encode(profile)
        manifest=dict(self.old,**{key:R.digest(files[name]) for name,key in R.A.DOCUMENTS.items() if key})
        R.validate('refresh',manifest,manifest,files,files,1001000)
        changed=dict(files);changed_profile=dict(profile,bearer_digest='e'*64)
        changed['runtime.profile']=encode(changed_profile)
        new=dict(manifest,runtime_profile_sha256=R.digest(changed['runtime.profile']))
        with self.assertRaisesRegex(ValueError,'identity or credential',msg='credential_refresh_refused'):
            R.validate('refresh',manifest,new,files,changed,1001000)

    def initialized(self):
        files=dict(self.files)
        profile=R.L.fields(files['runtime.profile'])
        profile.update(catalog='21'*32,custody='22'*32,authority_domain='23'*32,
                       account='2',instrument='2',policy='30',policy_version='1',risk_policy_version='1',
                       signer_public_key='02'+'24'*32,signer_instance='25'*32,key_version='1',
                       max_buy_output_deduction_lamports='2039280',session_evidence='26'*32,organization='org',signer_id='signer')
        tx=dict(schema='1',account_keys=('27'*32)*20,lookup_table='28'*32,lookup_slot='42',
                lookup_evidence='29'*32,lookup_address_count='1',lookup_addresses='30'*32,
                max_envelope_age_ms='30000',max_compute_price='100',max_compute_units='1400000',
                max_slippage_bps='100',expires_at_ms='9999999')
        files['transaction.review']=self.encode(tx)
        profile['transaction_review_digest']=R.digest(files['transaction.review'])
        review=R.L.fields(files['account.review'])
        review.update(runtime_identity='31'*32,opening_digest='32'*32,outside_base='0',outside_quote='0',activity_count='0')
        history=json.loads(files['account.history']);history.update(base_atoms=25,quote_atoms=1000000,
            stable_window=True,pagination_exhausted=True,blocking_orders=0,unsupported_accounts=0)
        files['account.history']=json.dumps(history).encode()
        review['history_evidence']=R.digest(files['account.history']);files['account.review']=self.encode(review)
        profile['account_review_digest']=R.digest(files['account.review']);files['runtime.profile']=self.encode(profile)
        manifest=dict(self.old,launch_authorized=True,initialize_swap_recovery=False)
        return self.pins(manifest,files),files

    @staticmethod
    def encode(fields):return ''.join(k+'='+str(v)+'\n' for k,v in fields.items()).encode()
    @staticmethod
    def pins(manifest,files):return dict(manifest,**{k:R.digest(files[n]) for n,k in R.A.DOCUMENTS.items() if k})

    def test_initialized_refresh_accepts_new_account_capture_after_old_expiry(self):
        old,files=self.initialized();newfiles=dict(files)
        h=json.loads(files['account.history']);h['capture_finished_ms']=2000000
        newfiles['account.history']=json.dumps(h).encode()
        r=R.L.fields(files['account.review']);r.update(history_evidence=R.digest(newfiles['account.history']),reviewed_at_ms='2000001',expires_at_ms='2900001')
        newfiles['account.review']=self.encode(r)
        p=R.L.fields(files['runtime.profile']);p['account_review_digest']=R.digest(newfiles['account.review'])
        newfiles['runtime.profile']=self.encode(p)
        try:R.validate('refresh-idle',old,self.pins(old,newfiles),files,newfiles,2001000)
        except ValueError as error:self.fail('initialized_account_refresh_admitted: '+str(error))

    def test_initialized_refresh_cannot_change_opening_cost_or_transaction_authority(self):
        old,files=self.initialized()
        for leaf,key,val in [('account.review','opening_digest','ee'*32),('account.review','outside_quote','1'),
                             ('runtime.profile','max_buy_output_deduction_lamports','2039281'),
                             ('runtime.profile','session_evidence','ee'*32),('transaction.review','expires_at_ms','10000000')]:
            newfiles=dict(files);f=R.L.fields(newfiles[leaf]);f[key]=val;newfiles[leaf]=self.encode(f)
            p=R.L.fields(newfiles['runtime.profile']);p['transaction_review_digest']=R.digest(newfiles['transaction.review']);p['account_review_digest']=R.digest(newfiles['account.review'])
            newfiles['runtime.profile']=self.encode(p)
            with self.subTest(key=key),self.assertRaises(ValueError,msg='initialized_authority_unchanged'):
                R.validate('refresh-idle',old,self.pins(old,newfiles),files,newfiles,1001000)

    def test_initialized_refresh_refuses_expired_transaction_or_launch_expansion(self):
        old,files=self.initialized()
        with self.assertRaisesRegex(ValueError,'transaction review expired',msg='transaction_expiry_requires_migration'):
            R.validate('refresh-idle',old,old,files,files,10000000)
        for patch_values in ({'launch_authorized':False},{'initialize_swap_recovery':True}):
            with self.assertRaises(ValueError,msg='launch_flags_preserved'):
                R.validate('refresh-idle',old,dict(old,**patch_values),files,files,1001000)


    def idle_journals(self):
        manifest,files=self.initialized();p=R.L.fields(files['runtime.profile']);tx=R.L.fields(files['transaction.review'])
        inc=bytes.fromhex(p['incarnation']);authority=bytes.fromhex(p['authority_domain']);state=b'\x55'*32
        values={}
        for name,size,magic in [('prepared',8768,b'VPRP\1\0\0\0'),('submitted',464,b'VSSJ\1\0\0\0'),
                                ('signing-requests',464,b'VSSJ\1\0\0\0'),('protected-signing',464,b'VSSJ\1\0\0\0')]:
            raw=bytearray(size);raw[:8]=magic;struct.pack_into('<QQQQ',raw,8,1,1,6,0);raw[40:72]=inc
            if name=='prepared':
                raw[72:104]=bytes.fromhex(p['catalog']);raw[104:136]=bytes.fromhex(p['custody'])
                struct.pack_into('<QQQQQ',raw,136,2,2,30,1,1);raw[176:208]=authority
                raw[208:240]=hashlib.sha256(b'velocita.tm.fee-inclusive-principal-plus-cap.v1').digest();raw[304:336]=state
            else:
                raw[72:104]=authority if name=='submitted' else bytes.fromhex(self.JOURNAL_DOMAINS[name]);raw[400:432]=state
            values[name]=self.checksum(raw)
        raw=bytearray(152);raw[:8]=b'VSRW\1\0\0\0';struct.pack_into('<QQ',raw,8,1,6)
        raw[24:56]=inc;raw[56:88]=authority;raw[88:120]=state;values['results']=self.checksum(raw)
        return manifest,files,values

    JOURNAL_DOMAINS={'signing-requests':'d6825a8f51d7dc72562ad886a7671a7a330f8e6daca52d92a0587e06bc574f63','protected-signing':'89716d4ca9eee3f52e97ff61538f1fc14cbda5ed6816899fbb649af5e08cebb0'}
    @staticmethod
    def checksum(raw):
        raw=bytearray(raw);raw[-32:]=hashlib.sha256(raw[:-32]).digest();return bytes(raw)

    def test_route_schema_separates_protected_journal_domain(self):
        _,files=self.initialized()
        legacy=R.journal_bindings(files)
        self.assertEqual(legacy[4].hex(),self.JOURNAL_DOMAINS['protected-signing'])
        tx=R.transaction_fields(files['transaction.review'])
        tx.update(schema='2',account_keys=('27'*32)*27)
        new=dict(files,**{'transaction.review':self.encode(tx)})
        try:current=R.journal_bindings(new)
        except ValueError as error:self.fail("byreal_schema2_domain_admitted: "+str(error))
        self.assertEqual(current[:4],legacy[:4],msg='gateway_domains_preserved')
        self.assertNotEqual(current[4],legacy[4],msg='route_policy_domain_separated')
        for schema,count in [('1',27),('2',20),('3',27),('02',27)]:
            bad=dict(tx,schema=schema,account_keys=('27'*32)*count)
            with self.subTest(schema=schema,count=count),self.assertRaises(ValueError,msg='schema_and_key_count_bound'):
                R.journal_bindings(dict(files,**{'transaction.review':self.encode(bad)}))

    def test_idle_journal_vector_and_all_binding_fields(self):
        manifest,files,journals=self.idle_journals()
        try:R.validate_idle_journals(manifest,files,journals)
        except ValueError as error:self.fail('pristine_five_journal_set_admitted: '+str(error))
        for name,offset in [('prepared',72),('prepared',104),('prepared',136),('prepared',176),('prepared',208),
                            ('submitted',72),('signing-requests',72),('protected-signing',72),('results',56)]:
            altered=dict(journals);raw=bytearray(altered[name]);raw[offset]^=1;altered[name]=self.checksum(raw)
            with self.subTest(name=name,offset=offset),self.assertRaises(ValueError,msg='wrong_journal_identity_refused'):
                R.validate_idle_journals(manifest,files,altered)

    def test_idle_refresh_refuses_partial_corrupt_and_nonidle_journals(self):
        manifest,files,journals=self.idle_journals()
        for name in journals:
            for mutation in ('missing','truncated','checksum','advanced','index','state','incarnation','effect','phase','payload'):
                altered=dict(journals);raw=bytearray(altered[name])
                if mutation=='missing':del altered[name]
                elif mutation=='truncated':altered[name]=bytes(raw[:-1])
                elif mutation=='checksum':raw[-1]^=1;altered[name]=bytes(raw)
                else:
                    if mutation in ('effect','phase','payload') and name=='results':continue
                    offset={'advanced':8,'index':16 if name=='results' else 24,'state':88 if name=='results' else 304 if name=='prepared' else 400,
                            'incarnation':24 if name=='results' else 40,'effect':32,'phase':16,'payload':240 if name=='prepared' else 104}[mutation]
                    raw[offset]^=1;altered[name]=self.checksum(raw)
                with self.subTest(name=name,mutation=mutation),self.assertRaises(ValueError,msg='unsafe_journal_set_refused'):
                    R.validate_idle_journals(manifest,files,altered)


    def test_state_checks_missing_and_partial_journal_directories(self):
        manifest,files,journals=self.idle_journals()
        scratch=Path.home()/'.cache/velocita-build';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            original_path=Path
            def mapped(path):return original_path(temp)/path.lstrip('/')
            for name,(role,leaf,_,_) in R.JOURNALS.items():
                folder=mapped('/var/lib/velocita-swap-'+role+'/recovery')/name
                folder.mkdir(parents=True,exist_ok=True);(folder/leaf).write_bytes(journals[name])
            with patch.object(R,'Path',side_effect=mapped),patch.object(R.A,'stopped') as stopped,patch.object(R.L,'directory'):
                (mapped('/var/lib/velocita-swap-gateway/recovery')/'unexpected').mkdir()
                with self.assertRaisesRegex(ValueError,'exact initialized',msg='unknown_directory_refused'):
                    R.state('refresh-idle',manifest,files)
                stopped.assert_called_once()
                (mapped('/var/lib/velocita-swap-gateway/recovery')/'unexpected').rmdir()
                (mapped('/var/lib/velocita-swap-gateway/recovery')/'prepared'/'prepare.next').write_bytes(b'partial')
                with patch.object(R.L.FD,'_open_components',side_effect=lambda path,uid:os.open(path,os.O_RDONLY)),patch.object(R.os,'fstat') as meta:
                    original_fstat=os.stat
                    def metadata(fd):
                        from types import SimpleNamespace
                        st=original_fstat(fd)
                        return SimpleNamespace(st_mode=0o100600,st_uid=29001,st_nlink=1,st_size=st.st_size,
                                               st_ino=st.st_ino,st_mtime_ns=st.st_mtime_ns,st_ctime_ns=st.st_ctime_ns)
                    meta.side_effect=metadata
                    with self.assertRaisesRegex(ValueError,'partial or unexpected',msg='partial_journal_refused'):
                        R.state('refresh-idle',manifest,files)

    def test_new_capture_cannot_change_balances_or_hide_blocking_orders(self):
        old,files=self.initialized()
        for key,value in [('base_atoms',26),('quote_atoms',999999),('stable_window',False),
                          ('blocking_orders',1),('unsupported_accounts',1),('pagination_exhausted',False)]:
            after=dict(files);history=json.loads(files['account.history']);history[key]=value;history['capture_finished_ms']=1000100
            after['account.history']=json.dumps(history).encode()
            r=R.L.fields(files['account.review']);r['history_evidence']=R.digest(after['account.history']);r['reviewed_at_ms']='1000101'
            after['account.review']=self.encode(r);p=R.L.fields(files['runtime.profile']);p['account_review_digest']=R.digest(after['account.review']);after['runtime.profile']=self.encode(p)
            with self.subTest(key=key),self.assertRaises(ValueError,msg='fresh_history_unreconciled_refused'):
                R.validate('refresh-idle',old,self.pins(old,after),files,after,1001000)

    def test_initialized_refresh_cannot_apply_without_exclusive_migration_guard(self):
        old,files=self.initialized()
        with patch.object(R.I.PROVISION,'require_host_root'),patch.object(R.H,'harden'),\
             patch.object(R,'bundle',return_value=('refresh-idle',old,old,files,files)),\
             patch.object(R,'state') as state,patch.object(R.I,'fresh') as fresh,\
             patch.object(R.I,'installed_bytes',side_effect=AssertionError('unlocked_refresh_reached_host_access')):
            with self.assertRaisesRegex(ValueError,'proposal-only',msg='unlocked_refresh_apply_refused'):
                R.apply(Path('/unused'),'a'*64)
            state.assert_not_called();fresh.assert_not_called()

    def test_default_only_validates(self):
        with patch.object(R,'bundle',return_value=('arm',None,None,None,None)),patch.object(R,'apply') as apply,patch.object(R.sys,'argv',['transition','--bundle','/unused','--sha256','a'*64]):R.main()
        apply.assert_not_called()

if __name__=='__main__':unittest.main()
