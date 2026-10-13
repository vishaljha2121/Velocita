import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

SESSION='src/v2_swap_signer_session.cpp'
SERVICE='src/v2_swap_signer_service.cpp'
PROTOCOL='src/v2_swap_signer_protocol.cpp'
SOURCES=(SESSION,SERVICE,PROTOCOL,'src/v2_swap_signing_stamp.cpp','src/v2_swap_submission_store.cpp',
         'src/v2_gateway_link.cpp','src/v2_auth_envelope.cpp','src/v2_node_protocol.cpp','src/v2_session_fence.cpp')
TESTS={name:f'tests/v2_swap_signer_{name}_tests.cpp' for name in ('session','service','protocol')}
MUTATIONS=[
(SESSION,'review_expiry_start','(config_.account_review_expires_at_ms && last_event_>=config_.account_review_expires_at_ms) || ','','session','clock_and_storage','expired review rejects signing start'),
(SESSION,'review_expiry_authority','(config_.account_review_expires_at_ms && event>=config_.account_review_expires_at_ms))return false;','false)return false;','session','clock_and_storage','review expires during authority read before any signature'),
(SESSION,'inclusive_principal',"text==inclusive_fixture && intent.quote_input.value()==998'000 && intent.max_quote_debit.value()==1'000'000",'text==inclusive_fixture','session','payload_and_economics','inclusive_fixture_cannot_sign_legacy_principal'),
(SESSION,'allocation_domain','std::copy(c.binding.custody.bytes.begin(),c.binding.custody.bytes.end(),bytes.begin()+161);','','session','key_and_domain','changed allocation cannot retrieve old signer result'),
(SESSION,'sha384','EVP_DigestSignInit(context,nullptr,EVP_sha256(),nullptr,','EVP_DigestSignInit(context,nullptr,EVP_sha384(),nullptr,','session','exact_signature','independent OpenSSL verifies exact original bytes'),
(SESSION,'domain','outcome.reading.authority_domain!=config_.authority_domain ||','','session','state_fences','bad domain index authentication or pending digest refuses without crypto fence'),
(SESSION,'state_index','outcome.reading.applied_index<minimum_index_ ||','','session','state_fences','bad domain index authentication or pending digest refuses without crypto fence'),
(SESSION,'pending_payload','pending.payload!=identity_.payload ||','','session','state_fences','bad domain index authentication or pending digest refuses without crypto fence'),
(SESSION,'pending_handle','pending.handle!=original_handle_digest_ ||','','session','state_fences','bad domain index authentication or pending digest refuses without crypto fence'),
(SESSION,'effect','pending.intent.effect!=identity_.effect ||','','session','remaining_admission_fences','wrong phase time halt allocation or effect refuses signing'),
(SESSION,'phase','pending.phase!=swap::Phase::prepared ||','','session','remaining_admission_fences','wrong phase time halt allocation or effect refuses signing'),
(SESSION,'fresh_authority','proof.barrier_index<=state_read_index_ ||','','session','authority_fences','missing unauthenticated or nonfresh authority cannot sign'),
(SESSION,'post_fsync_clock','status_=SwapSignerStatus::fenced;\n    if(!clock_valid(pending.intent,last_event_))return refuse(SwapSignerStatus::fenced);','status_=SwapSignerStatus::fenced;','session','clock_and_storage','deadline rechecked after durable fence before signing'),
(SESSION,'crypto_before_fence','if(store_.begin(identity_,committed_index)!=SwapSubmissionCode::none)','(void)key_.sign(std::span(payload_).first(size_),der_,der_size_);\n    if(store_.begin(identity_,committed_index)!=SwapSubmissionCode::none)','session','clock_and_storage','fsync failure never returns signature'),
(SESSION,'result_durability',' || store_.observe_transaction(identity_,raw)!=SwapSubmissionCode::none','','session','durable_result','stamp is never published before signature result is durable'),
(SESSION,'recover_verify',' || !key_.verify(payload,std::span(der_).first(der_size_))','','session','durable_result','retrieved raw signature must independently verify original payload'),
(SESSION,'replay_identity',' || expected!=record.identity','','session','lost_response_retrieval','changed payload sequence index expiry or expired result cannot be retrieved'),
(SESSION,'durable_clock_floor','if(event<floor || ','if(','session','lost_response_retrieval','changed payload sequence index expiry or expired result cannot be retrieved'),
(SESSION,'key_pin',' || !std::equal(pub.begin(),pub.end(),expected.begin())',' || expected.size()!=33','session','key_and_domain','wrong pinned public key refuses'),
(SESSION,'key_mode','(info.st_mode&07777)!=0600 ||','','session','key_and_domain','publicly readable private key refuses'),
(SESSION,'key_version','put64(std::span(bytes).subspan(24),c.key_version);','put64(std::span(bytes).subspan(24),1);','session','key_and_domain','key version cannot reuse old signing journal'),
(SESSION,'fixture_economics',"intent.min_base_receive.value()!=9'000'000 ||",'','session','payload_and_economics','synthetic admission pins exact economics not caller boolean'),
(SERVICE,'server_peer','if(!uid || *uid!=client_uid_)','if(!uid)','service','peer','wrong peer UID obtains no stamp'),
(SERVICE,'client_peer','if(!uid || *uid!=signer_uid_)','if(!uid || (*uid!=signer_uid_ && false))','service','peer','client verifies actual signer peer UID'),
(SERVICE,'client_key_pin','result.public_key!=public_key_ || ','(result.public_key!=public_key_ && false) || ','service','peer','client pins signer public key independently of peer UID'),
(SERVICE,'private_directory','((st.st_mode&07777)!=0700 && (st.st_mode&07777)!=0750) ||','false ||','service','peer','writable public directory cannot host signer socket'),
(SERVICE,'dedicated_identity','expected_uid==gateway_uid || ','(expected_uid==gateway_uid && false) || ','service','process','isolated signer process passes its assertions'),
(PROTOCOL,'reserved_request',' || !zeros(b.subspan(9,7)) || !zeros(b.subspan(52,4))','','protocol','malformed','malformed request clears reusable output'),
(PROTOCOL,'request_payload_byte','std::copy_n(r.payload.begin(),r.payload_size,out.begin()+56+r.handle_size);','std::copy_n(r.payload.begin(),r.payload_size-1,out.begin()+56+r.handle_size);','protocol','request','request preserves exact original newline bytes'),
(PROTOCOL,'request_handle','if(!valid(r))return false;out=r;return true;','out=r;return true;','protocol','malformed','malformed request clears reusable output'),
(PROTOCOL,'reply_capacity','if(out.size()<n)return 0;out=out.first(n);std::fill(out.begin(),out.end(),std::byte{});\n    constexpr std::string_view magic="VSR2";','if(out.size()<=n)return 0;out=out.first(n);std::fill(out.begin(),out.end(),std::byte{});\n    constexpr std::string_view magic="VSR2";','protocol','reply','maximum DER signature fits bounded reply'),
]

