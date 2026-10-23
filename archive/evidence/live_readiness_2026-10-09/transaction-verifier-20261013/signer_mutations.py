"""Named signer integration mutations; never edit repository source in place."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parents[4]
    source=(root/'src/v2_swap_signer_session.cpp').read_text()
    target=args.build/'CMakeFiles/v2-swap-signer-verified-tests.dir'
    flags={line.split(' = ',1)[0]:shlex.split(line.split(' = ',1)[1]) for line in (target/'flags.make').read_text().splitlines() if ' = ' in line}
    command=shlex.split((target/'link.txt').read_text())
    mutations=[
        ('baseline',None,None,None,None),
        ('one_buy_scope','if(!swap_mvp_buy_scope(swap,v))return refuse(SwapSignerStatus::refused);','/* mutation: omit MVP scope */','one_buy_scope','mvp_refuses_broader_runtime_purchase_budget'),
        ('proof_deadline','(verification_expires_at_ms_ && event>=verification_expires_at_ms_)','false','freshness_and_authority','expired_verified_proof_cannot_sign_after_authority'),
        ('reviewed_context','swap_signer_verification_digest(*verification_)!=config_.transaction_policy_digest','false','economic_and_context_refusal','changed_lookup_context_cannot_reuse_journal'),
        ('recovery_deadline','if(!verified_expiry)return false;','if(false)return false;','freshness_and_authority','expired_payload_cannot_recover_stamp'),
        ('journal_policy','std::copy(c.transaction_policy_digest.bytes.begin(),c.transaction_policy_digest.bytes.end(),bytes.begin()+233);','/* mutation: omit policy identity */','economic_and_context_refusal','journal_binds_changed_reviewed_policy'),
    ]
    results=[]
    for name,old,new,case,assertion in mutations:
        text=source
        if old:
            if text.count(old)!=1:raise RuntimeError('mutation target is not unique: '+name)
            text=text.replace(old,new,1)
        src=args.output/(name+'.cpp');obj=args.output/(name+'.o');binary=args.output/name
        src.write_text(text)
        compile_cmd=[command[0],*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],'-c',str(src),'-o',str(obj)]
        built=subprocess.run(compile_cmd,cwd=args.build,capture_output=True,text=True)
        if built.returncode:raise RuntimeError('compile failure is not a mutation kill: '+built.stderr)
        link=[str(obj) if x.endswith('/src/v2_swap_signer_session.cpp.o') else x for x in command]
        link[link.index('-o')+1]=str(binary)
        linked=subprocess.run(link,cwd=args.build,capture_output=True,text=True)
        if linked.returncode:raise RuntimeError('link failure is not a mutation kill: '+linked.stderr)
        run=subprocess.run([str(binary)]+([case] if case else []),capture_output=True,text=True)
        passed=run.returncode==0 if assertion is None else run.returncode==1 and ('FAIL '+case+': '+assertion) in run.stdout
        results.append(dict(name=name,case=case,assertion=assertion,exit=run.returncode,passed=passed,stdout=run.stdout,stderr=run.stderr))
    (args.output/'results.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'results':results},indent=2)+'\n')
    print(json.dumps({'passed':all(x['passed'] for x in results),'cases':len(results)}))
    return 0 if all(x['passed'] for x in results) else 1

if __name__=='__main__':raise SystemExit(main())
