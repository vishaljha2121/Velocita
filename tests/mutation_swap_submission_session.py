import argparse
import os
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
    sources = ["tests/v2_swap_submission_session_tests.cpp", "src/v2_swap_submission_store.cpp",
               "src/v2_gateway_link.cpp", "src/v2_auth_envelope.cpp", "src/v2_node_protocol.cpp", "src/v2_session_fence.cpp"]
    common = ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DVELOCITA_TARGET_CACHE_LINE_SIZE=128", "-DVELOCITA_SWAP_SUBMISSION_SESSION_TEST_ONLY=1",
              "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
              "-I" + str(root / "include"), "-I" + str(root / "tests"), "-I" + str(args.raft_include.resolve())]
    objects = []
    for source in sources:
        obj = output / (Path(source).stem + ".o")
        subprocess.run(common + ["-c", str(root / source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    libraries = [args.root_build.resolve() / ("lib" + name + ".a") for name in
                 ["velocita_v2_node", "velocita_v2_replication", "velocita_v2_kernel",
                  "velocita_v2_evaluator", "velocita_core", "willemt_raft", "velocita_v2_contracts"]]
    link = objects + [str(p) for p in libraries] + ["-pthread"]
    path = root / "src/v2_swap_submission_session.cpp"
    source = path.read_text()
    baseline = output / "baseline"
    subprocess.run(common + [str(path)] + link + ["-o", str(baseline)], check=True)
    subprocess.run([str(baseline)], check=True, env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
    mutations = [
        ("skip_request_hash", "sha256_digest(request)!=identity.execution_request ||", "",
         "payload_copy_and_input_binding", "request digest mismatch refuses before state or send"),
        ("skip_authority_domain", "outcome.reading.authority_domain!=config_.authority_domain ||", "",
         "authenticated_state_fences", "wrong domain stale state bad MAC digest or timeout never sends"),
        ("skip_commit_floor", "outcome.reading.applied_index<minimum_index_ ||", "",
         "authenticated_state_fences", "wrong domain stale state bad MAC digest or timeout never sends"),
        ("skip_effect", "pending.intent.effect!=identity_.effect ||", "",
         "exact_pending_identity", "effect payload and exact handle must match committed pending swap"),
        ("skip_payload", "pending.payload!=identity_.payload ||", "",
         "exact_pending_identity", "effect payload and exact handle must match committed pending swap"),
        ("skip_handle", "pending.handle!=identity_.handle_digest ||", "",
         "exact_pending_identity", "effect payload and exact handle must match committed pending swap"),
        ("skip_phase", "pending.phase!=swap::Phase::attempted ||", "",
         "current_controls_deadline_and_phase", "halt unknown expired or backwards event time refuses without releasing exposure"),
        ("skip_deadline", "event_time>=pending.intent.sign_by", "false",
         "current_controls_deadline_and_phase", "halt unknown expired or backwards event time refuses without releasing exposure"),
        ("skip_binding", "swap.opening.binding!=config_.binding ||", "",
         "invalid_startup_and_backward_tick", "authenticated state must match configured custody and allocation"),
        ("skip_monotonic_tick", "if(now<last_tick_)", "if(false)",
         "invalid_startup_and_backward_tick", "backwards timeout clock cannot extend submission admission"),
        ("skip_sync_failure", "if(store_.begin(identity_,outcome.reading.applied_index)!=SwapSubmissionCode::none)",
         "if((static_cast<void>(store_.begin(identity_,outcome.reading.applied_index)),false))",
         "disk_failure_and_ambiguous_send", "journal sync failure prevents send and preserves reservation"),
        ("send_before_durable_intent", "if(store_.begin(identity_,outcome.reading.applied_index)!=SwapSubmissionCode::none)",
         "port_.execute(identity_,std::span(request_).first(request_size_));\n    if(store_.begin(identity_,outcome.reading.applied_index)!=SwapSubmissionCode::none)",
         "durable_before_send_and_no_repeat", "exact execute identity is durable before port invocation"),
        ("skip_read_deadline", "if(now>=deadline_)", "if(false)",
         "late_queued_response_and_deadline_overflow", "queued valid reply at deadline cannot cause execution"),
        ("skip_deadline_overflow", "now>std::numeric_limits<Tick>::max()-config_.state_timeout ||", "",
         "late_queued_response_and_deadline_overflow", "overflowing deadline refuses before state request"),
        ("leave_read_on_destruct", "SwapSubmissionSession::~SwapSubmissionSession() {\n    if(status_==SwapSubmissionSessionStatus::reading_state)link_.abandon();",
         "SwapSubmissionSession::~SwapSubmissionSession() {",
         "owned_state_request_cleanup", "destroying session abandons only its owned state request"),
        ("leave_read_on_refusal", "SwapSubmissionSessionStatus SwapSubmissionSession::refuse(SwapSubmissionSessionStatus s) noexcept {\n    if(status_==SwapSubmissionSessionStatus::reading_state)link_.abandon();",
         "SwapSubmissionSessionStatus SwapSubmissionSession::refuse(SwapSubmissionSessionStatus s) noexcept {",
         "owned_state_request_cleanup", "early refusal releases owned link request"),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        original = source
        if original.count(old) != 1:
            raise RuntimeError(f"mutation {name} requires exactly one source match")
        changed = output / (name + ".cpp")
        changed.write_text(original.replace(old, new, 1))
        binary = output / name
        built = subprocess.run(common + [str(changed)] + link + ["-o", str(binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f"mutation {name} failed compilation: {built.stderr}")
        result = subprocess.run([str(binary), case], capture_output=True, text=True, env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
        killed = result.returncode == 1 and f"FAIL {case}: {assertion}\n" in result.stdout
        results.append({"mutation": name, "case": case, "assertion": assertion, "killed": killed,
                        "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        if not killed:
            raise RuntimeError(f"mutation {name} survived or failed elsewhere: {result.stdout} {result.stderr}")
    files = [root / p for p in sources] + [path, root / "include/velocita/v2/swap_submission_session.hpp", root / "include/velocita/v2/swap_submission_store.hpp"]
    report = {"source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              "linked_library_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in libraries},
              "results": results}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Killed {len(results)} mutants at their designated assertions")


if __name__ == "__main__":
    main()
