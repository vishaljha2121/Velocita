"""Nuc1 maintenance activation. Existing edge stopped; image and config preverified."""
from pathlib import Path
import json,os,re,subprocess,sys,time
home=Path.home();root=home/'velocita-indicator-paper';base=root/'stochastic'
stage=home/'stochastic-cash-20261007';image=sys.argv[1]
assert image.startswith('localhost/velocita-v2-shadow:paper-cash-')
def run(cmd):
    r=subprocess.run(cmd,text=True,capture_output=True)
    print(r.stdout,end='',flush=True);print(r.stderr,end='',flush=True)
    if r.returncode: raise RuntimeError(f'step failed ({r.returncode}): {cmd[0]}')
    return r.stdout
assert subprocess.run(['systemctl','--user','is-active','velocita-paper-stochastic-edge.service'],capture_output=True,text=True).stdout.strip()=='inactive'
# Original account finalization and copied continuation were prepared separately.
c=json.loads((base/'config/profile-cash.json').read_text())
assert c['incarnation']=='c8a6a0a78fbf49a099b222124a5d557f'
assert c['epoch']['cash']==24869170 and c['epoch']['variant']=='stochastic_1s_cash_v1'
assert c['ledger']=='/edge/ledger-cash-v1.db' and c['stop_ns']==0
assert not (stage/'activation-started').exists(), 'do not automatically replay a partial activation'
(stage/'activation-started').write_text(str(time.time())+'\n')
run(['systemctl','--user','stop','velocita-paper-stochastic-gateway.service'])
diag=run(['podman','run','--rm','--network','host','--userns=keep-id','--read-only',
 '--security-opt','no-new-privileges','--cap-drop','all','--memory','512m','--shm-size','256m',
 '-e','AERON_TERM_BUFFER_LENGTH=1048576',
 '-v',f'{home}/stochastic-fresh-c8a6a0a7/read-committed-state:/diag:ro',
 '-v',f'{base}/gateway:/state',
 '-v',f'/run/user/{os.getuid()}/velocita-secrets/caller-13.key:/run/velocita-secrets/caller-13.key:ro',
 '--entrypoint','/diag',image])
assert 'risk account=2 position=0 reserved=0 margin=0' in diag
for line in diag.splitlines():
    if line.startswith('order '): assert int(re.search(r'state=(\d+)',line)[1]) in (9,10,11)
    if line.startswith('effect '): assert int(re.search(r'state=(\d+)',line)[1]) in (4,5)
(stage/'committed-state-before.txt').write_text(diag)

old=json.loads((home/'stochastic-fresh-c8a6a0a7/manifest.json').read_text())
transport=old['profiles']['stochastic']['transport']
setup=['podman','run','--rm','--name','velocita-paper-stochastic-cash-setup','--network','host',
 '--userns=keep-id','--read-only','--security-opt','no-new-privileges','--cap-drop','all','--memory','512m','--shm-size','256m',
 '-e','AERON_TERM_BUFFER_LENGTH=1048576', '-v',f'{base}/config:/config:ro','-v',f'{base}/edge:/edge:ro',
 '-v',f'{base}/setup:/state','-v',f'/run/user/{os.getuid()}/velocita-secrets/caller-11.key:/run/velocita-secrets/caller-11.key:ro',
 '--entrypoint','/usr/local/bin/velocita-v2-shadow-submitter',image,'--setup-only','--program','/edge/indicator-cash.program',
 '--catalog','/config/catalog.bin','--caller-key-id','11','--caller-id','9','--key-file','/run/velocita-secrets/caller-11.key',
 '--session-file','/state/setup.session','--state-file','/state/setup.state','--response-endpoint','100.72.127.102:44411',
 *transport,'--aeron-dir','/dev/shm/velocita-setup-aeron']
text=run(setup);assert 'SETUP ' in text
# The gateway starts only after policy/control installation, avoiding control_change.
armed_after=time.time();run(['systemctl','--user','start','velocita-paper-stochastic-gateway.service'])
for i in range(40):
    time.sleep(.5);p=base/'gateway/gateway.prom'
    if p.exists() and p.stat().st_mtime>=armed_after and 'velocita_gateway_new_risk_stopped 0' in p.read_text(): break
else: raise RuntimeError('gateway did not arm; edge remains stopped')
units=home/'.config/systemd/user'
for name in ('velocita-paper-stochastic-edge','velocita-paper-indicator-observer'):
    p=units/(name+'.service');old=p.read_text()
    (stage/(name+'.service.before')).write_text(old)
    text=old.replace('localhost/velocita-v2-shadow:paper-1s-0680b08',image)
    assert text!=old
    text=text.replace('podman stop -t','podman stop --ignore -t')
    if name.endswith('edge'):
        text=text.replace('/config/profile-1s.json','/config/profile-cash.json').replace('/edge/indicator-1s.program','/edge/indicator-cash.program')
    p.write_text(text)
obs=dict(c)
for k in ('program','ledger','edge_metrics'):obs[k]=c[k].replace('/edge/','/runs/stochastic/edge/')
for k in ('venue_report','gateway_metrics'):obs[k]=c[k].replace('/venue/','/runs/stochastic/gateway/')
(root/'observer.json').write_text(json.dumps([obs],indent=2)+'\n')
# Change only monitoring mount paths; the shared host source tree is not swapped.
for name in ('velocita-v2-prometheus','velocita-v2-grafana'):
    p=units/(name+'.service');old=p.read_text();(stage/(name+'.service.before')).write_text(old)
    text=old.replace('%h/stochastic-fresh-c8a6a0a7/observability','%h/stochastic-cash-20261007/observability')
    if name.endswith('prometheus'):
        text=text.replace('%h/src/aeron-raft-pi/deploy/v2/observability/rules','%h/stochastic-cash-20261007/rules')
    assert text!=old;p.write_text(text)
run(['systemctl','--user','daemon-reload'])
run(['systemctl','--user','start','velocita-paper-stochastic-edge.service'])
run(['systemctl','--user','restart','velocita-paper-indicator-observer.service'])
run(['systemctl','--user','restart','velocita-v2-prometheus.service','velocita-v2-grafana.service'])
print(json.dumps(dict(status='started, verification required',incarnation=c['incarnation'],epoch=c['epoch']),indent=2))
