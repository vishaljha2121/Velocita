#!/usr/bin/env python3
"""Package previously qualified exact ELF bytes; never compile or contact venue."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys
base=Path.home()/'.cache/velocita-build/swap-cutover-20261013'
qual=Path.home()/'.cache/velocita-build/integrated-a890307-20261013/qualification'
source=qual.parent/'source';host=sys.argv[1]
assert host in ('nuc1','pi')
commit='86f440806c6adc6f4c1dc9dc7a271e3d95293d76'
assert subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()==commit
assert not subprocess.check_output(['git','-C',str(source),'status','--porcelain','--untracked-files=no']).strip()
images={'nuc1':'076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f','pi':'3f3cfb3403a2644b27bf21351e26683e62f6ad90fde1f1b1567c8d2279d4fb43'}
image=images[host]
assert subprocess.check_output(['podman','image','inspect','--format','{{.Id}}',image],text=True).strip()==image
assert shutil.disk_usage(base).free>2_500_000_000
pins=json.loads((base/'qualified-binary-pins.json').read_text())[host]
context=base/'image';context.mkdir(mode=0o700)
for name,digest in pins.items():
 raw=(qual/'build'/name).read_bytes();assert raw.startswith(b'\x7fELF') and hashlib.sha256(raw).hexdigest()==digest
 (context/name).write_bytes(raw);(context/name).chmod(0o755)
(context/'qualified-binary-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
(context/'Containerfile').write_text('FROM '+image+'\n'+''.join('COPY '+n+' /usr/local/bin/'+n+'\n' for n in pins)+'COPY qualified-binary-pins.json /usr/share/velocita/qualified-binary-pins.json\nLABEL org.velocita.source_commit="'+commit+'" org.velocita.qualification="native-integrated-a890307-20261013"\nENTRYPOINT ["/usr/local/bin/velocita-v2-node"]\n')
tag='localhost/velocita-v2:qualified-86f4408'
cmd=['podman','build','--pull-never','--network=none','--layers','--cpuset-cpus=3','--memory=1g','-t',tag,str(context)]
(base/'package-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
with (base/'package.log').open('x') as log: subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
result=dict(source_commit=commit,binary_sha256=pins,base_image=image,image=tag,image_id=subprocess.check_output(['podman','image','inspect','--format','{{.Id}}',tag],text=True).strip(),compiled=False)
(base/'package-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
