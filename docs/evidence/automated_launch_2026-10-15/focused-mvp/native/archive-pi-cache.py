"""Reversibly archive inactive diagnostic scratch and excluded test objects."""
from pathlib import Path
import fcntl, hashlib, json, os, shutil, subprocess, tarfile, time
os.umask(0o077)
os.sched_setaffinity(0,{3})
os.nice(10)
h=Path.home(); old=h/'.cache/velocita-build/integrated-a890307-20261013'
assert os.getuid()!=0 and os.uname().machine=='aarch64'
lock=(old/'bounded-qualification.lock').open('a'); fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
before=shutil.disk_usage(h).free
out=h/'.cache/velocita-build/pi-archive-focused-20261015-r2'
out.mkdir(exist_ok=False)
keep=('v2-swap-', 'v2-truemarkets-', 'v2-jupiter-', 'velocita', 'willemt', 'v2-operational-recovery-tests','v2-state-snapshot-tests','v2-kernel-codec-tests','v2-kernel-control-tests','v2-model-replay-tests')
roots=[h/'velocita-release-a073998/diagnostic', old/'qualification/tmp']
roots += sorted(p for p in (old/'qualification/build/CMakeFiles').iterdir() if p.is_dir() and p.name.endswith('.dir') and not p.name.startswith(keep))
for root in roots:
 assert root.is_dir() and not root.is_symlink() and root.resolve().is_relative_to(h)
running=subprocess.check_output(['podman','ps','-q'],text=True).split()
for cid in running:
 info=json.loads(subprocess.check_output(['podman','inspect',cid],text=True))[0]
 assert 'qualification' not in info['Name']
 for m in info.get('Mounts',[]):
  if info['Name'].lstrip('/')=='velocita-v2-node-exporter' and m['Source']=='/' and not m['RW']:
   continue  # Standard read-only host-metrics root mount is not a build-cache user.
  p=Path(m['Source']).resolve()
  assert not any(r.is_relative_to(p) or p.is_relative_to(r) for r in roots)
files=[]
for root in roots:
 for p in root.rglob('*'):
  assert not p.is_symlink(),str(p)
  if p.is_file():
   assert p.stat().st_nlink==1
   files.append(p)
manifest={str(p.relative_to(h)):dict(size=p.stat().st_size,sha256=hashlib.file_digest(p.open('rb'),'sha256').hexdigest()) for p in files}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
a=out/'inactive-artifacts.tar.gz'
with tarfile.open(a,'x:gz',compresslevel=1) as t:
 for p in files: t.add(p,arcname=str(p.relative_to(h)),recursive=False)
with tarfile.open(a,'r:gz') as t:
 assert set(t.getnames())==set(manifest)
 for member in t:
  expected=manifest[member.name]
  assert member.isfile() and member.size==expected['size']
  assert hashlib.file_digest(t.extractfile(member),'sha256').hexdigest()==expected['sha256']
# Check original bytes again before removing only the archived regular files.
for p in files:
 assert not p.is_symlink() and p.stat().st_nlink==1
 assert hashlib.file_digest(p.open('rb'),'sha256').hexdigest()==manifest[str(p.relative_to(h))]['sha256']
with a.open('rb') as f: os.fsync(f.fileno())
for p in files:p.unlink()
result=dict(archive=str(a),archive_sha256=hashlib.file_digest(a.open('rb'),'sha256').hexdigest(),files=len(files),original_bytes=sum(x['size'] for x in manifest.values()),archive_bytes=a.stat().st_size,free_before=before,free_after=shutil.disk_usage(h).free,verified=True,production_state_changed=False,restore='Extract archive relative to original home only after reviewing existing paths; do not overwrite newer artifacts.')
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
