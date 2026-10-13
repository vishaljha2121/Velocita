import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


MUTATIONS = (
    ('key_prefix', '(value(public_key[0])!=2 && value(public_key[0])!=3)', 'false',
     'public_key', 'uncompressed_prefix_refused'),
    ('der_sequence_tag', 'value(bytes[0])!=0x30 || ', '',
     'malformed', 'bad_der_container_refused'),
    ('der_exact_length', 'value(bytes[1])!=bytes.size()-2', 'false',
     'malformed', 'bad_der_container_refused'),
    ('positive_scalar', 'if((value(digits.front())&0x80)!=0) return false;', '',
     'malformed', 'negative_scalar_refused'),
    ('canonical_leading_zero', 'if((value(digits[1])&0x80)==0) return false;', '',
     'malformed', 'redundant_scalar_zero_refused'),
    ('scalar_width', 'if(digits.size()>32) return false;', '',
     'malformed', 'oversized_scalar_refused'),
    ('nonzero_scalar', 'if(std::all_of(digits.begin(),digits.end(),[](auto b){return value(b)==0;})) return false;', '',
     'malformed', 'zero_scalar_refused'),
    ('scalar_curve_order', 'if(digits.size()==32 && ', 'if(false && digits.size()==32 && ',
     'malformed', 'curve_order_scalar_refused'),
    ('scheme_literal', 'SIGNATURE_SCHEME_TK_API_P256', 'SIGNATURE_SCHEME_TK_API_P255',
     'oracle', 'independent_stamp_bytes'),
    ('second_tail_character', 'if(count>1)destination.text_', 'if(count>2)destination.text_',
     'oracle', 'maximum_der_fits_375'),
    ('clear_old_stamp', 'destination={};', '',
     'clearing', 'refusal_clears_previous_stamp'),
)
SOURCES = ('src/v2_swap_signing_stamp.cpp', 'tests/v2_swap_signing_stamp_tests.cpp')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def closure(root):
    pending = list(SOURCES)
    result = {}
    while pending:
        rel = pending.pop()
        if rel in result:
            continue
        data = (root / rel).read_bytes()
        result[rel] = data
        for name in re.findall(r'^\s*#include\s+"([^"]+)"', data.decode(), re.MULTILINE):
            target = 'include/' + name
            if not (root / target).is_file():
                raise RuntimeError('unresolved local include: ' + name)
            pending.append(target)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    if output.exists():
        raise RuntimeError('output directory must be new')
    output.mkdir(parents=True)
    snapshots = closure(root)
    export = output / 'source'
    for rel, data in snapshots.items():
        destination = export / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    source = snapshots[SOURCES[0]].decode()
    flags = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
             '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(export / 'include')]
    asan = 'detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1'
    env = dict(os.environ, ASAN_OPTIONS=asan, UBSAN_OPTIONS='halt_on_error=1')
    report = {'source_sha256': {rel: sha(data) for rel, data in snapshots.items()},
              'compile_prefix': flags, 'asan_options': asan, 'baseline_passed': False, 'results': []}
    def record():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    def execute(command, name, timeout=90):
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=env)
        (output / (name + '.log')).write_text(result.stdout + result.stderr)
        return result
    objects = []
    for index, rel in enumerate(SOURCES[1:]):
        obj = output / ('dependency-' + str(index) + '.o')
        result = execute(flags + ['-c', str(export / rel), '-o', str(obj)], 'dependency-' + str(index))
        if result.returncode:
            raise RuntimeError('dependency compile failure is not test evidence')
        objects.append(str(obj))
    def build(source_file, name):
        binary = output / name
        result = execute(flags + [str(source_file), *objects, '-o', str(binary)], name + '.compile')
        return binary, result
    binary, compiled = build(export / SOURCES[0], 'baseline')
    if compiled.returncode:
        raise RuntimeError('baseline compile failure is not test evidence')
    baseline = execute([str(binary)], 'baseline.run', 30)
    expected = {f'PASS {name}' for name in ('oracle', 'public_key', 'malformed', 'clearing')}
    if baseline.returncode or set(baseline.stdout.splitlines()) != expected or baseline.stderr:
        record()
        raise RuntimeError('baseline did not pass all four ASan/UBSan groups cleanly')
    report['baseline_passed'] = True
    record()
    for name, old, new, case, assertion in MUTATIONS:
        if source.count(old) != 1:
            raise RuntimeError(name + ': mutation anchor must match exactly once')
        changed = output / (name + '.cpp')
        changed.write_text(source.replace(old, new, 1))
        binary, compiled = build(changed, name)
        if compiled.returncode:
            entry = {'mutation': name, 'killed': False, 'classification': 'compile_error'}
        else:
            result = execute([str(binary), case], name + '.run', 30)
            killed = result.returncode == 1 and result.stderr == f'FAIL {case}: {assertion}\n' and not result.stdout
            entry = {'mutation': name, 'case': case, 'named_assertion': assertion, 'exit': result.returncode,
                     'killed': killed, 'classification': 'named_assertion' if killed else
                     'survived' if result.returncode == 0 else 'wrong_failure',
                     'stdout': result.stdout, 'stderr': result.stderr}
        report['results'].append(entry)
        record()
    report['source_changed_during_run'] = any((root / rel).read_bytes() != data for rel, data in snapshots.items())
    record()
    killed = sum(item['killed'] for item in report['results'])
    print(f'ASan/UBSan baseline four groups passed; {killed}/{len(MUTATIONS)} named mutation kills')
    print(output / 'results.json')
    return 0 if killed == len(MUTATIONS) and not report['source_changed_during_run'] else 1


if __name__ == '__main__':
    sys.exit(main())
