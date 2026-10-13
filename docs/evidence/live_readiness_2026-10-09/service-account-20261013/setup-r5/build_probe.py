#!/usr/bin/env python3
"""Compile one read-only helper against read-only qualified 86f4408 objects."""
import hashlib,json,os,subprocess
from pathlib import Path
base=Path.home()/'.cache/velocita-build/swap-services-20261013-r5'
qual=Path.home()/'.cache/velocita-build/integrated-a890307-20261013/qualification'
image='076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f'
assert subprocess.check_output(['git','-C',str(qual.parent/'source'),'rev-parse','HEAD'],text=True).strip()=='86f440806c6adc6f4c1dc9dc7a271e3d95293d76'
objects=[]
libs=['libvelocita_v2_node_aeron.a','libvelocita_v2_node.a','libvelocita_v2_replication.a','libvelocita_v2_kernel.a','libvelocita_v2_evaluator.a','libvelocita_v2_contracts.a','libvelocita_aeron.a','libvelocita_core.a','libwillemt_raft.a','_deps/aeron-build/lib/libaeron_driver_static.a']
inputs=['CMakeFiles/v2-swap-signer-service-tests.dir/src/'+n+'.cpp.o' for n in objects]+libs
pins={name:hashlib.sha256((qual/'build'/name).read_bytes()).hexdigest() for name in inputs}
args=['/usr/bin/c++','-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-DVELOCITA_TARGET_CACHE_LINE_SIZE=64','-I/source/include','-I/source/tests','-isystem/work/deps/willemt-raft/include','-isystem/work/deps/aeron/aeron-client/src/main/cpp_wrapper','-isystem/work/deps/aeron/aeron-client/src/main/c','-isystem/work/deps/aeron/aeron-driver/src/main/c','/probe/v2_authenticated_read_probe.cpp',*['/work/build/'+n for n in inputs],'-lcrypto','-ldl','-lbsd','-luuid','-lm','-pthread','-o','/probe/authenticated-read-probe']
command=['podman','run','--rm','--pull=never','--network=none','--cpuset-cpus=3','--memory=1g','--pids-limit=64','--userns=keep-id','--user',str(os.getuid())+':'+str(os.getgid()),'-v',str(qual.parent/'source')+':/source:ro','-v',str(qual)+':/work:ro','-v',str(base)+':/probe:rw','--entrypoint','/usr/bin/timeout',image,'120',*args]
(base/'build-command.json').write_text(json.dumps(command,indent=2)+'\n')
with (base/'build.log').open('x') as log: result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=150)
report={'source_commit':'86f440806c6adc6f4c1dc9dc7a271e3d95293d76','runtime_image':image,'helper_source_sha256':hashlib.sha256((base/'v2_authenticated_read_probe.cpp').read_bytes()).hexdigest(),'qualified_link_inputs':pins,'returncode':result.returncode,'production_binary_changed':False,'read_helper_only':True,'helper_source_commit':'06df068b7362a3b3abaf0716be659f1cc6a44dfd'}
if result.returncode==0: report['binary_sha256']=hashlib.sha256((base/'authenticated-read-probe').read_bytes()).hexdigest()
(base/'build-result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='qualified_link_inputs'}))
raise SystemExit(result.returncode)
