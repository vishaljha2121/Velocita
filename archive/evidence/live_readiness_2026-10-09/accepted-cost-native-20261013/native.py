"""Offline native qualification of the accepted-cost checkpoint; no venue access."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

os.umask(0o077)
source, work, evidence = Path('/source'), Path('/work'), Path('/evidence')
commit, arch = sys.argv[1:]

def git(*args):
    return subprocess.check_output(['git', '-c', 'safe.directory=/source', '-C', str(source), *args], text=True).strip()

def status(stage, **fields):
    (evidence/'status.json').write_text(json.dumps(dict(commit=commit, arch=arch,
        qualified=False, stage=stage, **fields), indent=2)+'\n')

def run(argv, name, timeout):
    status(name)
    (evidence/(name+'-command.json')).write_text(json.dumps(argv, indent=2)+'\n')
    print(time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), name, flush=True)
    with (evidence/(name+'.log')).open('x') as log:
        process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        start = time.monotonic()
        while process.poll() is None:
            if shutil.disk_usage(work).free < 1_800_000_000 or time.monotonic()-start > timeout:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                raise RuntimeError(name+' capacity/time guard stopped process group')
            time.sleep(2)
        if process.returncode:
            raise RuntimeError(name+' exit='+str(process.returncode))
    resources = {n: Path('/sys/fs/cgroup', n).read_text() for n in
                 ('memory.events', 'memory.peak', 'memory.current') if Path('/sys/fs/cgroup', n).exists()}
    (evidence/(name+'-resources.json')).write_text(json.dumps(resources, indent=2)+'\n')
    events = dict(line.split() for line in resources.get('memory.events', '').splitlines())
    assert events.get('oom_kill', '0') == '0' and events.get('oom', '0') == '0'

try:
    assert git('rev-parse', 'HEAD') == commit
    assert not git('status', '--porcelain=v1', '--untracked-files=all')
    assert os.uname().machine == arch and os.getuid() != 0 and os.getuid() == os.geteuid()
    assert list(os.sched_getaffinity(0)) == [3]
    assert int(Path('/sys/fs/cgroup/memory.max').read_text()) == 2147483648
    assert shutil.disk_usage(work).free > 2_500_000_000
    for name in ('aeron', 'willemt-raft'):
        assert (work/'deps'/name).is_dir()
    for name in ('home', 'tmp'):
        (evidence/name).mkdir()
    os.environ.update(HOME='/evidence/home', TMPDIR='/evidence/tmp', PYTHONDONTWRITEBYTECODE='1',
                      CCACHE_DIR='/ccache', CCACHE_MAXSIZE='512M', CCACHE_BASEDIR='/source',
                      AERON_TERM_BUFFER_LENGTH='1048576')
    run(['cmake', '-S', '/source', '-B', '/work/build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
         '-DFETCHCONTENT_SOURCE_DIR_AERON=/work/deps/aeron',
         '-DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT=/work/deps/willemt-raft',
         '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '-DVELOCITA_BUILD_SWAP_RUNTIME=ON',
         '-DVELOCITA_TEST_HTTPS=ON', '-DVELOCITA_TEST_SWAP_HTTPS=ON',
         '-DVELOCITA_TEST_SWAP_GATEWAY_TLS=ON', '-DVELOCITA_TEST_SWAP_SIGNER=ON'], 'configure', 120)
    run(['cmake', '--build', '/work/build', '--parallel', '1'], 'build', 10800)
    selection = '^v2-(swap-|truemarkets-|operational-recovery-tests$)'
    run(['ctest', '--test-dir', '/work/build', '--show-only=json-v1', '-R', selection], 'test-inventory', 30)
    inventory = json.loads((evidence/'test-inventory.log').read_text())
    names = {test['name'] for test in inventory['tests']}
    assert {'v2-swap-process-drill', 'v2-swap-cost-policy-tests', 'v2-swap-settlement-admission-tests',
            'v2-swap-runtime-production-tests', 'v2-swap-runtime-https-fixtures',
            'v2-swap-submission-gateway-drill', 'v2-swap-paper-process-drill'} <= names
    run(['ctest', '--test-dir', '/work/build', '--output-on-failure', '--stop-on-failure',
         '--parallel', '1', '-R', selection, '--output-junit', '/evidence/ctest.xml'], 'tests', 1800)
    cases = ET.parse(evidence/'ctest.xml').getroot().findall('.//testcase')
    assert len(cases) == len(names) and not any(c.find('failure') is not None or
        c.find('error') is not None or c.find('skipped') is not None for c in cases)
    run(['python3', '-B', '-m', 'unittest', 'discover', '-s', '/source/tests', '-p',
         'test_tm_account*.py'], 'account-tests', 60)
    run(['python3', '-B', '/source/tools/v2_schema_goldens.py', '--check'], 'golden-oracle', 30)
    run(['python3', '-B', '/source/tools/v2_schema_goldens.py', '--check-literals', '--schema', '12',
         '--replication-epoch', '2'], 'golden-literals', 30)
    validate = runpy.run_path('/source/tests/test_swap_process_drill.py')['validate_evidence']
    runs = list((evidence/'home/.cache/velocita-build/process-drill-20261013').glob('run-*'))
    assert len(runs) == 1
    for case in range(8):
        validate(runs[0], case)
    assert git('rev-parse', 'HEAD') == commit and not git('status', '--porcelain=v1', '--untracked-files=all')
    binaries = ['velocita-v2-node', 'velocita-v2-gateway', 'velocita-v2-signer', 'v2-swap-process-drill']
    result = dict(commit=commit, arch=arch, qualified=True, live_admitted=False,
                  scope='complete Release build; selected swap/venue/recovery suite and eight fixture process drills',
                  ctest_passed=len(cases), process_cases_validated=8, affinity=[3], uid=os.getuid(),
                  binary_sha256={name: hashlib.sha256((work/'build'/name).read_bytes()).hexdigest() for name in binaries})
    (evidence/'status.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result), flush=True)
except Exception as error:
    status('failed', error=str(error))
    raise
