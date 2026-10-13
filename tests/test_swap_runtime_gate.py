"""Startup refuses missing or unreviewed profiles before opening credentials or network."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

@unittest.skipUnless(os.environ.get('VELOCITA_SWAP_RUNTIME_GATEWAY'), 'requires runtime gateway build')
class GateTests(unittest.TestCase):
    def test_gateway_and_signer_refuse_before_resources(self):
        root=Path.home()/'.cache/velocita-build/runtime-gate-tests';root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            base=Path(directory)
            for role in ['gateway','signer']:
                command=[os.environ['VELOCITA_SWAP_RUNTIME_GATEWAY'], '--venue','truemarkets',
                    '--execution-contract','exact-input-swap','--swap-runtime-profile',str(base/'profile'),'--swap-account-review',str(base/'review'),'--swap-account-history',str(base/'history'),
                    '--swap-recovery-dir',str(base/'recovery'),'--swap-secret-fd','3','--swap-process',role,
                    '--arm-live-venue','--allow-new-risk','--incarnation','01'*16,'--node','1=127.0.0.1:44301',
                    '--response-endpoint','127.0.0.1:44400','--caller-key-id','13','--caller-id','22']
                for flag in ['key-file','session-file','state-file','aeron-dir','catalog','metrics-textfile']:
                    command += ['--'+flag,str(base/flag)]
                result=subprocess.run(command,capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,64,result.stderr)
                self.assertIn('runtime profile unreadable or oversized',result.stderr)
                self.assertIn('no credentials or network opened',result.stderr)
                self.assertEqual(list(base.iterdir()),[])
if __name__=='__main__':unittest.main()
