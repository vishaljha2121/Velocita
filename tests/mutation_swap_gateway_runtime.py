"""Snapshot linked Release objects; instrument only changed coordinator for named mutants."""
import argparse, hashlib, json, os, pathlib, re, shlex, shutil, subprocess

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1];build=a.build.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    src=root/'src/v2_swap_gateway_runtime.cpp';original=src.read_text();shutil.copytree(root/'include',out/'include')
    link=shlex.split((build/'CMakeFiles/v2-swap-gateway-runtime-tests.dir/link.txt').read_text())
    report={'instrumentation':'coordinator ASan/UBSan; frozen other objects and archives Release','files':{},'mutants':[]}
    for i,arg in enumerate(link):
        if arg.endswith(('.o','.a','.dylib')):
            path=pathlib.Path(arg);path=path if path.is_absolute() else build/path
            target=out/('dependency-'+str(i)+path.suffix);shutil.copyfile(path,target);link[i]=str(target);report['files'][str(path)]=hashlib.sha256(target.read_bytes()).hexdigest()
            if arg.endswith('/v2_swap_gateway_runtime.cpp.o'):coordinator=i
    flags=(build/'CMakeFiles/v2-swap-gateway-runtime-tests.dir/flags.make').read_text()
    definitions=shlex.split(re.search(r'^CXX_DEFINES = (.*)$',flags,re.M)[1]);includes=shlex.split(re.search(r'^CXX_INCLUDES = (.*)$',flags,re.M)[1])
    includes=[('-I'+str(out/'include')) if x=='-I'+str(root/'include') else x for x in includes]
    compile=[link[0],'-std=c++20','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',*definitions,*includes]
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
    def run(cmd,name):
        r=subprocess.run(cmd,text=True,capture_output=True,env=env,timeout=120);(out/(name+'.log')).write_text(r.stdout+r.stderr);return r
    def build_run(name,body,case=None):
        source=out/(name+'.cpp');source.write_text(body);obj=out/(name+'.o');r=run(compile+['-c',str(source),'-o',str(obj)],name+'.compile')
        if r.returncode:raise RuntimeError(r.stderr)
        command=link.copy();command[coordinator]=str(obj);binary=out/name;command[command.index('-o')+1]=str(binary);command.insert(1,'-fsanitize=address,undefined')
        r=run(command,name+'.link')
        if r.returncode:raise RuntimeError(r.stderr)
        return run([str(binary)]+([] if case is None else [str(case)]),name+'.run')
    r=build_run('baseline',original);report['baseline']={'exit':r.returncode,'output':r.stdout+r.stderr}
    if r.returncode or r.stdout.count('PASS runtime mode')!=7:raise RuntimeError(r.stdout+r.stderr)
    mutations=[
        ('recovery_only','if(config_.recovery_only && submissions_.latest().phase!=SwapSubmissionPhase::execute_intent &&',
         'if(false && submissions_.latest().phase!=SwapSubmissionPhase::execute_intent &&',5,
         'recovery-only reserved intent cannot prepare sign or execute'),
        ('oversized_request','if(!request.body_size || request.body_size>request.body.size())','if(!request.body_size)',4,'oversized signed request refused before span construction'),
        ('sent_reconciliation','pending.phase==swap::Phase::attempted && submissions_.latest().phase!=SwapSubmissionPhase::execute_intent','pending.phase==swap::Phase::attempted',1,'restart of sent request only reconciles without signing'),
        ('settlement_commit','command(settled,now,event);return;','stop(SwapGatewayRuntimeStatus::complete,SwapGatewayRuntimeCause::none);return;',0,'joined lifecycle completes once')]
    for name,before,after,case,assertion in mutations:
        if original.count(before)!=1:raise RuntimeError('nonunique mutation '+name)
        body=original.replace(before,after)
        if name=='recovery_only':body=body.replace('if(config_.recovery_only && pending.phase!=swap::Phase::none', 'if(false && pending.phase!=swap::Phase::none')
        r=build_run(name,body,case);named=r.returncode==1 and ('FAIL '+assertion) in r.stderr and not re.search('AddressSanitizer|runtime error:',r.stderr)
        report['mutants'].append({'name':name,'exit':r.returncode,'assertion':assertion,'named_kill':named})
    report['source_sha256']=hashlib.sha256(original.encode()).hexdigest();report['source_unchanged']=src.read_text()==original
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    if not report['source_unchanged'] or not all(x['named_kill'] for x in report['mutants']):raise RuntimeError('mutation or source check failed')
    print('PASS 4/4 named coordinator mutations')
if __name__=='__main__':main()
