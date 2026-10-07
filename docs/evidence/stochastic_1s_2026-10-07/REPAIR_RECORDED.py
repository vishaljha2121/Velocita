"""Exact one-time startup repair after ACTIVATE_RECORDED.py; not a general installer."""
from pathlib import Path
import json,subprocess,time
root=Path.home()/'velocita-indicator-paper'
units=Path.home()/'.config/systemd/user'
def run(*a):
    r=subprocess.run(a,check=True,text=True,capture_output=True)
    print(r.stdout,end='',flush=True)
    return r.stdout
assert run('podman','image','inspect','localhost/velocita-v2-shadow:paper-1s-0680b08','--format','{{index .Labels "org.velocita.source_commit"}}').strip()=='0680b08a5bbfbcaba32a77fbdf5cbe5d0162f40c'
s=json.loads((root/'stochastic/gateway/venue.json').read_text())
assert s['lots']==0 and len(s['orders'])==10 and s['cash']==24520467
assert s['buy_bps']==s['sell_bps']==20 and time.time()-s['stamp']<5
c=json.loads((root/'stochastic/config/profile-1s.json').read_text())
assert time.time_ns()<c['stop_ns']-120*10**9, 'insufficient remaining pilot entry time'
for name in ('velocita-paper-stochastic-edge.service','velocita-paper-indicator-observer.service'):
    p=units/name
    old=p.read_text()
    assert 'localhost/velocita-v2-shadow:paper-1s-978b4c4' in old
    with (root/'deployment/one-second-978b4c4'/('failed-start-'+name)).open('x') as out: out.write(old)
    p.write_text(old.replace('localhost/velocita-v2-shadow:paper-1s-978b4c4','localhost/velocita-v2-shadow:paper-1s-0680b08'))
run('systemctl','--user','daemon-reload')
run('systemctl','--user','start','velocita-paper-stochastic-edge.service')
run('systemctl','--user','restart','velocita-paper-indicator-observer.service')
print(json.dumps(dict(status='startup repaired',time_ns=time.time_ns(),entry_stop=c['stop_ns'],end=c['epoch']['end_ns'])))