def main():
    parser=argparse.ArgumentParser()
    for flag in ('root-build','raft-include','openssl-root','output-dir'):parser.add_argument('--'+flag,required=True,type=Path)
    a=parser.parse_args();root=Path(__file__).resolve().parents[1];out=a.output_dir.resolve()
    if out.exists():raise RuntimeError('fresh output directory required')
    out.mkdir(parents=True);copy=out/'source';pending=list(SOURCES)+list(TESTS.values())+['tests/mutation_swap_signer_boundary.py','tests/mutation_swap_signer_session.py'];snapshots={}
    while pending:
        rel=pending.pop()
        if rel in snapshots:continue
        data=(root/rel).read_bytes();snapshots[rel]=data;dest=copy/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        for name in re.findall(r'^\s*#include\s+"([^"]+)"',data.decode(),re.MULTILINE):
            candidates=[Path(rel).parent/name,Path('include')/name]
            found=next((p for p in candidates if(root/p).is_file()),None)
            if found is None:
                if (a.raft_include.resolve()/name).is_file():continue
                raise RuntimeError('unresolved quoted include '+name)
            pending.append(str(found))
    external=out/'raft';external.mkdir()
    external_hash={}
    for p in a.raft_include.resolve().rglob('*.h'):
        rel=p.relative_to(a.raft_include.resolve());dest=external/rel;dest.parent.mkdir(parents=True,exist_ok=True);data=p.read_bytes();dest.write_bytes(data);external_hash[str(rel)]=hashlib.sha256(data).hexdigest()
    libs=[];libhash={};libdir=out/'lib';libdir.mkdir()
    for name in ('velocita_v2_node','velocita_v2_replication','velocita_v2_kernel','velocita_v2_evaluator','velocita_core','willemt_raft','velocita_v2_contracts'):
        p=a.root_build.resolve()/('lib'+name+'.a');data=p.read_bytes();dest=libdir/p.name;dest.write_bytes(data);libs.append(str(dest));libhash[str(p)]=hashlib.sha256(data).hexdigest()
    flags=['c++','-std=c++20','-Wall','-Wextra','-Werror','-g','-O1','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(copy/'include'),'-I'+str(copy/'tests'),'-I'+str(external),'-I'+str(a.openssl_root.resolve()/'include')]
    fixture=['-DVELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1'];tail=libs+['-L'+str(a.openssl_root.resolve()/'lib'),'-lcrypto','-pthread']
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
    report={'source_sha256':{s:hashlib.sha256(b).hexdigest() for s,b in snapshots.items()},'libraries':libhash,'raft_headers':external_hash,'baselines':{},'mutations':[]}
    def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(cmd,name):
        result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=120);(out/(name+'.log')).write_text(result.stdout+result.stderr);return result
    objects={}
    for n,rel in enumerate(SOURCES+tuple(TESTS.values())):
        obj=out/f'object-{n}.o';result=run(flags+fixture+['-c',str(copy/rel),'-o',str(obj)],f'object-{n}')
        if result.returncode:raise RuntimeError('compile failure: '+rel+' '+result.stderr)
        objects[rel]=str(obj)
    def link(kind,name,replacement=None):
        binary=out/name;parts=[replacement[1] if replacement and s==replacement[0] else objects[s] for s in SOURCES]
        result=run(flags+parts+[objects[TESTS[kind]]]+tail+['-o',str(binary)],name+'.link');return binary,result
    for kind,count in (('session',11),('service',4),('protocol',3)):
        binary,built=link(kind,kind)
        if built.returncode:raise RuntimeError(built.stderr)
        result=run([str(binary)],kind+'.baseline');report['baselines'][kind]={'exit':result.returncode,'stdout':result.stdout};save()
        if result.returncode or result.stderr or result.stdout.count('PASS ')!=count:raise RuntimeError('baseline failed '+kind+' '+result.stdout+result.stderr)
    prod=out/'production.o';result=run(flags+['-c',str(copy/SESSION),'-o',str(prod)],'production.compile')
    if result.returncode:raise RuntimeError(result.stderr)
    binary,built=link('session','production',(SESSION,str(prod)));result=run([str(binary),'production_gate'],'production.baseline')
    if built.returncode or result.returncode or result.stderr:raise RuntimeError('production gate failed')
    report['production_gate']=result.stdout;save()
    for rel,name,old,new,kind,case,assertion in MUTATIONS:
        text=snapshots[rel].decode()
        if text.count(old)!=1:raise RuntimeError('nonunique mutant '+name)
        changed=out/(name+'.cpp');changed.write_text(text.replace(old,new,1));obj=out/(name+'.o')
        compiled=run(flags+fixture+['-c',str(changed),'-o',str(obj)],name+'.compile')
        if compiled.returncode:raise RuntimeError('mutant compilation failure is not a kill '+name+' '+compiled.stderr)
        binary,built=link(kind,name,(rel,str(obj)))
        if built.returncode:raise RuntimeError('mutant link failure '+name+' '+built.stderr)
        result=run([str(binary),case],name+'.run');killed=result.returncode==1 and f'FAIL {case}: {assertion}\n' in result.stdout and not result.stderr
        report['mutations'].append({'name':name,'killed':killed,'stdout':result.stdout,'stderr':result.stderr,'exit':result.returncode});save()
        if not killed:raise RuntimeError('not designated assertion '+name+' '+result.stdout+result.stderr)
    report['source_changed']=any((root/p).read_bytes()!=b for p,b in snapshots.items());save()
    print(f'18 sanitizer groups + production refusal; {len(MUTATIONS)}/{len(MUTATIONS)} named mutation kills; source_changed={report["source_changed"]}')
    return int(report['source_changed'])

if __name__=='__main__':raise SystemExit(main())
