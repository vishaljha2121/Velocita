# CP-037: approved integrated install semantics — contract witnesses

2026-09-23. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation is
uncommitted. Owner approved CP-036 after the simple-language/C++ explanation.
No unspecified archive or transport bytes, quotas or remote receipts are approved.

## Evidence classification

There is no production live-install API yet. A test-only incorrect acknowledgement
policy is a counterexample to the draft, NOT a reproduced current installer bug.
Likewise, cold recovery intentionally suppresses committed notifications; proving
that behavior is not proving a defect in cold recovery. The eventual live install
must instead preserve the SAME ledger and capture outstanding outcomes.

The original end-to-end lagger reproducer remains the actual implementation RED.
Do not replace it with either contract witness or report F5 closed. The full
mixed READY/covered RESERVED/committed suffix/uncommitted suffix activation trace
remains required when the integrated installer exists.

## Main review corrections

The first ACK draft used a single server and asserted a later verified response
without running replication. Main rejected that oracle. It also returned a fixture
whose callback context pointed into its local object: optional NRVO is not a
lifetime guarantee. Final fixture has stable nonmovable RAII ownership, two real
Raft servers, coherent shared term5/divergent term6/new term7 history, actual suffix
replacement and a subsequent commit heartbeat. The replacement request advertises
only the leader's actual committed prefix, not a speculative commit20.

The result worker supplied a warning-clean compile only. Main runtime found two
fixture failures, not product REDs: `/tmp` is a symlink on this host and rejected
by startup's no-follow parent open; caller17 cannot advance the kill epoch because
the operator band ends at15. Main canonicalized the newly owned temp directory
and used operator caller7. Both failed outputs are preserved as
`main-results-fixture-*-failure.txt`; neither counts as correctness RED evidence.
An initial main compile also used the wrong dependency include directory and
failed with missing raft.h; corrected to cached `build/_deps/willemt_raft-src/include`.

The results fixture's WAL writer is test-only raw framing. It proves readable
recovery image semantics, not file/directory-sync ordering or power-loss durability.
Declared peer framing in that test is lineage plumbing, not peer authentication.

Reachability correction: healthy public owner calls currently apply through
commit, and accepted install requires S>old applied. No healthy public live
S<old commit capture was established. The committed-suffix cohort remains a
defensive future/intermediate contract, not a reproduced current live failure.

## Outcome-based progress

Owner requires every task to enable E1 safe actual catch-up, E2 actual compaction
preserving verified historical evidence, E3 actual nonblocking persistence with
no premature success, or a separately named launch requirement. These witnesses
enable E1 only. All three exits remain OPEN; no extra helpers are authorized just
to increase suite or checkpoint counts. Next integration dependencies are durable
archive provenance/publication (E1/E2) and the shared persistence fence (E1/E2/E3).
Exact archive bytes/quotas and real transport framing remain review boundaries.

## Verification

Main rebuilt the baseline `velocita-all-tests` target before running CTest:
Release 74/74, 11.96 seconds. Final rebuilt Release76/76, 12.64 seconds; focused
contract suites2/2. Independent expected gate inventory matched all76 suites
(73 V2); tooling40/40. These counts establish regression coverage, not any E1–E3
completion. Full rebuilt C++ ASan+UBSan76/76, 155.31 seconds; nonrecovering UB,
halt-on-error, leaks disabled, Raft C uninstrumented. No model corpus or TSan run.
No target/device/latency/power-loss claim. No production source or bytes changed.

Observed executions and intended assertions:

| Execution | Exit | Observation |
|---|---:|---|
| Main rebuilt normal ACK witness | 0 | Real suffix replacement, leader/follower exact state, stale ACK does not regress or reapply |
| Same executable `--overclaim` | 1 | First applied-state/commit guard: commit20/applied20 instead of12; test-only rejected policy |
| Main rebuilt cold result witness | 0 | S2/C4/tail5; literal epoch/version4, no cold notifications; later result5 exact and once |
| Direct source mutant: commit only snapshot floor | 1 | `cold recovery did not install S and apply exactly S<i<=C` |
| Direct source mutant: omit restored uncommitted reservation | 1 | `retained i>C did not preserve its ordinary recovery reservation` |
| Original lagger integration | 1 | Snapshot12, lagger commit/applied0, seven actual failed retries |
| Original archive-loss integration | 1 | Snapshot3 survives, audited command1 absent from WAL and directory tree |

The two source mutants were copies of current `src/v2_replication.cpp`, compiled
directly with the new result test and fresh current Release libraries. No source
mutation was applied to the repo and no CMake timestamp inference was used.
Raw main outputs are the `main-*.txt` files here. Worker command snippets in the
two contract notes are explicitly secondary to the rebuilt main runs.

Reproduction (all paths refer to this host's existing offline dependency cache):

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
/private/tmp/velocita-full-audit.lYB4P5/release/v2-snapshot-install-ack-contract-tests --overclaim
cmake -S . -B /private/tmp/aeron-phase67-audit-build -DBUILD_TESTING=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Direct source recipes use C++20/O1/NDEBUG/cacheline64, `-Iinclude -Itests
-Ibuild/_deps/willemt_raft-src/include`, and Release libraries in order:
`velocita_v2_replication`, `velocita_v2_kernel`, `velocita_v2_evaluator`,
`velocita_v2_contracts`, `velocita_core`, `willemt_raft`, with `-pthread`.
For mutations add the modified replication source beside the result test before
the libraries. Main scratch: `/private/tmp/velocita-cp037.HqPoVm`; original REDs
use fresh nonexistent `lagger-fixture` and `archive-fixture` child roots there.
Do not reuse those roots on rerun. Original RED test sources were unchanged.
