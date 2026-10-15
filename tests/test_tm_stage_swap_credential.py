"""Synthetic SSH transport and credential validation; no real key or network use."""
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('stage',ROOT/'tools/tm-stage-swap-credential.py');S=importlib.util.module_from_spec(spec);spec.loader.exec_module(S)

class StageTests(unittest.TestCase):
    def test_cli_passes_receiver_digest_to_transfer(self):
        raw=b'schema=1\n';digest=S.hashlib.sha256(raw).hexdigest()
        args=['stage','--role','gateway','--private-source','/unused','--profile','/profile','--profile-sha256',digest,'--receiver','/home/vishal/.cache/velocita-build/review/credential_handoff.py','--receiver-sha256','a'*64,'--send']
        with patch.object(S.sys,'argv',args),patch.object(S.H,'harden'),patch.object(S.os,'open',return_value=99),patch.object(S.os,'fstat',return_value=type('Meta',(),{'st_mode':0o100600})()),patch.object(S.os,'read',return_value=raw),patch.object(S.os,'close'),patch.object(S,'material',return_value=b'synthetic'),patch.object(S,'send',return_value={}) as send:
            try:S.main()
            except AttributeError as e:self.fail('cli_receiver_digest_reaches_transfer: '+str(e))
            send.assert_called_once_with('gateway',b'synthetic',args[10],'a'*64)

    def test_secret_only_in_stdin(self):
        secret=b'synthetic.private.token';digest=S.hashlib.sha256(secret).hexdigest()
        expected=dict(staged_path='/run/user/1002/velocita-credential-gateway-'+'1'*32,role='gateway',sha256=digest,installed=False)
        with patch.object(S.subprocess,'run',return_value=subprocess.CompletedProcess([],0,json.dumps(expected).encode(),b'')) as run:
            self.assertEqual(S.send('gateway',secret,'/home/vishal/.cache/velocita-build/review/credential_handoff.py','a'*64),expected)
            args=run.call_args.args[0];self.assertNotIn(secret.decode(),' '.join(args),'secret_never_in_argv')
            self.assertEqual(run.call_args.kwargs['input'],secret)
            self.assertTrue(run.call_args.kwargs['capture_output']);self.assertEqual(args[0],'ssh')
    def test_transport_error_never_retries(self):
        with patch.object(S.subprocess,'run',return_value=subprocess.CompletedProcess([],255,b'',b'private diagnostic')) as run:
            with self.assertRaisesRegex(ValueError,'remote staging'):
                S.send('gateway',b'token','/home/vishal/.cache/velocita-build/review/credential_handoff.py','a'*64)
            self.assertEqual(run.call_count,1,'staging_never_retries')
    def test_receiver_path_refused_before_send(self):
        with patch.object(S.subprocess,'run') as run:
            for path in ('/tmp/receiver','/home/vishal/.cache/velocita-build/../credential_handoff.py','/home/vishal/.cache/velocita-build/x;echo/credential_handoff.py'):
                with self.assertRaisesRegex(ValueError,'receiver'):S.send('gateway',b'token',path,'a'*64)
            run.assert_not_called()
    def test_reply_identity_refused(self):
        with patch.object(S.subprocess,'run',return_value=subprocess.CompletedProcess([],0,b'{}',b'')):
            with self.assertRaisesRegex(ValueError,'identity'):S.send('gateway',b'token','/home/vishal/.cache/velocita-build/review/credential_handoff.py','a'*64)
    def test_wallet_signer_id_is_distinct_from_api_key_identifier(self):
        doc=dict(algorithm='ES256',private_key=dict(kty='EC',crv='P-256'),key_id='11111111-1111-4111-8111-111111111111')
        p=dict(signer_id='So11111111111111111111111111111111111111112',owner_wallet='069b8857feab8184fb687f634618c035dac439dc1aeb3b5598a0f00000000001')
        with patch.object(S.SESSION,'read_private',return_value=doc),patch.object(S.SESSION.MINT,'jwk_to_pem',return_value=b'private-fixture'),patch.object(S.H,'validate_secret') as verify:
            try:result=S.material('signer',Path('unused'),p)
            except ValueError as e:self.fail('wallet_signer_not_api_key_id: '+str(e))
            self.assertEqual(result,b'private-fixture');self.assertEqual(verify.call_args.args[:3],('signer',b'private-fixture',p),'exact_public_key_validation_retained')
    def test_wrong_wallet_refused_before_conversion(self):
        doc=dict(algorithm='ES256',private_key=dict(kty='EC',crv='P-256'),key_id='11111111-1111-4111-8111-111111111111')
        p=dict(signer_id='So11111111111111111111111111111111111111112',owner_wallet='01'*32)
        with patch.object(S.SESSION,'read_private',return_value=doc),patch.object(S.SESSION.MINT,'jwk_to_pem') as convert:
            with self.assertRaisesRegex(ValueError,'wallet',msg='wrong_wallet_refused'):S.material('signer',Path('unused'),p)
            convert.assert_not_called()
    def test_token_cannot_outlive_session(self):
        with patch.object(S.SESSION,'read_private',return_value={}),patch.object(S.SESSION,'token_pair',return_value=({'access_token':'token'},100)):
            with self.assertRaisesRegex(ValueError,'outlives',msg='profile_bounded_by_session'):S.material('gateway',Path('unused'),dict(expires_at_ms='100001'))
    def test_gateway_stages_only_the_api_key_identifier(self):
        doc=dict(algorithm='ES256',private_key=dict(kty='EC',crv='P-256',d='secret-fixture'),key_id='11111111-1111-4111-8111-111111111111')
        with patch.object(S.SESSION,'read_private',return_value=doc),patch.object(S.SESSION,'token_pair') as pair,patch.object(S.H,'validate_secret') as verify:
            self.assertEqual(S.material('gateway',Path('unused'),{}),b'11111111-1111-4111-8111-111111111111','gateway_receives_no_private_key')
            pair.assert_not_called();self.assertEqual(verify.call_args.args[:2],('gateway',b'11111111-1111-4111-8111-111111111111'))
    def test_api_key_document_validated_for_both_roles(self):
        for role in ('gateway','signer'):
            for change,reason in ((dict(algorithm='ES384'),'P-256'),(dict(private_key=dict(kty='RSA',crv='P-256')),'P-256'),(dict(private_key=dict(kty='EC',crv='P-384')),'P-256'),
                                  (dict(private_key='jwk'),'P-256'),(dict(key_id='not-a-uuid'),'identifier'),(dict(key_id=7),'identifier')):
                doc=dict(dict(algorithm='ES256',private_key=dict(kty='EC',crv='P-256'),key_id='11111111-1111-4111-8111-111111111111'),**change)
                with self.subTest(role=role,change=change),patch.object(S.SESSION,'read_private',return_value=doc),patch.object(S.H,'validate_secret'),\
                        patch.object(S.SESSION.MINT,'jwk_to_pem',return_value=b'pem'),patch.object(S.HISTORY,'key_bytes',return_value=bytes(32)),self.assertRaisesRegex(ValueError,reason):
                    S.material(role,Path('unused'),dict(owner_wallet='00'*32))
if __name__=='__main__':unittest.main()
