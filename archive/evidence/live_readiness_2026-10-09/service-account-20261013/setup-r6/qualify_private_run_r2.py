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
old=Path.home() # not used for credentials
manifest=Path(S.PUBLIC)/'manifest.json'
raw=I.read(manifest);m=json.loads(raw)
assert raw==S.manifest_bytes(m) and m['launch_authorized'] is False
assert m['incarnation']=='b79e65be1ea14e219016ec48ea02efef'
assert m['source_commit']=='45db6a64a3dd14b0c4f3a94dd2e9706309c445d1'
assert all(m[k] is None for k in S.PENDING_ACCOUNT_PINS)
I.installed_bytes(str(manifest),raw)
rendered=S.render(m);updates=[]
line=b'TemporaryFileSystem=/run:rw,nodev,nosuid,mode=0755\n'
for role in ('gateway','signer'):
 p=Path('/etc/systemd/system')/('velocita-swap-'+role+'.service')
 new=rendered[p.name+'.review'].encode();assert new.count(line)==1
 before=new.replace(line,b'');I.installed_bytes(str(p),before)
 assert not os.path.lexists(str(p)+'.private-run.pending')
 updates.append((p,before,new))
# All state checks before first unit mutation; both versions remain dormant.
for p,before,new in updates:
 (base/(p.name+'.before')).write_bytes(before)
 d=P.open_directory(str(p.parent))
 try:
  R.new_file(d,p.name+'.private-run.pending',new,0o444)
  os.rename(p.name+'.private-run.pending',p.name,src_dir_fd=d,dst_dir_fd=d);os.fsync(d)
 finally:os.close(d)
 (base/(p.name+'.after')).write_bytes(new)
subprocess.run(['systemd-analyze','verify','--man=no',*[str(p) for p,_,_ in updates]],check=True,timeout=20)
subprocess.run(['systemctl','daemon-reload'],check=True,timeout=20)
A.collision_check(subprocess.run)
config=A.pins('e8b8fa9888d393cfdcab5a8beda3b2891c8ec8d9aa74e7025a5edee5857e0a58','8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f',m['incarnation'],'3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe','29b772a949627cd25c657a527ef2ea79a7b470454de9be9aa842462982b9d95e')
report=A.run(base/'read-result',config)
print(json.dumps(report,indent=2))
for role in ('signer','gateway'):
 p=base/'read-result'/(role+'.json')
 if p.exists(): print(role+': '+p.read_text())
raise SystemExit(0 if report['authenticated_voter_reads'] else 1)
