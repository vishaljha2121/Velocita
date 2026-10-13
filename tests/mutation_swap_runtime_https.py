import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

SOURCE='src/v2_swap_runtime_https.cpp'
SOURCES=('src/v2_swap_runtime_https.cpp', 'src/v2_swap_runtime_config.cpp', 'src/v2_swap_signer_session.cpp', 'src/v2_swap_signer_service.cpp', 'src/v2_swap_signer_protocol.cpp', 'src/v2_swap_signing_stamp.cpp', 'src/v2_swap_submission_store.cpp', 'src/v2_gateway_link.cpp', 'src/v2_auth_envelope.cpp', 'src/v2_node_protocol.cpp', 'src/v2_session_fence.cpp', 'src/v2_swap_https.cpp', 'src/v2_swap_order_status.cpp', 'src/v2_swap_prepare_store.cpp', 'src/v2_swap_rest_codec.cpp', 'src/v2_truemarkets_rest.cpp', 'src/v2_truemarkets_wire.cpp')
TESTS={"runtime":"tests/v2_swap_runtime_https_probe.cpp"}
MUTATIONS=[('one_shot', 'if(used_)return;', '', 'test_exact_execute_once'), ('identity', ' || i!=expected_', ' || (i!=expected_ && false)', 'test_execution_bounds_before_io'), ('body_bytes', '!std::equal(body.begin(),body.end(),reinterpret_cast<const std::byte*>(request_.body.data()))', '(!std::equal(body.begin(),body.end(),reinterpret_cast<const std::byte*>(request_.body.data())) && false)', 'test_execution_bounds_before_io'), ('body_hash', 'sha256_digest(std::as_bytes(std::span(r.body.data(),r.body_size)))==i.execution_request', '(sha256_digest(std::as_bytes(std::span(r.body.data(),r.body_size)))==i.execution_request || !i.execution_request.is_zero())', 'test_execution_bounds_before_io'), ('route', 'return r.path_view()==path && ', 'return (r.path_view()==path || !path.empty()) && ', 'test_execution_bounds_before_io'), ('expiry', 'if(token.empty())return;', '', 'test_execution_bounds_before_io'), ('rpc_shape', ' || !canonical_rpc(body)', ' || (!canonical_rpc(body) && false)', 'test_rpc_rejects_other_methods_and_unbounded_timeout'), ('rpc_timeout', ' || timeout>maximum_timeout_', ' || (timeout>maximum_timeout_ && false)', 'test_rpc_rejects_other_methods_and_unbounded_timeout'), ('rpc_keyless', 'client.exchange({SwapHttpsMethod::post,"/",{},body})', 'client.exchange({SwapHttpsMethod::post,"/","fixture-token",body})', 'test_keyless_finalized_rpc'), ('response_capacity', 'response.body_size>result.body.size()', 'response.body_size>=result.body.size()', 'test_rpc_response_bound')]
def main():
    parser=argparse.ArgumentParser()
    for flag in ('root-build','raft-include','openssl-root','output-dir'):parser.add_argument('--'+flag,required=True,type=Path)
    a=parser.parse_args();root=Path(__file__).resolve().parents[1];out=a.output_dir.resolve()
    if out.exists():raise RuntimeError('fresh output directory required')
    out.mkdir(parents=True);copy=out/'source';pending=list(SOURCES)+list(TESTS.values())+['tests/mutation_swap_runtime_https.py','tests/test_swap_runtime_https.py','tests/test_swap_order_https.py','tests/test_swap_https.py'];snapshots={}
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
    fixture=['-DVELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY','-DVELOCITA_SWAP_HTTPS_TEST_ONLY','-DVELOCITA_SWAP_ORDER_STATUS_TEST_ONLY','-DVELOCITA_SWAP_PREPARE_STORE_TEST_ONLY'];tail=libs+['-L'+str(a.openssl_root.resolve()/'lib'),'-lcrypto','-lcurl','-pthread']
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
    def suite(binary,name,case=None):
        command=[sys.executable,'-m','unittest','-v','test_swap_runtime_https'+('.RuntimeHttpsTests.'+case if case else '')]
        child={**env,'PYTHONPATH':str(copy/'tests'),'VELOCITA_SWAP_RUNTIME_HTTPS_PROBE':str(binary)}
        result=subprocess.run(command,capture_output=True,text=True,env=child,timeout=60)
        (out/(name+'.log')).write_text(result.stdout+result.stderr);return result
    binary,built=link('runtime','baseline')
    if built.returncode:raise RuntimeError(built.stderr)
    result=suite(binary,'baseline.run');report['baseline']={'exit':result.returncode,'stderr':result.stderr};save()
    if result.returncode or 'Ran 8 tests' not in result.stderr or not result.stderr.endswith('OK\n'):raise RuntimeError('baseline failed '+result.stdout+result.stderr)
    for name,old,new,case in MUTATIONS:
        text=snapshots[SOURCE].decode()
        if text.count(old)!=1:raise RuntimeError('nonunique mutant '+name)
        mutated=out/(name+'.cpp');mutated.write_text(text.replace(old,new));obj=out/(name+'.o')
        built=run(flags+fixture+['-c',str(mutated),'-o',str(obj)],name+'.compile')
        if built.returncode:raise RuntimeError('mutation compile is not a kill: '+name+' '+built.stderr)
        binary,built=link('runtime',name,(SOURCE,str(obj)))
        if built.returncode:raise RuntimeError('mutation link is not a kill: '+name+' '+built.stderr)
        result=suite(binary,name+'.run',case)
        killed=result.returncode==1 and ('FAIL: '+case) in result.stderr and 'AssertionError:' in result.stderr and 'ERROR:' not in result.stderr and 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr
        report['mutations'].append({'name':name,'assertion':case,'killed':killed,'exit':result.returncode});save()
        if not killed:raise RuntimeError('not named assertion kill '+name+' '+result.stdout+result.stderr)
        print('KILL '+name,flush=True)
    report['source_changed']=any((root/p).read_bytes()!=v for p,v in snapshots.items());save()
    if report['source_changed']:raise RuntimeError('source changed during run')
    print('PASS baseline eight TLS groups and '+str(len(MUTATIONS))+' named mutants',flush=True)
if __name__=='__main__':main()
