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
    sources = ["tests/v2_swap_submission_resolution_tests.cpp", "src/v2_swap_submission_store.cpp",
               "src/v2_gateway_link.cpp", "src/v2_auth_envelope.cpp", "src/v2_node_protocol.cpp", "src/v2_session_fence.cpp"]
    common = ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DVELOCITA_TARGET_CACHE_LINE_SIZE=128", "-DVELOCITA_SWAP_SUBMISSION_RESOLUTION_TEST_ONLY=1",
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
    path = root / "src/v2_swap_submission_resolution.cpp"
    source = path.read_text()
    baseline = output / "baseline"
    subprocess.run(common + [str(path)] + link + ["-o", str(baseline)], check=True)
    green = subprocess.run([str(baseline)], text=True, capture_output=True, env=os.environ)
    (output / "green.log").write_text(green.stdout+green.stderr)
    if green.returncode:
        raise RuntimeError(green.stdout+green.stderr)
    print(green.stdout, end="")
    mutations = [
        ("skip_authority", "outcome.reading.authority_domain!=config_.authority_domain ||", "",
         "authenticated_state_and_arithmetic", "wrong domain stale index bad MAC digest arithmetic or response kind leaves journal unresolved"),
        ("skip_caller_floor", "outcome.reading.applied_index<minimum_index_ ||", "",
         "authenticated_state_and_arithmetic", "snapshot must cover caller committed floor"),
        ("skip_newer_result", "outcome.reading.applied_index<=captured_.committed_index ||", "",
         "authenticated_state_and_arithmetic", "wrong domain stale index bad MAC digest arithmetic or response kind leaves journal unresolved"),
        ("skip_effect", "if(candidate.effect!=captured_.identity.effect)continue;", "",
         "absent_or_mismatched_settlement", "absent wrong effect payload custody or conflicted settlement cannot resolve journal"),
        ("skip_payload", "candidate.payload!=captured_.identity.payload ||", "",
         "absent_or_mismatched_settlement", "absent wrong effect payload custody or conflicted settlement cannot resolve journal"),
        ("skip_conflict", "swap.conflicted ||", "",
         "absent_or_mismatched_settlement", "absent wrong effect payload custody or conflicted settlement cannot resolve journal"),
        ("skip_binding", "swap.opening.binding!=config_.binding", "false",
         "known_transaction_mapping_and_journal_change", "exact configured policy binding required for terminal resolution"),
        ("ignore_known_signature", "(known_transaction && candidate.transaction!=*known_transaction)", "(known_transaction && false)",
         "known_transaction_mapping_and_journal_change", "known signature must match canonical committed transaction identity"),
        ("accept_zero_signature", "if(std::all_of(signature.begin(),signature.end(),[](auto b){return b==std::byte{};}))return std::nullopt;", "",
         "canonical_signature_identity_oracle", "zero signature cannot create canonical transaction identity"),
        ("wrong_signature_domain", 'domain="velocita.solana.signature.v1"', 'domain="velocita.solana.signature.v2"',
         "canonical_signature_identity_oracle", "independent Python domain and raw signature hash oracle"),
        ("omit_signature_domain", "std::copy(signature.begin(),signature.end(),bytes.begin()+static_cast<std::ptrdiff_t>(domain.size()));\n    return sha256_digest(bytes);", "return sha256_digest(signature);",
         "canonical_signature_identity_oracle", "independent Python domain and raw signature hash oracle"),
        ("ignore_journal_changes", "if(!journal_unchanged())", "if(false)",
         "known_transaction_mapping_and_journal_change", "journal identity ordinal or phase change invalidates outstanding read"),
        ("ignore_timeout", "now>=deadline_", "false",
         "timeout_ownership_and_limits", "late queued response or backward clock refuses and releases owned read"),
        ("ignore_backward_tick", "now<last_tick_", "false",
         "timeout_ownership_and_limits", "late queued response or backward clock refuses and releases owned read"),
        ("ignore_overflow", "now>std::numeric_limits<Tick>::max()-config_.state_timeout", "false",
         "timeout_ownership_and_limits", "deadline overflow refuses before state request"),
        ("ignore_attempt_floor", "minimum_commit_index<store_.latest().committed_index ||", "",
         "timeout_ownership_and_limits", "minimum floor below retained attempt refuses"),
        ("ignore_resolution_sync", "if(store_.resolve({captured_.identity,outcome.reading.applied_index,outcome.reading.state_digest})!=SwapSubmissionCode::none)",
         "if(((void)store_.resolve({captured_.identity,outcome.reading.applied_index,outcome.reading.state_digest}),false))",
         "failed_durability_keeps_resolution_closed", "resolution sync failure never publishes resolved journal or sends command"),
        ("discard_destructor_read_ownership", "SwapSubmissionResolutionSession::~SwapSubmissionResolutionSession(){\n    if(result_.status==SwapSubmissionResolutionStatus::reading_state)link_.abandon();",
         "SwapSubmissionResolutionSession::~SwapSubmissionResolutionSession(){",
         "timeout_ownership_and_limits", "destroyed resolution session releases its pending read"),
        ("discard_refusal_read_ownership", "SwapSubmissionResolutionStatus status) noexcept {\n    if(result_.status==SwapSubmissionResolutionStatus::reading_state)link_.abandon();",
         "SwapSubmissionResolutionStatus status) noexcept {",
         "timeout_ownership_and_limits", "late queued response or backward clock refuses and releases owned read"),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        identity_mutation = name in {"accept_zero_signature", "wrong_signature_domain", "omit_signature_domain"}
        original = (root / "src/v2_swap_submission_store.cpp").read_text() if identity_mutation else source
        if original.count(old) != 1:
            raise RuntimeError(f"mutation {name} requires exactly one source match")
        changed = output / (name + ".cpp")
        changed.write_text(original.replace(old, new, 1))
        binary = output / name
        mutated_link = ([str(path)] + [p for p in link if p != str(output / "v2_swap_submission_store.o")]
                        if identity_mutation else link)
        built = subprocess.run(common + [str(changed)] + mutated_link + ["-o", str(binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f"mutation {name} failed compilation: {built.stderr}")
        result = subprocess.run([str(binary), case], capture_output=True, text=True, env=os.environ)
        killed = result.returncode == 1 and f"FAIL {case}: {assertion}\n" in result.stdout
        results.append({"mutation": name, "case": case, "assertion": assertion, "killed": killed,
                        "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        if not killed:
            raise RuntimeError(f"mutation {name} survived or failed elsewhere: {result.stdout} {result.stderr}")
    files = [root / p for p in sources] + [path, root / "include/velocita/v2/swap_submission_resolution.hpp", root / "include/velocita/v2/swap_submission_store.hpp"]
    report = {"source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              "linked_library_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in libraries},
              "results": results}
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Killed {len(results)} mutants at their designated assertions")


if __name__ == "__main__":
    main()
