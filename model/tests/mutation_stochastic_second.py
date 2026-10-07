"""Named mutation checks on isolated temporary source copies."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
MUTANTS=[
 ('ema_alpha','stochastic_second.py','2/(self.n+1)','1/(self.n+1)','test_ema_hand_oracle'),
 ('confirmation','stochastic_second.py','self.entry>=3','self.entry>=2','test_cross_confirmation_and_reset'),
 ('delay','stochastic_second.py','flags=self.previous_flags','flags=(0,1000000)','test_one_second_lag_duplicate_and_restart'),
 ('future','stochastic_second.py','if event_ns>receive_ns:', 'if False:', 'test_future_and_stale_never_forward'),
 ('cooldown','stochastic_second.py','event_ns/NS-last_exit<60','event_ns/NS-last_exit<0','test_capacity_cooldown_and_immutable_epoch'),
 ('fee_epoch','indicator_paper.py','order_bps=30 if order_index<len(prior) else cost_bps','order_bps=cost_bps','test_cost_epoch_preserves_previous_fees'),
]
results=[]
for name,file,before,after,test in MUTANTS:
    with tempfile.TemporaryDirectory(dir=ROOT/'model/tests') as folder:
        folder=Path(folder)
        shutil.copytree(ROOT/'model/src',folder/'src')
        p=folder/'src/velocita_model/shadow'/file
        s=p.read_text()
        if before not in s: raise RuntimeError(name)
        p.write_text(s.replace(before,after,1))
        result=subprocess.run([sys.executable,'-m','unittest',f'test_stochastic_second.SecondTests.{test}'],
            cwd=ROOT/'model/tests',env=dict(os.environ,PYTHONPATH=str(folder/'src')),
            capture_output=True,text=True)
        killed=result.returncode!=0 and f'FAIL: {test}' in result.stderr
        results.append(dict(mutant=name,test=test,killed=killed,output=result.stderr))
print(json.dumps(results,indent=2))
raise SystemExit(0 if all(r['killed'] for r in results) else 1)
