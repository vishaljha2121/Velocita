"""Offline production Invariant verifier mutations; no network or credentials.

Only an expected named assertion with exit 1 counts as a kill. Compilation,
linking, sanitizer failures and other test errors abort without claiming a kill.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
DEPENDENCIES = ['v2_digest.cpp', 'v2_solana_payload_view.cpp', 'v2_solana_resolved_accounts.cpp',
                'v2_solana_instruction_decode.cpp', 'v2_jupiter_route_decode.cpp',
                'v2_swap_owner_accounting.cpp', 'v2_truemarkets_wire.cpp']
MUTATIONS = [
    ('invariant_program', 'a[17]==invariant', 'true', 'invariant_canonical_programs_and_mint_pinned'),
    ('unused_twentieth_key', 'if(a[19]!=SolanaAccountKey{})return false;',
     'if(false)return false;', 'invariant_twentieth_key_must_be_zero'),
    ('five_instructions_only', '(is_invariant && d.instruction_count!=5) || ', '',
     'invariant_even_zero_compute_price_not_reviewed'),
    ('direction', 'static_cast<std::uint8_t>(is_invariant?1:0)', 'static_cast<std::uint8_t>(0)',
     'invariant_exact_five_instruction_layout_verified'),
    ('lookup_writability', '(i>=11 && i<15)', '(i>=11 && i<14)',
     'invariant_exact_five_instruction_layout_verified'),
    ('account_identity', 'accounts.accounts[i].key!=p.account_key(i)', 'false',
     'invariant_source_key_changed_in_wire'),
    ('account_permissions', 'accounts.accounts[i].writable!=writable', '(writable && false)',
     'invariant_static_permission_escalation'),
    ('program_sequence', 'd.instructions[i].program_index!=programs[i+(omit_price && i>0?1:0)]', 'false',
     'invariant_every_instruction_program_pinned'),
    ('route_roles', '!indices(d,route_index,route_accounts)', '(indices(d,route_index,route_accounts) && false)',
     'invariant_every_route_role_pinned'),
    ('ata_roles', '!indices(d,ata_index,is_obsidian?obsidian_ata:is_byreal?byreal_ata:legacy_ata)',
     '(indices(d,ata_index,is_obsidian?obsidian_ata:is_byreal?byreal_ata:legacy_ata) && false)',
     'invariant_every_ata_role_pinned'),
    ('close_roles', '!indices(d,close_index,is_byreal?byreal_close:legacy_close)',
     '(indices(d,close_index,is_byreal?byreal_close:legacy_close) && false)',
     'invariant_every_close_role_pinned'),
    ('repayment_roles', '!indices(d,pay_index,transfer)', '(indices(d,pay_index,transfer) && false)',
     'invariant_every_repayment_role_pinned'),
    ('fee_field', 'route.platform_fee_bps!=20 || ', '', 'invariant_fee_changed_refused'),
    ('principal', ' || route.input_atoms-fee!=intent.quote_input.value()', '',
     'invariant_principal_not_debit_minus_fee'),
    ('slippage_cap', 'route.slippage_bps>p.max_slippage_bps', 'false', 'invariant_slippage_cap_refused'),
    ('rounding', "+9'999)/10'000", "+0)/10'000", 'invariant_exact_five_instruction_layout_verified'),
    ('receipt_bound', '0,intent.min_base_receive,p.accepted_costs', '0,swap::BaseAtoms{1},p.accepted_costs',
     'invariant_floor_plus_one_refused'),
    ('presumed_sponsor_credit', 'gross,repayment,0,intent.min_base_receive',
     'gross,repayment,1,intent.min_base_receive', 'invariant_hand_computed_inclusive_fee_and_net_floor'),
    ('already_signed', '|| d.nonzero_signatures', '|| false', 'invariant_already_signed_refused'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, required=True)
    output = parser.parse_args().output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source_path = ROOT / 'src/v2_swap_transaction_verifier.cpp'
    original = source_path.read_text()
    inputs = [source_path, Path(__file__), ROOT / 'tests/v2_swap_transaction_verifier_tests.cpp',
              ROOT / 'tests/v2_swap_transaction_verifier_test_support.hpp']
    inputs += [ROOT / 'src' / name for name in DEPENDENCIES]
    inputs += list((ROOT / 'include/velocita/v2').glob('*.hpp'))
    snapshots = {str(path.relative_to(ROOT)): path.read_bytes() for path in inputs}
    snapshot = output / 'source'
    for relative, data in snapshots.items():
        target = snapshot / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    flags = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable',
             '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-I' + str(snapshot / 'include')]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1',
               UBSAN_OPTIONS='halt_on_error=1')
    commands = []
    report = dict(source_sha256={rel: hashlib.sha256(data).hexdigest() for rel, data in snapshots.items()},
                  baseline_passed=False, results=[], named_kills=0, test_errors=0)

    def record():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')

    def execute(command, label):
        commands.append(command)
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=120)
        (output / (label + '.log')).write_text(result.stdout + result.stderr)
        return result

    objects = []
    for rel in ['src/' + name for name in DEPENDENCIES] + ['tests/v2_swap_transaction_verifier_tests.cpp']:
        obj = output / (Path(rel).name + '.o')
        result = execute(flags + ['-c', str(snapshot / rel), '-o', str(obj)], Path(rel).name + '.build')
        if result.returncode:
            raise RuntimeError('dependency compilation is not a mutation kill: ' + rel)
        objects.append(str(obj))
    for name, old, new, assertion in [('baseline', None, None, None)] + MUTATIONS:
        if old and original.count(old) != 1:
            raise RuntimeError(name + ': expected one mutation anchor')
        mutated = output / (name + '.cpp')
        mutated.write_text(original.replace(old, new, 1) if old else original)
        binary = output / name
        build = execute(flags + [str(mutated)] + objects + ['-o', str(binary)], name + '.build')
        if build.returncode:
            raise RuntimeError(name + ': compilation is not a mutation kill')
        result = execute([str(binary)] + (['invariant'] if old else []), name)
        passed = (result.returncode == 1 and result.stderr == f'FAIL invariant: {assertion}\n') if old else result.returncode == 0
        report['results'].append(dict(name=name, assertion=assertion, exit=result.returncode,
                                      passed=passed, stdout=result.stdout, stderr=result.stderr))
        if old and passed:
            report['named_kills'] += 1
        if not old:
            report['baseline_passed'] = passed
        record()
        if not passed:
            raise RuntimeError(name + ': not the expected named assertion')
    if any((ROOT / rel).read_bytes() != data for rel, data in snapshots.items()):
        raise RuntimeError('source changed during mutation run; rerun stable source')
    print(str(report['named_kills']) + ' named assertion kills')


if __name__ == '__main__':
    main()
