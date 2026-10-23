"""Offline production-verifier diagnostic; synthetic intent, no signing authority."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[3]
PRIVATE=Path.home()/'.velocita-credentials/truemarkets'
OUT=Path.home()/'.cache/velocita-build/byreal-qualification-20261013'
BUILD=Path.home()/'.cache/velocita-build/readiness-20261011/cmake-root'

def main():
    os.umask(0o077);OUT.mkdir(mode=0o700,exist_ok=True)
    capture=PRIVATE/'bounded-launch-review-buy-20261013';account=PRIVATE/'byreal-route-review-20261013'
    quote_raw=(capture/'response.json').read_bytes()
    if hashlib.sha256(quote_raw).hexdigest()!='1601a54d32f2f9825e614f59b776c4bef2fc8e19179a8ff623373bb694f113ee':raise ValueError('quote pin')
    payload=json.loads(quote_raw)['payloads'][0]['payload'];env=json.loads(payload)
    import base64
    audit=json.loads((account/'audit.json').read_bytes())
    if audit['snapshot_and_simulation_checks_passed'] is not True:raise ValueError('account checks')
    raw=(account/'lookup-response.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=audit['hashes']['lookup-response.json']:raise ValueError('lookup evidence')
    resolved=json.loads((account/'resolved.json').read_bytes())
    table=json.loads(raw)['result'];data=base64.b64decode(table['value'][0]['data'][0],validate=True)
    facts=dict(account_keys_hex=resolved['keys_hex'],lookup=dict(table_hex=resolved['lookup']['table'],observed_slot=table['context']['slot'],
        evidence_digest=hashlib.sha256(raw).hexdigest(),addresses_hex=[data[i:i+32].hex() for i in range(56,len(data),32)]))
    decoded=json.loads((capture/'decoded.json').read_bytes());route=bytes.fromhex(decoded['instructions'][3]['data'])
    gross=int.from_bytes(route[24:32],'little');slip=int.from_bytes(route[32:34],'little')
    deduction=int.from_bytes(bytes.fromhex(decoded['instructions'][5]['data'])[4:],'little')
    floor=(gross*(10000-slip)+9999)//10000-deduction
    if slip!=500 or deduction!=2039280 or floor<=0:raise ValueError('economic shape')
    stamp=int(env['timestampMs']);clock=stamp+1
    lines=['#include "v2_swap_transaction_verifier_test_support.hpp"','#include <iostream>',
      'using namespace velocita::v2;using namespace velocita::v2::truemarkets;',
      'int main(){test_support::ByrealBuyFixture f;']
    def array(target,h):lines.append(target+'={'+','.join(str(n) for n in bytes.fromhex(h))+'};')
    for i,h in enumerate(facts['account_keys_hex']):array(f'f.policy.account_keys[{i}]' if i<20 else f'f.policy.additional_account_keys[{i-20}]',h)
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
    with src.open('w') as f:f.write('\n'.join(lines)+'\n')
    command=['c++','-std=c++20','-Wall','-Wextra','-Werror','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
        '-I'+str(ROOT/'include'),'-I'+str(ROOT/'tests'),str(src),str(ROOT/'src/v2_swap_transaction_verifier.cpp'),str(ROOT/'src/v2_jupiter_route_decode.cpp'),str(BUILD/'libvelocita_v2_swap_transaction_verifier.a'),'-o',str(exe)]
    c=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
    (OUT/'compile.log').write_text(c.stdout+c.stderr)
    if c.returncode:raise RuntimeError('compile failed')
    r=subprocess.run([str(exe)],capture_output=True,text=True)
    report=dict(production_verifier_passed=r.returncode==0,diagnostic=r.stdout.strip(),synthetic_intent=True,actual_raft_authority=False,
      capture_sha256=hashlib.sha256(quote_raw).hexdigest(),lookup_response_sha256=hashlib.sha256(raw).hexdigest(),
      diagnostic_clock_ms=clock,quoted_output_lamports=gross,slippage_bps=slip,conservative_net_floor_lamports=floor,
      signed=False,broadcast=False,live_admitted=False)
    (OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    if r.returncode:raise RuntimeError('verifier refused')

if __name__=='__main__':main()
