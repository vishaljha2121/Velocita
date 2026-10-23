#!/usr/bin/env python3
"""Approved one-cluster state retirement; all processes must already be stopped."""
from pathlib import Path
import hashlib,json,os,re,shutil,stat,subprocess,sys
node=int(sys.argv[1]);assert node in (1,2,3)
base=Path.home()/'.cache/velocita-build/swap-cutover-20261013'
config=base/('node-'+str(node));live=Path.home()/'velocita-indicator-paper/stochastic'
retired=live.parent/'retired/stochastic-348e0b2c-before-schema12-20261013'
units=Path.home()/'.config/systemd/user'
pair=['velocita-paper-stochastic-node','velocita-paper-stochastic-exporter']
blocked=['velocita-paper-stochastic-edge','velocita-paper-stochastic-gateway','velocita-paper-indicator-observer'] if node==1 else []
names=pair+blocked
for name in names:
 assert subprocess.check_output(['systemctl','--user','show',name+'.service','-p','ActiveState','--value'],text=True).strip()=='inactive',name
assert not any(re.fullmatch(r'velocita-(?:v2-node-[0-9]+|paper-[a-z]+-node)',n) for n in subprocess.check_output(['podman','ps','--format','{{.Names}}'],text=True).splitlines())
assert not retired.exists() and live.is_dir() and not live.is_symlink()
assert (live/'config'/('node-'+str(node)+'.conf')).read_bytes()==(config/'before-node.conf').read_bytes()
for kind in ('node','exporter'):
 assert (units/('velocita-paper-stochastic-'+kind+'.service')).read_bytes()==(config/('before-'+kind+'.service')).read_bytes()
assert (live/'config/catalog.bin').is_file()
assert hashlib.sha256((live/'config/catalog.bin').read_bytes()).hexdigest()=='8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f'
assert 'cluster.incarnation = b79e65be1ea14e219016ec48ea02efef' in (config/('node-'+str(node)+'.conf')).read_text()
assert subprocess.check_output(['podman','image','inspect','--format','{{.Id}}','localhost/velocita-v2:qualified-86f4408'],text=True).strip()==('6ecd4141692c3fce6dea7d491b658733163fa665f92884575778b460b6c95a75' if node==2 else 'd965170cd8168ada8ea91b6f8a72b46babe0b771f8cc39c7f0de7f1e045d6e04')
# Hash frozen old files; no raw ledger or account records leave host.
def flush(path):
 fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
 try:os.fsync(fd)
 finally:os.close(fd)
def inventory(root):
 rows={}
 for path in sorted(root.rglob('*')):
  if path.is_symlink(): raise ValueError('unexpected symlink in archived state')
  if path.is_file():
   h=hashlib.sha256()
   with path.open('rb') as f:
    while chunk:=f.read(65536):h.update(chunk)
   rows[str(path.relative_to(root))]=h.hexdigest()
 return rows
before=inventory(live)
retired.parent.mkdir(exist_ok=True)
os.rename(live,retired)
assert inventory(retired)==before
archive_units=retired/'units-before';archive_units.mkdir()
for name in names: shutil.copy2(units/(name+'.service'),archive_units/(name+'.service'))
if node==1 and (live.parent/'observer.json').is_file(): shutil.copy2(live.parent/'observer.json',retired/'observer-before.json')
(retired/'archived-state-sha256.json').write_text(json.dumps(before,indent=2)+'\n')
for path in retired.rglob('*'):
 if path.is_file():flush(path)
for path in sorted((p for p in retired.rglob('*') if p.is_dir()),key=lambda p:len(p.parts),reverse=True):flush(path)
for path in (retired,retired.parent,live.parent):flush(path)
for suffix in ('config','data'):(live/suffix).mkdir(parents=True)
shutil.copy2(retired/'config/catalog.bin',live/'config/catalog.bin')
shutil.copy2(config/('node-'+str(node)+'.conf'),live/'config'/('node-'+str(node)+'.conf'))
for path in (live/'config/catalog.bin',live/'config'/('node-'+str(node)+'.conf'),live/'config',live/'data',live,live.parent):flush(path)
for kind in ('node','exporter'):
 name='velocita-paper-stochastic-'+kind+'.service';temp=units/(name+'.cutover')
 with temp.open('xb') as f:f.write((config/name).read_bytes());f.flush();os.fsync(f.fileno())
 os.replace(temp,units/name)
# Preserve original unit bytes; persistent masks prevent a reboot starting an old trading run.
for name in blocked:
 path=units/(name+'.service');os.rename(path,archive_units/(name+'.service.original'))
 path.symlink_to('/dev/null')
flush(units);flush(archive_units)
subprocess.run(['systemctl','--user','daemon-reload'],check=True,timeout=30)
result=dict(node=node,archive=str(retired),archived_files=len(before),archive_verified=True,new_incarnation='b79e65be1ea14e219016ec48ea02efef',trading_masked=blocked,started=False,live_admitted=False)
(base/'cutover-stage-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
