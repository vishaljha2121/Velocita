import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source_path = root / 'src/v2_swap_account_setup.cpp'
    test = root / 'tests/v2_swap_account_setup_tests.cpp'
    files = [source_path, test, root / 'include/velocita/v2/swap_account_setup.hpp']
    hashes = {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
    source = source_path.read_text()
    libraries = [args.build / ('lib' + name + '.a') for name in ['velocita_v2_swap_runtime', 'velocita_v2_contracts']]
    common = ['c++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I' + str(root / 'include')]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    baseline = out / 'baseline'
    subprocess.run(common + [str(test), str(source_path)] + [str(x) for x in libraries] + ['-o', str(baseline)], check=True)
    result = subprocess.run([str(baseline)], env=env, capture_output=True, text=True)
    (out / 'baseline.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('baseline failed')
    mutants = [
        ('user_identity', 'if(json_member_string(body,"id")!=user)', 'if(false)', 'identity', 'another authenticated user refuses'),
        ('wallet_identity', 'if(!observed || *observed!=owner)', 'if(!observed)', 'identity', 'another custody wallet refuses'),
        ('defi_enabled', 'if(!defi || json_member_raw(*defi,"enabled")!="true")', 'if(!defi)', 'permissions', 'DeFi disabled refuses'),
        ('provider', ' || json_member_string(rows[i],"provider")!="turnkey"', '', 'permissions', 'unsupported wallet provider refuses'),
        ('duplicate_sol', 'if(++sol_count!=1)', 'if((sol_count=1)!=1)', 'ambiguity', 'duplicate SOL address refuses even when equal'),
        ('json_duplicates', 'if(validate_json_object(body)!=JsonError::none)', 'if(false)', 'malformed', 'duplicate JSON key refuses'),
    ]
    results = []
    for name, old, new, case, assertion in mutants:
        if source.count(old) != 1:
            raise RuntimeError('nonunique mutation ' + name)
        changed = out / (name + '.cpp')
        changed.write_text(source.replace(old, new))
        binary = out / name
        subprocess.run(common + [str(test), str(changed)] + [str(x) for x in libraries] + ['-o', str(binary)], check=True)
        result = subprocess.run([str(binary), case], env=env, capture_output=True, text=True)
        killed = result.returncode == 1 and ('FAIL ' + case + ': ' + assertion + '\n') in result.stdout
        results.append(dict(name=name, killed=killed, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
    unchanged = all(hashlib.sha256((root / f).read_bytes()).hexdigest() == h for f,h in hashes.items())
    (out / 'results.json').write_text(json.dumps(dict(baseline_groups=5, mutations=results, source_sha256=hashes,
        libraries={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in libraries}, source_unchanged=unchanged), indent=2)+'\n')
    if not unchanged or not all(x['killed'] for x in results):
        raise RuntimeError('mutation or source check failed')
    print('5 sanitizer groups; 6/6 designated mutation kills; source unchanged')


if __name__ == '__main__':
    main()
