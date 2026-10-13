"""Finish supplementary checks without repeating the unchanged native CTest suite."""
import hashlib,json,os,runpy,subprocess,sys,time,xml.etree.ElementTree as ET
from pathlib import Path
os.umask(0o077)
work=Path('/work');source=Path('/source')
base='1d2c820a18066b4616af6bf8cba65fccc32f6005'
commit='04c991dbbab225ca564ea18eeb03627f83a6d2b6'
arch=sys.argv[1]
def git(*args):return subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),*args],text=True).strip()
assert git('rev-parse','HEAD')==commit and not git('status','--porcelain=v1','--untracked-files=all')
assert git('diff','--name-only',base,commit)=='tests/test_tm_account_session.py'
assert os.uname().machine==arch and os.getuid()!=0 and os.getuid()==os.geteuid()
assert list(os.sched_getaffinity(0))==[3]
xml=ET.parse(work/'ctest.xml').getroot()
assert len(xml.findall('.//testcase'))==201
assert not xml.findall('.//failure') and not xml.findall('.//error') and not xml.findall('.//skipped')
assert '100% tests passed, 0 tests failed out of 201' in (work/'tests.log').read_text()
resources=json.loads((work/'tests-resources.json').read_text())
events=dict(line.split() for line in resources['memory.events'].splitlines())
assert events['oom_kill']=='0' and events['oom']=='0'
for name in ('home-final-04c991d','tmp-final-04c991d'):(work/name).mkdir(exist_ok=True)
os.environ.update(HOME='/work/home-final-04c991d',TMPDIR='/work/tmp-final-04c991d',PYTHONDONTWRITEBYTECODE='1',CCACHE_DIR='/ccache',CCACHE_MAXSIZE='512M',CCACHE_BASEDIR='/source',AERON_TERM_BUFFER_LENGTH='1048576')
def run(cmd,name,timeout):
 (work/(name+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
 (work/'status.json').write_text(json.dumps({'commit':commit,'arch':arch,'qualified':False,'stage':name}))
 print(time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),name,flush=True)
 with (work/(name+'.log')).open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 if r.returncode:raise RuntimeError(name+' exit='+str(r.returncode))
paths=sorted(p for p in (work/'build').iterdir() if p.is_file() and p.stat().st_mode & 0o111)
def hashes():return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
before=hashes()
run(['cmake','--build','/work/build','--parallel','1'],'final-incremental-build',180)
assert hashes()==before,'an executable changed; full native qualification must be repeated'
run(['python3','-B','-m','unittest','discover','-s','/source/tests','-p','test_tm_account*.py'],'account-tests-corrected',60)
run(['python3','-B','/source/tools/v2_schema_goldens.py','--check'],'final-golden-oracle',30)
run(['python3','-B','/source/tools/v2_schema_goldens.py','--check-literals','--schema','12','--replication-epoch','2'],'final-golden-literals',30)
v=runpy.run_path('/source/tests/test_swap_process_drill.py')['validate_evidence']
runs=[]
for h in ('home-1d2c820','home-maintenance-1d2c820'):
 found=list((work/h/'.cache/velocita-build/process-drill-20261013').glob('run-*'))
 assert len(found)==1,'exactly one successful process drill per retained stage required'
 runs+=found
for evidence in runs:
 for n in range(8):v(evidence,n)
assert not git('status','--porcelain=v1','--untracked-files=all')
result={'qualified':True,'commit':commit,'native_ctest_commit':base,'equivalence_changed_paths':['tests/test_tm_account_session.py'],'executable_hashes_unchanged':True,'arch':arch,'ctest_passed':201,'account_tests_passed':44,'process_artifact_cases_validated':16,'scope':'native optional-runtime Release/offline tests; Python scratch-only correction checked without repeating unchanged native tests; no deployed transport or production UID claim','affinity':[3],'uid':os.getuid(),'full_suite_resources':resources,'binary_sha256':before}
(work/'status.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='binary_sha256'}),flush=True)
