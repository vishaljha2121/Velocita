#!/usr/bin/env python3
"""Compile one synthetic helper against read-only qualified 86f4408 objects."""
import hashlib,json,os,subprocess
from pathlib import Path
base=Path.home()/'.cache/velocita-build/swap-services-20261013-r4'
qual=Path.home()/'.cache/velocita-build/integrated-a890307-20261013/qualification'
image='076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f'
assert subprocess.check_output(['git','-C',str(qual.parent/'source'),'rev-parse','HEAD'],text=True).strip()=='86f440806c6adc6f4c1dc9dc7a271e3d95293d76'
objects=['v2_swap_signer_protocol','v2_swap_signer_service','v2_swap_signer_session','v2_swap_submission_store','v2_swap_signing_stamp']
libs=['libvelocita_v2_node.a','libvelocita_v2_replication.a','libvelocita_v2_kernel.a','libvelocita_v2_evaluator.a','libvelocita_v2_contracts.a','libvelocita_aeron.a','libvelocita_core.a','libwillemt_raft.a','_deps/aeron-build/lib/libaeron_driver_static.a']
inputs=['CMakeFiles/v2-swap-signer-service-tests.dir/src/'+n+'.cpp.o' for n in objects]+libs
pins={name:hashlib.sha256((qual/'build'/name).read_bytes()).hexdigest() for name in inputs}
args=['/usr/bin/c++','-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-DVELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1','-DVELOCITA_TARGET_CACHE_LINE_SIZE=64','-I/source/include','-I/source/tests','-I/work/deps/willemt-raft/include','-I/work/deps/aeron/aeron-client/src/main/cpp_wrapper','-I/work/deps/aeron/aeron-client/src/main/c','-I/work/deps/aeron/aeron-driver/src/main/c','/probe/communication_probe.cpp',*['/work/build/'+n for n in inputs],'-lcrypto','-ldl','-lbsd','-luuid','-lm','-pthread','-o','/probe/communication-probe']
command=['podman','run','--rm','--pull=never','--network=none','--cpuset-cpus=3','--memory=1g','--pids-limit=64','--userns=keep-id','--user',str(os.getuid())+':'+str(os.getgid()),'-v',str(qual.parent/'source')+':/source:ro','-v',str(qual)+':/work:ro','-v',str(base)+':/probe:rw','--entrypoint','/usr/bin/timeout',image,'120',*args]
(base/'build-r2-command.json').write_text(json.dumps(command,indent=2)+'\n')
with (base/'build-r2.log').open('x') as log: result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=150)
report={'source_commit':'86f440806c6adc6f4c1dc9dc7a271e3d95293d76','runtime_image':image,'helper_source_sha256':hashlib.sha256((base/'communication_probe.cpp').read_bytes()).hexdigest(),'qualified_link_inputs':pins,'returncode':result.returncode,'production_binary_changed':False,'test_harness_only':True}
if result.returncode==0: report['binary_sha256']=hashlib.sha256((base/'communication-probe').read_bytes()).hexdigest()
(base/'build-r2-result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='qualified_link_inputs'}))
raise SystemExit(result.returncode)
