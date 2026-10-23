import hashlib,json,os,subprocess,sys,time,shutil
from pathlib import Path
os.umask(0o077)
source=Path('/source');work=Path('/work');commit=sys.argv[1];arch=sys.argv[2]
def run(cmd,name,timeout):
 (work/(name+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
 print(time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),name,flush=True)
 with (work/(name+'.log')).open('w') as log:
  r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
 (work/(name+'-resources.json')).write_text(json.dumps({n:Path('/sys/fs/cgroup',n).read_text() for n in ('memory.events','memory.peak','memory.current') if Path('/sys/fs/cgroup',n).exists()},indent=2))
 if r.returncode:raise RuntimeError(name+' exit='+str(r.returncode))
def git(*args):return subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),*args],text=True).strip()
assert git('rev-parse','HEAD')==commit and not git('status','--porcelain=v1','--untracked-files=all')
assert os.uname().machine==arch
assert os.getuid() != 0 and os.getuid() == os.geteuid(), 'non-root qualification identity required'
assert list(os.sched_getaffinity(0))==[3]
assert int(Path('/sys/fs/cgroup/memory.max').read_text())<=2147483648
assert shutil.disk_usage(work).free>3_000_000_000
(work/'status.json').write_text(json.dumps({'commit':commit,'arch':arch,'qualified':False,'stage':'build'}))
for p in ('tmp-1d2c820','home-1d2c820','deps'): (work/p).mkdir(exist_ok=True)
os.environ.update(HOME='/work/home-1d2c820',TMPDIR='/work/tmp-1d2c820',PYTHONDONTWRITEBYTECODE='1',CCACHE_DIR='/ccache',CCACHE_MAXSIZE='512M',CCACHE_BASEDIR='/source',AERON_TERM_BUFFER_LENGTH='1048576')
# Copy only pinned public dependencies because CMake applies its recorded Raft patch.
for name in ('aeron','willemt-raft'):
 assert (work/'deps'/name).is_dir(), 'missing original pinned dependency'
# Reuse only this isolated build's original pinned and CMake-patched dependencies.
run(['cmake','-S','/source','-B','/work/build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER_LAUNCHER=ccache','-DCMAKE_CXX_COMPILER_LAUNCHER=ccache','-DFETCHCONTENT_SOURCE_DIR_AERON=/work/deps/aeron','-DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT=/work/deps/willemt-raft','-DFETCHCONTENT_FULLY_DISCONNECTED=ON','-DVELOCITA_BUILD_SWAP_RUNTIME=ON','-DVELOCITA_TEST_HTTPS=ON','-DVELOCITA_TEST_SWAP_HTTPS=ON','-DVELOCITA_TEST_SWAP_GATEWAY_TLS=ON','-DVELOCITA_TEST_SWAP_SIGNER=ON'], 'configure',120)
# Watch available disk while compiling; stop rather than prune or affect services.
p=subprocess.Popen(['cmake','--build','/work/build','--parallel','1'],stdout=(work/'build.log').open('w'),stderr=subprocess.STDOUT)
start=time.monotonic()
while p.poll() is None:
 if shutil.disk_usage(work).free<2_500_000_000 or time.monotonic()-start>10800:
  p.terminate();p.wait(timeout=30);raise RuntimeError('capacity/time guard stopped build')
 time.sleep(5)
if p.returncode:raise RuntimeError('build exit='+str(p.returncode))
run(['ctest','--test-dir','/work/build','--output-on-failure','--parallel','1','-R','^v2-(compaction-persistence-model-tests|generation-base-tests|sol-limit-probe-offline|https-read-local-fixtures|jupiter-idl-reader-offline|owner-settlement-reader-offline|swap-signer-service-tests|swap-runtime-config-tests|swap-runtime-production-tests|swap-runtime-https-fixtures|swap-process-drill|node-process-tests)$'], 'regression-preflight',600)
run(['ctest','--test-dir','/work/build','--output-on-failure','--parallel','1','--output-junit','/work/ctest.xml'], 'tests',3600)
run(['python3','-B','/source/tools/v2_schema_goldens.py','--check'],'golden-oracle',30)
run(['python3','-B','/source/tools/v2_schema_goldens.py','--check-literals','--schema','12','--replication-epoch','2'],'golden-literals',30)
run(['python3','-B','-m','unittest','discover','-s','/source/tests','-p','test_tm_account*.py'],'account-tests',60)
# Independently check the eight process artifacts, including strict numeric types.
import runpy
v=runpy.run_path('/source/tests/test_swap_process_drill.py')['validate_evidence']
runs=list((work/'home-1d2c820/.cache/velocita-build/process-drill-20261013').glob('run-*'))
assert len(runs)==2, "preflight and full-suite process evidence required"
for evidence in runs:
 for n in range(8):v(evidence,n)
assert git('rev-parse','HEAD')==commit and not git('status','--porcelain=v1','--untracked-files=all')
paths=['velocita-v2-node','velocita-v2-gateway','v2-swap-process-drill']
result={'qualified':True,'commit':commit,'arch':arch,'scope':'native complete optional-runtime Release build and registered offline tests; no deployed transport or production UID claim','affinity':[3],'uid':os.getuid(),'memory_limit_bytes':int(Path('/sys/fs/cgroup/memory.max').read_text()),'aeron_term_buffer_bytes':1048576,'binary_sha256':{p:hashlib.sha256((work/'build'/p).read_bytes()).hexdigest() for p in paths}}
(work/'status.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
