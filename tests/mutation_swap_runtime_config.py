import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

SOURCE='src/v2_swap_runtime_config.cpp'
SOURCES=(SOURCE,'src/v2_swap_signer_session.cpp','src/v2_swap_signer_service.cpp','src/v2_swap_signer_protocol.cpp','src/v2_swap_signing_stamp.cpp','src/v2_swap_submission_store.cpp','src/v2_gateway_link.cpp','src/v2_auth_envelope.cpp','src/v2_node_protocol.cpp','src/v2_session_fence.cpp')
TESTS={"runtime":"tests/v2_swap_runtime_config_tests.cpp"}
MUTATIONS=[('custody', 'a.custody.is_zero() || ', '', 'invalid', 'missing identity binding refuses'), ('uid', 'b.gateway_uid==b.signer_uid || ', '(b.gateway_uid==b.signer_uid && false) || ', 'invalid', 'shared operating system identity refuses'), ('caller', 'b.gateway_caller==b.signer_caller)', '(b.gateway_caller==b.signer_caller && false))', 'invalid', 'shared Raft caller refuses'), ('point', 'if(!public_key_valid(b.signer_public_key))', 'if(!public_key_valid(b.signer_public_key) && false)', 'invalid', 'invalid curve point refuses'), ('path_parent', ' || part==".."', '', 'invalid', 'noncanonical socket refuses before IO'), ('bind_session', 'h.update(b.account_session.session_evidence_digest.bytes);', '', 'identity', 'runtime identity binds every account and process pin'), ('bind_wallet', 'h.update(b.owner_wallet);', '', 'identity', 'runtime identity binds every account and process pin'), ('digest_pin', 'if(sha256_digest(std::as_bytes(std::span(result->bearer_.data(),size)))!=c.bearer_digest)', 'if(sha256_digest(std::as_bytes(std::span(result->bearer_.data(),size)))!=c.bearer_digest && false)', 'session', 'substituted account token refuses'), ('permissions', '(st.st_mode&07777)==0600 && ', '', 'session', 'readable-by-others session refuses'), ('readonly', '(flags&O_ACCMODE)==O_RDONLY && ', '', 'descriptors', 'writable inherited session refuses'), ('hardlinks', 'st.st_nlink==1 && ', '', 'descriptors', 'multiply linked inherited session refuses'), ('expiry', 'now>=config_.expires_at_ms', '(now>=config_.expires_at_ms && false)', 'session', 'expiry permanently erases bearer'), ('rollback', 'now<last_time_ || ', '(now<last_time_ && false) || ', 'session', 'clock rollback permanently erases bearer'), ('restriction', 'if(!swap_signer::restrict_process(c.binding.signer_uid,c.binding.gateway_uid))', 'if(!swap_signer::restrict_process(c.binding.signer_uid,c.binding.gateway_uid) && false)', 'order', 'wrong process identity refuses before key or socket access'), ('keyversion', ' || !c.key_version', '', 'descriptors', 'missing key version refuses'), ('state_timeout', ' || c.state_timeout>60000', '', 'descriptors', 'oversized state timeout refuses')]
def main():
    parser=argparse.ArgumentParser()
    for flag in ('root-build','raft-include','openssl-root','output-dir'):parser.add_argument('--'+flag,required=True,type=Path)
    a=parser.parse_args();root=Path(__file__).resolve().parents[1];out=a.output_dir.resolve()
    if out.exists():raise RuntimeError('fresh output directory required')
    out.mkdir(parents=True);copy=out/'source';pending=list(SOURCES)+list(TESTS.values())+['tests/mutation_swap_runtime_config.py'];snapshots={}
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
        binary=out/name;parts=[replacement[1] if replacement and s==replacement[0] else objects[s] for s in SOURCES]
        result=run(flags+parts+[objects[TESTS[kind]]]+tail+['-o',str(binary)],name+'.link');return binary,result
    binary,built=link('runtime','baseline')
    if built.returncode:raise RuntimeError(built.stderr)
    result=run([str(binary)],'baseline.run');report['baseline']={'exit':result.returncode,'stdout':result.stdout};save()
    if result.returncode or result.stderr or result.stdout.count('PASS ')!=7:raise RuntimeError('baseline failed '+result.stdout+result.stderr)
    for name,old,new,case,assertion in MUTATIONS:
        text=snapshots[SOURCE].decode()
        if text.count(old)!=1:raise RuntimeError('nonunique mutant '+name)
        changed=text.replace(old,new)
        if name=='hardlinks':
            if changed.count(' || after.st_nlink!=1')!=1:raise RuntimeError('hardlink recheck mutant missing')
            changed=changed.replace(' || after.st_nlink!=1','')
        mutated=out/(name+'.cpp');mutated.write_text(changed);obj=out/(name+'.o')
        built=run(flags+['-c',str(mutated),'-o',str(obj)],name+'.compile')
        if built.returncode:raise RuntimeError('mutation compile is not a kill: '+name+' '+built.stderr)
        binary,built=link('runtime',name,(SOURCE,str(obj)))
        if built.returncode:raise RuntimeError('mutation link is not a kill: '+name+' '+built.stderr)
        result=run([str(binary),case],name+'.run')
        killed=result.returncode==1 and (case+': '+assertion+'\n')==result.stderr and not result.stdout
        report['mutations'].append({'name':name,'assertion':assertion,'killed':killed,'exit':result.returncode});save()
        if not killed:raise RuntimeError('not named assertion kill '+name+' '+result.stdout+result.stderr)
        print('KILL '+name,flush=True)
    report['source_changed']=any((root/p).read_bytes()!=v for p,v in snapshots.items());save()
    if report['source_changed']:raise RuntimeError('source changed during run')
    print('PASS baseline seven groups and '+str(len(MUTATIONS))+' named mutants',flush=True)
if __name__=='__main__':main()
