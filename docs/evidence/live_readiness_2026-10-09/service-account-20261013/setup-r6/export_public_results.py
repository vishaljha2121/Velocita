import json,os
from pathlib import Path
base=Path('/home/vishal/.cache/velocita-build')
report={}
for label,folder in [('first','swap-cutover-20261013-owner'),('temporary_mount','swap-cutover-20261013-read-r2'),('explicit_bind','swap-cutover-20261013-read-r3')]:
 root=base/folder/'read-result';report[label]={}
 for name in ['result.json','signer.json','gateway.json']:
  p=root/name
  if p.exists(): report[label][name]=json.loads(p.read_text())
report['manifest']=json.loads(Path('/etc/velocita-swap/public/manifest.json').read_text())
report['units']={r:Path('/etc/systemd/system/velocita-swap-'+r+'.service').read_text() for r in ['gateway','signer']}
# Only fixed public helper results, public manifest and unit definitions.
p=base/'swap-cutover-20261013-read-r3/public-results.json'
with p.open('x') as f:
 os.fchmod(f.fileno(),0o600);json.dump(report,f,indent=2);f.write('\n');f.flush();os.fsync(f.fileno());os.fchown(f.fileno(),1002,1003)
print(json.dumps({'exported_public_results':True,'results':report['explicit_bind']['result.json']['results']}))
