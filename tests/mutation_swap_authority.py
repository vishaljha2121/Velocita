import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root-build', type=Path, required=True)
    parser.add_argument('--raft-include', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    origin = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    root = out/'source'
    shutil.copytree(origin/'include', root/'include')
    (root/'tests').mkdir()
    for p in (origin/'tests').glob('*.hpp'):
        shutil.copy2(p, root/'tests'/p.name)
    for name in ['tests/v2_swap_authority_tests.cpp', 'tests/mutation_swap_authority.py', 'src/v2_swap_authority.cpp']:
        target = root/name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin/name, target)
    shutil.copytree(args.raft_include, out/'raft-include')
    (out/'libraries').mkdir()
    libraries = []
    for name in ['velocita_v2_node', 'velocita_v2_replication', 'velocita_v2_kernel',
                 'velocita_v2_evaluator', 'velocita_core', 'willemt_raft', 'velocita_v2_contracts']:
        p = out/'libraries'/('lib'+name+'.a')
        shutil.copy2(args.root_build/p.name, p)
        libraries.append(str(p))
    paths = [p for p in out.rglob('*') if p.is_file()]
    hashes = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    (out/'snapshot-sha256.json').write_text(json.dumps(hashes, indent=2)+'\n')
    common = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I'+str(root/'include'), '-I'+str(out/'raft-include')]
    test_object = out/'tests.o'
    subprocess.run(common+['-c', str(root/'tests/v2_swap_authority_tests.cpp'), '-o', str(test_object)], check=True)
    link = [str(test_object)]+libraries+['-pthread']
    source_path = root/'src/v2_swap_authority.cpp'
    source = source_path.read_text()
    baseline = out/'baseline'
    subprocess.run(common+[str(source_path)]+link+['-o', str(baseline)], check=True)
    green = subprocess.run([str(baseline)], text=True, capture_output=True)
    (out/'green.log').write_text(green.stdout+green.stderr)
    if green.returncode:
        raise RuntimeError(green.stdout+green.stderr)
    print(green.stdout, end='', flush=True)
    mutations = [
        ('omit_domain_hash', 'hash.update(std::as_bytes(std::span(domain.data(),domain.size())));', '(void)domain;', 'exact', 'independent Python signing authority digest oracle'),
        ('omit_intent_hash', 'hash.update(std::span(bytes).first(encoded.size));', '', 'exact', 'independent Python signing authority digest oracle'),
        ('omit_payload_hash', 'hash.update(p.payload.bytes);', '', 'exact', 'independent Python signing authority digest oracle'),
        ('omit_handle_hash', 'hash.update(p.handle.bytes);', '', 'exact', 'independent Python signing authority digest oracle'),
        ('missing_payload_allowed', ' || p.payload.is_zero()', '', 'invalid_digests', 'missing payload yields no signing digest'),
        ('missing_handle_allowed', ' || p.handle.is_zero()', '', 'invalid_digests', 'missing handle yields no signing digest'),
        ('skip_domain_match', 'r.authority_domain==domain &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_global_bindings', 'swap_state_bindings_valid(state) &&', '', 'current_controls_and_expiry', 'unsynchronized outer kernel controls refuse authority'),
        ('skip_prepared', 'p.phase==swap::Phase::prepared &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_halted', 's.mode!=swap::Mode::halted &&', '', 'current_controls_and_expiry', 'halted sell refuses signing authority'),
        ('skip_buy_mode', '(p.intent.side!=Side::buy || s.mode==swap::Mode::normal) &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_effect', 'r.effect_id==p.intent.effect &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_catalog', 'r.catalog_root==s.opening.binding.catalog &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_requested_kill', 'r.expected_kill_epoch.value()==s.kill_epoch &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_requested_generation', 'r.expected_executor_generation.value()==s.executor_generation &&', '', 'fences', 'wrong identity controls or phase refuses signing authority'),
        ('skip_pending_kill', 'p.intent.kill_epoch==s.kill_epoch &&', '', 'current_controls_and_expiry', 'stale pending generation refuses despite current requested controls'),
        ('skip_pending_generation', 'p.intent.executor_generation==s.executor_generation &&', '', 'current_controls_and_expiry', 'stale pending generation refuses despite current requested controls'),
        ('inclusive_expiry', 's.event_time<p.intent.sign_by', 's.event_time<=p.intent.sign_by', 'current_controls_and_expiry', 'authority refuses exactly at signing expiry'),
        ('skip_expected_digest', 'r.expected_effect_payload_digest==swap_signing_authority_digest(p)', 'true', 'fences', 'wrong identity controls or phase refuses signing authority'),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old) != 1:
            raise RuntimeError(name+': mutation anchor not unique')
        path = out/(name+'.cpp')
        path.write_text(source.replace(old, new, 1))
        exe = out/name
        compiled = subprocess.run(common+[str(path)]+link+['-o', str(exe)], text=True, capture_output=True)
        if compiled.returncode:
            raise RuntimeError(name+': compile failure is not a kill: '+compiled.stderr)
        result = subprocess.run([str(exe), case], text=True, capture_output=True)
        killed = result.returncode == 1 and f'FAIL {case}: {assertion}\n' in result.stdout
        results.append(dict(mutation=name, case=case, assertion=assertion, killed=killed,
                            exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out/'partial-results.json').write_text(json.dumps(results, indent=2)+'\n')
        if not killed:
            raise RuntimeError(name+': survived or failed elsewhere: '+result.stdout+result.stderr)
    if hashes != {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}:
        raise RuntimeError('snapshot changed during mutation run')
    (out/'results.json').write_text(json.dumps(dict(snapshot_sha256=hashes, snapshot_unchanged=True,
        compile_args=common, linked_archive_limitation='Copied linked archives are hashed, not claimed rebuilt or sanitizer-instrumented here.',
        scope='Source mutations cover swap_authority.cpp; wire codecs receive roundtrip/malformed tests, not source mutations in this runner.', results=results), indent=2)+'\n')
    print(f'Killed {len(results)} mutants at designated assertions')


if __name__ == '__main__':
    main()
