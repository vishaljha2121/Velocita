import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

MUTATIONS = (
    ('base58_overflow', 'if(carry)return {};', '', 'signature', 'noncanonical zero invalid digit overflow or wrong length signature refuses'),
    ('base58_canonical', 'if(!significant || leading+significant!=out.size())return {};', 'if(!significant || leading+significant==0)return {};', 'signature', 'noncanonical zero invalid digit overflow or wrong length signature refuses'),
    ('read_once', 'if(result_.status!=SwapOrderReadStatus::not_read)return result_;', '', 'exact', 'same reader never automatically retries'),
    ('initial_session', '!identity_valid(config_.http_identity) || http_.identity()!=config_.http_identity ||', '!identity_valid(config_.http_identity) ||', 'fences', 'invalid initial scope refuses before GET'),
    ('prepare_binding', ' || prepare_.latest().binding!=config_.binding', '', 'prepare', 'full preparation allocation binding is required before GET'),
    ('prepare_effect', 'prepared.operation.effect!=i.effect || ', '', 'prepare', 'retained prepare effect payload handle and causal index must match execute journal'),
    ('prepare_payload', 'prepared.prepared.payload_digest!=i.payload ||', 'false ||', 'prepare', 'retained prepare effect payload handle and causal index must match execute journal'),
    ('strict_causal_index', 'captured.committed_index<=prepared.committed_index ||', 'captured.committed_index<prepared.committed_index ||', 'prepare', 'retained prepare effect payload handle and causal index must match execute journal'),
    ('prepare_handle', 'prepared.prepared.handle.view()!=std::string_view(reinterpret_cast<const char*>(i.handle.data()),i.handle_size)', 'false', 'prepare', 'retained prepare effect payload handle and causal index must match execute journal'),
    ('get_deadline', 'finish>=start+config_.timeout_ms || ', '', 'fences', 'session binding journal or exact timeout fence refuses stale HTTP evidence'),
    ('clock_backwards', 'finish<start || ', '', 'fences', 'session binding journal or exact timeout fence refuses stale HTTP evidence'),
    ('session_during_get', 'finish>=start+config_.timeout_ms || http_.identity()!=config_.http_identity ||', 'finish>=start+config_.timeout_ms ||', 'fences', 'session binding journal or exact timeout fence refuses stale HTTP evidence'),
    ('submission_changed', 'store_.latest().checksum!=captured.checksum ||\n        store_.latest().ordinal!=captured.ordinal || ', '', 'fences', 'session binding journal or exact timeout fence refuses stale HTTP evidence'),
    ('prepare_changed', 'prepare_.latest().ordinal!=prepared.ordinal || prepare_.latest().checksum!=prepared.checksum', 'false', 'prepare_changes', 'changed or failed prepare journal cannot publish old order evidence'),
    ('prepare_failed', 'store_.latest().ordinal!=captured.ordinal || prepare_.status()!=SwapPrepareStoreCode::none ||', 'store_.latest().ordinal!=captured.ordinal ||', 'prepare_changes', 'changed or failed prepare journal cannot publish old order evidence'),
    ('http_status', 'response.http_status!=200 || ', '', 'refusals', 'wrong request identity HTTP transport or transaction cannot publish evidence'),
    ('http_exposure', 'response.exposure!=transport::SwapHttpsExposure::response_received ||', 'false ||', 'refusals', 'wrong request identity HTTP transport or transaction cannot publish evidence'),
    ('http_transport', 'response.status!=transport::SwapHttpsStatus::complete || ', '', 'refusals', 'wrong request identity HTTP transport or transaction cannot publish evidence'),
    ('body_bound', ' || response.body_size>kMaxTmResponseBytes', '', 'refusals', 'wrong request identity HTTP transport or transaction cannot publish evidence'),
    ('known_signature', 'if(captured.transaction!=SwapSubmissionTransaction{} && captured.transaction!=*signature)return result_;', '', 'conflict', 'different authenticated transaction cannot overwrite retained signature'),
    ('durable_signature', 'if(store_.observe_transaction(i,*signature)!=SwapSubmissionCode::none)', 'if((static_cast<void>(store_.observe_transaction(i,*signature)),false))', 'conflict', 'failed durable signature write publishes no usable status evidence'),
)
SOURCES = ('src/v2_swap_order_status.cpp', 'src/v2_swap_prepare_store.cpp', 'src/v2_swap_submission_store.cpp',
           'src/v2_swap_rest_codec.cpp', 'src/v2_swap_signing_stamp.cpp', 'src/v2_truemarkets_rest.cpp',
           'src/v2_truemarkets_wire.cpp', 'src/v2_digest.cpp', 'tests/v2_swap_order_status_tests.cpp')

