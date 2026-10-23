"""Export only offline qualification logs and synthetic result transcripts."""
from pathlib import Path
import io,json,hashlib,tarfile,sys,platform,subprocess
root=Path.home()/'.cache/velocita-build/integrated-a890307-20261013/qualification'
files=[]
for folder in (root,*sorted(root.glob('attempt-*'))):
 for p in sorted(folder.iterdir()):
  if p.is_file() and p.suffix in ('.json','.log','.py','.xml'):files.append(p)
for p in sorted(root.glob('home*/.cache/velocita-build/process-drill-20261013/run-*/case-*/*')):
 if p.name in ('result.json','venue.log','processes.jsonl','shutdown.jsonl','replicas.jsonl'):files.append(p)
manifest={'host':platform.node(),'arch':platform.machine(),'files':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
with tarfile.open(fileobj=sys.stdout.buffer,mode='w|gz') as tar:
 for p in files:tar.add(p,arcname=str(p.relative_to(root)),recursive=False)
 b=(json.dumps(manifest,indent=2)+'\n').encode();info=tarfile.TarInfo('artifact-manifest.json');info.size=len(b);tar.addfile(info,io.BytesIO(b))
