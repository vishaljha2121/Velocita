#!/usr/bin/env python3
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('admission_mutations', ROOT / 'tests/mutation_swap_settlement_admission.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
LIFECYCLE = base.LIFECYCLE
ADMISSION = base.ADMISSION[0]
EVIDENCE = 'src/v2_swap_settlement_evidence.cpp'
TEST = 'tests/v2_swap_failed_settlement_tests.cpp'
MUTANTS = [
    ('sponsor_pin_ignored', LIFECYCLE, 'evidence.accounts[0].key!=policy.expected_external_fee_payer', 'false', 'sponsor_policy', 'unconfigured_wrong_or_owner_sponsor_refused'),
    ('owner_payer_allowed', LIFECYCLE, 'policy.expected_external_fee_payer==owner ||\n        !evidence.account_count || evidence.accounts[0].key!=policy.expected_external_fee_payer || evidence.owner_index==0', 'false ||\n        !evidence.account_count || evidence.accounts[0].key!=policy.expected_external_fee_payer || false', 'owner_payer', 'owner_fee_payer_never_zero_charge'),
    ('native_delta_ignored', LIFECYCLE, 'else if(a.before_lamports!=a.after_lamports)', 'else if(false)', 'native_rollback', 'any_nonpayer_delta_or_wrong_fee_refused'),
    ('token_amount_ignored', LIFECYCLE, 'before.atoms!=after.atoms ||', 'false ||', 'token_rollback', 'failed_token_amount_owner_or_presence_change_refused'),
    ('token_owner_ignored', LIFECYCLE, 'before.owner!=after.owner ||', 'false ||', 'token_rollback', 'failed_token_amount_owner_or_presence_change_refused'),
    ('nonce_allowed', LIFECYCLE, 'if(bytes.size()!=12 || bytes[0]!=2 || bytes[1] || bytes[2] || bytes[3])', 'if(false)', 'failure_shape', 'nonce_success_or_malformed_failure_refused'),
    ('success_treated_as_failure', LIFECYCLE, 'if(evidence.code!=SettlementEvidenceCode::observed_failed_transaction)return {};', 'if(false)return {};', 'failure_shape', 'nonce_success_or_malformed_failure_refused'),
    ('failed_instruction_index_ignored', EVIDENCE, '*index>=decoded.instruction_count ||', 'false ||', 'failure_shape', 'nonce_success_or_malformed_failure_refused'),
    ('zero_key_allowed', EVIDENCE, '!key_from_text(text,key) || key==SolanaAccountKey{}', '!key_from_text(text,key)', 'account_key_codec', 'noncanonical_zero_or_overflow_key_refused'),
    ('failure_policy_bypassed', ADMISSION, 'result.failure.code==FailedSwapAccountingCode::observed_external_fee_only && failed_transaction', 'failed_transaction.has_value()', 'retained_failed_admission', 'disabled_failure_policy_keeps_exposure'),
    ('wrong_failure_identity', ADMISSION, 'failure.transaction=*failed_transaction;', 'failure.transaction=sha256_digest(observed.evidence.signature);', 'retained_failed_admission', 'failed_proof_exact_identity_and_zero_atoms'),
    ('failure_reported_success', ADMISSION, 'failure.outcome=swap::Outcome::failed_without_charge;', 'failure.outcome=swap::Outcome::succeeded;', 'retained_failed_admission', 'admitted_receipt_is_kernel_compatible'),
    ('owner_failure_fee_invented', ADMISSION, 'failure.outcome=swap::Outcome::failed_without_charge;', 'failure.outcome=swap::Outcome::failed_without_charge;failure.fee=swap::QuoteAtoms{1};', 'retained_failed_admission', 'admitted_receipt_is_kernel_compatible'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = base.COMMON + base.ADMISSION + [LIFECYCLE, TEST]
    watched = [ROOT / p for p in sources] + [ROOT / 'tests/swap_settlement_test_fixture.hpp'] + sorted((ROOT / 'include/velocita/v2').glob('*.hpp'))
    snapshot = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
    (out / 'source-sha256.json').write_text(json.dumps(snapshot, indent=2) + '\n')
    flags = ['c++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Wpedantic', '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-DVELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY=1', '-I', str(ROOT / 'include')]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0' if sys.platform == 'darwin' else 'detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1')
    commands = []
    def run(command, name):
        commands.append(command)
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (out / (name + '.log')).write_text(result.stdout)
        return result
    objects = {}
    for n, source in enumerate(sources):
        obj = out / f'base-{n}.o'
        if run(flags + ['-c', str(ROOT / source), '-o', str(obj)], f'compile-{n}').returncode:
            raise RuntimeError(f'baseline compile failed {source}')
        objects[source] = obj
    def link(name, replacement=None):
        exe = out / name
        objs = [str(replacement[1] if replacement and source == replacement[0] else objects[source]) for source in sources]
        if run(flags + objs + ['-o', str(exe)], name + '-link').returncode:
            raise RuntimeError(f'link failed {name}')
        return exe
    baseline = run([str(link('baseline'))], 'baseline-run')
    if baseline.returncode or 'FAIL ' in baseline.stdout or 'AddressSanitizer' in baseline.stdout or 'runtime error:' in baseline.stdout:
        raise RuntimeError('baseline failed')
    results = []
    for name, source, before, after, group, assertion in MUTANTS:
        text = (ROOT / source).read_text()
        if text.count(before) != 1:
            raise RuntimeError(f'nonunique mutation {name}')
        changed = out / (name + '.cpp')
        changed.write_text(text.replace(before, after))
        obj = out / (name + '.o')
        if run(flags + ['-c', str(changed), '-o', str(obj)], name + '-compile').returncode:
            raise RuntimeError(f'compile error is not a kill: {name}')
        result = run([str(link(name, (source, obj))), group], name + '-run')
        expected = f'FAIL {group}: {assertion}'
        killed = result.returncode == 1 and expected in result.stdout and 'AddressSanitizer' not in result.stdout and 'runtime error:' not in result.stdout
        results.append({'name': name, 'named_assertion': expected, 'killed': killed, 'exit': result.returncode})
        print(f'{name}: {"KILLED" if killed else "SURVIVED_OR_ERROR"}', flush=True)
    unchanged = all(hashlib.sha256((ROOT / p).read_bytes()).hexdigest() == digest for p, digest in snapshot.items())
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (out / 'results.json').write_text(json.dumps({'baseline_groups': 8, 'source_unchanged': unchanged, 'mutations': results}, indent=2) + '\n')
    return 0 if unchanged and all(r['killed'] for r in results) else 1

if __name__ == '__main__':
    raise SystemExit(main())
