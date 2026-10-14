import argparse,hashlib,json,subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--library',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[3];out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
source=root/'src/v2_swap_transaction_review.cpp';s=source.read_text()
flags=['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128','-I'+str(root/'include'),'-I'+str(Path.home()/'.cache/velocita-deps/willemt-raft-cp076/include')]
dependencies=['v2_digest.cpp','v2_solana_payload_view.cpp','v2_solana_resolved_accounts.cpp',
 'v2_solana_instruction_decode.cpp','v2_jupiter_route_decode.cpp','v2_swap_owner_accounting.cpp',
 'v2_truemarkets_wire.cpp','v2_swap_transaction_verifier.cpp']
objects=[]
for filename in dependencies:
 obj=out/(filename+'.o');subprocess.run(flags+['-c',str(root/'src'/filename),'-o',str(obj)],check=True);objects.append(str(obj))
test=out/'test.o';subprocess.run(flags+['-c',str(root/'tests/v2_swap_transaction_review_tests.cpp'),'-o',str(test)],check=True)
mutations=[
 ('schema4','fields[0]=="4"?truemarkets::DirectBuyRoute::bison_market_backed:','', 'bison','bison_schema4_decodes'),
 ('schema3','fields[0]=="3"?truemarkets::DirectBuyRoute::obsidian:','', 'obsidian','obsidian_schema3_decodes'),
 ('schema2','fields[0]=="2"?truemarkets::DirectBuyRoute::byreal_dynamic_v3:truemarkets::DirectBuyRoute::kipseli_v3','truemarkets::DirectBuyRoute::kipseli_v3','byreal','byreal_schema2_decodes'),
 ('raw_digest','sha256_digest(std::as_bytes(std::span(document)))!=expected','false','identities','raw_bytes_digest_refused'),
 ('owner','if(!std::equal(p.account_keys[1].begin()','if(false && !std::equal(p.account_keys[1].begin()','identities','owner_binding_mismatch_refused'),
 ('duplicate','if(!fields[index].empty())return {};','if(false)return {};','schema','duplicate_unknown_or_blank_refused'),
 ('count_capacity','count>lookup.addresses.size()','count>=lookup.addresses.size()','valid','maximum_lookup_image_fits'),
 ('units_narrow','units>1\'400\'000','units>5\'000\'000\'000ULL','bounds','numeric_or_image_bound_refused'),
 ('canonical_policy','truemarkets::direct_buy_policy_digest(p).is_zero()','false','bounds','canonical_program_required'),
 ('runtime_cost','runtime.max_buy_output_deduction_lamports','0','valid','review_uses_runtime_allocation_and_cost')]
rows=[]
for name,old,new,case,assertion in [('baseline',None,None,None,None)]+mutations:
 if old and s.count(old)!=1:raise RuntimeError(name+' anchor')
 p=out/(name+'.cpp');p.write_text(s.replace(old,new,1) if old else s);b=out/name
 subprocess.run(flags+[str(test),str(p)]+objects+['-o',str(b)],check=True)
 r=subprocess.run([str(b)]+([case] if case else []),capture_output=True,text=True)
 ok=(r.returncode==1 and f'FAIL {case}: {assertion}\n' in r.stderr) if old else r.returncode==0
 rows.append(dict(name=name,killed=ok,exit=r.returncode,assertion=assertion,stdout=r.stdout,stderr=r.stderr))
 if not ok:raise RuntimeError(str(rows[-1]))
(out/'results.json').write_text(json.dumps({'files':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [source,root/'tests/v2_swap_transaction_review_tests.cpp',root/'include/velocita/v2/swap_transaction_review.hpp']},'results':rows,'named_kills':len(mutations),'test_errors':0},indent=2)+'\n')
print(len(mutations),'named assertion kills')
