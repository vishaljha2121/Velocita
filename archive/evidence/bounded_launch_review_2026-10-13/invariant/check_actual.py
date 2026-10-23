"""Fresh local objects + retained private capture + C++/Python document parity.

No credentials, network, signatures or deployment. The historical diagnostic
constructs a synthetic pending intent; it is not live Raft authorization.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
BISON = Path(__file__).parents[1]/'bison'
spec = importlib.util.spec_from_file_location('bison_build', BISON/'check_actual.py')
B = importlib.util.module_from_spec(spec); spec.loader.exec_module(B)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--old-policy',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args = p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    inputs = [ROOT/'src'/x for x in B.SOURCES]+list((ROOT/'include/velocita/v2').glob('*.hpp'))
    inputs += [BISON/'actual_quote_verifier.cpp',BISON/'actual_document_parity.cpp',Path(__file__)]
    inputs += [ROOT/'deploy/v2/swap-runtime'/n for n in ('review_transition.py','idle_policy_renewal.py')]
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {str(path.relative_to(ROOT)):digest(path) for path in inputs}
    flags = ['c++','-std=c++20','-O2','-ffunction-sections','-fdata-sections',
             '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128','-DVELOCITA_SWAP_RUNTIME_WIRING=1',
             '-I'+str(ROOT/'include'),'-I'+str(Path.home()/'.cache/velocita-deps/willemt-raft-cp076/include'),
             '-isystem','/opt/homebrew/opt/openssl@3/include']
    def compile_one(src):
        obj = args.output/(src+'.o'); cmd = flags+['-c',str(ROOT/'src'/src),'-o',str(obj)]
        r = subprocess.run(cmd,capture_output=True,text=True)
        (args.output/(src+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode: raise RuntimeError('compile failed '+src)
        return str(obj),cmd
    with ThreadPoolExecutor(max_workers=2) as pool:
        built = list(pool.map(compile_one,B.SOURCES))
    objects = [v[0] for v in built]; commands = [v[1] for v in built]
    for name in ('actual_quote_verifier','actual_document_parity'):
        cmd = flags+['-Wl,-dead_strip',str(BISON/(name+'.cpp'))]+objects+[
            '/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib','-o',str(args.output/name)]
        commands.append(cmd); r = subprocess.run(cmd,capture_output=True,text=True)
        (args.output/(name+'.build.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('link failed '+name)
    if before != {str(path.relative_to(ROOT)):digest(path) for path in inputs}:
        raise RuntimeError('source changed during build; reject evidence')
    (args.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    def invoke(name, paths):
        r = subprocess.run([str(args.output/name)]+list(map(str,paths)),capture_output=True,text=True)
        if r.returncode not in (0,2):raise RuntimeError(name+': '+r.stderr)
        return r.returncode,B.fields(r.stdout.encode())
    profile = args.candidate/'runtime.profile'; review = args.candidate/'transaction.review'
    envelope = args.candidate/'offline-envelope.json'
    code,verified = invoke('actual_quote_verifier',[profile,review,envelope])
    assert code==0 and verified['native_route_kind']=='18','actual Invariant admitted'
    assert verified['verified_net_floor_lamports']=='6910173','independent Invariant floor'
    assert verified['verified_debit_usdc_atoms']=='1000000' and verified['verified_fee_usdc_atoms']=='2000'
    assert verified['exact_floor_verification_code']=='12' and verified['floor_plus_one_verification_code']=='11'
    oldcode,old = invoke('actual_quote_verifier',[args.old_policy/'runtime.profile',args.old_policy/'transaction.review',envelope])
    assert oldcode==2 and old['verified']=='0','installed policy refuses different route'
    code,parity = invoke('actual_document_parity',[profile,review]); assert code==0
    sys.path.insert(0,str(ROOT/'deploy/v2/swap-runtime'))
    import review_transition as R
    import idle_policy_renewal as N
    files = {'runtime.profile':profile.read_bytes(),'transaction.review':review.read_bytes()}
    bindings = R.journal_bindings(files)
    assert bindings[3].hex()==parity['signing'],'signing binding parity'
    assert bindings[4].hex()==parity['protected'],'protected binding parity'
    assert N.runtime_identity(B.fields(files['runtime.profile']))==parity['runtime'],'runtime identity parity'
    result = dict(synthetic_intent=True,historical_clock=True,actual_raft_authority=False,
                  live_admitted=False,signed=False,broadcast=False,network=False,
                  project_libraries_reused=False,source_unchanged_during_build=True,
                  actual_invariant=verified,old_policy_refusal=old,native_python_parity=parity,
                  original_price_cap_cleared=6910173>=6819647,
                  source_sha256=before)
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':main()
