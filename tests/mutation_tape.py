#!/usr/bin/env python3
"""Mutation run for the tape's core guarantees.

Each mutant is applied to a scratch copy of the sources, compiled directly with c++ (never through CMake, so no
stale binary can answer for it), and must make one named test fail. Usage:
  mutation_tape.py REPO BUILD_DIR OUT.json [--only NAME]
BUILD_DIR supplies the prebuilt Raft and storage libraries, which no mutant touches.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CORE = ["src/tape_frame.cpp", "src/tape_entry.cpp", "src/tape_merkle.cpp", "src/tape_reducer.cpp", "src/v2_digest.cpp"]
REPLICA = CORE + ["src/tape_wal.cpp", "src/tape_replica.cpp"]
VOTER = REPLICA + ["src/tape_auth.cpp", "src/tape_config.cpp", "src/tape_signing.cpp", "src/tape_peer_codec.cpp",
                   "src/tape_voter.cpp"]

R = "src/tape_reducer.cpp"
MUTANTS = [
    ("close-one-tick-late", R, "s.last_tick >= v.first_tick + c.config.verdict_ticks", "s.last_tick > v.first_tick + c.config.verdict_ticks",
     "core", "test_silent_recorder_closes_at_exactly_n_ticks"),
    ("close-on-two-reports", R, "if (reported == kRecorders) close_verdict", "if (reported >= 2) close_verdict",
     "core", "test_all_three_close_at_the_third_copy"),
    ("count-duplicate-copy", R, "        if (v->copies[r].present) {\n            ++s.counters.duplicate_copies;",
     "        if (false && v->copies[r].present) {\n            ++s.counters.duplicate_copies;", "core", "test_a_recorders_second_copy_is_ignored"),
    ("late-copy-reopens", R, "    if (const auto* closed = find_closed(s, owner, stream, o.kind, o.seqnum)) {",
     "    if (const ClosedRef* closed = nullptr) {", "core", "test_late_divergent_copy_makes_the_book_unknown_without_reopening"),
    ("majority-vote", R, "out.outcome = distinct > 1 ", "out.outcome = distinct > 2 ", "core", "test_three_versions_are_divergent_not_a_majority"),
    ("evict-newest", R, "if (!oldest || v.first_index < oldest->first_index)", "if (!oldest || v.first_index > oldest->first_index)",
     "core", "test_capacity_closes_the_oldest"),
    ("accept-tick-jump", R, "if (t->number == state.last_tick + 1)", "if (t->number > state.last_tick)", "core", "test_ticks_must_be_consecutive"),
    ("ignore-divergence", R, "if (copy.hash != v->applied_hash && divergence_matters) set_unknown",
     "if (copy.hash != v->applied_hash && false) set_unknown", "core", "test_three_versions_are_divergent_not_a_majority"),
    ("apply-out-of-order", R, "            if (seqnum > book.position) {\n                hold_update(s, book, seqnum, c.index, payload);\n                return;\n            }",
     "", "core", "test_held_message_applies_in_order_when_it_arrives_in_time"),
    ("wait-unbounded", R, "        else if (book.held_count > 0 && s.last_tick >= book.hold_tick + c.config.verdict_ticks)\n            set_unknown(s, book, Reason::gap);",
     "", "core", "test_missing_message_makes_the_book_unknown_after_n_ticks"),
    ("full-buffer-silent", R, "    if (book.held_count == kHeldPerBook) {\n        set_unknown(s, book, Reason::capacity);\n        return false;\n    }",
     "    if (book.held_count == kHeldPerBook) return false;", "core", "test_reorder_buffer_full_is_unknown_at_once"),
    ("apply-stale-snapshot", R, "            if (book.has_position && seqnum < book.position) {\n                c.flags |= kFlagStaleSnapshot;\n                return;\n            }",
     "", "core", "test_stale_snapshot_is_ignored_and_interleaved_snapshot_applies_at_complete"),
    ("hash-unknown-book", R, "book.hash = book.state == BookState::valid ? book_hash(s.symbols[sym], book) : Digest{};",
     "book.hash = book_hash(s.symbols[sym], book);", "core", "test_capacity_while_draining_leaves_no_hash"),
    ("silent-level-overflow", R, "    if (side.size == kLevelsPerSide) return false;", "    if (side.size == kLevelsPerSide) return true;",
     "core", "test_level_capacity_makes_the_book_unknown"),
    ("round-precision", "src/tape_frame.cpp", "            if (fraction_digits == 9) return DecimalStatus::precision;",
     "            if (fraction_digits == 9) break;", "core", "test_decimal_boundaries"),
    ("accept-duplicate-request", R, "    if (h.request_seq <= session.last_request) return ApplyCode::duplicate_request;", "",
     "core", "test_duplicate_and_out_of_order_requests_are_refused"),
    ("accept-request-gap", R, "    if (h.request_seq != session.last_request + 1) return ApplyCode::out_of_order_request;", "",
     "core", "test_duplicate_and_out_of_order_requests_are_refused"),
    ("skip-part-hash", R, "a.filled == a.whole_size && v2::sha256_digest(whole) == a.whole_hash", "a.filled == a.whole_size",
     "core", "test_split_observation_matches_a_whole_one"),
    ("node-hash-prefix", "src/tape_merkle.cpp", "    const std::byte prefix{0x01};", "    const std::byte prefix{0x00};", "core", "test_cpp_merkle_matches_rfc_vectors"),
    ("frontier-order", "src/tape_merkle.cpp", "acc = have ? merkle_node_hash(levels[level], acc) : levels[level];",
     "acc = have ? merkle_node_hash(acc, levels[level]) : levels[level];", "core", "test_cluster_root_equals_reference_root_and_proofs_verify"),
    ("integrity-changes-book", R, "        if (bid > ask) c.flags |= kFlagCrossed;", "        if (bid > ask) { c.flags |= kFlagCrossed; book.position += 1; }",
     "core", "test_integrity_checks_never_change_books"),
    ("snapshots-compared-across-copies", R, "const std::uint64_t owner = o.kind == UpdateKind::update ? 0 : reply_owner(o.header);",
     "const std::uint64_t owner = 0;", "core", "test_snapshots_from_different_subscriptions_are_not_copies"),
    ("snapshot-compared-too-early", R, "            book.compare_pending = book.staging_compare && c.config.integrity_checks;",
     "            if (book.staging_compare && c.config.integrity_checks && (compare_sides(book.bids, book.staged_bids) + compare_sides(book.asks, book.staged_asks)) > 0) c.flags |= kFlagSnapshotDiffers;\n            book.compare_pending = false;",
     "core", "test_snapshots_from_different_subscriptions_are_not_copies"),
    ("voter-proposes-duplicates", "src/tape_voter.cpp",
     "            auto next = replica_->highest_request_in_log(r, s) + 1;\n            while (true) {\n                const auto it = queue.find({s, next});\n                if (it == queue.end()) break;\n                if (replica_->propose(it->second) != ProposeStatus::proposed) return;\n                queue.erase(it);",
     "            auto next = replica_->highest_request_in_log(r, s) + 1;\n            while (true) {\n                const auto it = queue.find({s, next});\n                if (it == queue.end()) break;\n                if (replica_->propose(it->second) != ProposeStatus::proposed) return;\n                (void)replica_->propose(it->second);\n                queue.erase(it);",
     "voter", "the leader never proposed a duplicate or out-of-order request"),
    ("voter-accepts-recorder-tick", "src/tape_voter.cpp", "        if (!h || h->recorder != from.id) {\n            ++refused_;\n            return;\n        }",
     "        if (std::holds_alternative<Tick>(*entry)) {\n            if (replica_->is_leader()) (void)replica_->propose(bytes);\n            return;\n        }\n        if (!h || h->recorder != from.id) {\n            ++refused_;\n            return;\n        }",
     "voter", "a recorder's tick and a recorder impersonating another are refused by every voter"),
    ("head-never-gains-signatures", "src/tape_voter.cpp",
     "(already != published_ticks_.end() && valid <= already->second)", "(already != published_ticks_.end())",
     "voter", "a published head gains the third signature once it lands"),
    ("skip-hmac-check", "src/tape_auth.cpp", "    if (!same(expected.bytes, frame.last(32))) return std::nullopt;", "",
     "voter", "a frame sealed with the wrong key is rejected"),
    ("wal-accepts-corruption", "src/tape_wal.cpp", "            if (allow_torn_tail && last) break;", "            if (allow_torn_tail) break;",
     "replica", "mid-log corruption refuses recovery"),
]


def build(scratch, build_dir, target, out):
    inc = ["-I", str(scratch / "include"), "-I", str(Path.home() / ".cache/velocita-deps/willemt-raft-cp076/include"),
           "-I/opt/homebrew/include"]
    base = ["c++", "-std=c++20", "-O1", "-DVELOCITA_TARGET_CACHE_LINE_SIZE=64", *inc]
    libs = [str(build_dir / "libvelocita_core.a"), str(build_dir / "libwillemt_raft.a")]
    if target == "core":
        cmd = base + [str(scratch / "tests/tape_log_apply.cpp")] + [str(scratch / f) for f in CORE] + ["-o", str(out)]
    elif target == "replica":
        cmd = base + [str(scratch / "tests/tape_replica_tests.cpp")] + [str(scratch / f) for f in REPLICA] + libs + ["-o", str(out)]
    else:
        cmd = base + [str(scratch / "tests/tape_voter_tests.cpp")] + [str(scratch / f) for f in VOTER] + libs + \
              ["-L/opt/homebrew/lib", "-lcrypto", "-o", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0 and out.exists(), r.stderr[-2000:]


def run_test(scratch, target, binary, test):
    if target == "core":
        env = dict(os.environ, TAPE_LOG_APPLY=str(binary))
        cls = "MerkleAgainstReference" if test.startswith(("test_rfc", "test_cluster_root", "test_proof", "test_cpp_merkle")) else "ReducerAgainstReference"
        r = subprocess.run([sys.executable, "-B", str(scratch / "tests/test_tape_core.py"), f"{cls}.{test}"],
                           capture_output=True, text=True, env=env, timeout=600)
        return r.returncode != 0 and ("FAIL: " + test in r.stderr or "ERROR: " + test in r.stderr), r.stderr[-1500:]
    r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=600)
    return r.returncode != 0 and f"FAIL: {test}" in r.stderr, r.stderr[-1500:]


def main():
    repo, build_dir, out_path = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    results = []
    for name, rel, old, new, target, test in MUTANTS:
        if only and name != only:
            continue
        with tempfile.TemporaryDirectory(prefix="tape-mut-") as tmp:
            scratch = Path(tmp)
            for part in ("include", "src", "tests"):
                shutil.copytree(repo / part, scratch / part, ignore=shutil.ignore_patterns("__pycache__"))
            text = (scratch / rel).read_text()
            if text.count(old) != 1:
                results.append({"mutant": name, "verdict": "NOT-APPLIED", "detail": f"pattern found {text.count(old)} times"})
                print(name, "NOT-APPLIED", flush=True)
                continue
            clean_binary = scratch / "clean"
            ok, _ = build(scratch, build_dir, target, clean_binary)
            clean_failed, _ = run_test(scratch, target, clean_binary, test) if ok else (True, "")
            (scratch / rel).write_text(text.replace(old, new))
            binary = scratch / "mutant"
            built, err = build(scratch, build_dir, target, binary)
            if not ok or clean_failed:
                verdict, detail = "CLEAN-FAILS", "the unmutated build or test failed"
            elif not built:
                verdict, detail = "NOT-BUILT", err
            else:
                killed, detail = run_test(scratch, target, binary, test)
                verdict = "KILLED" if killed else "SURVIVED"
            results.append({"mutant": name, "file": rel, "test": test, "verdict": verdict, "detail": detail[-400:]})
            print(name, verdict, flush=True)
    out_path.write_text(json.dumps(results, indent=1))
    survivors = [r for r in results if r["verdict"] != "KILLED"]
    print(f"{len(results) - len(survivors)}/{len(results)} killed")
    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
