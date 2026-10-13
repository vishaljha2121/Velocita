import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    origin = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    root = out/'source'
    shutil.copytree(origin/'include', root/'include')
    sources = ['src/v2_swap_settlement_evidence.cpp', 'src/v2_solana_payload_view.cpp',
               'src/v2_solana_resolved_accounts.cpp', 'src/v2_swap_owner_accounting.cpp',
               'src/v2_truemarkets_wire.cpp', 'src/v2_digest.cpp',
               'tests/v2_swap_settlement_evidence_tests.cpp', 'tests/mutation_swap_settlement_evidence.py']
    for name in sources:
        target = root/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin/name, target)
    paths = [p for p in root.rglob('*') if p.is_file()]
    hashes = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    (out/'snapshot-sha256.json').write_text(json.dumps(hashes, indent=2)+'\n')
    common = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I'+str(root/'include')]
    objects = []
    for name in sources[1:-1]:
        obj = out/(Path(name).stem+'.o')
        subprocess.run(common+['-c', str(root/name), '-o', str(obj)], check=True)
        objects.append(str(obj))
    source_path = root/sources[0]
    source = source_path.read_text()
    baseline = out/'baseline'
    subprocess.run(common+[str(source_path)]+objects+['-o', str(baseline)], check=True)
    green = subprocess.run([str(baseline)], text=True, capture_output=True)
    (out/'green.log').write_text(green.stdout+green.stderr)
    if green.returncode:
        raise RuntimeError(green.stdout+green.stderr)
    print(green.stdout, end='', flush=True)
    mutations = [
        ('skip_provider_after', 'end-start>=timeout_ || port_.provider_identity()!=provider_', 'end-start>=timeout_',
         'read_identity_and_time', 'unattributed late or failed read grants no evidence'),
        ('skip_deadline', 'end-start>=timeout_', 'false',
         'read_identity_and_time', 'unattributed late or failed read grants no evidence'),
        ('skip_backward_clock', 'end<start || end-start>=timeout_', '(end>=start && end-start>=timeout_)',
         'read_identity_and_time', 'unattributed late or failed read grants no evidence'),
        ('skip_response_status', 'response.http_status!=200 ||', '',
         'read_identity_and_time', 'unattributed late or failed read grants no evidence'),
        ('skip_transport', '!response.transport_complete ||', '',
         'read_identity_and_time', 'unattributed late or failed read grants no evidence'),
        ('skip_request_id', 'number(document,"id")!=request_id ||', '',
         'rpc_shape_and_watermark', 'wrong RPC identity duplicate or old slot grants no evidence'),
        ('skip_slot_floor', '*slot<minimum_slot', '*slot<minimum_slot && false',
         'rpc_shape_and_watermark', 'wrong RPC identity duplicate or old slot grants no evidence'),
        ('skip_rpc_version', 'json_member_string(document,"jsonrpc")!="2.0" ||', '',
         'rpc_shape_and_watermark', 'wrong RPC identity duplicate or old slot grants no evidence'),
        ('skip_rpc_error', ' || json_member_raw(document,"error")', '',
         'rpc_capacity_and_base64', 'capacity zero signature clock overflow RPC error and fractional slot refuse'),
        ('skip_message', '!std::equal(raw.begin()+message,raw.begin()+size,decoded.transaction.begin()+message)', 'false',
         'exact_transaction_and_conservation', 'changed message signature failed execution or conservation refused'),
        ('skip_signature', 'raw[1+i]!=std::to_integer<unsigned>(read.signature()[i])', '(raw[1+i]!=std::to_integer<unsigned>(read.signature()[i]) && false)',
         'exact_transaction_and_conservation', 'changed message signature failed execution or conservation refused'),
        ('skip_success', '*err!="null"', '*err!="null" && false',
         'exact_transaction_and_conservation', 'changed message signature failed execution or conservation refused'),
        ('skip_native_conservation', 'before!=after', 'false',
         'exact_transaction_and_conservation', 'changed message signature failed execution or conservation refused'),
        ('skip_token_conservation', 'basis.mint!=wrapped && pre!=post', 'basis.mint!=wrapped && pre!=post && false',
         'exact_transaction_and_conservation', 'changed message signature failed execution or conservation refused'),
        ('skip_token_program', ' || token.program!=kSolanaClassicTokenProgram', '',
         'token_attribution_and_coverage', 'token index decimals program duplicates and loaded coverage refused'),
        ('skip_duplicate_token', 'if(token.present)return false;', '',
         'token_attribution_and_coverage', 'token index decimals program duplicates and loaded coverage refused'),
        ('skip_loaded_validation', '!loaded(*meta,decoded,resolved)', '(!loaded(*meta,decoded,resolved) && false)',
         'token_attribution_and_coverage', 'token index decimals program duplicates and loaded coverage refused'),
        ('skip_wallet_lamports', 'p.lamports!=lamports ||', '',
         'rent_oracle_and_owner_coverage', 'native wallet declaration must match exact chain delta'),
        ('skip_declared_token_amount', ' || t.atoms!=p.token_atoms', '',
         'property_corroboration', 'declared amounts must equal transaction evidence'),
        ('skip_owner_scope', 'if(at==scope.end())return {SettlementPropertyCode::scope_mismatch,{}};', 'if(at==scope.end()){--expected;continue;}',
         'property_corroboration', 'omitted owner token account refused'),
        ('skip_accounting_validation', 'measurement.code!=OwnerAccountingCode::measured_declared_scope_only', 'measurement.code!=OwnerAccountingCode::measured_declared_scope_only && false',
         'property_corroboration', 'unverified close control cannot measure owner property'),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old) != 1:
            raise RuntimeError(name+': mutation anchor not unique')
        path = out/(name+'.cpp')
        path.write_text(source.replace(old, new, 1))
        exe = out/name
        compile_result = subprocess.run(common+[str(path)]+objects+['-o', str(exe)], text=True, capture_output=True)
        if compile_result.returncode:
            raise RuntimeError(name+': compile failure is not a kill: '+compile_result.stderr)
        result = subprocess.run([str(exe), case], text=True, capture_output=True)
        killed = result.returncode == 1 and f'FAIL {case}: {assertion}\n' in result.stdout
        results.append(dict(mutation=name, case=case, assertion=assertion, killed=killed,
                            exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out/'partial-results.json').write_text(json.dumps(results, indent=2)+'\n')
        if not killed:
            raise RuntimeError(name+': survived or failed elsewhere: '+result.stdout+result.stderr)
    after = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    if hashes != after:
        raise RuntimeError('snapshot changed during mutation run')
    (out/'results.json').write_text(json.dumps(dict(source_sha256=hashes, snapshot_unchanged=True,
        compile_args=common, results=results), indent=2)+'\n')
    print(f'Killed {len(results)} mutants at designated assertions')


if __name__ == '__main__':
    main()
