#!/usr/bin/env python3
"""Owner-approved Raft-only custody/session preparation, no venue credentials."""
from pathlib import Path
import hashlib,json,os,stat,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import provision_services as P
import install_runtime as R
import fd_handoff as F
import service_profile as S
BASE=Path(__file__).resolve().parent
ROOTFS='/opt/velocita/runtimes/076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f/rootfs'
HELPERS={'authenticated-read-probe':'e8b8fa9888d393cfdcab5a8beda3b2891c8ec8d9aa74e7025a5edee5857e0a58','read-setup-probe':'da1824752c707646c46bb732a5e1c42ca645fdeaf405b1008917ef8193ebe406'}
P.require_host_root()
from authenticated_read_run import collision_check
collision_check(subprocess.run)
raws={}
for name,sha in HELPERS.items():
 raw=(BASE/name).read_bytes();assert raw.startswith(b'\x7fELF') and hashlib.sha256(raw).hexdigest()==sha
 assert not os.path.lexists('/opt/velocita/qualification/'+sha)
 raws[name]=raw
for role in ('gateway','signer'):
 assert not os.path.lexists('/var/lib/velocita-swap-'+role+'/client.session')
 assert not os.path.lexists('/run/velocita-swap-credentials/'+role+'/caller.key')
# Existing dormant identity is replaced only after checking exact previous public bytes.
manifest_path=Path(S.PUBLIC)/'manifest.json'
fd=F.open_pinned_public(str(manifest_path),'b3008f1e3bac9004ff6d0c8bd72d0fdba385c8d93e48159bfdb13a35bc97bb56')
old=json.loads(os.read(fd,16384));os.close(fd);assert old['launch_authorized'] is False
old_units=S.render(old)
for role in ('gateway','signer'):
 path=Path('/etc/systemd/system')/('velocita-swap-'+role+'.service')
 assert path.read_text()==old_units[path.name+'.review']
new=dict(old,incarnation='b79e65be1ea14e219016ec48ea02efef');rendered=S.render(new)
# Complete both-role custody and session-init preflight before first mutation.
assert Path(ROOTFS+'/etc/velocita-runtime-image').read_text()=='076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f\n'
for role,key,uid in [('gateway','caller-13',29001),('signer','caller-14',29002)]:
 fd=F._open_components('/run/user/1003/velocita-secrets/'+key+'.key',1003)
 try:
  meta=os.fstat(fd);assert stat.S_ISREG(meta.st_mode) and meta.st_uid==1003 and stat.S_IMODE(meta.st_mode)==0o600 and meta.st_nlink==1 and meta.st_size==32
 finally:os.close(fd)
 dest=Path('/run/velocita-swap-credentials')/role;meta=dest.lstat()
 assert stat.S_ISDIR(meta.st_mode) and (meta.st_uid,meta.st_gid,stat.S_IMODE(meta.st_mode))==(uid,uid,0o700)
 assert subprocess.check_output(['findmnt','-n','-o','FSTYPE','--target',str(dest)],text=True).strip()=='tmpfs'
 assert subprocess.check_output(['systemctl','show','velocita-read-init-'+role+'.service','-p','LoadState','--value'],text=True).strip()=='not-found'
for path in [manifest_path]+[Path('/etc/systemd/system')/('velocita-swap-'+r+'.service') for r in ('gateway','signer')]:
 assert not os.path.lexists(str(path)+'.read-cutover')
# Install immutable helper bytes, preserving every old release.
parent=P.open_directory('/opt/velocita/qualification')
try:
 for name,sha in HELPERS.items():
  os.mkdir(sha,0o700,dir_fd=parent)
  d=os.open(sha,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=parent)
  try:R.new_file(d,name,raws[name],0o555);os.fchmod(d,0o755);os.fsync(d)
  finally:os.close(d)
 os.fsync(parent)
finally:os.close(parent)
# Copy only current Raft authentication material directly from tmpfs to tmpfs.
for role,key,uid in [('gateway','caller-13',29001),('signer','caller-14',29002)]:
 source='/run/user/1003/velocita-secrets/'+key+'.key'
 fd=F._open_components(source,1003)
 try:
  meta=os.fstat(fd);assert stat.S_ISREG(meta.st_mode) and meta.st_uid==1003 and stat.S_IMODE(meta.st_mode)==0o600 and meta.st_nlink==1
  raw=os.read(fd,33);assert len(raw)==32
 finally:os.close(fd)
 dest=Path('/run/velocita-swap-credentials')/role;meta=dest.lstat()
 assert stat.S_ISDIR(meta.st_mode) and (meta.st_uid,meta.st_gid,stat.S_IMODE(meta.st_mode))==(uid,uid,0o700)
 assert subprocess.check_output(['findmnt','-n','-o','FSTYPE','--target',str(dest)],text=True).strip()=='tmpfs'
 d=os.open(dest,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
 try:
  fd=os.open('caller.key',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=d)
  try:R.write_all(fd,raw);os.fchown(fd,uid,uid);os.fchmod(fd,0o600);os.fsync(fd)
  finally:os.close(fd)
 finally:os.close(d)
 del raw
 sha=HELPERS['read-setup-probe'];exe='/opt/velocita/qualification/'+sha+'/read-setup-probe'
 cmd=['systemd-run','--quiet','--wait','--pipe','--collect','--unit=velocita-read-init-'+role,
  '--property=User='+str(uid),'--property=Group='+str(uid),'--property=UMask=0077',
  '--property=RootDirectory='+ROOTFS,'--property=MountAPIVFS=yes','--property=BindReadOnlyPaths='+exe+':/opt/read-setup-probe',
  '--property=StateDirectory=velocita-swap-'+role,'--property=StateDirectoryMode=0700',
  '--property=ProtectSystem=strict','--property=ProtectHome=yes','--property=PrivateNetwork=yes',
  '--property=NoNewPrivileges=yes','--property=CapabilityBoundingSet=','--property=AmbientCapabilities=',
  '--property=RuntimeMaxSec=15','--property=TimeoutStopSec=2','--property=LimitCORE=0',
  '/opt/read-setup-probe','--create-session','/var/lib/velocita-swap-'+role+'/client.session']
 subprocess.run(cmd,check=True,timeout=20)
# Atomic per-file update, not a multi-file transaction. All old/new states refuse activation.
for path,raw in [(manifest_path,rendered['manifest.json'].encode())]+[(Path('/etc/systemd/system')/('velocita-swap-'+r+'.service'),rendered['velocita-swap-'+r+'.service.review'].encode()) for r in ('gateway','signer')]:
 d=P.open_directory(str(path.parent));temp=path.name+'.read-cutover'
 try:R.new_file(d,temp,raw);os.rename(temp,path.name,src_dir_fd=d,dst_dir_fd=d);os.fsync(d)
 finally:os.close(d)
subprocess.run(['systemctl','daemon-reload'],check=True,timeout=20)
print(json.dumps(dict(prepared=True,raft_credentials_only=True,live_admitted=False,new_incarnation=new['incarnation'])))
