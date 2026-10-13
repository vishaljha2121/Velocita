import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SCRATCH = Path.home() / '.cache/velocita-build/readiness-20261011/mutations'
RESULT = ROOT / 'docs/evidence/live_readiness_2026-10-09/mutations-20261011.json'
CASES = [
    ('rest_asset_binding', 'rest', 'value.asset_id != catalog_asset->asset_id', 'false',
     'missing_market_class_requires_exact_catalog_binding', 'catalog asset mismatch refuses and clears output'),
    ('rest_precision_binding', 'rest', 'decimals.base != catalog_asset->decimals.base', 'false',
     'missing_market_class_requires_exact_catalog_binding', 'catalog precision mismatch refused'),
    ('balance_residual', 'balance', ' * expected.base_atoms_per_lot + expected.residual_base_atoms;',
     ' * expected.base_atoms_per_lot;', 'residual_is_part_of_inventory', 'three lots plus residual matches exact atoms'),
    ('balance_unknown_order', 'balance', 'expected.unresolved_orders != 0', 'false',
     'snapshot_cannot_resolve_unknown_activity', 'matching balances do not resolve an unknown order'),
    ('balance_identity', 'balance', 'expected.account_and_assets_digest != observed.account_and_assets_digest', 'false',
     'identity_and_freshness_are_required', 'another account or asset binding refused'),
    ('tls_hostname', 'tls', 'set(CURLOPT_SSL_VERIFYHOST, 2L)', 'set(CURLOPT_SSL_VERIFYHOST, 0L)',
     'test_wrong_hostname_is_refused', 'hostname guard must reject before HTTP'),
    ('tls_peer', 'tls', 'set(CURLOPT_SSL_VERIFYPEER, 1L)', 'set(CURLOPT_SSL_VERIFYPEER, 0L)',
     'test_untrusted_certificate_is_refused', 'CA guard must reject before HTTP'),
    ('tls_redirect', 'tls', 'set(CURLOPT_FOLLOWLOCATION, 0L)', 'set(CURLOPT_FOLLOWLOCATION, 1L)',
     'test_redirect_is_not_followed', 'redirect guard must refuse'),
    ('tls_total_deadline', 'tls', 'set(CURLOPT_TIMEOUT_MS, config_.timeout_ms)', 'set(CURLOPT_TIMEOUT_MS, 0L)',
     'test_response_bound_and_whole_request_timeout', 'total deadline must bound body reads'),
]


def main():
    SCRATCH.mkdir(parents=True, exist_ok=True)
    sources = {'rest': 'v2_truemarkets_rest', 'balance': 'v2_spot_balance', 'tls': 'v2_https_read'}
    originals = {key: (ROOT / 'src' / f'{stem}.cpp').read_bytes() for key, stem in sources.items()}
    results = []
    for name, kind, before, after, test, assertion in CASES:
        text = originals[kind].decode()
        if text.count(before) != 1:
            raise RuntimeError(f'{name}: nonunique mutation')
        source = SCRATCH / f'{name}.cpp'
        binary = SCRATCH / name
        source.write_text(text.replace(before, after))
        suite = {'rest': 'v2_truemarkets_rest_tests.cpp', 'balance': 'v2_spot_balance_tests.cpp',
                 'tls': 'v2_https_read_probe.cpp'}[kind]
        cmd = ['c++', '-std=c++20', '-O1', '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
               '-I', str(ROOT / 'include'), str(ROOT / 'tests' / suite), str(source)]
        if kind == 'rest':
            cmd.append(str(ROOT / 'src/v2_truemarkets_wire.cpp'))
        if kind == 'tls':
            cmd.append('-lcurl')
        subprocess.run(cmd + ['-o', str(binary)], check=True, capture_output=True)
        env = os.environ.copy()
        if kind == 'tls':
            env['VELOCITA_HTTPS_PROBE'] = str(binary)
            cmd = ['python3', '-B', str(ROOT / 'tests/test_https_read.py'), f'HttpsReadTests.{test}']
        else:
            cmd = [str(binary), test]
        run = subprocess.run(cmd, env=env, text=True, capture_output=True, timeout=15)
        output = run.stdout + run.stderr
        caught = run.returncode == 1 and f'FAIL {test}:' in output and assertion in output
        if kind == 'tls':
            caught = run.returncode == 1 and f'FAIL: {test} (' in output and assertion in output
        results.append(dict(name=name, test=test, assertion=assertion, caught=caught, output=output))
        print(name, 'CAUGHT' if caught else 'MISSED', flush=True)
    for key, original in originals.items():
        assert (ROOT / 'src' / f'{sources[key]}.cpp').read_bytes() == original
    RESULT.write_text(json.dumps({'source_sha256': {k: hashlib.sha256(v).hexdigest() for k, v in originals.items()},
                                 'results': results}, indent=2) + '\n')
    return 0 if all(row['caught'] for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
