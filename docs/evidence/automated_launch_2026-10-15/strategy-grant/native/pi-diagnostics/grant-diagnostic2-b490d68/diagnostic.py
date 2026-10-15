import json,shlex,subprocess,hashlib,os
from pathlib import Path
source=Path('/source');build=Path('/work/build');out=Path('/evidence')
assert subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),'rev-parse','HEAD'],text=True).strip()=='b490d6843b53d114a49678304089541bfe192c7a'
assert not subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),'status','--porcelain'])
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','v2-swap-continuous-drill'],text=True).splitlines()
objects=[]
for relative,label in [('src/v2_swap_gateway_runtime.cpp','runtime'),('tests/v2_swap_continuous_drill.cpp','drill')]:
    original=(source/relative).read_text();text=original
    if label=='runtime':
        text='#include <cstdio>\n'+text
        needle='stop(SwapGatewayRuntimeStatus::failed,SwapGatewayRuntimeCause::state);'
        text=text.replace(needle,'(std::fprintf(stderr,"DIAG failed/state line=%d\\n",__LINE__), '+needle[:-1]+');')
        needle='const auto outcome=link_.pump(now);'
        text=text.replace(needle,needle+'if(outcome.status!=LinkStatus::pending && outcome.status!=LinkStatus::idle && outcome.status!=LinkStatus::done)std::fprintf(stderr,"DIAG link request=%u status=%u refusal=%u index=%lld now=%llu\\n",unsigned(outcome.request),unsigned(outcome.status),unsigned(outcome.refusal_reason),(long long)outcome.index,(unsigned long long)now);')
    else:
        text=text.replace('" cause="+std::to_string(int(runtime.cause()))','" cause="+std::to_string(int(runtime.cause()))+" pending="+std::to_string(int(link.state().spot_swap.pending.phase))+" effect="+std::to_string(link.state().spot_swap.last_effect)')
    file=out/(label+'.cpp');file.write_text(text)
    obj=out/(label+'.o');objects.append(obj)
    matches=[line for line in commands if ' -c '+str(source/relative) in line]
    assert len(matches)==1,(relative,len(matches))
    cmd=shlex.split(matches[0]);cmd=cmd[cmd.index('/usr/bin/c++'):]
    cmd[cmd.index('-o')+1]=str(obj);cmd[cmd.index('-c')+1]=str(file)
    if '-MF' in cmd:cmd[cmd.index('-MF')+1]=str(out/(label+'.d'))
    cmd+=['-I/source/tests']
    subprocess.run(cmd,cwd=build,check=True)
link=[line for line in commands if ' -o v2-swap-continuous-drill ' in line]
assert len(link)==1
cmd=shlex.split(link[0]);cmd=cmd[cmd.index('/usr/bin/c++'):];cmd=cmd[:cmd.index('&&')] if '&&' in cmd else cmd
cmd=[str(objects[1]) if value.endswith('/v2_swap_continuous_drill.cpp.o') else value for value in cmd]
cmd.insert(1,str(objects[0]));binary=out/'diagnostic';cmd[cmd.index('-o')+1]=str(binary)
subprocess.run(cmd,cwd=build,check=True)
(out/'build.json').write_text(json.dumps({'diagnostic_only':True,'production_source_modified':False,'commit':'b490d6843b53d114a49678304089541bfe192c7a','binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()},indent=2)+'\n')
p=subprocess.run([str(binary),'--case','0',str(out/'case')],timeout=100)
(out/'result.json').write_text(json.dumps({'diagnostic_only':True,'exit':p.returncode})+'\n')
raise SystemExit(p.returncode)
