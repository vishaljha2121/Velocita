"""Run once only after the owner verifies all four Mac commands committed."""
from pathlib import Path
import json,os,subprocess,time
home=Path.home();root=home/'velocita-indicator-paper';base=root/'stochastic';stage=home/'stochastic-fresh-c8a6a0a7'
m=json.loads((stage/'manifest.json').read_text());profile=m['profiles']['stochastic']
assert profile['incarnation']=='c8a6a0a78fbf49a099b222124a5d557f'
assert not (base/'gateway/paper.state').exists(), 'not a fresh account; do not rerun'
def expand(xs): return [x.replace('%h',str(home)).replace('%t',f'/run/user/{os.getuid()}') for x in xs]
def run(xs,expected=None):
 r=subprocess.run(expand(xs),text=True,capture_output=True)
 print(r.stdout,end='',flush=True);print(r.stderr,end='',flush=True)
 if r.returncode or (expected and expected not in r.stdout): raise RuntimeError('step refused; inspect before retrying')
cmd=list(profile['gateway_command'])+['--paper-state-init','--paper-quote-usd','25']
cmd[cmd.index('--name')+1]+='-initialize'
run(cmd,'PAPER_STATE created')
run(['systemctl','--user','start','velocita-paper-stochastic-gateway.service'])
for i in range(20):
 time.sleep(.5)
 p=base/'gateway/venue.json'
 if p.exists():
  s=json.loads(p.read_text())
  if time.time()-s['stamp']<2: break
else: raise RuntimeError('fresh report unavailable')
image='localhost/velocita-v2-shadow:paper-1s-0680b08'
run(['podman','run','--rm','--network','none','--read-only','--userns=keep-id',
 '-v',f'{base}/config:/config','-v',f'{base}/edge:/edge','-v',f'{base}/gateway:/venue:ro',
 '-v',f'{stage}/prepare-fresh-profile.py:/prepare.py:ro','--entrypoint','python3',image,'/prepare.py'])
c=json.loads((base/'config/profile-1s.json').read_text())
obs=dict(c)
for k in ('program','ledger','edge_metrics'): obs[k]=c[k].replace('/edge/','/runs/stochastic/edge/')
for k in ('venue_report','gateway_metrics'): obs[k]=c[k].replace('/venue/','/runs/stochastic/gateway/')
(root/'observer.json').write_text(json.dumps([obs],indent=2)+'\n')
run(['podman','run','--rm','--name','velocita-paper-stochastic-1s-setup','--network','host',
 '--userns=keep-id','--read-only','--security-opt','no-new-privileges','--cap-drop','all',
 '--memory','512m','--shm-size','256m','-e','AERON_TERM_BUFFER_LENGTH=1048576',
 '-v',f'{base}/config:/config:ro','-v',f'{base}/edge:/edge:ro','-v',f'{base}/setup:/state',
 '-v',f'/run/user/{os.getuid()}/velocita-secrets/caller-11.key:/run/velocita-secrets/caller-11.key:ro',
 '--entrypoint','/usr/local/bin/velocita-v2-shadow-submitter',image,
 '--setup-only','--program','/edge/indicator-1s.program','--catalog','/config/catalog.bin',
 '--caller-key-id','11','--caller-id','9','--key-file','/run/velocita-secrets/caller-11.key',
 '--session-file','/state/setup.session','--state-file','/state/setup.state',
 '--response-endpoint','100.72.127.102:44411',*profile['transport'],
 '--aeron-dir','/dev/shm/velocita-setup-aeron'],'SETUP ')
# Installing policy/normal mode changes controls after the first gateway arm.
# Reconcile and arm against those final controls before accepting edge decisions.
armed_after=time.time()
run(['systemctl','--user','restart','velocita-paper-stochastic-gateway.service'])
metrics=base/'gateway/gateway.prom'
for i in range(30):
 time.sleep(.5)
 if metrics.exists() and metrics.stat().st_mtime>=armed_after and 'velocita_gateway_new_risk_stopped 0' in metrics.read_text(): break
else: raise RuntimeError('gateway not armed after setup; edge remains stopped')
run(['systemctl','--user','start','velocita-paper-stochastic-edge.service','velocita-paper-indicator-observer.service'])
print(json.dumps(dict(status='started',incarnation=c['incarnation'],epoch=c['epoch']),indent=2))
