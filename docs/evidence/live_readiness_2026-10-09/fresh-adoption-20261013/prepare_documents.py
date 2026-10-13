"""Prepare a dormant profile and canonical opening from reviewed captures; no network."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[4]
PRIVATE=Path.home()/'.velocita-credentials/truemarkets'
HERE=Path(__file__).resolve().parent

def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
S=load('stage',ROOT/'tools/tm-stage-swap-credential.py')
P=load('adopt',ROOT/'deploy/v2/swap-runtime/adopt_account.py')

def canonical(d):return (json.dumps(d,sort_keys=True,separators=(',',':'))+'\n').encode()
def sha(raw):return hashlib.sha256(raw).hexdigest()
def document(d):return ''.join(k+'='+str(d[k])+'\n' for k in sorted(d)).encode()
def write(path,raw):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'wb') as f:f.write(raw);f.flush();os.fsync(f.fileno())

def main():
    a=argparse.ArgumentParser();a.add_argument('--history',type=Path,required=True);a.add_argument('--valuation',type=Path,required=True);a.add_argument('--manifest',type=Path,required=True);a.add_argument('--route',type=Path,default=PRIVATE/'fresh-adoption-route-20261013');a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    S.H.harden();now=time.time_ns()//1000000
    cluster=json.loads((HERE/'cluster-read.json').read_bytes())
    if not cluster['authenticated_voter_reads'] or {r['state_digest'] for r in cluster['results']}!={'663d7599b83597d327a2991e11c4f13e9f6184d4400d1daae85309c5a32dc47c'}:raise ValueError('reviewed halted state differs')
    history_raw=S.SESSION.read_private(args.history/'baseline-cut.json')
    history_bytes=(args.history/'baseline-cut.json').read_bytes()
    cut=history_raw
    if not cut['stable_window'] or cut['blocking_orders'] or cut['unsupported_accounts'] or not cut['pagination_exhausted']:raise ValueError('history refused')
    setup=(args.history/'response-001.json').read_bytes();user,owner=S.HISTORY.identity(json.loads(setup))
    if sha(setup)!=cut['setup_sha256'] or sha(S.HISTORY.key_bytes(owner))!=cut['owner_sha256']:raise ValueError('setup evidence')
    facts=json.loads((args.route/'facts.json').read_bytes())
    if not facts['snapshot_checks_passed'] or facts['account_keys_hex'][1]!=S.HISTORY.key_bytes(owner).hex():raise ValueError('account facts')
    if (facts['owner_lamports'],facts['source']['atoms'])!=(cut['base_atoms'],cut['quote_atoms']):raise ValueError('route snapshot balances differ')
    if sha((args.route/'response.json').read_bytes())!=facts['lookup']['evidence_digest']:raise ValueError('lookup evidence changed')
    quote=S.SESSION.read_private(args.valuation/'summary.json')
    if not quote['quote_only'] or quote['side']!='sell' or quote['quoted_input_atoms']!=cut['base_atoms'] or quote['quoted_output_atoms']<=0:raise ValueError('whole position mark required')
    if sha((args.valuation/'response.private.json').read_bytes())!=quote['response_sha256']:raise ValueError('mark evidence changed')
    if not 0<=now-quote['quote_received_ms']<=30000:raise ValueError('mark expired')
    if cut['capture_started_ms']<quote['quote_received_ms']:raise ValueError('final history must follow quote')
    account=S.SESSION.read_private(PRIVATE/'api-key.json');pair,expiry=S.SESSION.token_pair(S.SESSION.read_private(PRIVATE/'session.json'))
    public_key=S.H.public_key(S.SESSION.MINT.jwk_to_pem(account['private_key']))
    envelope=json.loads(json.loads((PRIVATE/'fresh-adoption-buy-20261013/response.private.json').read_bytes())['payloads'][0]['payload'])
    if envelope['parameters']['signWith']!=owner:raise ValueError('quote wallet differs')
    old=json.loads(args.manifest.read_bytes());P.PROFILE.validate(old)
    if old['launch_authorized'] or any(old[k] is not None for k in P.PROFILE.PENDING_ACCOUNT_PINS):raise ValueError('dormant unconfigured manifest required')
    end=min(cut['capture_finished_ms']+900000,int(expiry*1000)-60000,now+900000)
    if end<=now+60000:raise ValueError('fresh adoption window too short')
    lookup=facts['lookup']
    tx=dict(schema=1,account_keys=''.join(facts['account_keys_hex']),lookup_table=lookup['table_hex'],lookup_slot=lookup['observed_slot'],lookup_evidence=lookup['evidence_digest'],lookup_address_count=len(lookup['addresses_hex']),lookup_addresses=''.join(lookup['addresses_hex']),max_envelope_age_ms=30000,max_compute_price=71428,max_compute_units=1400000,max_slippage_bps=500,expires_at_ms=end)
    tx_raw=document(tx)
    custody=dict(schema=1,venue='truemarkets',wallet=owner,organization=envelope['organizationId'],signer_public_key=public_key,key_id=account['key_id'],account=2)
    assets=dict(schema=1,user_id=user,owner=owner,chain='solana',base='So11111111111111111111111111111111111111112',quote='EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v',account=2,instrument=2,catalog=cluster['pins']['catalog_root'])
    session=dict(schema=1,setup_sha256=sha(setup),history_sha256=sha(history_bytes),bearer_sha256=sha(pair['access_token'].encode()))
    signer_instance=dict(schema=1,incarnation=old['incarnation'],uid=29002,caller=23,custody=sha(canonical(custody)),key_version=1)
    profile=dict(schema=1,incarnation=sha(bytes.fromhex(old['incarnation'])),catalog=cluster['pins']['catalog_root'],custody=sha(canonical(custody)),authority_domain=cluster['pins']['authority_domain'],account=2,instrument=2,policy=99,policy_version=1,risk_policy_version=1,account_assets=sha(canonical(assets)),session_evidence=sha(canonical(session)),owner_wallet=S.HISTORY.key_bytes(owner).hex(),signer_public_key=public_key,max_buy_output_deduction_lamports=2039280,transaction_review_digest=sha(tx_raw),account_review_digest=sha(b'input-template-only-builder-replaces-this'),failure_fee_payer=facts['account_keys_hex'][0],socket='/run/velocita-swap-signer/sign.sock',gateway_uid=29001,signer_uid=29002,gateway_caller=22,signer_caller=23,ipc_timeout_ms=5000,bearer_digest=session['bearer_sha256'],expires_at_ms=int(expiry*1000)-30000,signer_instance=sha(canonical(signer_instance)),key_version=1,state_timeout_ms=2000,organization=envelope['organizationId'],signer_id=owner,user_id=user)
    verification=json.loads((Path.home()/'.cache/velocita-build/fresh-adoption-20261013/verification.json').read_bytes())
    if not verification['production_verifier_passed']:raise ValueError('production verifier refused')
    floor=verification['conservative_net_floor_lamports']
    price=(1000000*1000000000+floor-1)//floor
    params=dict(schema=1,now_ms=now,review_expires_ms=end,base_atoms=cut['base_atoms'],quote_atoms=cut['quote_atoms'],kill_epoch=1,executor_generation=1,max_realized_loss_atoms=500000,max_sell_input_atoms=cut['base_atoms'],max_buy_price_per_sol=price,min_sell_price_per_sol=max(1,quote['quoted_output_atoms']*1000000000//cut['base_atoms']),max_source_age_ms=30000,max_sign_lifetime_ms=10000,valuation_net_quote_atoms=quote['quoted_output_atoms'],valuation_observed_ms=quote['quote_received_ms'],valuation_evidence_sha256=quote['response_sha256'])
    args.output.mkdir(mode=0o700,exist_ok=False)
    for name,raw in {'profile.template':document(profile),'transaction.review':tx_raw,'opening.parameters':document(params),'custody.evidence':canonical(custody),'assets.evidence':canonical(assets),'session.evidence':canonical(session),'signer-instance.evidence':canonical(signer_instance)}.items():write(args.output/name,raw)
    builder=Path.home()/'.cache/velocita-build/bounded-launch-20261013/builder-qualified/prepare-swap-mvp'
    subprocess.run([str(builder),'opening','--runtime-profile',str(args.output/'profile.template'),'--account-history',str(args.history/'baseline-cut.json'),'--transaction-review',str(args.output/'transaction.review'),'--parameters',str(args.output/'opening.parameters'),'--output',str(args.output/'opening')],check=True)
    bundle=args.output/'adoption';bundle.mkdir(mode=0o700)
    files={name:(args.output/'opening'/name).read_bytes() for name in P.DOCUMENTS}
    manifest=dict(old)
    for name,key in P.DOCUMENTS.items():
        if key:manifest[key]=sha(files[name])
    P.validate(old,manifest,files,time.time_ns()//1000000)
    files.update({'previous-manifest.json':P.PROFILE.manifest_bytes(old),'manifest.json':P.PROFILE.manifest_bytes(manifest)})
    for name,raw in files.items():write(bundle/name,raw)
    spec=canonical(dict(schema=1,files={name:sha(raw) for name,raw in files.items()}));write(bundle/'adoption.json',spec)
    reviewed=S.H.LAUNCH.fields(files['runtime.profile'])
    for role,path in [('gateway',PRIVATE/'session.json'),('signer',PRIVATE/'api-key.json')]:S.material(role,path,reviewed)
    result=dict(prepared=True,account_adoption_sha256=sha(spec),runtime_profile_sha256=manifest['runtime_profile_sha256'],account_review_expires_ms=end,base_atoms=cut['base_atoms'],quote_atoms=cut['quote_atoms'],opening_sol_mark_usdc_atoms=quote['quoted_output_atoms'],mark_is_quote_not_settlement=True,profile_signer_is_wallet=True,credentials_locally_matched=True,ledger_initialized=False,services_started=False,live_admitted=False)
    write(args.output/'summary.json',canonical(result));print(json.dumps(result,indent=2))

if __name__=='__main__':main()
