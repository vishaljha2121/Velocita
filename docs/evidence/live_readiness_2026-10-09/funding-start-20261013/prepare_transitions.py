"""Prepare pinned dormant-refresh, first-start and recovery-only-flag bundles offline."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import time

ROOT=Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('transition',ROOT/'deploy/v2/swap-runtime/review_transition.py')
T=importlib.util.module_from_spec(spec);spec.loader.exec_module(T)

def write(path,raw):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'wb') as stream:stream.write(raw);stream.flush();os.fsync(stream.fileno())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous',type=Path,required=True);parser.add_argument('--prepared',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();os.umask(0o077)
    old=json.loads((args.previous/'manifest.json').read_bytes())
    previous={name:(args.previous/name).read_bytes() for name in T.A.DOCUMENTS}
    current={name:(args.prepared/'opening'/name).read_bytes() for name in T.A.DOCUMENTS}
    refreshed=dict(old,**{key:T.digest(current[name]) for name,key in T.A.DOCUMENTS.items() if key})
    armed=dict(refreshed,launch_authorized=True,initialize_swap_recovery=True)
    sealed=dict(armed,initialize_swap_recovery=False)
    plans=[('refresh',old,refreshed,previous,current),('arm',refreshed,armed,current,current),('seal',armed,sealed,current,current)]
    for mode,before,after,old_files,new_files in plans:T.validate(mode,before,after,old_files,new_files,time.time_ns()//1000000)
    args.output.mkdir(mode=0o700,exist_ok=False);result={}
    for mode,before,after,old_files,new_files in plans:
        dest=args.output/mode;dest.mkdir(mode=0o700)
        files={'old-manifest.json':T.P.manifest_bytes(before),'new-manifest.json':T.P.manifest_bytes(after)}
        for prefix,mapping in [('old-',old_files),('new-',new_files)]:files.update({prefix+k:v for k,v in mapping.items()})
        for name,raw in files.items():write(dest/name,raw)
        manifest={'schema':1,'mode':mode,'files':{name:T.digest(raw) for name,raw in files.items()}}
        raw=(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n').encode();write(dest/'transition.json',raw)
        result[mode]={'bundle_sha256':T.digest(raw),'manifest_sha256':T.digest(T.P.manifest_bytes(after))}
    write(args.output/'summary.json',(json.dumps(result,indent=2)+'\n').encode());print(json.dumps(result,indent=2))

if __name__=='__main__':main()
