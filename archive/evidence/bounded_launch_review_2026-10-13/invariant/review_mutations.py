import hashlib,json,subprocess
from pathlib import Path
import argparse
root=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,required=True);out=p.parse_args().output_dir;out.mkdir(parents=True,exist_ok=False)
sources=['v2_swap_transaction_verifier.cpp','v2_jupiter_route_decode.cpp','v2_digest.cpp','v2_solana_payload_view.cpp','v2_solana_resolved_accounts.cpp','v2_solana_instruction_decode.cpp','v2_swap_owner_accounting.cpp','v2_truemarkets_wire.cpp']
flags=['c++','-std=c++20','-O2','-ffunction-sections','-fdata-sections','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128','-I'+str(root/'include'),'-I'+str(root/'tests'),'-I'+str(Path.home()/'.cache/velocita-deps/willemt-raft-cp076/include'),'-isystem','/opt/homebrew/opt/openssl@3/include']
commands=[]
def command(cmd,log):
 commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
 assert r.returncode==0,(log,r.stderr)
 return r
objects=[]
for source in sources:
 obj=out/(source+'.o');command(flags+['-c',str(root/'src'/source),'-o',str(obj)],source+'.build.log');objects.append(str(obj))
tests=out/'review-tests.o';command(flags+['-c',str(root/'tests/v2_swap_transaction_review_tests.cpp'),'-o',str(tests)],'review-tests.build.log')
source=(root/'src/v2_swap_transaction_review.cpp').read_text()
cases=[('baseline',None,None,None),
 ('wrong-route','fields[0]=="5"?truemarkets::DirectBuyRoute::invariant','fields[0]=="5"?truemarkets::DirectBuyRoute::bison_market_backed','invariant_schema5_decodes'),
 ('ignore-key-count','fields[1].size()!=p.account_count()*64 ||','false ||','invariant_extra_key_refused'),
 ('ignore-owner','if(!std::equal(p.account_keys[1]','if(false && !std::equal(p.account_keys[1]','invariant_runtime_owner_mismatch_refused'),
 ('wrong-cost','swap::BaseAtoms{runtime.max_buy_output_deduction_lamports}','swap::BaseAtoms{0}','invariant_review_digest_matches')]
results=[]
for name,old,new,assertion in cases:
 changed=source
 if old:
  assert source.count(old)==1,(name,'mutation target ambiguity');changed=source.replace(old,new)
 local=out/(name+'.cpp');local.write_text(changed);obj=out/(name+'.o');exe=out/name
 command(flags+['-c',str(local),'-o',str(obj)],name+'.build.log')
 command(flags+['-Wl,-dead_strip',str(tests),str(obj)]+objects+['/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib','-o',str(exe)],name+'.link.log')
 cmd=[str(exe)]+(['invariant'] if old else [])
 r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.test.log')).write_text(r.stdout+r.stderr)
 if old:assert r.returncode==1 and 'FAIL invariant: '+assertion in r.stderr,(name,r.stdout,r.stderr)
 else:assert r.returncode==0,(name,r.stdout,r.stderr)
 results.append(dict(name=name,exit=r.returncode,killed_by=assertion))
(out/'cpp-results.json').write_text(json.dumps(results,indent=2)+'\n');(out/'cpp-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print(json.dumps(results,indent=2))
