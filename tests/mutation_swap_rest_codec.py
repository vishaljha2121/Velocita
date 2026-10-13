import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


MUTATIONS = (
    ('sell_decimal_scale', 'body.quantity(input.quantity_atoms,input.side==Side::buy?6:9)', 'body.quantity(input.quantity_atoms,6)',
     'exact_create_requests', 'sell uses exact base atoms without price or limit conversion'),
    ('money_range', 'input.quantity_atoms<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())', 'true',
     'create_refusals_clear_output', 'invalid input clears stale executable request'),
    ('created_http_status', 'if(http!=201)', 'if(http!=201 && false)',
     'quote_and_payload_refusals', 'non201 HTTP status refuses with empty output'),
    ('initialized_lifecycle', 'if(json_member_string(document,"status")!="initialized")', 'if(false)',
     'quote_and_payload_refusals', 'noninitialized lifecycle refuses with empty output'),
    ('outer_issues', '!no_issues(document) || ', '',
     'quote_and_payload_refusals', 'outer warning refuses with empty output'),
    ('quote_issues', '!no_issues(*quote)', 'false',
     'quote_and_payload_refusals', 'quote warning refuses with empty output'),
    ('base_mint', 'json_member_string(*quote,"base_asset")!=kSwapSolMint || ', '',
     'asset_and_amount_bounds', 'wrong base mint refused'),
    ('quote_mint', 'json_member_string(*quote,"quote_asset")!=kSwapUsdcMint ||', '',
     'asset_and_amount_bounds', 'wrong quote mint refused'),
    ('fee_mint', 'json_member_string(*quote,"fee_asset")!=kSwapUsdcMint', 'false',
     'quote_and_payload_refusals', 'wrong fee mint refuses with empty output'),
    ('quoted_input_match', '*qty!=input.quantity_atoms || ', '',
     'quote_and_payload_refusals', 'quoted input mismatch refuses with empty output'),
    ('output_nonzero', '*receive==0 || ', '',
     'asset_and_amount_bounds', 'zero output refused'),
    ('single_payload', '*count!=1', '*count==0',
     'payload_cardinality_and_capacity', 'multiple signing payloads refused'),
    ('payload_hash_match', 'value.payload_digest!=*claimed', 'false',
     'quote_and_payload_refusals', 'payload hash mismatch refuses with empty output'),
    ('nested_payload_object', 'if(validate_json_object(entries[0])!=JsonError::none)return SwapRestCode::malformed;', '',
     'nested_payload_ambiguity', 'duplicate nested payload digest refuses before usable bytes'),
    ('payload_capacity', 'count==value.payload.size()', 'count+1==value.payload.size()',
     'payload_cardinality_and_capacity', 'exact payload capacity is retained without truncation'),
    ('nonempty_stamp', 'if(stamp.empty())return SwapRestCode::invalid_input;', '',
     'exact_execute_and_stamp_gate', 'empty stamp cannot become execute body'),
)
SOURCES = ('src/v2_swap_rest_codec.cpp', 'src/v2_swap_signing_stamp.cpp',
           'src/v2_truemarkets_wire.cpp', 'src/v2_digest.cpp', 'tests/v2_swap_rest_codec_tests.cpp')


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
    expected = {f'PASS {name}' for name in ('exact_create_requests', 'create_refusals_clear_output', 'preserve_original_payload',
                        'quote_and_payload_refusals', 'unsafe_handle_and_malformed_json',
                        'exact_execute_and_stamp_gate', 'nested_payload_ambiguity',
                        'asset_and_amount_bounds', 'payload_cardinality_and_capacity')}
    if baseline.returncode or set(baseline.stdout.splitlines()) != expected or baseline.stderr:
        record()
        raise RuntimeError('baseline did not pass all nine ASan/UBSan groups cleanly')
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
            killed = result.returncode == 1 and result.stdout == f'FAIL {case}: {assertion}\n' and not result.stderr
            entry = {'mutation': name, 'case': case, 'named_assertion': assertion, 'exit': result.returncode,
                     'killed': killed, 'classification': 'named_assertion' if killed else
                     'survived' if result.returncode == 0 else 'wrong_failure',
                     'stdout': result.stdout, 'stderr': result.stderr}
        report['results'].append(entry)
        record()
    report['source_changed_during_run'] = any((root / rel).read_bytes() != data for rel, data in snapshots.items())
    record()
    killed = sum(item['killed'] for item in report['results'])
    print(f'ASan/UBSan baseline nine groups passed; {killed}/{len(MUTATIONS)} named mutation kills')
    print(output / 'results.json')
    return 0 if killed == len(MUTATIONS) and not report['source_changed_during_run'] else 1


if __name__ == '__main__':
    sys.exit(main())
