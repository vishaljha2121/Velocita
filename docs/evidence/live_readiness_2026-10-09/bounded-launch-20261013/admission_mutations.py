import argparse
import json
from pathlib import Path
import shlex
import subprocess

p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[4]
source=(root/'src/v2_swap_runtime_production.cpp').read_text()
target=a.build/'CMakeFiles/v2-swap-runtime-production-tests.dir'
flags={x.split(' = ',1)[0]:shlex.split(x.split(' = ',1)[1]) for x in (target/'flags.make').read_text().splitlines() if ' = ' in x}
command=shlex.split((target/'link.txt').read_text())
cases=[('baseline',None,None,None),
('unconditional','return validate_swap_gateway_startup(profile.gateway,now)==SwapRuntimeCode::valid &&','return true || validate_swap_gateway_startup(profile.gateway,now)==SwapRuntimeCode::valid &&','legacy_profile_cannot_admit_live_runtime'),
('repayment',"b.max_buy_output_deduction_lamports<=2'039'280",'true','larger_repayment_profile_refused'),
('identity','swap_runtime_identity_digest(b)==swap_runtime_identity_digest(profile.signer.binding)','true','different_process_policy_refused'),
('expiry','validate_swap_gateway_startup(profile.gateway,now)==SwapRuntimeCode::valid','validate_swap_gateway_startup(profile.gateway,1)==SwapRuntimeCode::valid','expired_bearer_profile_refused')]
results=[]
for name,old,new,assertion in cases:
    text=source
    if old:
        assert text.count(old)==1
        text=text.replace(old,new)
    src=a.output/(name+'.cpp');obj=a.output/(name+'.o');binary=a.output/name;src.write_text(text)
    subprocess.run([command[0],*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],'-c',str(src),'-o',str(obj)],cwd=a.build,check=True,capture_output=True)
    link=command.copy();link.insert(1,str(obj));link[link.index('-o')+1]=str(binary)
    subprocess.run(link,cwd=a.build,check=True,capture_output=True)
    run=subprocess.run([str(binary)],capture_output=True,text=True)
    ok=run.returncode==0 if assertion is None else run.returncode==1 and assertion in run.stderr
    results.append(dict(name=name,assertion=assertion,passed=ok,exit=run.returncode,stdout=run.stdout,stderr=run.stderr))
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(dict(passed=all(r['passed'] for r in results),cases=len(results))))
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
