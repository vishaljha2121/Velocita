import hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import authenticated_read_run as A
import service_profile as S
import provision_services as P
import install_runtime as R
import install_disabled as I
P.require_host_root();A.collision_check(subprocess.run)
base=Path(__file__).resolve().parent
manifest=Path(S.PUBLIC)/'manifest.json'
raw=I.read(manifest);m=json.loads(raw)
assert raw==S.manifest_bytes(m) and m['launch_authorized'] is False
assert m['incarnation']=='b79e65be1ea14e219016ec48ea02efef'
assert m['source_commit']=='45db6a64a3dd14b0c4f3a94dd2e9706309c445d1'
assert all(m[k] is None for k in S.PENDING_ACCOUNT_PINS)
I.installed_bytes(str(manifest),raw)
rendered=S.render(m);updates=[]
old_line=b'TemporaryFileSystem=/run:rw,nodev,nosuid,mode=0755\n'
namespace=Path('/etc/velocita-swap/runtime-roots')
assert not os.path.lexists(namespace)
for role in ('gateway','signer'):
 p=Path('/etc/systemd/system')/('velocita-swap-'+role+'.service')
 new=rendered[p.name+'.review'].encode()
 line=('BindReadOnlyPaths=/etc/velocita-swap/runtime-roots/'+role+':/run\n').encode()
 assert new.count(line)==1
 before=new.replace(line,old_line);I.installed_bytes(str(p),before)
 assert not os.path.lexists(str(p)+'.runtime-root.pending')
 updates.append((p,before,new))
# All checks precede mutation. Parent-only disk directories; runtime child binds stay tmpfs.
I.mkdir(str(namespace))
for role in ('gateway','signer'): I.mkdir(str(namespace/role))
for p,before,new in updates:
 (base/(p.name+'.before')).write_bytes(before)
 d=P.open_directory(str(p.parent))
 try:
  R.new_file(d,p.name+'.runtime-root.pending',new,0o444)
  os.rename(p.name+'.runtime-root.pending',p.name,src_dir_fd=d,dst_dir_fd=d);os.fsync(d)
 finally:os.close(d)
 (base/(p.name+'.after')).write_bytes(new)
subprocess.run(['systemd-analyze','verify','--man=no',*[str(p) for p,_,_ in updates]],check=True,timeout=20)
subprocess.run(['systemctl','daemon-reload'],check=True,timeout=20)
A.collision_check(subprocess.run)
print(json.dumps(dict(runtime_roots_installed=True,units_dormant=True,live_admitted=False)))
