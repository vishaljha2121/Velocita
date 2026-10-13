#!/usr/bin/env python3
"""Unprivileged preparation only; no running containers or live credentials."""
import hashlib, json, os, shutil, stat, subprocess, sys
from pathlib import Path
IMAGE='076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f'
BASE=Path.home()/'.cache/velocita-build/swap-services-20261013-r4'
QUAL=Path.home()/'.cache/velocita-build/integrated-a890307-20261013/qualification'
SOURCE=QUAL.parent/'source'

def cmd(args, **kw):
    return subprocess.run(args, check=True, text=True, **kw)

def stage():
    if os.getuid()==0: raise ValueError('run as deployment user, never host root')
    BASE.mkdir(mode=0o700, parents=True, exist_ok=True)
    cid=cmd(['podman','create','--network','none','--entrypoint','/bin/false',IMAGE],capture_output=True).stdout.strip()
    (BASE/'stopped-container-id').write_text(cid+'\n')
    # The created container is never started. Mount/copy happen in its rootless
    # user namespace; no root extraction or host runtime library replacement.
    cmd(['podman','unshare','python3','-B',str(Path(__file__).resolve()),'copy',cid,str(BASE)])
    cmd(['podman','rm',cid],capture_output=True)

def copy(cid, base):
    base=Path(base); dest=base/'rootfs-stage'; dest.mkdir(mode=0o755)
    mount=Path(cmd(['podman','mount',cid],capture_output=True).stdout.strip())
    try:
        for root, dirs, files in os.walk(mount, followlinks=False):
            relative=Path(root).relative_to(mount); out=dest/relative
            for name in dirs+files:
                source=Path(root)/name; target=out/name; meta=source.lstat()
                if stat.S_ISLNK(meta.st_mode): target.symlink_to(os.readlink(source))
                elif stat.S_ISDIR(meta.st_mode): target.mkdir(mode=0o755)
                elif stat.S_ISREG(meta.st_mode):
                    shutil.copyfile(source,target,follow_symlinks=False)
                    target.chmod(0o755 if meta.st_mode&0o111 else 0o644)
                else: raise ValueError('special file in pinned image: '+str(source.relative_to(mount)))
        (base/'runtime-source.json').write_text(json.dumps({'image_sha256':IMAGE,'container':cid,'normalized_modes':True,'credentials_added':False},indent=2)+'\n')
    finally: cmd(['podman','unmount',cid],capture_output=True)

if __name__=='__main__':
    if len(sys.argv)==4 and sys.argv[1]=='copy': copy(sys.argv[2],sys.argv[3])
    elif len(sys.argv)==1: stage()
    else: raise SystemExit('unexpected arguments')
