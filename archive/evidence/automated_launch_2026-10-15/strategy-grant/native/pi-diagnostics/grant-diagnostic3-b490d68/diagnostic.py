import json,shlex,subprocess,hashlib,os
from pathlib import Path
source=Path('/source');build=Path('/work/build');out=Path('/evidence')
assert subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),'rev-parse','HEAD'],text=True).strip()=='b490d6843b53d114a49678304089541bfe192c7a'
assert not subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),'status','--porcelain'])
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','v2-swap-continuous-drill'],text=True).splitlines()
objects=[Path('/objects/runtime.o'),Path('/objects/drill.o')]
link=[line for line in commands if ' -o v2-swap-continuous-drill ' in line]
assert len(link)==1
cmd=shlex.split(link[0]);cmd=cmd[cmd.index('/usr/bin/c++'):];cmd=cmd[:cmd.index('&&')] if '&&' in cmd else cmd
cmd=[str(objects[1]) if value.endswith('/v2_swap_continuous_drill.cpp.o') else value for value in cmd]
cmd=[value for value in cmd if not value.startswith('-Wl,--dependency-file=')];cmd.insert(1,str(objects[0]));binary=out/'diagnostic';cmd[cmd.index('-o')+1]=str(binary)
subprocess.run(cmd,cwd=build,check=True)
(out/'build.json').write_text(json.dumps({'diagnostic_only':True,'production_source_modified':False,'commit':'b490d6843b53d114a49678304089541bfe192c7a','binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()},indent=2)+'\n')
p=subprocess.run([str(binary),'--case','0',str(out/'case')],timeout=100)
(out/'result.json').write_text(json.dumps({'diagnostic_only':True,'exit':p.returncode})+'\n')
raise SystemExit(p.returncode)
