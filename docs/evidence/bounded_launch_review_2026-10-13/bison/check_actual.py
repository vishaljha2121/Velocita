"""Offline retained-envelope and document parity; no network, keys or startup.

Requires private prepared candidate/old-policy documents, never emits their raw
contents. Recompiles every project dependency from current source, no old libs.
"""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
SOURCES=['v2_swap_runtime_production.cpp','v2_swap_transaction_review.cpp','v2_swap_transaction_verifier.cpp','v2_jupiter_route_decode.cpp','v2_swap_signing_connection.cpp','v2_swap_runtime_config.cpp','v2_swap_signer_session.cpp','v2_digest.cpp','v2_solana_payload_view.cpp','v2_solana_resolved_accounts.cpp','v2_solana_instruction_decode.cpp','v2_swap_owner_accounting.cpp','v2_truemarkets_wire.cpp','v2_swap_account_setup.cpp']
def fields(raw):return dict(line.split('=',1) for line in raw.decode().splitlines())
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--old-policy',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 inputs=[ROOT/'src'/x for x in SOURCES]+list((ROOT/'include/velocita/v2').glob('*.hpp'))+[Path(__file__),Path(__file__).with_name('actual_quote_verifier.cpp'),Path(__file__).with_name('actual_document_parity.cpp')]
 before={str(x.relative_to(ROOT)):digest(x) for x in inputs}
 flags=['c++','-std=c++20','-O2','-ffunction-sections','-fdata-sections','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128','-DVELOCITA_SWAP_RUNTIME_WIRING=1','-I'+str(ROOT/'include'),'-I'+str(Path.home()/'.cache/velocita-deps/willemt-raft-cp076/include'),'-isystem','/opt/homebrew/opt/openssl@3/include']
 commands=[];objects=[]
 for src in SOURCES:
  obj=a.output/(src+'.o');cmd=flags+['-c',str(ROOT/'src'/src),'-o',str(obj)];commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True);(a.output/(src+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError('compile failed '+src)
  objects.append(str(obj))
 for name in ('actual_quote_verifier','actual_document_parity'):
  cmd=flags+['-Wl,-dead_strip',str(Path(__file__).with_name(name+'.cpp'))]+objects+['/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib','-o',str(a.output/name)];commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True);(a.output/(name+'.build.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError('link failed '+name)
 after={str(x.relative_to(ROOT)):digest(x) for x in inputs}
 if before!=after:raise RuntimeError('source changed during compile; reject evidence and rebuild')
 (a.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 def invoke(name,paths):
  r=subprocess.run([str(a.output/name)]+[str(x) for x in paths],capture_output=True,text=True)
  if r.returncode not in (0,2):raise RuntimeError(name+' diagnostic error: '+r.stderr)
  return r.returncode,fields(r.stdout.encode())
 profile=a.candidate/'runtime.profile';review=a.candidate/'transaction.review';envelope=a.candidate/'offline-envelope.json'
 code,verified=invoke('actual_quote_verifier',[profile,review,envelope]);assert code==0 and verified['verified_net_floor_lamports']=='6777951','actual Bison economic verification'
 assert verified['exact_floor_verification_code']=='12' and verified['floor_plus_one_verification_code']=='11','exact integer boundary'
 oldcode,old=invoke('actual_quote_verifier',[a.old_policy/'runtime.profile',a.old_policy/'transaction.review',envelope]);assert oldcode==2 and old['verified']=='0','old policy refuses different route'
 code,parity=invoke('actual_document_parity',[profile,review]);assert code==0
 sys.path.insert(0,str(ROOT/'deploy/v2/swap-runtime'));import review_transition as R;import idle_policy_renewal as N
 bindings=R.journal_bindings({'runtime.profile':profile.read_bytes(),'transaction.review':review.read_bytes()})
 assert bindings[3].hex()==parity['signing'],'signing parity';assert bindings[4].hex()==parity['protected'],'protected parity';assert N.runtime_identity(fields(profile.read_bytes()))==parity['runtime'],'runtime parity'
 report=dict(synthetic_intent=True,historical_clock=True,actual_raft_authority=False,live_admitted=False,signed=False,broadcast=False,network=False,project_libraries_reused=False,source_unchanged_during_build=True,actual_bison=verified,old_policy_refusal=old,native_python_parity=parity,source_sha256=before)
 (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
