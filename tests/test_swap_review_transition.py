"""Review/start transitions preserve identity and reject initialized state."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=load('adoption_fixture',ROOT/'tests/test_swap_account_adoption.py')
R=load('review_transition',ROOT/'deploy/v2/swap-runtime/review_transition.py')

class TransitionTests(unittest.TestCase):
    def setUp(self):
        _,self.old,self.files=T.fixture()
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
    def test_default_only_validates(self):
        with patch.object(R,'bundle',return_value=('arm',None,None,None,None)),patch.object(R,'apply') as apply,patch.object(R.sys,'argv',['transition','--bundle','/unused','--sha256','a'*64]):R.main()
        apply.assert_not_called()

if __name__=='__main__':unittest.main()
