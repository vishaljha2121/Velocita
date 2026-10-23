#!/usr/bin/env python3
"""One-shot public monitoring identity correction; never touches voters or trading."""
import hashlib,json,os,shutil,socket,subprocess
from pathlib import Path
assert socket.gethostname()=='bulbazar-NUC' and os.getuid()==1003
base=Path(__file__).resolve().parent
source=base/'observability';previous=Path.home()/'roles-348e0b2c-884816d/observability'
root=Path.home()/'read-b79e65be-86f4408';target=root/'observability'
assert not root.exists()
pins=json.loads((source/'before-sha256.json').read_text())
for name,digest in pins.items(): assert hashlib.sha256((previous/name).read_bytes()).hexdigest()==digest
unitroot=Path.home()/'.config/systemd/user'
updates=[]
for role in ('prometheus','grafana'):
 p=unitroot/('velocita-v2-'+role+'.service');old=p.read_text()
 assert old.count('%h/roles-348e0b2c-884816d/observability')==1
 new=old.replace('%h/roles-348e0b2c-884816d/observability','%h/read-b79e65be-86f4408/observability')
 assert not p.with_suffix('.cutover-pending').exists()
 updates.append((p,old,new))
# Validate candidate against unchanged rule files before replacing any service unit.
cmd=['podman','run','--rm','--network','none','--read-only','--memory','128m','--cap-drop','all',
 '--security-opt','no-new-privileges','-v',str(source/'prometheus.yml')+':/etc/prometheus/prometheus.yml:ro',
 '-v',str(previous.parent/'rules')+':/etc/prometheus/rules:ro','--entrypoint','/bin/promtool',
 'docker.io/prom/prometheus:v3.13.2','check','config','/etc/prometheus/prometheus.yml']
subprocess.run(cmd,check=True,timeout=60)
root.mkdir(mode=0o700);target.mkdir()
for name in pins: shutil.copyfile(source/name,target/name)
for p,old,new in updates:
 (root/(p.name+'.before')).write_text(old)
 pending=p.with_suffix('.cutover-pending')
 with pending.open('x') as f:
  f.write(new);f.flush();os.fsync(f.fileno())
 os.replace(pending,p)
for directory in (target,root,unitroot):
 for p in directory.iterdir():
  if p.is_file() and not p.is_symlink():
   fd=os.open(p,os.O_RDONLY)
   try:os.fsync(fd)
   finally:os.close(fd)
 fd=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
 try:os.fsync(fd)
 finally:os.close(fd)
subprocess.run(['systemctl','--user','daemon-reload'],check=True,timeout=15)
subprocess.run(['systemctl','--user','restart','velocita-v2-prometheus.service','velocita-v2-grafana.service'],check=True,timeout=60)
print(json.dumps({'monitoring_run':'b79e65be1ea14e219016ec48ea02efef-read-qualification','strategy':'none','voters_restarted':False,'live_admitted':False}))
