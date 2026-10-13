#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COMMON = [
    'src/v2_swap_settlement_evidence.cpp', 'src/v2_solana_payload_view.cpp',
    'src/v2_solana_resolved_accounts.cpp', 'src/v2_solana_instruction_decode.cpp',
    'src/v2_jupiter_route_decode.cpp', 'src/v2_swap_owner_accounting.cpp', 'src/v2_truemarkets_wire.cpp', 'src/v2_digest.cpp',
]
ADMISSION = [
    'src/v2_swap_settlement_admission.cpp', 'src/v2_swap_settlement_connection.cpp',
    'src/v2_swap_prepare_store.cpp', 'src/v2_swap_submission_store.cpp',
    'src/v2_swap_rest_codec.cpp', 'src/v2_swap_signing_stamp.cpp',
    'src/v2_swap_codec.cpp', 'src/v2_swap.cpp',
]
LIFECYCLE = 'src/v2_swap_settlement_lifecycle.cpp'
TESTS = {'lifecycle': 'tests/v2_swap_settlement_lifecycle_tests.cpp',
         'admission': 'tests/v2_swap_settlement_admission_tests.cpp'}
MUTATIONS = [
    ('sponsor_rent_counted_as_owner', LIFECYCLE, 'if(a.close==evidence.owner){', 'if(true){', 'lifecycle', 'new_usdc_deposit_control', 'sponsor_close_control_excludes_owner_rent'),
    ('ata_wallet_ignored', LIFECYCLE, '!std::equal(key(2).begin(),key(2).end(),child.data.begin()+1)', 'false', 'lifecycle', 'ata_identity', 'ATA_wallet_and_child_initialization_must_agree'),
    ('network_fee_omitted', LIFECYCLE, 'state[0].lamports-=evidence.network_fee;', 'state[0].lamports-=0;', 'lifecycle', 'captured_shape_arithmetic', 'WSOL_create_transfer_close_replayed'),
    ('create_size_unchecked', LIFECYCLE, 'little(ix,12,8)!=165 ||', 'false ||', 'lifecycle', 'lifetime_and_program_edges', 'unsupported_lifetime_layout_mint_or_close_refused'),
    ('create_program_unchecked', LIFECYCLE, '!std::equal(kSolanaClassicTokenProgram.begin(),kSolanaClassicTokenProgram.end(),ix.data.begin()+20) ||', 'false ||', 'lifecycle', 'lifetime_and_program_edges', 'unsupported_lifetime_layout_mint_or_close_refused'),
    ('transfer_authority_unchecked', LIFECYCLE, 'from.owner!=key(authority) ||', 'false ||', 'lifecycle', 'missing_or_unexplained', 'missing_trace_unknown_authority_or_unexplained_atoms_refused'),
    ('native_transfer_not_replayed', LIFECYCLE, 'if(from.native && !move(from.lamports,to.lamports,value))', 'if(false && !move(from.lamports,to.lamports,value))', 'lifecycle', 'captured_shape_arithmetic', 'WSOL_create_transfer_close_replayed'),
    ('close_authority_unchecked', LIFECYCLE, 'account(0).close!=key(2) ||', 'false ||', 'lifecycle', 'lifetime_and_program_edges', 'unsupported_lifetime_layout_mint_or_close_refused'),
    ('final_native_not_matched', LIFECYCLE, 'a.lamports!=expected.after_lamports ||', 'false ||', 'lifecycle', 'missing_or_unexplained', 'missing_trace_unknown_authority_or_unexplained_atoms_refused'),
    ('final_token_not_matched', LIFECYCLE, 'a.atoms!=after.atoms', 'false', 'lifecycle', 'missing_or_unexplained', 'missing_trace_unknown_authority_or_unexplained_atoms_refused'),
    ('unknown_leaf_accepted', LIFECYCLE, 'if(!children)for(std::size_t k=0;k<ix.account_count;++k)if(writable(k))return Code::unsupported_instruction;', 'if(false)return Code::unsupported_instruction;', 'lifecycle', 'missing_or_unexplained', 'missing_trace_unknown_authority_or_unexplained_atoms_refused'),
    ('stack_height_jump_accepted', LIFECYCLE, '*h>height+1 ||', 'false ||', 'lifecycle', 'trace_shape', 'trace_index_height_and_encoding_must_be_exact'),
    ('close_does_not_transfer_rent', LIFECYCLE, '!move(account(0).lamports,account(1).lamports,account(0).lamports)', '!move(account(0).lamports,account(1).lamports,0)', 'lifecycle', 'captured_shape_arithmetic', 'WSOL_create_transfer_close_replayed'),
    ('checked_decimals_ignored', LIFECYCLE, 'ix.data[9]!=(from.native?9:6)', 'false', 'lifecycle', 'supported_checked_and_sync', 'checked_transfer_wrong_decimals_refused'),
    ('intent_hash_ignored', ADMISSION[0], 'preparation.latest().operation.intent_digest!=sha256_digest(std::span(bytes).first(encoded.size))', 'false', 'admission', 'retained_admission', 'changed_intent_refused_before_RPC'),
    ('rpc_unknown_misclassified', ADMISSION[0], 'result.code=SwapSettlementAdmissionCode::rpc_unresolved;break;', 'result.code=SwapSettlementAdmissionCode::lifecycle_unproven;break;', 'admission', 'retained_admission', 'missing_transaction_remains_unknown'),
    ('fee_evidence_omitted', ADMISSION[0], 'complete_lifecycle_trace|account_control_continuity|\n                fee_and_principal_attribution|owner_net_receipt_and_costs;', 'complete_lifecycle_trace|account_control_continuity|owner_net_receipt_and_costs;', 'admission', 'retained_admission', 'all_missing_economic_facts_explicit'),
    ('observation_forges_proof', ADMISSION[0], 'result.observation=std::move(observed);', 'result.verified=settlement_test_fixture::make_verified(swap::Settlement{});result.observation=std::move(observed);', 'admission', 'retained_admission', 'no_observation_can_mint_settlement_proof'),
    ('unknown_size_extension_accepted', LIFECYCLE, 'ix.data[1]==7 && ix.data[2]==0', 'true', 'lifecycle', 'observed_size_request', 'unknown_size_extension_refused'),
    ('cpi_account_scope_ignored', LIFECYCLE, 'if(!includes(ix.accounts[a]))return Code::malformed_trace;', 'if(false)return Code::malformed_trace;', 'lifecycle', 'cpi_account_scope', 'readonly_parent_cannot_hide_writable_descendant'),
    ('wrong_signature_identity', ADMISSION[0], 'settlement.transaction=*transaction;', 'settlement.transaction=sha256_digest(observed.evidence.signature);', 'admission', 'bounded_buy_admission', 'admission_identity_matches_retained_resolution'),
    ('ABI_fee_role_shifted', LIFECYCLE, 'const auto fee=is_shared?9:6', 'const auto fee=is_shared?8:5', 'admission', 'bounded_buy_admission', 'ABI_bound_finalized_buy_can_construct_private_proof'),
    ('auxiliary_SOL_cost_ignored', ADMISSION[0], 'static_cast<std::uint64_t>(replay.owner_wallet_lamports)==replay.route_destination_credit', 'true', 'admission', 'bounded_buy_admission', 'admitted_atoms_apply_once_without_double_fee'),
    ('intent_net_bounds_ignored', ADMISSION[0], 'result.bounds==ObservedSwapBounds::within_observed_net_bounds_only', 'true', 'admission', 'bounded_buy_admission', 'admitted_receipt_is_kernel_compatible'),
    ('sell_fee_recorded_zero', ADMISSION[0], 'settlement.fee=swap::QuoteAtoms{replay.fee_atoms};', 'settlement.fee=swap::QuoteAtoms{intent.side==Side::sell?0:replay.fee_atoms};', 'admission', 'bounded_sell_admission', 'admitted_atoms_apply_once_without_double_fee'),
    ('sell_receipt_recorded_gross', ADMISSION[0], 'settlement.quote_credit=swap::QuoteAtoms{static_cast<std::uint64_t>(replay.owner_usdc_atoms)};', 'settlement.quote_credit=swap::QuoteAtoms{replay.route_destination_credit};', 'admission', 'bounded_sell_admission', 'admitted_atoms_apply_once_without_double_fee'),

]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=False)
    source_paths = sorted(set(COMMON + ADMISSION + [LIFECYCLE] + list(TESTS.values())))
    watched = [ROOT / 'tests/swap_settlement_test_fixture.hpp'] + [ROOT / p for p in source_paths] + sorted((ROOT / 'include/velocita/v2').glob('*.hpp'))
    snapshot = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
    (out / 'source-sha256.json').write_text(json.dumps(snapshot, indent=2) + '\n')
    flags = ['c++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-Wall', '-Wextra', '-Wpedantic', '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
             '-DVELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY=1', '-DVELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE=1', '-I', str(ROOT / 'include')]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1')
    commands = []

    def run(command, name):
        commands.append(command)
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (out / (name + '.log')).write_text(result.stdout)
        return result

    objects = {}
    for i, path in enumerate(source_paths):
        obj = out / f'base-{i}.o'
        result = run(flags + ['-c', str(ROOT / path), '-o', str(obj)], f'compile-{i}')
        if result.returncode:
            raise RuntimeError(f'baseline compile failed: {path}')
        objects[path] = obj

    def link(kind, name, replacement=None):
        paths = COMMON + [LIFECYCLE, TESTS[kind]] + (ADMISSION if kind == 'admission' else [])
        selected = [str(replacement[1] if replacement and p == replacement[0] else objects[p]) for p in paths]
        exe = out / name
        result = run(flags + selected + ['-o', str(exe)], name + '-link')
        if result.returncode:
            raise RuntimeError(f'link failed: {name}')
        return exe

    for kind in TESTS:
        baseline = run([str(link(kind, kind + '-baseline'))], kind + '-baseline')
        if baseline.returncode or 'FAIL ' in baseline.stdout or 'runtime error:' in baseline.stdout or 'AddressSanitizer' in baseline.stdout:
            raise RuntimeError(f'baseline failed: {kind}')
    results = []
    for name, path, before, after, kind, group, assertion in MUTATIONS:
        source = (ROOT / path).read_text()
        if source.count(before) != 1:
            raise RuntimeError(f'mutation anchor not unique: {name}')
        changed = out / (name + '.cpp')
        changed.write_text(source.replace(before, after))
        obj = out / (name + '.o')
        compiled = run(flags + ['-c', str(changed), '-o', str(obj)], name + '-compile')
        if compiled.returncode:
            raise RuntimeError(f'mutant compilation is not a kill: {name}')
        exe = link(kind, name, (path, obj))
        result = run([str(exe), group], name + '-run')
        expected = f'FAIL {group}: {assertion}'
        killed = result.returncode == 1 and expected in result.stdout and 'AddressSanitizer' not in result.stdout and 'runtime error:' not in result.stdout
        results.append({'name': name, 'named_assertion': expected, 'killed': killed, 'exit': result.returncode})
        print(f'{name}: {"KILLED" if killed else "SURVIVED_OR_ERROR"}', flush=True)
    unchanged = all(hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == value for path, value in snapshot.items())
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (out / 'results.json').write_text(json.dumps({'baseline_groups': 16, 'source_unchanged': unchanged, 'mutations': results}, indent=2) + '\n')
    return 0 if unchanged and all(r['killed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
