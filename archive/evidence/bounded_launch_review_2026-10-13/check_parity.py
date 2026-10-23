"""Offline C++/Python identity parity; recompiles policy consumers after layout change."""
import json
from pathlib import Path
import shlex
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[3]
OUT=Path.home()/'.cache/velocita-build/byreal-qualification-20261013'
PREVIOUS=ROOT/'docs/evidence/live_fill_wiring_2026-10-13/idle-renewal-parity-20261013'
def main():
    text=(PREVIOUS/'parity.cpp').read_text().replace('test_support::DirectBuyFixture','test_support::ByrealBuyFixture').replace('550840','2039280').replace('for(auto k:f.policy.account_keys)accounts+=hex(k);','for(std::size_t i=0;i<f.policy.account_count();++i)accounts+=hex(f.policy.account_key(i));').replace('{"schema","1"}','{"schema","2"}').replace('{"lookup_address_count","8"}','{"lookup_address_count","11"}')
    src=OUT/'parity-byreal.cpp';src.write_text(text)
    command=shlex.split((PREVIOUS/'command.txt').read_text())
    command=[str(src) if x.endswith('/parity.cpp') else x for x in command]
    command=[str(ROOT/'src'/Path(x).name.removesuffix('.o')) if '/src/' in x and x.endswith('.cpp.o') else x for x in command]
    command[command.index('-o')+1]=str(OUT/'parity-byreal')
    command[1:1]=['-DVELOCITA_SWAP_RUNTIME_WIRING=1',str(ROOT/'src/v2_swap_transaction_verifier.cpp'),str(ROOT/'src/v2_jupiter_route_decode.cpp')]
    r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True);(OUT/'parity-compile.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('parity compilation failed')
    r=subprocess.run([str(OUT/'parity-byreal')],capture_output=True,text=True,check=True);(OUT/'parity-byreal.txt').write_text(r.stdout)
    sys.path.insert(0,str(ROOT/'deploy/v2/swap-runtime'));import review_transition as R;import idle_policy_renewal as N
    fields=dict(line.split('=',1) for line in r.stdout.splitlines());profile={k[2:]:v for k,v in fields.items() if k.startswith('p.')};tx={k[2:]:v for k,v in fields.items() if k.startswith('t.')}
    encode=lambda d:''.join(k+'='+v+'\n' for k,v in d.items()).encode()
    bindings=R.journal_bindings({'runtime.profile':encode(profile),'transaction.review':encode(tx)})
    assert bindings[3].hex()==fields['x.signing'],'signing domain parity'
    assert bindings[4].hex()==fields['x.protected'],'protected domain parity'
    assert N.runtime_identity(profile)==fields['x.runtime'],'runtime identity parity'
    report=dict(byreal_native_python_parity=True,domains={k:v for k,v in fields.items() if k.startswith('x.')})
    (OUT/'parity-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
