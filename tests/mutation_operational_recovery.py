import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

WAL='src/v2_wal_arena.cpp'
ARCHIVE='src/v2_archive_store.cpp'
SOURCES=(WAL,ARCHIVE)
TESTS={'recovery':'tests/v2_operational_recovery_tests.cpp','faults':'tests/v2_archive_publication_fault_tests.cpp'}
MUTATIONS=[
(WAL,'partial_tail_skipped','for (std::uint64_t offset = 0; offset < size; offset += kWalBlockBytes)', 'for (std::uint64_t offset = 0; offset + kWalBlockBytes <= size; offset += kWalBlockBytes)', 'recovery','partial_tail','nonzero partial WAL tail is never silently discarded'),
(WAL,'zero_gap_skipped','if (zero) { padding = true; continue; }','if (zero) { padding = true; break; }','recovery','zero_gap','nonzero WAL block after zero padding is never silently discarded'),
(ARCHIVE,'skip_quarantine','if (recover[i] && (!resync_name(s->directory, recovery_temps[i]) ||','if (false && recover[i] && (!resync_name(s->directory, recovery_temps[i]) ||','recovery','archive_temporaries','quarantine preserves exact file identity'),
(ARCHIVE,'private_temporary','good_file_stat(info) && info.st_size >= 0 &&','S_ISREG(info.st_mode) && info.st_size >= 0 &&','recovery','archive_bad_file','nonprivate or aliased temporary refuses quarantine'),
]

def main():
    parser=argparse.ArgumentParser()
    for flag in ('root-build','raft-include','openssl-root','output-dir'):parser.add_argument('--'+flag,required=True,type=Path)
    a=parser.parse_args();root=Path(__file__).resolve().parents[1];out=a.output_dir.resolve()
    if out.exists():raise RuntimeError('fresh output directory required')
    out.mkdir(parents=True);copy=out/'source';pending=list(SOURCES)+list(TESTS.values())+['tests/mutation_operational_recovery.py'];snapshots={}
    while pending:
        rel=pending.pop()
        if rel in snapshots:continue
        data=(root/rel).read_bytes();snapshots[rel]=data;dest=copy/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        for name in re.findall(r'^\s*#include\s+"([^"]+)"',data.decode(),re.MULTILINE):
            candidates=[Path(rel).parent/name,Path('include')/name]
            found=next((p for p in candidates if(root/p).is_file()),None)
            if found is None:
                if (a.raft_include.resolve()/name).is_file():continue
                raise RuntimeError('unresolved quoted include '+name)
            pending.append(str(found))
    external=out/'raft';external.mkdir()
    external_hash={}
    for p in a.raft_include.resolve().rglob('*.h'):
        rel=p.relative_to(a.raft_include.resolve());dest=external/rel;dest.parent.mkdir(parents=True,exist_ok=True);data=p.read_bytes();dest.write_bytes(data);external_hash[str(rel)]=hashlib.sha256(data).hexdigest()
    libs=[];libhash={};libdir=out/'lib';libdir.mkdir()
    for name in ('velocita_v2_node','velocita_v2_replication','velocita_v2_kernel','velocita_v2_evaluator','velocita_core','willemt_raft','velocita_v2_contracts'):
        p=a.root_build.resolve()/('lib'+name+'.a');data=p.read_bytes();dest=libdir/p.name;dest.write_bytes(data);libs.append(str(dest));libhash[str(p)]=hashlib.sha256(data).hexdigest()
    flags=['c++','-std=c++20','-Wall','-Wextra','-Werror','-g','-O1','-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(copy/'include'),'-I'+str(copy/'tests'),'-I'+str(external),'-I'+str(a.openssl_root.resolve()/'include')]
    fixture=[];tail=libs+['-L'+str(a.openssl_root.resolve()/'lib'),'-lcrypto','-pthread']
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'}
    report={'source_sha256':{s:hashlib.sha256(b).hexdigest() for s,b in snapshots.items()},'libraries':libhash,'raft_headers':external_hash,'baselines':{},'mutations':[]}
    def save():(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(cmd,name):
        result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=120);(out/(name+'.log')).write_text(result.stdout+result.stderr);return result
    objects={}
    for n,rel in enumerate(SOURCES+tuple(TESTS.values())):
        obj=out/f'object-{n}.o';result=run(flags+fixture+['-c',str(copy/rel),'-o',str(obj)],f'object-{n}')
        if result.returncode:raise RuntimeError('compile failure: '+rel+' '+result.stderr)
        objects[rel]=str(obj)
    def link(kind,name,replacement=None):
        binary=out/name;parts=[replacement[1] if replacement and s==replacement[0] else objects[s] for s in SOURCES if not(kind=='faults' and s==ARCHIVE)]
        result=run(flags+parts+[objects[TESTS[kind]]]+tail+['-o',str(binary)],name+'.link');return binary,result
    for kind,count in (('recovery',9),('faults',None)):
        binary,built=link(kind,kind)
        if built.returncode:raise RuntimeError(built.stderr)
        result=run([str(binary)],kind+'.baseline');report['baselines'][kind]={'exit':result.returncode,'stdout':result.stdout};save()
        if result.returncode or result.stderr or (count is not None and result.stdout.count('PASS ')!=count):raise RuntimeError('baseline failed '+kind+' '+result.stdout+result.stderr)
    for rel,name,old,new,kind,case,assertion in MUTATIONS:
        text=snapshots[rel].decode()
        if text.count(old)!=1:raise RuntimeError('nonunique mutant '+name)
        changed=out/(name+'.cpp');changed.write_text(text.replace(old,new,1));obj=out/(name+'.o')
        compiled=run(flags+fixture+['-c',str(changed),'-o',str(obj)],name+'.compile')
        if compiled.returncode:raise RuntimeError('mutant compilation failure is not a kill '+name+' '+compiled.stderr)
        binary,built=link(kind,name,(rel,str(obj)))
        if built.returncode:raise RuntimeError('mutant link failure '+name+' '+built.stderr)
        result=run([str(binary),case],name+'.run');killed=result.returncode==1 and f'FAIL {case}: {assertion}\n' in result.stdout and not result.stderr
        report['mutations'].append({'name':name,'killed':killed,'stdout':result.stdout,'stderr':result.stderr,'exit':result.returncode});save()
        if not killed:raise RuntimeError('not designated assertion '+name+' '+result.stdout+result.stderr)
    report['source_changed']=any((root/p).read_bytes()!=b for p,b in snapshots.items());save()
    print(f'9 operational recovery groups plus publication/quarantine fault suite; {len(MUTATIONS)}/{len(MUTATIONS)} named mutation kills; source_changed={report["source_changed"]}')
    return int(report['source_changed'])

if __name__=='__main__':raise SystemExit(main())
