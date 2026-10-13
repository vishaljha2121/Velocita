import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root-build", type=Path, required=True)
    parser.add_argument("--raft-include", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources = ["tests/v2_swap_reconcile_session_tests.cpp", "src/v2_swap_https.cpp",
               "src/v2_swap_reconcile.cpp", "src/v2_swap_balance.cpp", "src/v2_spot_balance.cpp",
               "src/v2_truemarkets_wire.cpp", "src/v2_gateway_link.cpp", "src/v2_auth_envelope.cpp",
               "src/v2_node_protocol.cpp", "src/v2_session_fence.cpp"]
    common = ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DVELOCITA_TARGET_CACHE_LINE_SIZE=128", "-DVELOCITA_SWAP_RECONCILE_SESSION_TEST_ONLY=1",
              "-DVELOCITA_SWAP_HTTPS_TEST_ONLY=1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
              "-I" + str(root / "include"), "-I" + str(root / "tests"), "-I" + str(args.raft_include.resolve())]
    objects = []
    for source in sources:
        obj = output / (Path(source).stem + ".o")
        subprocess.run(common + ["-c", str(root / source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    libraries = [args.root_build.resolve() / ("lib" + name + ".a") for name in
                 ["velocita_v2_node", "velocita_v2_replication", "velocita_v2_kernel",
                  "velocita_v2_evaluator", "velocita_core", "willemt_raft", "velocita_v2_contracts"]]
    link = objects + [str(p) for p in libraries] + ["-lcurl", "-pthread"]
    path = root / "src/v2_swap_reconcile_session.cpp"
    source = path.read_text()
    watermark_path = root / "src/v2_swap_result_watermark.cpp"
    watermark_source = watermark_path.read_text()
    baseline = output / "baseline"
    subprocess.run(common + [str(path), str(watermark_path)] + link + ["-o", str(baseline)], check=True)
    subprocess.run([str(baseline)], check=True)
    mutations = [
        ("accept_unexplained_delta", "result_.comparison.comparison.verdict==SpotBalanceVerdict::matching_snapshot_only?",
         "true?", "unexplained_delta_is_not_complete",
         "unexplained wallet withdrawal refuses completion and retains exact delta"),
        ("skip_seed_gate", "!valid_ || watermark_.index()<=0 ||", "!valid_ ||",
         "unseeded_and_bad_identity_never_read_http", "restart starts closed without durable seed"),
        ("skip_authority_domain", "outcome.reading.authority_domain!=config_.authority_domain ||", "",
         "unseeded_and_bad_identity_never_read_http", "authenticated other authority domain cannot authorize balance read"),
        ("freeze_end_watermark", "result_.end_result_watermark=watermark_.index();",
         "result_.end_result_watermark=outcome.reading.applied_index;",
         "watermark_during_http_and_monotonicity", "commit learned during synchronous GET invalidates captured old state"),
        ("allow_lower_index", "if(committed_index<current) return false;", "if(committed_index<current) return true;",
         "watermark_during_http_and_monotonicity", "committed watermark cannot decrease"),
        ("do_not_close_invalid_index", "index_.store(-1,std::memory_order_release);", "",
         "watermark_during_http_and_monotonicity", "invalid committed index permanently closes watermark"),
        ("ignore_http_status", "response.http_status!=200 ||", "",
         "http_failures_and_stale_completion", "bad HTTP result refused without body span or retry"),
        ("ignore_http_exposure", "response.exposure!=transport::SwapHttpsExposure::response_received ||", "",
         "http_failures_and_stale_completion", "bad HTTP result refused without body span or retry"),
        ("disable_completion_age_gate", "received,config_.max_age_ms};", "received,std::numeric_limits<std::uint64_t>::max()};",
         "http_failures_and_stale_completion", "late HTTP response fails request age gate"),
        ("skip_pending_preflight", "if(projected.code!=SwapBalanceCode::ready_for_comparison)", "if(false)",
         "pending_state_and_late_identity_remain_closed", "authenticated pending state keeps reservation and never issues GET"),
        ("skip_pre_http_watermark", "if(outcome.reading.applied_index<before_http)", "if(false)",
         "pending_state_and_late_identity_remain_closed", "state behind known result refuses HTTP before read"),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        in_session = source.count(old) == 1
        original = source if in_session else watermark_source
        other = watermark_path if in_session else path
        if original.count(old) != 1:
            raise RuntimeError(f"mutation {name} requires exactly one source match")
        changed = output / (name + ".cpp")
        changed.write_text(original.replace(old, new, 1))
        binary = output / name
        built = subprocess.run(common + [str(changed), str(other)] + link + ["-o", str(binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f"mutation {name} failed compilation: {built.stderr}")
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        killed = result.returncode == 1 and f"FAIL {case}: {assertion}\n" in result.stdout
        results.append({"mutation": name, "case": case, "assertion": assertion, "killed": killed,
                        "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        if not killed:
            raise RuntimeError(f"mutation {name} survived or failed elsewhere: {result.stdout} {result.stderr}")
    files = [root / p for p in sources] + [path, watermark_path, root / "include/velocita/v2/swap_reconcile_session.hpp"]
    report = {"source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              "linked_library_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in libraries},
              "results": results}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Killed {len(results)} mutants at their designated assertions")


if __name__ == "__main__":
    main()
