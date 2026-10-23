import datetime,hashlib,json,os,re,socket,subprocess,urllib.request
from pathlib import Path
nodes={'bulbazar-NUC':(1,'100.72.127.102'),'vishalkapi':(2,'100.84.151.116'),'nuc2':(3,'100.85.36.22')}
n,ip=nodes[socket.gethostname()]
def run(argv):
 p=subprocess.run(argv,text=True,capture_output=True,timeout=15)
 return dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
report=dict(checked_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),node=n,host=socket.gethostname())
report['services']=run(['systemctl','--user','show','velocita-paper-stochastic-node.service','velocita-paper-stochastic-exporter.service','-p','ActiveState','-p','SubState','-p','NRestarts'])
report['containers']=run(['podman','ps','--format','{{.Names}} {{.Image}}'])
names=[row.split()[0] for row in report['containers']['stdout'].splitlines()]
report['one_cluster']=sum(bool(re.fullmatch(r'velocita-(?:v2-node-[0-9]+|paper-[a-z]+-node)',x)) for x in names)==1
report['image']=run(['podman','image','inspect','localhost/velocita-v2:qualified-86f4408','--format','{{.Id}}'])
p=Path.home()/'velocita-indicator-paper/stochastic/config'/('node-'+str(n)+'.conf')
b=p.read_bytes();report['config_sha256']=hashlib.sha256(b).hexdigest();report['recover']=b'startup = recover\n' in b
report['metrics']=urllib.request.urlopen('http://'+ip+':11410/metrics',timeout=8).read().decode()
if n==1:
 report['trading_units']=run(['systemctl','--user','show','velocita-paper-stochastic-edge.service','velocita-paper-stochastic-gateway.service','velocita-paper-indicator-observer.service','-p','Id','-p','LoadState','-p','ActiveState','-p','UnitFileState'])
 report['production_units']=run(['systemctl','show','velocita-swap-gateway.service','velocita-swap-signer.service','-p','Id','-p','LoadState','-p','ActiveState','-p','UnitFileState'])
print(json.dumps(report,indent=2))
