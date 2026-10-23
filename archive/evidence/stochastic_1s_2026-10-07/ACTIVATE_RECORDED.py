from pathlib import Path
import hashlib,json,os,shlex,shutil,sqlite3,subprocess,time,urllib.request

HOME=Path.home();ROOT=HOME/'velocita-indicator-paper';BASE=ROOT/'stochastic'
SOURCE=HOME/'src/stochastic-1s-978b4c4'
TAG='paper-1s-978b4c4'
STAGE=ROOT/'deployment/one-second-978b4c4'
UNITS=HOME/'.config/systemd/user'

def run(*args):
    result=subprocess.run(args,text=True,capture_output=True,check=True)
    print(result.stdout,end='',flush=True)
    return result.stdout

for host in ('100.72.127.102','100.84.151.116','100.85.36.22'):
    with urllib.request.urlopen(f'http://{host}:11410/metrics',timeout=4) as r: metrics=r.read().decode()
    def value(name,fragment=''):
        return [float(x.rsplit(' ',1)[1]) for x in metrics.splitlines() if x.startswith(name+'{') and fragment in x]
    assert value('velocita_v2_up')==[1],host
    assert value('velocita_v2_retained_log_entries','pool="ordinary"')[0]<=1000,host
s=json.loads((BASE/'gateway/venue.json').read_text())
assert s['lots']==0 and s['pending_reports']==0 and not s['failed'],'wait for old strategy to exit normally'
assert s['buy_bps']==s['sell_bps']==30
assert time.time()-s['stamp']<5
image=run('podman','image','inspect',f'localhost/velocita-v2-shadow:{TAG}','--format','{{index .Labels "org.velocita.source_commit"}}').strip()
assert image=='978b4c46d02e9513c2f42bffb1c9f72d2b24aed1'
STAGE.mkdir()
for name in ('edge','gateway'):
    target=BASE/name
    if name=='gateway': shutil.copy2(target/'paper.state',STAGE/'paper-before.state')
for name in ('velocita-paper-stochastic-edge.service','velocita-paper-stochastic-gateway.service','velocita-paper-indicator-observer.service'):
    shutil.copy2(UNITS/name,STAGE/name)
shutil.copy2(ROOT/'observer.json',STAGE/'observer-before.json')
run('systemctl','--user','stop','velocita-paper-stochastic-edge.service')
s=json.loads((BASE/'gateway/venue.json').read_text())
assert s['lots']==0 and s['pending_reports']==0 and not s['failed'],'position changed before stop; do not reset'
assert time.time()-s['stamp']<5
shutil.copy2(BASE/'gateway/paper.state',STAGE/'paper-before.state')
(STAGE/'venue-before.json').write_text(json.dumps(s,indent=2)+'\n')
with sqlite3.connect(BASE/'edge/ledger.db') as db,sqlite3.connect(STAGE/'ledger-before.db') as backup:
    db.backup(backup)
run('podman','run','--rm','--network','none','--userns=keep-id','--read-only',
    '-v',f'{BASE}/config:/config','-v',f'{BASE}/edge:/edge','-v',f'{BASE}/gateway:/venue:ro',
    '-v',f'{SOURCE}/deploy/v2/indicator-paper/prepare-second.py:/prepare.py:ro',
    '--entrypoint','python3',f'localhost/velocita-v2-shadow:{TAG}',
    '/prepare.py','--config','/config/profile.json','--report','/venue/venue.json',
    '--output','/config/profile-1s.json','--program','/edge/indicator-1s.program')
c=json.loads((BASE/'config/profile-1s.json').read_text())
observer=dict(c)
for key in ('program','ledger','edge_metrics'): observer[key]=c[key].replace('/edge/','/runs/stochastic/edge/')
for key in ('venue_report','gateway_metrics'): observer[key]=c[key].replace('/venue/','/runs/stochastic/gateway/')
(ROOT/'observer-1s.json').write_text(json.dumps([observer],indent=2)+'\n')
transport=['--incarnation',c['incarnation'],'--node','1=100.72.127.102:44301','--node','2=100.84.151.116:44302','--node','3=100.85.36.22:44303']
run('podman','run','--rm','--name','velocita-paper-stochastic-1s-setup','--network','host',
    '--userns=keep-id','--read-only','--security-opt','no-new-privileges','--cap-drop','all',
    '--memory','512m','--shm-size','256m','-e','AERON_TERM_BUFFER_LENGTH=1048576',
    '-v',f'{BASE}/config:/config:ro','-v',f'{BASE}/edge:/edge:ro','-v',f'{BASE}/setup:/state',
    '-v',f'/run/user/{os.getuid()}/velocita-secrets/caller-11.key:/run/velocita-secrets/caller-11.key:ro',
    '--entrypoint','/usr/local/bin/velocita-v2-shadow-submitter',f'localhost/velocita-v2-shadow:{TAG}',
    '--setup-only','--program','/edge/indicator-1s.program','--catalog','/config/catalog.bin',
    '--caller-key-id','11','--caller-id','9','--key-file','/run/velocita-secrets/caller-11.key',
    '--session-file','/state/setup.session','--state-file','/state/setup.state',
    '--response-endpoint','100.72.127.102:44411',*transport,'--aeron-dir','/dev/shm/velocita-setup-aeron')
for name in ('velocita-paper-stochastic-edge.service','velocita-paper-stochastic-gateway.service','velocita-paper-indicator-observer.service'):
    p=UNITS/name;text=p.read_text()
    if name.endswith('-edge.service'):
        assert 'run_indicator_paper' in text
        text=text.replace('localhost/velocita-v2-shadow:paper-7297bd5',f'localhost/velocita-v2-shadow:{TAG}')
        text=text.replace('run_indicator_paper','run_stochastic_second').replace('/config/profile.json','/config/profile-1s.json').replace('/edge/indicator.program','/edge/indicator-1s.program')
    elif name.endswith('-gateway.service'):
        assert '--venue paper' in text and '--buy-fee-bps 30 --sell-fee-bps 30' in text
        text=text.replace('--buy-fee-bps 30 --sell-fee-bps 30','--buy-fee-bps 20 --sell-fee-bps 20')
    else:
        assert '/runs/stochastic/edge:ro' in text
        text=text.replace('localhost/velocita-v2-shadow:paper-7297bd5',f'localhost/velocita-v2-shadow:{TAG}')
        text=text.replace('%h/velocita-indicator-paper/observer.json:', '%h/velocita-indicator-paper/observer-1s.json:')
    p.write_text(text)
run('systemctl','--user','daemon-reload')
run('systemctl','--user','restart','velocita-paper-stochastic-gateway.service')
for i in range(20):
    time.sleep(.5)
    after=json.loads((BASE/'gateway/venue.json').read_text())
    if after['buy_bps']==after['sell_bps']==20 and time.time()-after['stamp']<2: break
else: raise RuntimeError('new gateway report unavailable')
for key in ('cash','lots','fees','orders','buy_volume','sell_volume'):
    assert after[key]==s[key],f'gateway restart changed {key}'
run('systemctl','--user','start','velocita-paper-stochastic-edge.service')
run('systemctl','--user','restart','velocita-paper-indicator-observer.service')
print(json.dumps(dict(status='started',baseline_cash=s['cash'],prior_orders=len(s['orders']),entry_stop=c['stop_ns'],end=c['epoch']['end_ns']),indent=2),flush=True)
