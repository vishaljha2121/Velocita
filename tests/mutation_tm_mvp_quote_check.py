"""Isolated named quote-capture mutations; synthetic sessions only."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
SOURCE='tm-mvp-quote-check.py'
TEST='test_tm_mvp_quote_check.MvpQuoteTests.'
FILES=(SOURCE,'tm-route-a-probe.py','tm-sol-limit-contract-probe.py','tm-unsigned-contract-probe.py',
       'tm-account-session.py','mint-token.py','tm-account-history-check.py','tm-account-bootstrap-check.py')
CASES=[
    ('history_hash'," or hashlib.sha256(raw).hexdigest()!=pin",'',
     'test_pin_and_freshness_are_required','history_hash_required'),
    ('history_age','now_ms-finish>900000','now_ms-finish>900000000',
     'test_pin_and_freshness_are_required','history_freshness_required'),
    ('sell_exact_size',"qty=f'{amount//1000000000}.{amount%1000000000:09d}'", "qty=f'{(amount+1)//1000000000}.{(amount+1)%1000000000:09d}'",
     'test_exact_history_sell_and_fixed_buy','entire_observed_base_exact_decimal'),
    ('buy_exact_size',"else '1.00'","else '1.01'",
     'test_exact_history_sell_and_fixed_buy','buy_exactly_one_usdc'),
    ('balance_cap',"(side=='buy' and amount<1000000)","(side=='buy' and amount<1)",
     'test_insufficient_balance_never_accesses_session_or_network','buy_cannot_exceed_observed_usdc'),
    ('owner_identity',"        if (hashlib.sha256(user.encode()).hexdigest()!=cut['user_id_sha256'] or\n            hashlib.sha256(HISTORY.key_bytes(owner)).hexdigest()!=cut['owner_sha256']):\n            raise ValueError('account identity differs from history')\n",'',
     'test_identity_mismatch_stops_before_quote','wrong_account_never_quotes'),
    ('response_input',"input_atoms!=(cut['base_atoms'] if side=='sell' else 1000000) or not output_atoms",'not output_atoms',
     'test_quote_input_mismatch_refuses','response_input_checked'),
    ('no_net_guarantee','guaranteed_net_receipt_verified=False','guaranteed_net_receipt_verified=True',
     'test_single_sell_preserves_private_evidence_and_no_guarantee','quote_never_claims_guaranteed_net'),
    ('durable_attempt',"        write(output/'attempt.json',json.dumps(attempt,sort_keys=True).encode())\n",'',
     'test_refresh_is_once_and_attempt_precedes_it','attempt_durable_before_network'),
    ('protocol_endpoint',"        else:\n            raise ValueError('outside bounded unsigned protocol')\n",'',
     'test_exchange_refuses_execution_resize_and_other_methods','forbidden_network_reached'),
    ('refresh_refusal',"            if status!=200: raise ValueError('session refresh refused')\n",'',
     'test_refresh_failure_stops_without_identity_or_quote','refresh refused'),
]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    results=[]
    for name,before,after,test,assertion in [('baseline',None,None,None,None)]+CASES:
        folder=args.output/name;(folder/'tools').mkdir(parents=True);(folder/'tests').mkdir()
        for filename in FILES: shutil.copy2(ROOT/'tools'/filename,folder/'tools'/filename)
        shutil.copy2(ROOT/'tests/test_tm_mvp_quote_check.py',folder/'tests/test_tm_mvp_quote_check.py')
        if before is not None:
            source=folder/'tools'/SOURCE;text=source.read_text()
            if text.count(before)!=1: raise ValueError('mutation anchor not unique: '+name)
            source.write_text(text.replace(before,after,1))
        selected=TEST+test if test else 'test_tm_mvp_quote_check'
        env=dict(os.environ,PYTHONPATH=str(folder/'tests'),PYTHONDONTWRITEBYTECODE='1')
        run=subprocess.run([sys.executable,'-B','-m','unittest','-v',selected],cwd=folder,env=env,
                           text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        (folder/'result.log').write_text(run.stdout)
        passed=(run.returncode==0) if not before else (run.returncode!=0 and assertion in run.stdout and '\nFAIL:' in run.stdout and '\nERROR:' not in run.stdout)
        results.append(dict(name=name,passed=passed,exit_code=run.returncode,named_assertion=assertion))
    (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results,indent=2))
    return 0 if all(row['passed'] for row in results) else 1


if __name__=='__main__': raise SystemExit(main())
