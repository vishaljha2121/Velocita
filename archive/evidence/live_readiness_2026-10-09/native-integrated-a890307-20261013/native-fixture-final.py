"""Finish supplementary checks without repeating the unchanged native CTest suite."""
import hashlib,json,os,runpy,subprocess,sys,time,xml.etree.ElementTree as ET
from pathlib import Path
os.umask(0o077)
work=Path('/work');source=Path('/source')
base='1d2c820a18066b4616af6bf8cba65fccc32f6005'
commit='86f440806c6adc6f4c1dc9dc7a271e3d95293d76'
arch=sys.argv[1]
def git(*args):return subprocess.check_output(['git','-c','safe.directory=/source','-C',str(source),*args],text=True).strip()
assert git('rev-parse','HEAD')==commit and not git('status','--porcelain=v1','--untracked-files=all')
changed=['tests/test_tm_account_session.py','tests/v2_node_tests.cpp','tests/v2_swap_process_drill.cpp']
assert git('diff','--name-only',base,commit).splitlines()==changed
assert os.uname().machine==arch and os.getuid()!=0 and os.getuid()==os.geteuid()
assert list(os.sched_getaffinity(0))==[3]
xml=ET.parse(work/'ctest.xml').getroot()
assert len(xml.findall('.//testcase'))==201
failed={c.attrib['name'] for c in xml.findall('.//testcase') if c.find('failure') is not None}
assert failed <= {'v2-node-tests','v2-swap-process-drill'}
assert not xml.findall('.//error') and not xml.findall('.//skipped')
resources=json.loads((work/'tests-resources.json').read_text())
events=dict(line.split() for line in resources['memory.events'].splitlines())
assert events['oom_kill']=='0' and events['oom']=='0'
for name in ('home-fixture-86f4408','tmp-fixture-86f4408'):(work/name).mkdir(exist_ok=True)
os.environ.update(HOME='/work/home-fixture-86f4408',TMPDIR='/work/tmp-fixture-86f4408',PYTHONDONTWRITEBYTECODE='1',CCACHE_DIR='/ccache',CCACHE_MAXSIZE='512M',CCACHE_BASEDIR='/source',AERON_TERM_BUFFER_LENGTH='1048576')
def run(cmd,name,timeout):
 (work/(name+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
 (work/'status.json').write_text(json.dumps({'commit':commit,'arch':arch,'qualified':False,'stage':name}))
 print(time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),name,flush=True)
 with (work/(name+'.log')).open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 if r.returncode:raise RuntimeError(name+' exit='+str(r.returncode))
paths=sorted(p for p in (work/'build').iterdir() if p.is_file() and p.stat().st_mode & 0o111)
def hashes():return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
before=hashes()
run(['cmake','--build','/work/build','--parallel','1'],'fixture-incremental-build',600)
after=hashes()
assert before.keys()==after.keys()
changed_binaries={name for name in before if before[name]!=after[name]}
assert changed_binaries <= {'v2-node-tests','v2-swap-process-drill'},changed_binaries
run(['ctest','--test-dir','/work/build','--output-on-failure','--parallel','1','--repeat','until-fail:3','-R','^v2-(node-tests|swap-process-drill)$','--output-junit','/work/fixture-ctest.xml'],'fixture-tests',900)
assert '100% tests passed, 0 tests failed out of 2' in (work/'fixture-tests.log').read_text()
run(['python3','-B','-m','unittest','discover','-s','/source/tests','-p','test_tm_account*.py'],'account-tests-corrected',60)
run(['python3','-B','/source/tools/v2_schema_goldens.py','--check'],'final-golden-oracle',30)
run(['python3','-B','/source/tools/v2_schema_goldens.py','--check-literals','--schema','12','--replication-epoch','2'],'final-golden-literals',30)
v=runpy.run_path('/source/tests/test_swap_process_drill.py')['validate_evidence']
runs=list((work/'home-fixture-86f4408/.cache/velocita-build/process-drill-20261013').glob('run-*'))
assert len(runs)==3,'three bounded complete process-drill repetitions required'
for evidence in runs:
 for n in range(8):v(evidence,n)
 for path in evidence.glob('case-*/shutdown.jsonl'):
  for line in path.read_text().splitlines():
   row=json.loads(line)
   assert row['elapsed_ms']<=10000
   if row['elapsed_ms']>100:assert row['voter_turns']>0
assert not git('status','--porcelain=v1','--untracked-files=all')
result={'qualified':True,'commit':commit,'native_ctest_commit':base,'equivalence_changed_paths':changed,'changed_test_binaries':sorted(changed_binaries),'other_executable_hashes_unchanged':True,'baseline_failed_tests':sorted(failed),'affected_test_repetitions':3,'arch':arch,'ctest_passed':201,'account_tests_passed':44,'process_artifact_cases_validated':24,'scope':'native optional-runtime Release/offline tests; three test-fixture corrections validated selectively; 199 unchanged CTest cases retained and two affected cases each pass three repetitions; no deployed transport or production UID claim','affinity':[3],'uid':os.getuid(),'full_suite_resources':resources,'binary_sha256':after,'baseline_binary_sha256':before}
(work/'status.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('binary_sha256','baseline_binary_sha256')}),flush=True)