def closure(root):
    pending=list(SOURCES)+['tests/mutation_swap_order_status.py']; result={}
    while pending:
        rel=pending.pop()
        if rel in result:continue
        data=(root/rel).read_bytes();result[rel]=data
        for name in re.findall(r'^\s*#include\s+"([^"]+)"',data.decode(),re.MULTILINE):
            path='include/'+name
            if not (root/path).is_file():raise RuntimeError('unresolved local include '+name)
            pending.append(path)
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output-dir',required=True,type=Path);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];out=args.output_dir.resolve()
    if out.exists():raise RuntimeError('output directory must be new')
    out.mkdir(parents=True);snapshots=closure(root);export=out/'source'
    for rel,data in snapshots.items():
        dest=export/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    flags=['c++','-std=c++20','-Wall','-Wextra','-Werror','-g','-O1','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
           '-DVELOCITA_SWAP_ORDER_STATUS_TEST_ONLY=1','-DVELOCITA_SWAP_PREPARE_STORE_TEST_ONLY=1',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(export/'include')]
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
    report={'source_sha256':{rel:hashlib.sha256(data).hexdigest() for rel,data in snapshots.items()},'flags':flags,'baseline_passed':False,'results':[]}
    def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(cmd,name):
        result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=90)
        (out/(name+'.log')).write_text(result.stdout+result.stderr);return result
    objects=[]
    for n,rel in enumerate(SOURCES[1:]):
        obj=out/f'dependency-{n}.o';result=run(flags+['-c',str(export/rel),'-o',str(obj)],f'dependency-{n}')
        if result.returncode:raise RuntimeError('dependency compile failed: '+result.stderr)
        objects.append(str(obj))
    def build(path,name):
        binary=out/name;compiled=run(flags+[str(path),*objects,'-o',str(binary)],name+'.compile');return binary,compiled
    text=snapshots[SOURCES[0]].decode();binary,built=build(export/SOURCES[0],'baseline')
    if built.returncode:raise RuntimeError('baseline compile failed: '+built.stderr)
    baseline=run([str(binary)],'baseline.run')
    expected={f'PASS {case}' for case in ('signature','exact','refusals','fences','conflict','prepare','prepare_changes')}
    if baseline.returncode or set(baseline.stdout.splitlines())!=expected or baseline.stderr:raise RuntimeError('baseline failed: '+baseline.stdout+baseline.stderr)
    report['baseline_passed']=True;save()
    for name,old,new,case,assertion in MUTATIONS:
        if text.count(old)!=1:raise RuntimeError('mutation requires one source match: '+name)
        path=out/(name+'.cpp');path.write_text(text.replace(old,new,1));binary,built=build(path,name)
        result=None if built.returncode else run([str(binary),case],name+'.run')
        killed=result is not None and result.returncode==1 and f'FAIL {case}: {assertion}\n' in result.stdout and not result.stderr
        report['results'].append({'name':name,'case':case,'assertion':assertion,'killed':killed,'compile_exit':built.returncode,
                                  'exit':None if result is None else result.returncode,'stdout':'' if result is None else result.stdout,'stderr':built.stderr if result is None else result.stderr});save()
        if not killed:raise RuntimeError('not a named kill: '+json.dumps(report['results'][-1]))
    report['source_changed_during_run']=any((root/rel).read_bytes()!=data for rel,data in snapshots.items());save()
    print(f'Seven sanitizer groups passed; {len(MUTATIONS)}/{len(MUTATIONS)} named mutation kills; source drift={report["source_changed_during_run"]}')
    return int(report['source_changed_during_run'])

if __name__=='__main__':raise SystemExit(main())
