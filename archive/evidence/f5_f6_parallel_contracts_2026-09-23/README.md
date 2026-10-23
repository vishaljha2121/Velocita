# CP-029 four-lane contract review and diagnostics

2026-09-23. Branch `v2/phase-5-corrections`, HEAD
`d44a4a964fc8c13279878e53d245ab56b921a824`, dirty CP-028 remediation retained.
No production code or CMake inventory changed during CP-029. This is not target
acceptance or a claim that F5/F6 are closed.

## Fresh regression

Main built `velocita-all-tests` in
`/private/tmp/velocita-full-audit.lYB4P5/release` before CTest. Build exit 0 in
`release-build.txt`; Release 63/63, 11.16 seconds, exit 0 in `release-tests.txt`.
No fresh full sanitizer/model/tool/target/latency result claimed. CP-028's prior
63/63 C++ sanitizer and tooling40 are historical and retain their stated scope.

## Actual-syscall async REDs

`tests/v2_async_hard_state_red.cpp` compiles the real metadata store behind one
observing `fsync` shim, forwarding actual calls. Strict direct compile succeeded
(`async-syscall-red-build.txt`); all three independent runs exit 1 at the intended
owner-thread I/O assertion:

- `async-election-syscall-red.txt`: 4 syncs, all owner; persisted term1/vote1.
- `async-vote-syscall-red.txt`: 4 syncs, all owner; persisted term7/vote2.
- `async-commit-syscall-red.txt`: 2 syncs, all owner; persisted commit1 and authored
  reducer transition checked before the failure.

These are fresh permanent REDs, intentionally outside CTest until approved async
implementation. They are not a premature-vote defect, latency measurement or power
cut model. File and directory syncs are both counted. Non-vacuity checks require
real persisted transitions and protocol output; deleting all syncs cannot pass.

Reproduce from repository root:

```sh
c++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Werror -DNDEBUG \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude -Itests \
  -Ibuild/_deps/willemt_raft-src/include tests/v2_async_hard_state_red.cpp \
  -L/private/tmp/velocita-full-audit.lYB4P5/release \
  -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator \
  -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread \
  -o /private/tmp/v2-async-hard-state-syscall-red
async_fixture_root=$(mktemp -d /private/tmp/async-cp029.XXXXXX)
/private/tmp/v2-async-hard-state-syscall-red election "$async_fixture_root/election"
/private/tmp/v2-async-hard-state-syscall-red vote "$async_fixture_root/vote"
/private/tmp/v2-async-hard-state-syscall-red commit "$async_fixture_root/commit"
```

Each root must not already exist; fixtures preserve their files. A diagnostic
failure returns 1 intentionally; do not chain runs with `&&`.

Earlier `async-*-red.txt` used checkpoint observation, not actual syscall tracing;
they are retained but superseded by the syscall logs. The initial fixture compile
error used an ambiguous V1/V2 `Command` name; corrected before runs and recorded
in `async-fixture-compile-error.txt`. Neither is relabeled as a production RED.

## Independently rerun repair characterization

Main read the new `tests/lost_voter_repair_reachability.cpp`, directly compiled with
the same strict flags and freshly built six libraries, and ran a fresh root.
Compile/run exit 0. `repair-main-trace.txt` reproduces two byte-identical donor
directories despite different old-leader commit outcomes. Exact state, canonical
payload, missing-hard-state refusal and retained RESERVED outcome are checked.
This is a characterization PASS, not a repair GREEN or a missing-API RED.
Worker commands, provenance and limitations:
`../lost_voter_repair_2026-09-23/README.md`.

## Remaining failures and limits

CP-026 snapshot-catchup and archive-loss diagnostics remained RED when freshly
rerun in CP-028; they were not rerun or repaired here. Neither codec hashes nor
these test results certify archive publication, generation selection, repair,
authenticated transport, device power-loss safety or async protocol ordering.
The review documents are PROPOSED. New persistent/control/transfer formats and
budgets still require owner review before encoding. No staging/commit/tag,
credential, venue, deployment or protected-path action was taken.
