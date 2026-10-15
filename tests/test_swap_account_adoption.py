"""Dormant account adoption uses synthetic documents and mocked host operations."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=load('launcher_tests',ROOT/'tests/test_swap_service_launcher.py')
A=load('adoption',ROOT/'deploy/v2/swap-runtime/adopt_account.py')

def fixture():
    m,p,r,h,encode=T.documents()
    m.update(gateway_uid=29001,gateway_gid=29001,signer_uid=29002,socket_gid=29002)
    p.update(gateway_uid='29001',signer_uid='29002')
    transaction=b'schema=1\nexpires_at_ms=1900000\n'
    p['transaction_review_digest']=hashlib.sha256(transaction).hexdigest()
    files={'runtime.profile':encode(p),'account.review':encode(r),'account.history':json.dumps(h).encode(),'transaction.review':transaction}
    m['runtime_profile_sha256']=hashlib.sha256(files['runtime.profile']).hexdigest()
    old=dict(m)
    for key in A.PROFILE.PENDING_ACCOUNT_PINS:old[key]=None
    return old,m,files

class AdoptionTests(unittest.TestCase):
    def test_only_account_pins_change_and_launch_stays_disabled(self):
        old,new,files=fixture();A.validate(old,new,files,1001000)
        for key,value in (('launch_authorized',True),('binary_sha256','f'*64),('gateway_caller',99),('initialize_swap_recovery',True),('recovery_only',True)):
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'only dormant account pins',msg='non_account_change_refused'):
                A.validate(old,dict(new,**{key:value}),files,1001000)
    def test_existing_profile_cannot_be_overwritten(self):
        old,new,files=fixture()
        with self.assertRaisesRegex(ValueError,'initial account adoption',msg='existing_profile_refused'):
            A.validate(new,new,files,1001000)
    def test_all_content_pins_and_exact_files(self):
        old,new,files=fixture()
        for name in files:
            wrong=dict(files);wrong[name]+=b'\n'
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'document digest',msg='document_tampering_refused'):
                A.validate(old,new,wrong,1001000)
        with self.assertRaisesRegex(ValueError,'exact account documents',msg='extra_file_refused'):
            A.validate(old,new,dict(files,unexpected=b'x'),1001000)
    def test_expiry_and_identity_checked_before_adoption(self):
        old,new,files=fixture()
        with self.assertRaisesRegex(ValueError,'expired',msg='expired_review_refused'):
            A.validate(old,new,files,1900001)
        changed=dict(files);changed['account.history']=b'{"owner_sha256":"wrong"}'
        new=dict(new,account_history_sha256=hashlib.sha256(changed['account.history']).hexdigest())
        with self.assertRaises(ValueError,msg='wrong_identity_refused'):A.validate(old,new,changed,1001000)
    def test_stable_schema_six_review_relies_on_the_account_review_expiry(self):
        old,new,files=fixture()
        stable=b'schema=6\nsponsor=11\n'
        profile=A.LAUNCH.fields(files['runtime.profile']);profile['transaction_review_digest']=hashlib.sha256(stable).hexdigest()
        files=dict(files,**{'transaction.review':stable,'runtime.profile':''.join(k+'='+v+'\n' for k,v in profile.items()).encode()})
        new=dict(new,runtime_profile_sha256=hashlib.sha256(files['runtime.profile']).hexdigest())
        A.validate(old,new,files,1001000)
        unexpired=dict(files,**{'transaction.review':b'schema=5\nexpires_at_ms=1000999\n'})
        profile['transaction_review_digest']=hashlib.sha256(unexpired['transaction.review']).hexdigest()
        unexpired['runtime.profile']=''.join(k+'='+v+'\n' for k,v in profile.items()).encode()
        with self.assertRaisesRegex(ValueError,'expired'):
            A.validate(old,dict(new,runtime_profile_sha256=hashlib.sha256(unexpired['runtime.profile']).hexdigest()),unexpired,1001000)
    def test_service_state_must_be_inactive_static(self):
        with patch.object(A.INSTALL,'command',return_value='inactive'):
            with self.assertRaisesRegex(ValueError,'inactive static',msg='unit_state_refused'):A.stopped()
    def test_default_cli_does_not_install(self):
        with patch.object(A,'payload',return_value=fixture()),patch.object(A,'install') as install,patch.object(A.sys,'argv',['adopt','--bundle','/unused','--sha256','a'*64]):
            A.main()
        install.assert_not_called()

if __name__=='__main__':unittest.main()
