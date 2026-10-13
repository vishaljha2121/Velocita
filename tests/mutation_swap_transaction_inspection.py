import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


MUTATIONS = (
    ('audited_ceiling', "*factor+9'999)/10'000;", "*factor)/10'000;",
     'oracle', 'hand_worked_payment_and_nominal_ceiling'),
    ('quote_fee_accounting', 'p.intent.max_quote_debit.value()-p.intent.quote_input.value()!=p.intent.fee_cap.value() ||', '',
     'profile', 'one_atom_unaccounted_debit_refused'),
    ('signed_money_range', 'p.intent.max_quote_debit.value()>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||', '',
     'profile', 'debit_above_signed_money_range_refused'),
    ('sell_base_in_buy', 'p.intent.base_input.value()!=0 || ', '',
     'profile', 'buy_with_sell_base_input_refused'),
    ('sell_quote_in_buy', 'p.intent.min_quote_receive.value()!=0', 'false',
     'profile', 'buy_with_sell_quote_output_refused'),
    ('base_mint', 'p.account_keys[26]!=wrapped_sol', '(p.account_keys[26]!=wrapped_sol && false)',
     'identity', 'wrong_base_mint_refused'),
    ('quote_mint', 'p.account_keys[29]!=usdc', '(p.account_keys[29]!=usdc && false)',
     'identity', 'wrong_quote_mint_refused'),
    ('account_identity', 'r.accounts[i].key!=p.account_keys[i]', '(r.accounts[i].key!=p.account_keys[i] && false)',
     'identity', 'owner_identity_pinned'),
    ('readonly_owner', ' || r.accounts[i].writable!=(i<8 || (i>=15 && i<26))', '',
     'topology', 'readonly_owner_refused'),
    ('close_sequence', '!account_indices(d,4,close)', '(!account_indices(d,4,close) && false)',
     'topology', 'close_destination_pinned'),
    ('remaining_sequence', '17,15,16,2,7,11,11,27,31,10', '17,15,16,2,6,11,11,27,31,10',
     'topology', 'remaining_accounts_exact'),
    ('route_connectivity', 'route.steps[0]!=JupiterRouteStep{JupiterSwapKind::deriverse,0,100,0,1,1}',
     '(route.steps[0]!=JupiterRouteStep{JupiterSwapKind::deriverse,0,100,0,1,1} && false)',
     'bounds', 'route_connectivity_exact'),
    ('input_atom', 'route.input_atoms!=p.intent.quote_input.value() || ', '',
     'bounds', 'one_input_atom_mismatch'),
    ('slippage_cap', 'route.slippage_bps>p.max_slippage_bps || ', '',
     'bounds', 'slippage_cap_enforced'),
    ('platform_fee_cap', 'route.platform_fee_bps>p.max_platform_fee_bps', 'false',
     'bounds', 'platform_fee_field_cap_enforced'),
    ('provenance_blockers', 'chain_validity_unproven|lookup_provenance_unproven;', '0;',
     'oracle', 'structure_never_implies_signing_authority'),
    ('auxiliary_cost', 'if(result.explicit_owner_payment>0)', 'if(false)',
     'oracle', 'structure_never_implies_signing_authority'),
    ('nominal_shortfall', 'if(result.nominal_output_after_slippage<p.intent.min_base_receive.value())', 'if(false)',
     'oracle', 'structure_never_implies_signing_authority'),
    ('envelope_age', 'p.now_ms-static_cast<std::uint64_t>(d.timestamp_ms)>p.max_envelope_age_ms', 'false',
     'identity', 'expired_envelope_refused'),
    ('signature_present', ' || d.nonzero_signatures!=0', '',
     'identity', 'preexisting_signature_not_blindly_trusted'),
    ('compute_price', 'micros>p.max_compute_price ||', '',
     'bounds', 'compute_price_cap_enforced'),
)
SOURCES = ('src/v2_swap_transaction_inspection.cpp', 'src/v2_solana_payload_view.cpp',
           'src/v2_solana_resolved_accounts.cpp', 'src/v2_solana_instruction_decode.cpp',
           'src/v2_jupiter_route_decode.cpp', 'src/v2_truemarkets_wire.cpp', 'src/v2_digest.cpp',
           'tests/v2_swap_transaction_inspection_tests.cpp')


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
             '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-DVELOCITA_SWAP_INSPECTION_TEST_ONLY=1',
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
    expected = {f'PASS {name}' for name in ('oracle', 'profile', 'identity', 'topology', 'bounds', 'shape')}
    if baseline.returncode or set(baseline.stdout.splitlines()) != expected or baseline.stderr:
        record()
        raise RuntimeError('baseline did not pass all six ASan/UBSan groups cleanly')
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
    print(f'ASan/UBSan baseline six groups passed; {killed}/{len(MUTATIONS)} named mutation kills')
    print(output / 'results.json')
    return 0 if killed == len(MUTATIONS) and not report['source_changed_during_run'] else 1


if __name__ == '__main__':
    sys.exit(main())
