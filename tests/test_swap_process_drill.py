"""Run isolated named mutations against the linked production process drill.

Explicit CLI only; this is not a test that alters the working tree. Linked objects
are snapshotted before compiling changed production sources in the output folder.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def validate_evidence(evidence, case):
    directory = Path(evidence) / ('case-' + str(case))
    result = json.loads((directory / 'result.json').read_text())
    settled = case in (1, 2, 3, 6)
    expected = {
        'case': case,
        'passed': True,
        'cash_atoms': 4000000 if settled else 5000000,
        'base_atoms': 9000000 if settled else 0,
        'reserved_quote_atoms': 0 if settled else 1000000,
        'fee_atoms': 2000 if settled else 0,
        'purchase_atoms': 1000000 if settled else 0,
        'settlements': 1 if settled else 0,
        'pending_phase': 0 if settled else (4 if case == 0 else 2),
        'creates': 1,
        'signatures': 0 if case == 4 else 1,
        'executes': 1 if settled else 0,
    }
    if set(result) != set(expected):
        raise ValueError('result fields differ from reviewed schema')
    for field, value in expected.items():
        if type(result[field]) is not type(value) or result[field] != value:
            raise ValueError('wrong result type/value: ' + field)
    transcript = (directory / 'venue.log').read_text().splitlines()
    for field, line in (('creates', 'create'), ('signatures', 'signature'), ('executes', 'execute')):
        if transcript.count(line) != expected[field]:
            raise ValueError('transcript disagrees with independent count: ' + field)
    processes = [json.loads(line) for line in (directory / 'processes.jsonl').read_text().splitlines()]
    if len(processes) < 4 or len({record['pid'] for record in processes}) != len(processes):
        raise ValueError('distinct gateway/signer process evidence missing')
    if {record['role'] for record in processes} != {'gateway', 'signer'}:
        raise ValueError('missing process roles')
    for record in processes:
        if any(type(record[key]) is not int for key in ('pid', 'epoch', 'signal', 'exit')):
            raise ValueError('process integer types invalid')
        if record['pid'] <= 0 or record['epoch'] <= 0 or (record['signal'], record['exit']) not in ((0, 0), (9, -1)):
            raise ValueError('unexpected process termination')
    killed_role = 'signer' if case == 7 else 'gateway'
    if not any(record['role'] == killed_role and record['signal'] == 9 for record in processes):
        raise ValueError('missing actual SIGKILL evidence')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--verify-evidence', type=Path)
    parser.add_argument('--expect-open-f07', action='store_true', help='Validate historical pre-fix evidence only; do not count F07 as passing')
    args = parser.parse_args()
    cases = (0, 1, 2, 3, 4, 5, 7) if args.expect_open_f07 else tuple(range(8))
    if args.verify_evidence:
        for case in cases:
            validate_evidence(args.verify_evidence, case)
        print(f'PASS {len(cases)}/{len(cases)} JSON, type, economic, transcript and process-evidence checks')
        return
    if not args.build or not args.out:
        parser.error('--build and --out are required for mutation verification')
    root = Path(__file__).resolve().parents[1]
    build, out = args.build.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    target = build / 'CMakeFiles/v2-swap-process-drill.dir'
    link = shlex.split((target / 'link.txt').read_text())
    flags = (target / 'flags.make').read_text()
    definitions = shlex.split(re.search(r'^CXX_DEFINES = (.*)$', flags, re.M)[1])
    includes = shlex.split(re.search(r'^CXX_INCLUDES = (.*)$', flags, re.M)[1])
    shutil.copytree(root / 'include', out / 'include')
    includes = ['-I' + str(out / 'include') if value == '-I' + str(root / 'include') else value for value in includes]
    objects, hashes = {}, {}
    for index, argument in enumerate(link):
        if argument.endswith(('.o', '.a', '.dylib')):
            source = Path(argument)
            if not source.is_absolute():
                source = build / source
            copied = out / ('dependency-' + str(index) + source.suffix)
            shutil.copyfile(source, copied)
            hashes[str(source)] = hashlib.sha256(copied.read_bytes()).hexdigest()
            link[index] = str(copied)
            objects[source.name] = index
    report = {'linked_inputs': hashes, 'instrumentation': 'Release objects; isolated changed production source', 'cases': []}

    def command(argv, name):
        result = subprocess.run(argv, text=True, capture_output=True, timeout=150)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        return result

    def compile_run(name, source_name, before=None, after=None, case=None):
        original = (root / 'src' / source_name).read_text()
        body = original
        if before is not None:
            if body.count(before) != (2 if name == 'unsafe_resume' else 1):
                raise RuntimeError('nonunique mutation ' + name)
            body = body.replace(before, after)
        source = out / (name + '.cpp')
        source.write_text(body)
        obj = out / (name + '.o')
        result = command([link[0], '-std=c++20', '-O2', *definitions, *includes, '-c', str(source), '-o', str(obj)], name + '.compile')
        if result.returncode:
            raise RuntimeError(result.stderr)
        mutated_link = link.copy()
        if source_name == 'v2_node.cpp':
            mutated_link.insert(objects['libvelocita_v2_node.a'], str(obj))
        else:
            mutated_link[objects[source_name + '.o']] = str(obj)
        binary = out / name
        mutated_link[mutated_link.index('-o') + 1] = str(binary)
        result = command(mutated_link, name + '.link')
        if result.returncode:
            raise RuntimeError(result.stderr)
        evidence = Path.home() / '.cache/velocita-build' / ('pd-' + str(os.getpid()) + '-' + name)
        arguments = [str(binary), str(evidence)] if case is None else [str(binary), '--case', str(case), str(evidence)]
        result = command(arguments, name + '.run')
        if (root / 'src' / source_name).read_text() != original:
            raise RuntimeError('source changed concurrently: ' + source_name)
        return result, str(evidence)

    baseline, evidence = compile_run('baseline', 'v2_swap_gateway_runtime.cpp', case=0)
    report['baseline'] = [{'case': 0, 'exit': baseline.returncode, 'evidence': evidence}]
    if baseline.returncode or baseline.stdout.count('PASS F') != 1:
        raise RuntimeError('baseline case 0 failed: ' + baseline.stdout + baseline.stderr)
    report['baseline'][0]['validated_result'] = validate_evidence(evidence, 0)
    if args.expect_open_f07:
        report['open_failure'] = {'case': 6, 'drill': 'F07', 'reason': 'historical pre-fix current-term commitment gap'}
    for case in cases[1:]:
        evidence = Path.home() / '.cache/velocita-build' / ('pd-' + str(os.getpid()) + '-base-' + str(case))
        baseline = command([str(out / 'baseline'), '--case', str(case), str(evidence)], 'baseline-' + str(case) + '.run')
        report['baseline'].append({'case': case, 'exit': baseline.returncode, 'evidence': str(evidence)})
        if baseline.returncode or baseline.stdout.count('PASS F') != 1:
            raise RuntimeError('baseline failed: ' + baseline.stdout + baseline.stderr)
        report['baseline'][-1]['validated_result'] = validate_evidence(evidence, case)
    mutants = [
        ('stale_term_read', 'v2_node.cpp',
         'const auto readiness = kernel_->ensure_current_term_applied();',
         'const auto readiness = ProposeStatus::admitted;',
         6, 'unknown restart blocks'),
        ('unsafe_resume', 'v2_swap_gateway_runtime.cpp',
         'if(config_.recovery_only &&',
         'if(false &&',
         5, 'recovery-only restart blocks pre-Attempt execution'),
        ('skip_settle', 'v2_swap_gateway_runtime.cpp',
         'command(settled,now,event);return;',
         'stop(SwapGatewayRuntimeStatus::complete,SwapGatewayRuntimeCause::none);return;',
         1, 'finalized restart resolves exactly once'),
    ]
    for name, source, before, after, case, assertion in mutants:
        result, evidence = compile_run(name, source, before, after, case)
        killed = result.returncode == 1 and ('FAIL ' + assertion) in result.stderr
        report['cases'].append({'name': name, 'assertion': assertion, 'named_kill': killed, 'exit': result.returncode, 'evidence': evidence})
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    if not all(case['named_kill'] for case in report['cases']):
        raise RuntimeError('designated assertion did not kill every mutant')
    print(f'PASS {len(mutants)}/{len(mutants)} named production process mutations')


if __name__ == '__main__':
    main()
