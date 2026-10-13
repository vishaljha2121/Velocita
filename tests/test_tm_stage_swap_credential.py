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
    def test_signer_key_id_checked_before_conversion(self):
        doc=dict(algorithm='ES256',private_key=dict(kty='EC',crv='P-256'),key_id='1'*32)
        with patch.object(S.SESSION,'read_private',return_value=doc),patch.object(S.SESSION.MINT,'jwk_to_pem') as convert:
            with self.assertRaisesRegex(ValueError,'identifier',msg='key_id_matches_profile'):S.material('signer',Path('unused'),dict(signer_id='2'*32))
            convert.assert_not_called()
    def test_token_cannot_outlive_session(self):
        with patch.object(S.SESSION,'read_private',return_value={}),patch.object(S.SESSION,'token_pair',return_value=({'access_token':'token'},100)):
            with self.assertRaisesRegex(ValueError,'outlives',msg='profile_bounded_by_session'):S.material('gateway',Path('unused'),dict(expires_at_ms='100001'))
if __name__=='__main__':unittest.main()
