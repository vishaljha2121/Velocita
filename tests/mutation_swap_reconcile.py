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
    source_path = root / "src/v2_swap_reconcile.cpp"
    source = source_path.read_text()
    mutations = [
        ("skip_watermark", "if (barrier.applied_index < cut.required_result_index)", "if (false)",
         "independent_time_and_result_barriers", "newly known in flight result invalidates earlier adequate state"),
        ("refresh_on_receive", "cut.now_ms - start <= cut.max_age_ms", "cut.now_ms - received <= cut.max_age_ms",
         "independent_time_and_result_barriers", "fresh receive cannot refresh stale state request"),
        ("skip_read_order", " || read.request_started_ms < barrier.received_at_ms", "",
         "independent_time_and_result_barriers", "balance request must follow completed state barrier"),
        ("skip_request_correlation", "read.request_id != cut.expected_request_id || ", "",
         "independent_time_and_result_barriers", "late reply from prior request refused"),
        ("skip_clock_epoch", "barrier.clock_epoch != cut.clock_epoch || read.clock_epoch != cut.clock_epoch ||\n        outside.clock_epoch != cut.clock_epoch ||", "",
         "independent_time_and_result_barriers", "restart cannot reuse prior clock epoch"),
        ("skip_partition_pins", "partition != cut.outside_partition_digest || partition != read.outside_partition_digest ||", "",
         "external_changes_and_pinned_partition", "changing outside atoms cannot reuse pinned declaration"),
        ("skip_partition_time", "outside.declared_at_ms > barrier.request_started_ms", "false",
         "external_changes_and_pinned_partition", "outside partition must precede state read"),
        ("skip_chain_identity", 'json_member_string(row, "chain") != "solana" || ', "",
         "rows_and_exact_decimals", "same symbol on another chain refused"),
        ("skip_held_funds", "if (wallet.base.held_atoms != 0 || wallet.quote.held_atoms != 0)", "if (false)",
         "rows_and_exact_decimals", "one held USDC atom blocks comparison"),
        ("ignore_outside_quote", "expected.quote_atoms += outside.quote_atoms;", "expected.quote_atoms += 0;",
         "exact_partition_and_response", "managed plus explicit outside atoms matches snapshot only"),
        ("drop_wallet_residual", "expected.residual_base_atoms = wallet_base % expected.base_atoms_per_lot;", "expected.residual_base_atoms = 0;",
         "exact_partition_and_response", "managed plus explicit outside atoms matches snapshot only"),
        ("misclassify_projection_refusal", "return {SwapReconcileCode::projection_refused, projected.code};", "return {SwapReconcileCode::compared_snapshot_only, projected.code};",
         "unknown_restart_and_late_settlement", "matching wallet never resolves unknown or releases holds"),
        ("zero_base_delta", "result.wallet = wallet;", "result.comparison.base_delta_atoms = 0; result.wallet = wallet;",
         "external_changes_and_pinned_partition", "one atom external deposit remains unexplained"),
    ]
    common = ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DVELOCITA_TARGET_CACHE_LINE_SIZE=128", "-fsanitize=address,undefined",
              "-fno-omit-frame-pointer", "-I" + str(root / "include"),
              str(root / "tests/v2_swap_reconcile_tests.cpp"),
              str(root / "src/v2_swap.cpp"), str(root / "src/v2_swap_codec.cpp"),
              str(root / "src/v2_swap_balance.cpp"), str(root / "src/v2_spot_balance.cpp"),
              str(root / "src/v2_truemarkets_wire.cpp"), str(root / "src/v2_digest.cpp")]
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
    files = [source_path, root / "include/velocita/v2/swap_reconcile.hpp",
             root / "tests/v2_swap_reconcile_tests.cpp", root / "src/v2_swap.cpp", root / "src/v2_swap_codec.cpp",
             root / "src/v2_swap_balance.cpp", root / "src/v2_spot_balance.cpp", root / "src/v2_truemarkets_wire.cpp",
             root / "src/v2_digest.cpp"]
    report = {"source_sha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in files}, "results": results}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Killed {len(results)} mutations by their designated assertions")


if __name__ == "__main__":
    main()
