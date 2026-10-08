"""Run once on nuc1, only after all four owner commands committed on the new incarnation."""
from pathlib import Path
import json,os,re,subprocess,time
home=Path.home();root=home/'velocita-indicator-paper';base=root/'stochastic';stage=home/'ledger-abd1e149'
inc='abd1e149a26d4062ba5bbba2d90b3305'
units=home/'.config/systemd/user'
voter='localhost/velocita-v2:ledger-8a15b98';shadow='localhost/velocita-v2-shadow:paper-ledger-8a15b98'
uid=os.getuid()
assert inc in (base/'config/node-1.conf').read_text()
assert not (base/'gateway/paper.state').exists(), 'not a fresh account; do not rerun'
assert not (stage/'activation-started').exists(), 'do not replay a partial activation'
def run(cmd,expected=None):
    r=subprocess.run(cmd,text=True,capture_output=True)
    print(r.stdout,end='',flush=True);print(r.stderr,end='',flush=True)
    if r.returncode or (expected and expected not in r.stdout): raise RuntimeError(f'step refused: {cmd[0]} {cmd[1] if len(cmd)>1 else ""}')
    return r.stdout
def unit_command(name):
    text=(units/(name+'.service')).read_text()
    line=next(l for l in text.splitlines() if l.startswith('ExecStart='))
    import shlex
    return [x.replace('%h',str(home)).replace('%t',f'/run/user/{uid}') for x in shlex.split(line[len('ExecStart='):])]
(stage/'activation-started').write_text(str(time.time())+'\n')
gateway=unit_command('velocita-paper-stochastic-gateway')
assert voter in gateway and inc in gateway and '--buy-fee-bps' in gateway and gateway[gateway.index('--buy-fee-bps')+1]=='20'
init=list(gateway)+['--paper-state-init','--paper-quote-usd','25']
init[init.index('--name')+1]+='-initialize'
run(init,'PAPER_STATE created')
run(['systemctl','--user','start','velocita-paper-stochastic-gateway.service'])
for _ in range(40):
    time.sleep(.5);p=base/'gateway/venue.json'
    if p.exists() and time.time()-json.loads(p.read_text())['stamp']<2: break
else: raise RuntimeError('fresh venue report unavailable')
run(['podman','run','--rm','--network','none','--read-only','--userns=keep-id',
 '-v',f'{base}/config:/config','-v',f'{base}/edge:/edge','-v',f'{base}/gateway:/venue:ro',
 '-v',f'{stage}/prepare-profile.py:/prepare.py:ro','--entrypoint','python3',shadow,'/prepare.py'],'PROFILE created')
c=json.loads((base/'config/profile-cash.json').read_text())
obs=dict(c)
for k in ('program','ledger','edge_metrics'): obs[k]=c[k].replace('/edge/','/runs/stochastic/edge/')
for k in ('venue_report','gateway_metrics'): obs[k]=c[k].replace('/venue/','/runs/stochastic/gateway/')
(root/'observer.json').write_text(json.dumps([obs],indent=2)+'\n')
edge=unit_command('velocita-paper-stochastic-edge')
transport=[]
for flag in ('--incarnation','--node'):
    for i,x in enumerate(edge):
        if x==flag: transport+=[flag,edge[i+1]]
assert transport.count('--node')==3 and inc in transport
run(['podman','run','--rm','--name','velocita-paper-stochastic-setup','--network','host',
 '--userns=keep-id','--read-only','--security-opt','no-new-privileges','--cap-drop','all',
 '--memory','512m','--shm-size','256m','-e','AERON_TERM_BUFFER_LENGTH=1048576',
 '-v',f'{base}/config:/config:ro','-v',f'{base}/edge:/edge:ro','-v',f'{base}/setup:/state',
 '-v',f'/run/user/{uid}/velocita-secrets/caller-11.key:/run/velocita-secrets/caller-11.key:ro',
 '--entrypoint','/usr/local/bin/velocita-v2-shadow-submitter',shadow,
 '--setup-only','--program','/edge/indicator-cash.program','--catalog','/config/catalog.bin',
 '--caller-key-id','11','--caller-id','9','--key-file','/run/velocita-secrets/caller-11.key',
 '--session-file','/state/setup.session','--state-file','/state/setup.state',
 '--response-endpoint','100.72.127.102:44411',*transport,
 '--aeron-dir','/dev/shm/velocita-setup-aeron'],'SETUP ')
armed_after=time.time()
run(['systemctl','--user','restart','velocita-paper-stochastic-gateway.service'])
metrics=base/'gateway/gateway.prom'
for _ in range(60):
    time.sleep(.5)
    if metrics.exists() and metrics.stat().st_mtime>=armed_after and 'velocita_gateway_new_risk_stopped 0' in metrics.read_text(): break
else: raise RuntimeError('gateway not armed after setup; edge remains stopped')
run(['systemctl','--user','start','velocita-paper-stochastic-edge.service','velocita-paper-indicator-observer.service'])
print(json.dumps(dict(status='started',incarnation=inc,epoch=c['epoch']),indent=2))
