import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


MUTATIONS = [
    ('reverse_integer_bytes', 'data[i]) << (8U * i)', 'data[data.size() - i - 1]) << (8U * i)',
     'compute_oracles', 'compute_limit_little_endian'),
    ('narrow_price', 'ComputeUnitPrice{little(data.subspan(1))}',
     'ComputeUnitPrice{static_cast<std::uint32_t>(little(data.subspan(1)))}',
     'compute_oracles', 'compute_price_little_endian'),
    ('limit_trailing_bytes', 'data.size() != 5', 'data.size() < 5',
     'exact_shapes', 'trailing_instruction_data_refused'),
    ('compute_extra_accounts', 'data.size() != 5 || !accounts.empty()', 'data.size() != 5',
     'exact_shapes', 'extra_accounts_including_multisig_refused'),
    ('ata_funder_signature', '!accounts[0].signer || !accounts[0].writable || !accounts[1].writable',
     '!accounts[0].writable || !accounts[1].writable',
     'privileges_and_fixed_programs', 'ata_funder_signer_required'),
    ('ata_account_writable', '!accounts[0].signer || !accounts[0].writable || !accounts[1].writable',
     '!accounts[0].signer || !accounts[0].writable',
     'privileges_and_fixed_programs', 'ata_account_writable_required'),
    ('ata_system_identity', 'accounts[4].key != kSolanaSystemProgram || ', '',
     'privileges_and_fixed_programs', 'ata_system_program_identity_required'),
    ('ata_token_identity', 'if (accounts[5].key != kSolanaClassicTokenProgram) return {};', '',
     'privileges_and_fixed_programs', 'ata_classic_token_program_only'),
    ('ata_roles_swapped', 'accounts[0],accounts[1],accounts[2],accounts[3],accounts[4],accounts[5]',
     'accounts[0],accounts[1],accounts[3],accounts[2],accounts[4],accounts[5]',
     'typed_roles_and_ownership', 'ata_roles_owned_in_original_order'),
    ('close_multisig', 'accounts.size() != 3', 'accounts.size() < 3',
     'exact_shapes', 'extra_accounts_including_multisig_refused'),
    ('close_signature', '!accounts[2].signer', 'false',
     'privileges_and_fixed_programs', 'close_authority_signature_required'),
    ('alias_privilege_consistency',
     'if (accounts[i].key == accounts[j].key && accounts[i] != accounts[j]) return false;',
     'if (accounts[i].key == accounts[j].key && accounts[i] != accounts[j]) return true;',
     'privileges_and_fixed_programs', 'alias_privileges_must_be_consistent'),
    ('transfer_source_signature', '!consistent(accounts) || !accounts[0].signer || !accounts[0].writable',
     '!consistent(accounts) || !accounts[0].writable',
     'privileges_and_fixed_programs', 'transfer_source_signature_required'),
    ('transfer_recipient', 'TransferLamports{accounts[0],accounts[1],little(data.subspan(4))}',
     'TransferLamports{accounts[0],accounts[0],little(data.subspan(4))}',
     'typed_roles_and_ownership', 'system_rent_transfer_exact_amount_and_roles'),
    ('system_tag_narrowed', 'little(data.first(4)) != 2', 'data[0] != 2',
     'unsupported_never_returns_a_body', 'system_tag_uses_all_four_bytes'),
    ('bad_result_retains_body', 'return {Status::invalid, {}};',
     'return {Status::invalid, ComputeUnitLimit{1}};',
     'exact_shapes', 'every_truncation_refused'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scratch', type=Path, default=Path.home() /
                        '.cache/velocita-build/solana-instructions-20261012')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_path = root / 'src/v2_solana_instruction_decode.cpp'
    test_path = root / 'tests/v2_solana_instruction_decode_tests.cpp'
    source = source_path.read_text()
    work = args.scratch / f'mutations-{time.time_ns()}'
    work.mkdir(parents=True, exist_ok=False)
    report = {'source_sha256': hashlib.sha256(source_path.read_bytes()).hexdigest(),
              'test_sha256': hashlib.sha256(test_path.read_bytes()).hexdigest(), 'mutations': []}
    for name, old, new, case, assertion in [('baseline', '', '', '', '')] + MUTATIONS:
        if old and source.count(old) != 1:
            raise RuntimeError(f'{name}: expected one mutation site, found {source.count(old)}')
        changed = source.replace(old, new, 1) if old else source
        unit = work / f'{name}.cpp'
        unit.write_text(changed)
        binary = work / name
        built = subprocess.run(['c++', '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', '-pedantic',
                                '-I', str(root / 'include'), str(unit), str(test_path), '-o', str(binary)],
                               capture_output=True, text=True, timeout=60, check=False)
        (work / f'{name}-build.log').write_text(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError(f'{name}: compile error is not a mutation kill')
        result = subprocess.run([str(binary)] + ([case] if case else []),
                                capture_output=True, text=True, timeout=10, check=False)
        output = result.stdout + result.stderr
        (work / f'{name}-run.log').write_text(output)
        if name == 'baseline':
            if result.returncode or 'PASS unsupported_never_returns_a_body' not in output:
                raise RuntimeError('baseline must pass before mutation testing')
            report['baseline_passed'] = True
        else:
            killed = result.returncode == 1 and f'FAIL {assertion}\n' in output
            report['mutations'].append({'name': name, 'case': case, 'assertion': assertion,
                                        'returncode': result.returncode, 'killed_by_named_assertion': killed})
            if not killed:
                (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
                raise RuntimeError(f'{name}: did not fail the intended assertion')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(MUTATIONS)} named-assertion mutation kills; evidence: {work / "results.json"}')


if __name__ == '__main__':
    main()
