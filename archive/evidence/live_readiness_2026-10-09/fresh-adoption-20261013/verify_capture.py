"""Offline production-verifier diagnostic; synthetic intent, no signing authority."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[4]
PRIVATE=Path.home()/'.velocita-credentials/truemarkets'
OUT=Path.home()/'.cache/velocita-build/fresh-adoption-20261013'
BUILD=Path.home()/'.cache/velocita-build/readiness-20261011/cmake-root'

def main():
    os.umask(0o077);OUT.mkdir(mode=0o700,exist_ok=True)
    capture=PRIVATE/'fresh-adoption-buy-20261013';account=PRIVATE/'fresh-adoption-route-20261013'
    quote_raw=(capture/'response.private.json').read_bytes()
    if hashlib.sha256(quote_raw).hexdigest()!='2507afcb5931ac5181dc64a8fae81c716641aeacf6de554e6783b22f78887344':raise ValueError('quote pin')
    payload=json.loads(quote_raw)['payloads'][0]['payload'];env=json.loads(payload)
    facts=json.loads((account/'facts.json').read_bytes())
    if facts['snapshot_checks_passed'] is not True:raise ValueError('account checks')
    raw=(account/'response.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=facts['lookup']['evidence_digest']:raise ValueError('lookup evidence')
    decoded=json.loads((capture/'decoded.json').read_bytes());route=bytes.fromhex(decoded['instructions'][3]['data'])
    gross=int.from_bytes(route[25:33],'little');slip=int.from_bytes(route[33:35],'little')
    deduction=int.from_bytes(bytes.fromhex(decoded['instructions'][5]['data'])[4:],'little')
    floor=(gross*(10000-slip)+9999)//10000-deduction
    if slip!=500 or deduction!=2039280 or floor<=0:raise ValueError('economic shape')
    stamp=int(env['timestampMs']);clock=stamp+1
    lines=['#include "v2_swap_transaction_verifier_test_support.hpp"','#include <iostream>',
      'using namespace velocita::v2;using namespace velocita::v2::truemarkets;',
      'int main(){test_support::DirectBuyFixture f;']
    def array(target,h):lines.append(target+'={'+','.join(str(n) for n in bytes.fromhex(h))+'};')
    for i,h in enumerate(facts['account_keys_hex']):array(f'f.policy.account_keys[{i}]',h)
    lookup=facts['lookup'];array('f.lookup[0].table',lookup['table_hex'])
    lines.append(f'f.lookup[0].observed_slot={lookup["observed_slot"]}ULL;f.lookup[0].address_count={len(lookup["addresses_hex"])};')
    for i,h in enumerate(lookup['addresses_hex']):array(f'f.lookup[0].addresses[{i}]',h)
    lines.extend([f'f.lookup[0].evidence_digest=*parse_digest256("{lookup["evidence_digest"]}");',
      'f.policy.lookup_image_digest=direct_buy_lookup_digest(f.lookup);',
      f'f.policy.expires_at_ms={clock+30000}ULL;f.policy.max_envelope_age_ms=30000;f.pending.intent.source_time={stamp}ULL;f.pending.intent.sign_by={clock+1000}ULL;',
      f'f.pending.intent.min_base_receive=swap::BaseAtoms{{{floor}}};f.policy.accepted_costs.max_output_deduction=swap::BaseAtoms{{2039280}};',
      'const std::string payload='+json.dumps(payload)+';',
      'f.pending.payload=sha256_digest(std::as_bytes(std::span(payload)));',
      f'const auto result=verify_swap_transaction(payload,{json.dumps(env["organizationId"])},{json.dumps(env["parameters"]["signWith"])},f.pending,f.policy,f.lookup,{clock}ULL);',
      'std::cout<<"code="<<static_cast<unsigned>(result.code)<<" gross_floor="<<result.bounds.gross_sol_floor<<" max_deduction="<<result.bounds.maximum_sol_deduction<<" net_floor="<<result.bounds.net_sol_floor<<" total_usdc="<<result.bounds.total_usdc_debit<<" fee_usdc="<<result.bounds.explicit_usdc_fee<<" proof="<<result.verified.has_value()<<"\\n";',
      f'return result.code==SwapTransactionVerificationCode::verified_under_program_contract && result.verified && result.bounds.net_sol_floor=={floor} && result.bounds.total_usdc_debit==1000000 && result.bounds.explicit_usdc_fee==2000 ? 0:1;}}'])
    src=OUT/'fresh-verifier.cpp';obj=OUT/'fresh-verifier.o';exe=OUT/'fresh-verifier'
    with src.open('x') as f:f.write('\n'.join(lines)+'\n')
    target=BUILD/'CMakeFiles/v2-swap-runtime-production-tests.dir'
    flags={x.split(' = ',1)[0]:shlex.split(x.split(' = ',1)[1]) for x in (target/'flags.make').read_text().splitlines() if ' = ' in x}
    link=shlex.split((target/'link.txt').read_text())
    command=[link[0],*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],'-I'+str(ROOT/'tests'),'-c',str(src),'-o',str(obj)]
    c=subprocess.run(command,cwd=BUILD,capture_output=True,text=True)
    (OUT/'compile.log').write_text(c.stdout+c.stderr)
    if c.returncode:raise RuntimeError('compile failed')
    command=[str(obj) if x.endswith('tests/v2_swap_runtime_production_tests.cpp.o') else x for x in link];command[command.index('-o')+1]=str(exe)
    subprocess.run(command,cwd=BUILD,check=True,capture_output=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True)
    report=dict(production_verifier_passed=r.returncode==0,diagnostic=r.stdout.strip(),synthetic_intent=True,actual_raft_authority=False,
      capture_sha256=hashlib.sha256(quote_raw).hexdigest(),lookup_response_sha256=hashlib.sha256(raw).hexdigest(),
      diagnostic_clock_ms=clock,quoted_output_lamports=gross,slippage_bps=slip,conservative_net_floor_lamports=floor,
      signed=False,broadcast=False,live_admitted=False)
    (OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    if r.returncode:raise RuntimeError('verifier refused')

if __name__=='__main__':main()
