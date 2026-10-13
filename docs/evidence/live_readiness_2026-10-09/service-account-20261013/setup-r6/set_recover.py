import hashlib,json,os,re,socket,urllib.request
from pathlib import Path
NODES={'bulbazar-NUC':(1,'100.72.127.102'),'vishalkapi':(2,'100.84.151.116'),'nuc2':(3,'100.85.36.22')}
n,ip=NODES[socket.gethostname()]
raw=urllib.request.urlopen('http://'+ip+':11410/metrics',timeout=8).read().decode()
def val(name,extra=''):
    matches=re.findall(r'^velocita_v2_'+name+r'\{[^\n]*'+extra+r'[^\n]*\} ([0-9.]+)$',raw,re.M)
    if len(matches)!=1: raise RuntimeError('ambiguous metric '+name)
    return float(matches[0])
assert val('up')==1 and val('ready')==1 and val('failed')==0 and val('telemetry_age_seconds')<2
assert val('log_index','kind="applied"')>=3 and val('strategy_mode')==5
p=Path.home()/'velocita-indicator-paper/stochastic/config'/('node-'+str(n)+'.conf')
s=p.read_bytes()
assert s.count(b'startup = bootstrap\n')==1
assert b'cluster.incarnation = b79e65be1ea14e219016ec48ea02efef\n' in s
new=s.replace(b'startup = bootstrap\n',b'startup = recover\n')
t=p.with_suffix('.recover-pending')
with t.open('xb') as f:
    os.fchmod(f.fileno(),p.stat().st_mode & 0o777);f.write(new);f.flush();os.fsync(f.fileno())
os.replace(t,p)
fd=os.open(p.parent,os.O_RDONLY|os.O_DIRECTORY)
try: os.fsync(fd)
finally: os.close(fd)
print(json.dumps(dict(host=socket.gethostname(),node=n,config_before_sha256=hashlib.sha256(s).hexdigest(),config_after_sha256=hashlib.sha256(new).hexdigest(),startup='recover',restart=False,metrics=raw),indent=2))
