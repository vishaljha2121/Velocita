import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MUTANTS = [
    ('journal_failure_stop', 'src/v2_swap_paper_gateway.cpp',
     'pending_digest_}) != SwapReconcileStoreCode::none) {\n            stopped_ = true; return;',
     'pending_digest_}) != SwapReconcileStoreCode::none) {\n            return;',
     'journal failure stops gateway in the acknowledgement step'),
    ('recovery_start_fence', 'src/v2_swap_paper_gateway.cpp', 'seen_index_ = recovery_.latest().result.index;',
     'seen_index_ = 0;', 'recovered durable index fences first stale state before any command'),
    ('acknowledgement_persistence', 'src/v2_swap_paper_gateway.cpp',
     'recovery_.persist({recovery_.latest().result.binding, outcome.index,\n                pending_digest_}) != SwapReconcileStoreCode::none',
     'false', 'gateway persists authenticated command acknowledgements before next action'),
    ('state_read_fence', 'src/v2_swap_paper_gateway.cpp', 'if (outcome.reading.applied_index < seen_index_) return;',
     'if (false) return;', 'recovered durable index fences first stale state before any command'),
    ('legacy_double_funding', 'src/v2_kernel.cpp', 'if (state.spot_swap.initialized &&', 'if (false &&',
     'legacy path cannot double fund swap balance'),
    ('strategy_bypass', 'src/v2_kernel.cpp', 'std::holds_alternative<swap::Intent>(value.record.command)) return kOperatorRole;',
     'std::holds_alternative<swap::Intent>(value.record.command)) return kOperatorRole | kStrategyRole;',
     'strategy cannot bypass rule evaluation with explicit swap intent'),
    ('catalog_admission', 'src/v2_swap_kernel.cpp', 'if (!swap_catalog_matches(opening->binding, *catalog))', 'if (false)',
     'SOL atomic denomination cannot bind BTC row'),
    ('prepared_economics', 'src/v2_swap_paper_gateway.cpp', 'if (payload != p.payload || payload != p.handle) return {};',
     'if (false) return {};', 'restart cannot change prepared quote economics'),
    ('snapshot_balance', 'src/v2_swap.cpp', 'baseline.reserved_base != s.reserved_base || baseline.reserved_quote != s.reserved_quote',
     'baseline.reserved_base != s.reserved_base', 'snapshot refuses corrupt reserved balance'),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    link = shlex.split((build / 'CMakeFiles/v2-swap-integration-tests.dir/link.txt').read_text())
    flags = (build / "CMakeFiles/velocita_v2_swap_paper_gateway.dir/flags.make").read_text()
    includes = shlex.split(next(line.split("=", 1)[1] for line in flags.splitlines() if line.startswith("CXX_INCLUDES =")))
    scratch = Path.home() / '.cache/velocita-build/swap-integration-mutations'
    scratch.mkdir(parents=True, exist_ok=True)
    results = []
    for name, relative, old, new, assertion in MUTANTS:
        original = (ROOT / relative).read_text()
        assert original.count(old) == 1, name
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            work = Path(directory)
            source, obj, binary = work / 'mutant.cpp', work / 'mutant.o', work / 'check'
            source.write_text(original.replace(old, new))
            compiled = subprocess.run([link[0], '-std=c++20', '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
                *includes, '-c', str(source), '-o', str(obj)], capture_output=True, text=True)
            if compiled.returncode:
                raise RuntimeError(f'{name}: compiler failure is not a kill: {compiled.stderr}')
            command = shlex.split((build / "CMakeFiles/v2-gateway-node-tests.dir/link.txt").read_text()) if name in {"state_read_fence", "recovery_start_fence", "acknowledgement_persistence", "journal_failure_stop"} else link[:]
            command[command.index('-o') + 1] = str(binary)
            command.insert(1, str(obj))
            linked = subprocess.run(command, cwd=build, capture_output=True, text=True)
            if linked.returncode:
                raise RuntimeError(f'{name}: link failure is not a kill: {linked.stderr}')
            env = dict(os.environ)
            if name in {"state_read_fence", "recovery_start_fence", "acknowledgement_persistence", "journal_failure_stop"}:
                env.update(GATEWAY_TEST=("swap_paper_journal_write_failure" if name == "journal_failure_stop" else "swap_paper_raft_gateway_restart"), TMPDIR=str(scratch))
            tested = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, env=env)
            output = tested.stdout + tested.stderr
            killed = tested.returncode == 1 and f': {assertion}' in output
            results.append(dict(name=name, killed=killed, named_assertion=assertion, output=output,
                source_sha256=hashlib.sha256(original.encode()).hexdigest()))
            print(name, 'KILLED' if killed else 'SURVIVED', flush=True)
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    return 0 if all(r['killed'] for r in results) else 1

if __name__ == '__main__':
    raise SystemExit(main())
