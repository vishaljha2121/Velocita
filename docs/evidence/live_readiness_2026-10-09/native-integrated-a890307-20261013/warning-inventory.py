"""Read-only diagnostic collection across retained native build attempts."""
from pathlib import Path
import json,re,hashlib,platform
root=Path.home()/'.cache/velocita-build/integrated-a890307-20261013/qualification'
attempts=[]
for directory in [*sorted(root.glob('attempt-*')),root]:
 if (directory/'status.json').is_file():
  commit=json.loads((directory/'status.json').read_text())['commit']
  attempts.append((commit,directory))
found={};logs=[];seen_logs=set()
pattern=re.compile(r'^(/[^\n]+?):(\d+)(?::(\d+))?: warning: (.*)$')
for commit,directory in attempts:
 for name in ('configure.log','build.log','final-incremental-build.log','fixture-incremental-build.log'):
  p=directory/name
  if not p.is_file():continue
  b=p.read_bytes();lines=b.decode(errors='replace').splitlines()
  logs.append({'commit':commit,'path':str(p.relative_to(root)),'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)})
  digest=hashlib.sha256(b).hexdigest()
  logs[-1]['duplicate_content']=digest in seen_logs
  if digest in seen_logs:continue
  seen_logs.add(digest)
  for position,line in enumerate(lines):
   if not re.search(r'warning:',line,re.I):continue
   m=pattern.match(line)
   if m:
    path,number,column,message=m.groups();number=int(number);column=int(column or 0)
   else:path,number,column,message=name,0,0,line
   kind=re.search(r'\[(-W[^\]]+)\]\s*$',message)
   key=(path,number,column,message)
   if key not in found:
    found[key]={'path':path,'line':number,'column':column,'message':message,'category':kind.group(1) if kind else 'configuration/toolchain','scope':'project' if path.startswith('/source/') else 'header/dependency (inspect origin)','context':lines[max(0,position-20):position+5],'occurrences':0,'attempts':{}}
   row=found[key];row['occurrences']+=1;row['attempts'][commit]=row['attempts'].get(commit,0)+1
status=json.loads((root/'status.json').read_text())
driver=(root/'driver.log').read_text()
print(json.dumps({'schema':2,'source_commit':status['commit'],'host':platform.node(),'arch':platform.machine(),'logs':logs,'build_finished':bool(re.search(r'Z tests$',driver,re.M)),'qualification_status':status,'unique_warnings':len(found),'total_occurrences':sum(v['occurrences'] for v in found.values()),'warnings':list(found.values())},indent=2))
