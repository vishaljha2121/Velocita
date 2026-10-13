import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


MUTATIONS = (
    ('unknown_kind', 'if (kind!=OwnedAccountKind::native_wallet && kind!=OwnedAccountKind::wrapped_sol && kind!=OwnedAccountKind::usdc_token)',
     'if (false)', 'scope', 'unknown_absent_account_kind_refused'),
    ('duplicate_account', 'if (scope[i].key==scope[j].key) return {};',
     'if (false) return {};', 'scope', 'duplicate_owned_account_refused'),
    ('account_identity', ' || scope[i].key!=accounts[i].key', '',
     'scope', 'every_declared_account_present'),
    ('backing_bound', ' || p.token_atoms>p.lamports-p.native_reserve', '',
     'scope', 'token_amount_cannot_exceed_native_backing'),
    ('unsynced_backing',
     '!add(total.wrapped_backing,p.lamports-p.native_reserve) || !add(total.total_lamports,p.lamports-p.native_reserve)',
     '!add(total.wrapped_backing,p.token_atoms) || !add(total.total_lamports,p.token_atoms)',
     'scope', 'unsynced_native_amount_does_not_erase_backing'),
    ('sponsor_rent_as_owner',
     'if (!add(total.usdc_atoms,p.token_atoms)) return OwnerAccountingCode::overflow;\n        if (p.deposit_control==DepositControl::owner)',
     'if (!add(total.usdc_atoms,p.token_atoms)) return OwnerAccountingCode::overflow;\n        if (true)',
     'external', 'sponsor_controlled_rent_is_not_owner_receipt'),
    ('unknown_rent_control',
     'if (p.native_reserve || (p.deposit_control!=DepositControl::owner && p.deposit_control!=DepositControl::external))',
     'if (p.native_reserve)', 'external', 'unknown_close_control_refused'),
    ('discard_owner_wrapped_rent',
     '!add(total.refundable_deposits,p.native_reserve) || !add(total.total_lamports,p.native_reserve)',
     '!add(total.refundable_deposits,p.native_reserve) || !add(total.total_lamports,0)',
     'rent', 'existing_owner_rent_is_not_sponsored'),
    ('overflow_partial_result',
     'if (code!=OwnerAccountingCode::measured_declared_scope_only) return {code};\n        code=append',
     'if (code!=OwnerAccountingCode::measured_declared_scope_only) return {code,result.before,result.after};\n        code=append',
     'overflow', 'overflow_does_not_return_partial_totals'),
    ('buy_one_atom_excess',
     'result.usdc_change < -static_cast<std::int64_t>(intent.max_quote_debit.value())',
     'result.usdc_change < -static_cast<std::int64_t>(intent.max_quote_debit.value()) - 1',
     'bounds', 'one_quote_atom_excess'),
    ('sell_quote_one_atom_short',
     'result.usdc_change < static_cast<std::int64_t>(intent.min_quote_receive.value())',
     'result.usdc_change < static_cast<std::int64_t>(intent.min_quote_receive.value()) - 1',
     'bounds', 'sell_quote_shortfall'),
    ('buy_receipt_inclusive_error',
     'result.sol_change < static_cast<std::int64_t>(intent.min_base_receive.value())',
     'result.sol_change <= static_cast<std::int64_t>(intent.min_base_receive.value())',
     'bounds', 'exact_buy_net_boundary'),
    ('buy_deposit_hides_receipt',
     ' ||\n            result.base_change_excluding_deposits < static_cast<std::int64_t>(intent.min_base_receive.value())',
     '', 'bounds', 'new_deposit_cannot_replace_base_receipt'),
    ('sell_deposit_hides_overspend',
     ' ||\n            result.base_change_excluding_deposits < -static_cast<std::int64_t>(intent.base_input.value())',
     '', 'bounds', 'deposit_gift_cannot_hide_base_overspend'),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    if output.exists():
        raise RuntimeError('output directory must be new')
    output.mkdir(parents=True)
    paths = ('src/v2_swap_owner_accounting.cpp', 'include/velocita/v2/swap_owner_accounting.hpp',
             'tests/v2_swap_owner_accounting_tests.cpp', 'include/velocita/v2/swap.hpp',
             'include/velocita/v2/solana_instruction_decode.hpp')
    snapshots = {rel: (root / rel).read_bytes() for rel in paths}
    source = snapshots[paths[0]].decode()
    test = output / 'accounting_tests.cpp'
    test.write_bytes(snapshots[paths[2]])
    baseline_source = output / 'baseline.cpp'
    baseline_source.write_text(source)
    common = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I' + str(root / 'include'), str(test)]
    sanitizer_options = 'detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1'
    env = dict(os.environ, ASAN_OPTIONS=sanitizer_options, UBSAN_OPTIONS='halt_on_error=1')
    report = {'source_sha256': {rel: sha(data) for rel, data in snapshots.items()},
              'compile_prefix': common, 'asan_options': sanitizer_options,
              'ubsan_options': 'halt_on_error=1', 'baseline_passed': False, 'results': []}
    def record():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    def compile_and_run(name, source_file, case=None):
        binary = output / name
        compiled = subprocess.run(common + [str(source_file), '-o', str(binary)],
                                  capture_output=True, text=True, timeout=90, env=env)
        (output / (name + '.compile.log')).write_text(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise RuntimeError(name + ': compile error is not a mutation kill')
        executed = subprocess.run([str(binary)] + ([case] if case else []),
                                  capture_output=True, text=True, timeout=30, env=env)
        (output / (name + '.run.log')).write_text(executed.stdout + executed.stderr)
        return executed
    baseline = compile_and_run('baseline', baseline_source)
    expected = {f'PASS {name}' for name in ('rent', 'bounds', 'external', 'capture', 'simulation', 'scope', 'overflow')}
    if baseline.returncode or set(baseline.stdout.splitlines()) != expected or baseline.stderr:
        record()
        raise RuntimeError('baseline did not pass all seven ASan/UBSan groups cleanly')
    report['baseline_passed'] = True
    record()
    for name, old, new, case, assertion in MUTATIONS:
        if source.count(old) != 1:
            raise RuntimeError(name + ': mutation anchor must match exactly once')
        changed = output / (name + '.cpp')
        changed.write_text(source.replace(old, new, 1))
        result = compile_and_run(name, changed, case)
        expected_failure = f'FAIL {case}: {assertion}\n'
        killed = result.returncode == 1 and result.stderr == expected_failure and not result.stdout
        report['results'].append({'mutation': name, 'case': case, 'named_assertion': assertion,
                                  'exit': result.returncode, 'killed': killed,
                                  'stdout': result.stdout, 'stderr': result.stderr})
        record()
        if not killed:
            raise RuntimeError(name + ': survived or failed outside designated assertion')
    if any((root / rel).read_bytes() != data for rel, data in snapshots.items()):
        report['source_changed_during_run'] = True
        record()
        raise RuntimeError('source changed during run; result not accepted')
    report['source_changed_during_run'] = False
    record()
    print(f'ASan/UBSan baseline: {len(expected)} groups passed; {len(MUTATIONS)}/{len(MUTATIONS)} named mutation kills')
    print(output / 'results.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
