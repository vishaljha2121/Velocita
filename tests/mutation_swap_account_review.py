"""Isolated sanitizer builds; only the designated assertion counts as a kill."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--component', choices=('review', 'history'), default='review')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    target = build / 'CMakeFiles/v2-swap-runtime-production-tests.dir'
    flags = {}
    for line in (target / 'flags.make').read_text().splitlines():
        if ' = ' in line:
            k, v = line.split(' = ', 1)
            flags[k] = shlex.split(v)
    link = shlex.split((target / 'link.txt').read_text())
    libraries = [str(build / x) if not Path(x).is_absolute() else x
                 for x in link if x.endswith(('.a', '.dylib', '.tbd', '.so'))]
    source_path = root / ('src/v2_swap_account_' + args.component + '.cpp')
    test = root / 'tests/v2_swap_runtime_production_tests.cpp'
    tracked = [source_path, test, root / ('include/velocita/v2/swap_account_' + args.component + '.hpp')]
    hashes = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    source = source_path.read_text()
    common = [link[0], '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] + flags['CXX_DEFINES'] + flags['CXX_INCLUDES']
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    mutations = [
        ('digest', 'sha256_digest(std::as_bytes(std::span(document.data(),document.size())))!=expected', 'false', 'unreviewed document digest refuses'),
        ('identity', 'r.runtime_identity==identity', 'true', 'wrong owner refuses'),
        ('opening', 'r.opening_digest==opening', 'true', 'changed funding evidence refuses'),
        ('user', 'r.user_id.view()==user', 'true', 'wrong account user refuses'),
        ('duplicates', 'if(event.evidence==r.activity[j].evidence)return {};', 'if(false)return {};', 'duplicate activity evidence refuses'),
        ('expiry', 'now<r.expires_at_ms', 'now<=r.expires_at_ms', 'review expiry exclusive oracle'),
        ('balance_verdict', 'result.comparison.verdict==SpotBalanceVerdict::matching_snapshot_only', 'true', 'unexplained atom refuses without funding mutation'),
    ]
    if args.component == 'history':
        mutations = [
            ('digest', 'sha256_digest(std::as_bytes(std::span(body)))!=expected', 'false', 'unreviewed history bytes refused'),
            ('stable', 'json_member_raw(body,"stable_window")!="true"', 'false', 'unsafe or incomplete history cut refused'),
            ('pagination', 'json_member_raw(body,"pagination_exhausted")!="true"', 'false', 'unsafe or incomplete history cut refused'),
            ('pending', 'json_member_raw(body,"blocking_orders")!="0"', 'false', 'unsafe or incomplete history cut refused'),
            ('owner', 'cut.owner==sha256_digest(binding.owner_wallet)', '(static_cast<void>(binding),true)', 'history for another wallet refused'),
            ('user', 'cut.user==sha256_digest(std::as_bytes(std::span(review.user_id.view())))', 'true', 'history for another user refused'),
            ('evidence', 'cut.document==review.history_evidence', 'true', 'different history evidence refused'),
            ('funding', 'static_cast<std::uint64_t>(outside->quote_atoms)==cut.quote_atoms-state.quote.value()', 'true', 'one unassigned history atom refused'),
            ('age', "now-cut.finished_ms>900'000", 'false', 'new review cannot revive stale history'),
            ('duration', "cut.finished_ms-cut.started_ms>300'000", 'false', 'capture over five minutes refused'),
        ]
    results = []
    for name, old, new, assertion in [('baseline', '', '', '')] + mutations:
        changed = out / (name + '.cpp')
        if old and source.count(old) != 1:
            raise RuntimeError('nonunique mutation: ' + name)
        changed.write_text(source.replace(old, new) if old else source)
        binary = out / name
        built = subprocess.run(common + [str(test), str(changed)] + libraries + ['-o', str(binary)], capture_output=True, text=True)
        (out / (name + '-build.log')).write_text(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError('compile failure is not a kill: ' + name)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, env=env)
        (out / (name + '.log')).write_text(ran.stdout + ran.stderr)
        ok = (ran.returncode == 0) if not old else (ran.returncode == 1 and ran.stderr.strip() == assertion)
        results.append(dict(name=name, passed=ok, exit=ran.returncode, assertion=assertion))
    unchanged = all(hashlib.sha256((root / p).read_bytes()).hexdigest() == h for p, h in hashes.items())
    (out / 'results.json').write_text(json.dumps(dict(results=results, source_sha256=hashes,
        source_unchanged=unchanged, libraries={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in libraries}), indent=2)+'\n')
    if not unchanged or not all(r['passed'] for r in results):
        raise RuntimeError('baseline, mutation, or source preservation failed')
    print(f'Sanitizer baseline and {len(mutations)}/{len(mutations)} named mutation kills; source unchanged')


if __name__ == '__main__':
    main()
