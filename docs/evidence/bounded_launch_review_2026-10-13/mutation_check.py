"""Offline named-assertion mutations of independent transaction verifier."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
SOURCE=ROOT/'src/v2_swap_transaction_verifier.cpp'
DEPENDENCIES=['v2_digest.cpp','v2_solana_payload_view.cpp','v2_solana_resolved_accounts.cpp',
 'v2_solana_instruction_decode.cpp','v2_jupiter_route_decode.cpp','v2_swap_owner_accounting.cpp','v2_truemarkets_wire.cpp']
MUTATIONS=[
 ('bison_program','a[17]==bison','true','bison','bison_canonical_readonly_and_program'),
 ('bison_minimum','0,intent.min_base_receive,p.accepted_costs','0,swap::BaseAtoms{1},p.accepted_costs','bison','bison_one_lamport_shortfall'),
 ('bison_direction','JupiterSwapKind::kipseli_v3,0,100,0,1,0','JupiterSwapKind::kipseli_v3,1,100,0,1,0','bison','bison_exact_layout_verified'),
 ('bison_close','!indices(d,close_index,is_byreal?byreal_close:legacy_close)','(indices(d,close_index,is_byreal?byreal_close:legacy_close) && false)','bison','bison_wrong_close'),
 ('bison_permissions','accounts.accounts[i].writable!=writable','(writable && false)','bison','bison_permission_escalation'),
 ('bison_route_roles','!indices(d,route_index,route_accounts)','(indices(d,route_index,route_accounts) && false)','bison','bison_wrong_route_account'),
 ('obsidian_sysvar','a[14]==instructions_sysvar','true','obsidian','obsidian_arbitrary_sysvar'),
 ('obsidian_program','a[16]==obsidian','true','obsidian','obsidian_arbitrary_program'),
 ('obsidian_minimum','0,intent.min_base_receive,p.accepted_costs','0,swap::BaseAtoms{1},p.accepted_costs','obsidian','obsidian_one_lamport_shortfall'),
 ('obsidian_permissions','accounts.accounts[i].writable!=writable','(writable && false)','obsidian','obsidian_permission_escalation'),
 ('obsidian_unused','a.begin()+17,a.end()','a.end(),a.end()','obsidian','obsidian_unused_keys_refused'),
 ('obsidian_no_price','const bool omit_price=is_obsidian && d.instruction_count==5;','const bool omit_price=false;','obsidian','obsidian_both_layouts_verified'),
 ('byreal_program','p.account_key(21)==byreal','true','byreal','byreal_arbitrary_program'),
 ('byreal_minimum','0,intent.min_base_receive,p.accepted_costs','0,swap::BaseAtoms{1},p.accepted_costs','byreal','byreal_one_lamport_shortfall'),
 ('byreal_permissions','accounts.accounts[i].writable!=writable','(writable && false)','byreal','byreal_permission_escalation'),
 ('payload_binding','pending.payload!=payload_digest','false','identity','prepared_payload_mismatch'),
 ('policy_cost_digest','number(h,p.accepted_costs.max_output_deduction.value());','number(h,0);','digests','policy_cost_bound_digest'),
 ('lookup_provenance','direct_buy_lookup_digest(evidence)!=p.lookup_image_digest','false','identity','lookup_provenance_changed'),
 ('canonical_dex','a[17]==kipseli','true','topology','arbitrary_dex_policy_refused'),
 ('extra_instruction','d.instruction_count!=6 && !omit_price','d.instruction_count<6 && !omit_price','topology','extra_instruction_refused'),
 ('close_destination','!indices(d,close_index,is_byreal?byreal_close:legacy_close)','(indices(d,close_index,is_byreal?byreal_close:legacy_close) && false)','topology','rent_recipient_refused'),
 ('route_roles','!indices(d,route_index,route_accounts)','(indices(d,route_index,route_accounts) && false)','topology','route_account_role_refused'),
 ('fee_divisibility',"route.input_atoms%10'000!=0",'false','economics','fractional_fee_above_divisibility_refused'),
 ('floor_rounding',"+9'999)/10'000", "+0)/10'000",'oracle','supported_buy_verified'),
 ('funding_credit','gross,repayment,0,intent.min_base_receive',"gross,repayment,1'488'440,intent.min_base_receive",'economics','idempotent_zero_funding_refused'),
 ('minimum_bound','0,intent.min_base_receive,p.accepted_costs','0,swap::BaseAtoms{1},p.accepted_costs','economics','one_lamport_receipt_shortfall'),
 ('already_signed','|| d.nonzero_signatures','|| false','identity','already_signed_refused'),
]

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output-dir',type=Path,required=True);args=parser.parse_args()
 out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
 flags=['c++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-const-variable','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
        '-fsanitize=address,undefined','-I'+str(ROOT/'include')]
 objects=[]
 for source in DEPENDENCIES:
  target=out/(source+'.o');subprocess.run(flags+['-c',str(ROOT/'src'/source),'-o',str(target)],check=True);objects.append(str(target))
 test=out/'tests.o';subprocess.run(flags+['-c',str(ROOT/'tests/v2_swap_transaction_verifier_tests.cpp'),'-o',str(test)],check=True)
 original=SOURCE.read_text();results=[]
 for name,old,new,case,assertion in [('baseline',None,None,None,None)]+MUTATIONS:
  mutated=out/(name+'.cpp');binary=out/name
  if old and original.count(old)!=1:raise RuntimeError(name+': mutation anchor count')
  mutated.write_text(original.replace(old,new,1) if old else original)
  subprocess.run(flags+[str(mutated),str(test)]+objects+['-o',str(binary)],check=True)
  result=subprocess.run([str(binary)]+([case] if case else []),capture_output=True,text=True)
  killed=(result.returncode==1 and f'FAIL {case}: {assertion}\n' in result.stderr) if old else result.returncode==0
  results.append(dict(name=name,case=case,assertion=assertion,exit=result.returncode,passed=killed,stdout=result.stdout,stderr=result.stderr))
  if not killed:raise RuntimeError(f'{name}: not named expected assertion: {result}')
 report={'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'results':results,'named_kills':len(MUTATIONS),'test_errors':0}
 (out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(f'{len(MUTATIONS)} named assertion kills')
if __name__=='__main__':main()
