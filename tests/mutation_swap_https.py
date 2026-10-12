import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


MUTATIONS = [
    ('hostname', 'set(CURLOPT_SSL_VERIFYHOST, 2L);', 'set(CURLOPT_SSL_VERIFYHOST, 0L);',
     'test_wrong_hostname', 'hostname_verification_required'),
    ('peer', 'set(CURLOPT_SSL_VERIFYPEER, 1L);', 'set(CURLOPT_SSL_VERIFYPEER, 0L);',
     'test_untrusted_and_expired_certificates', 'peer_verification_required'),
    ('redirect', 'set(CURLOPT_FOLLOWLOCATION, 0L);\n    set(CURLOPT_MAXREDIRS, 0L);',
     'set(CURLOPT_FOLLOWLOCATION, 1L);\n    set(CURLOPT_MAXREDIRS, 1L);',
     'test_redirect_never_resends_post', 'redirect_must_be_refused'),
    ('body_capacity', 'n > s.limit - s.result.body_size', 'n > kSwapHttpsBodyCapacity - s.result.body_size',
     'test_body_and_header_bounds', 'oversized_response_refused'),
    ('header_capacity', 'n > 8192 - s.headers', 'n > 16384 - s.headers',
     'test_body_and_header_bounds', 'oversized_response_refused'),
    ('deadline', 'set(CURLOPT_TIMEOUT_MS, config_.timeout_ms);', 'set(CURLOPT_TIMEOUT_MS, 30000L);',
     'test_ambiguous_timeout_and_lost_ack_never_retry', 'timeout_is_ambiguous'),
    ('exposure', 'result.exposure = SwapHttpsExposure::possibly_sent;',
     'result.exposure = SwapHttpsExposure::not_attempted;',
     'test_ambiguous_timeout_and_lost_ack_never_retry', 'timeout_is_ambiguous'),
    ('retry', 'const auto code = curl_easy_perform(handle.value);',
     'auto code = curl_easy_perform(handle.value); if (code != CURLE_OK) code = curl_easy_perform(handle.value);',
     'test_ambiguous_timeout_and_lost_ack_never_retry', 'timeout_must_not_retry'),
    ('truthful_agent', '"velocita-swap-fixture/1"', '"Mozilla/5.0"',
     'test_get_and_post_exact_owned_bytes', 'request_contract_changed'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scratch', type=Path, default=Path.home() / '.cache/velocita-build/abc-transport-20261012')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_file = root / 'src/v2_swap_https.cpp'
    source = source_file.read_text()
    work = args.scratch / f'mutations-{time.time_ns()}'
    work.mkdir(parents=True, exist_ok=False)
    report = {'source_sha256': hashlib.sha256(source_file.read_bytes()).hexdigest(), 'mutations': []}
    for name, old, new, case, assertion in [('baseline','','','','')] + MUTATIONS:
        if old and source.count(old) != 1:
            raise RuntimeError(f'{name}: mutation site not unique')
        unit = work / f'{name}.cpp'
        unit.write_text(source.replace(old, new, 1) if old else source)
        binary = work / name
        built = subprocess.run(['c++','-std=c++20','-O1','-Wall','-Wextra','-Werror','-pedantic',
                                '-DVELOCITA_SWAP_HTTPS_TEST_ONLY=1','-I',str(root/'include'),str(unit),
                                str(root/'tests/v2_swap_https_probe.cpp'),'-lcurl','-o',str(binary)],
                               capture_output=True,text=True,timeout=60,check=False)
        (work/f'{name}-build.log').write_text(built.stdout+built.stderr)
        if built.returncode:
            raise RuntimeError(f'{name}: build failure is not a mutation kill')
        env = os.environ.copy()
        env['VELOCITA_SWAP_HTTPS_PROBE'] = str(binary)
        command = ['python3','-B',str(root/'tests/test_swap_https.py')]
        if case:
            command.append(f'SwapHttpsTests.{case}')
        result = subprocess.run(command,env=env,capture_output=True,text=True,timeout=20,check=False)
        output = result.stdout+result.stderr
        (work/f'{name}-run.log').write_text(output)
        if name == 'baseline':
            if result.returncode or '\nOK\n' not in output:
                raise RuntimeError('baseline must pass')
            report['baseline_passed'] = True
        else:
            killed = result.returncode == 1 and 'AssertionError:' in output and assertion in output and '\nERROR:' not in output
            report['mutations'].append({'name':name,'test':case,'assertion':assertion,'killed_by_named_assertion':killed})
            if not killed:
                (work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
                raise RuntimeError(f'{name}: expected named assertion did not fail')
    (work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(MUTATIONS)} named-assertion mutation kills; evidence: {work / "results.json"}')


if __name__ == '__main__':
    main()
