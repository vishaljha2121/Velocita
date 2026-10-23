from pathlib import Path
import subprocess,json,os
base=Path.home()/'.cache/velocita-build/swap-services-20261013-r5'
args=['podman','run','--rm','--pull=never','--network=none','--cpuset-cpus=3','--memory=256m','--pids-limit=32','--userns=keep-id','--user',str(os.getuid())+':'+str(os.getgid()),'-v',str(base)+':/probe:ro','--entrypoint','/probe/authenticated-read-probe','076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f','--check-config']
r=subprocess.run(args,capture_output=True,text=True,timeout=30)
passed=r.returncode==1 and r.stderr.strip()=='AUTHENTICATED_READ_REFUSED mode and every pin required'
(base/'native-smoke.json').write_text(json.dumps(dict(command=args,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,native_loader_and_fail_closed_parser=passed,authenticated_voter_reads=False,live_admitted=False),indent=2)+'\n')
raise SystemExit(0 if passed else 1)
