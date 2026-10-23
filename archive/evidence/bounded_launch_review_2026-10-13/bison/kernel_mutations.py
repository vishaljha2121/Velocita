import argparse,json,shlex,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output;out.mkdir(parents=True,exist_ok=False)
link=shlex.split((a.build/'CMakeFiles/v2-swap-integration-tests.dir/link.txt').read_text())
flags=(a.build/'CMakeFiles/velocita_v2_kernel.dir/flags.make').read_text()
includes=shlex.split(next(x.split('=',1)[1] for x in flags.splitlines() if x.startswith('CXX_INCLUDES =')))
mutations=[
 ('gateway_role','src/v2_kernel.cpp','std::holds_alternative<swap::AmendBuyPrice>(value.record.command)) return kOperatorRole;',
  'std::holds_alternative<swap::AmendBuyPrice>(value.record.command)) return kOperatorRole | kGatewayRole;','gateway_cannot_amend_price'),
 ('strategy_role','src/v2_kernel.cpp','std::holds_alternative<swap::AmendBuyPrice>(value.record.command)) return kOperatorRole;',
  'std::holds_alternative<swap::AmendBuyPrice>(value.record.command)) return kOperatorRole | kStrategyRole;','strategy_cannot_amend_price'),
 ('snapshot_length','src/v2_state_snapshot.cpp','(swap::encoded_state_size(state.spot_swap) - 1)',
  '(state.spot_swap.initialized ? swap::max_state_bytes - 1 : 0)','production snapshot restores swap state'),
]
rows=[]
for name,file,old,new,expected in mutations:
 text=(ROOT/file).read_text();assert text.count(old)==1,name;source=out/(name+'.cpp');source.write_text(text.replace(old,new,1));obj=out/(name+'.o');binary=out/name
 subprocess.run([link[0],'-std=c++20','-DVELOCITA_TARGET_CACHE_LINE_SIZE=64',*includes,'-c',str(source),'-o',str(obj)],check=True)
 cmd=link[:];cmd[cmd.index('-o')+1]=str(binary);cmd.insert(1,str(obj));subprocess.run(cmd,cwd=a.build,check=True)
 r=subprocess.run([str(binary)],capture_output=True,text=True);ok=r.returncode==1 and ': '+expected+'\n' in r.stdout+r.stderr
 rows.append(dict(name=name,assertion=expected,passed=ok,exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
 if not ok:raise RuntimeError(str(rows[-1]))
(out/'results.json').write_text(json.dumps({'named_kills':len(rows),'results':rows},indent=2)+'\n');print(len(rows),'named kills')
