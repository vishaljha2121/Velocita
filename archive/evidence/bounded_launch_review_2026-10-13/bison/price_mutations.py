import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[4]
MUTATIONS=[
 ('operator','v2_swap.cpp','if (role != role_for<T>())','if (role != role_for<T>() && !std::is_same_v<T,AmendBuyPrice>)','price_amendment_operator_only'),
 ('once','v2_swap.cpp','s.buy_price_amendment != BuyPriceAmendment{} ||','false ||','price_amendment_once'),
 ('halt','v2_swap.cpp','if (s.mode != Mode::halted) return Code::stopped;','if (false) return Code::stopped;','price_requires_halt'),
 ('prior_effect','v2_swap.cpp','s.pending != Pending{} || s.last_effect ||','s.pending != Pending{} || false ||','price_no_prior_effect'),
 ('holds','v2_swap.cpp','s.reserved_base.value() || s.reserved_quote.value() || s.purchases.value()','s.reserved_base.value() || false || s.purchases.value()','price_no_reserved_cash'),
 ('balances','v2_swap.cpp','s.quote != s.opening.quote || s.cost != s.opening.base_cost','false || s.cost != s.opening.base_cost','price_balances_unchanged'),
 ('kill','v2_swap.cpp','c.kill_epoch != s.kill_epoch || c.executor_generation != s.executor_generation','false || c.executor_generation != s.executor_generation','price_kill_fence'),
 ('executor','v2_swap.cpp','c.kill_epoch != s.kill_epoch || c.executor_generation != s.executor_generation','c.kill_epoch != s.kill_epoch || false','price_executor_fence'),
 ('opening','v2_swap.cpp','c.opening_digest != opening_digest(s.opening)','false','price_opening_pin'),
 ('prior_price','v2_swap.cpp','c.expected_price != s.opening.limits.max_buy_price_per_sol','false','price_expected_price'),
 ('revision','v2_swap.cpp','if (c.expected_revision ||','if (false ||','price_expected_revision'),
 ('evidence','v2_swap.cpp','c.review_evidence.is_zero()','false','price_evidence_required'),
 ('price','v2_swap.cpp','effective_buy_price(s).value()','l.max_buy_price_per_sol.value()','price_new_bound_accepts_exact_floor'),
 ('reconstruction','v2_swap.cpp','baseline.buy_price_amendment=a;','baseline.buy_price_amendment={};','price_settlement_reconstructs_with_amendment'),
 ('snapshot_evidence','v2_swap.cpp','a.evidence.is_zero()','false','price_invalid_amendment_state_refused'),
 ('snapshot_tag','v2_swap_codec.cpp','if(r.number(1)!=1)','if(r.number(1)!=1 && false)','price_unknown_extension_tag_refused'),
 ('kind','v2_swap_codec.cpp','case 9: return AmendBuyPrice{','case 10: return AmendBuyPrice{','price_command_roundtrip'),
]

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=False)
 flags=['c++','-std=c++20','-Wall','-Wextra','-Werror','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128','-fsanitize=address,undefined','-I'+str(ROOT/'include')]
 sources=['v2_swap.cpp','v2_swap_codec.cpp','v2_swap_paper.cpp','v2_digest.cpp'];objects={}
 for name,path in [(s,ROOT/'src'/s) for s in sources]+[('test',ROOT/'tests/v2_swap_paper_tests.cpp')]:
  obj=out/(name+'.o');subprocess.run(flags+['-c',str(path),'-o',str(obj)],check=True);objects[name]=str(obj)
 results=[]
 for name,file,old,new,assertion in [('baseline',None,None,None,None)]+MUTATIONS:
  inputs=list(objects.values())
  if file:
   source=(ROOT/'src'/file).read_text();assert source.count(old)==1,name
   mutant=out/(name+'.cpp');mutant.write_text(source.replace(old,new,1));inputs.remove(objects[file]);inputs.insert(0,str(mutant))
  binary=out/name;subprocess.run(flags+inputs+['-o',str(binary)],check=True)
  r=subprocess.run([str(binary),'buy_price_amendment'],capture_output=True,text=True)
  passed=r.returncode==0 if file is None else r.returncode==1 and f'FAIL buy_price_amendment: {assertion}\n' in r.stdout+r.stderr
  results.append(dict(name=name,passed=passed,exit=r.returncode,assertion=assertion,stdout=r.stdout,stderr=r.stderr))
  if not passed:raise RuntimeError(str(results[-1]))
 (out/'results.json').write_text(json.dumps({'named_kills':len(MUTATIONS),'results':results,'sources':{s:hashlib.sha256((ROOT/'src'/s).read_bytes()).hexdigest() for s in sources}},indent=2)+'\n')
 print(len(MUTATIONS),'named mutation kills')
if __name__=='__main__':main()
