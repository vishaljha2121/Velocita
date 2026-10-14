import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source_path = root / "src/v2_swap_balance.cpp"
    source = source_path.read_text()
    mutations = [
        ("skip_state_validation", "if (!swap::valid_state(state))", "if (false)",
         "binding_and_state_validation", "unattributed one atom corruption yields no usable expectation"),
        ("skip_allocation_identity", "binding.allocation != state.opening.binding || ", "",
         "binding_and_state_validation", "changed allocation version refused"),
        ("use_custody_digest", "expected.account_and_assets_digest = binding.account_and_assets_digest;",
         "expected.account_and_assets_digest = state.opening.binding.custody;",
         "residual_and_sub_lot_atoms", "trusted account digest is distinct from custody digest"),
        ("discard_residual", "static_cast<std::int64_t>(state.base.value() % binding.base_atoms_per_lot)", "0",
         "residual_and_sub_lot_atoms", "oracle three lots plus 1896934 atoms preserves funding"),
        ("double_charge_fee", "static_cast<std::int64_t>(state.quote.value())",
         "static_cast<std::int64_t>(state.quote.value() - state.fees.value())",
         "exact_settlement_projects_once", "oracle 31896934 plus 9100123 and 5000000 minus 1002000"),
        ("skip_pending_gate", "if (state.pending.phase != swap::Phase::none)", "if (false)",
         "unresolved_and_conflicted_do_not_match", "reserved swap blocks baseline comparison"),
        ("skip_conflict_gate", "if (state.conflicted)", "if (false)",
         "unresolved_and_conflicted_do_not_match", "conflict cannot expose last good balance as current"),
        ("skip_base_range", "state.base.value() > maximum || ", "",
         "signed_range_boundaries", "unsigned base beyond comparator range refused"),
        ("skip_lot_range", " || binding.base_atoms_per_lot > maximum", "",
         "signed_range_boundaries", "unsigned lot divisor beyond comparator range refused"),
    ]
    common = ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DVELOCITA_TARGET_CACHE_LINE_SIZE=128", "-fsanitize=address,undefined",
              "-fno-omit-frame-pointer", "-I" + str(root / "include"),
              str(root / "tests/v2_swap_balance_tests.cpp"),
              str(root / "src/v2_spot_balance.cpp"), str(root / "src/v2_swap.cpp"),
              str(root / "src/v2_swap_codec.cpp"), str(root / "src/v2_digest.cpp")]
    baseline = output / "baseline"
    subprocess.run(common + [str(source_path), "-o", str(baseline)], check=True)
    subprocess.run([str(baseline)], check=True)
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old) != 1:
            raise RuntimeError(f"mutation {name} needs exactly one source match")
        mutated = output / f"{name}.cpp"
        binary = output / name
        mutated.write_text(source.replace(old, new, 1))
        compile_result = subprocess.run(common + [str(mutated), "-o", str(binary)], capture_output=True, text=True)
        if compile_result.returncode:
            raise RuntimeError(f"mutation {name} did not compile: {compile_result.stderr}")
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        killed = result.returncode == 1 and f"FAIL {case}: {assertion}\n" in result.stdout
        results.append({"mutation": name, "case": case, "named_assertion": assertion,
                        "exit": result.returncode, "killed": killed, "stdout": result.stdout,
                        "stderr": result.stderr})
        if not killed:
            raise RuntimeError(f"mutation {name} survived or failed for the wrong reason: {result.stdout} {result.stderr}")
    files = [source_path, root / "include/velocita/v2/swap_balance.hpp",
             root / "tests/v2_swap_balance_tests.cpp", root / "src/v2_swap.cpp",
             root / "include/velocita/v2/swap.hpp", root / "src/v2_spot_balance.cpp"]
    report = {"source_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in files}, "results": results}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Killed {len(results)} mutations by their designated assertions")


if __name__ == "__main__":
    main()
