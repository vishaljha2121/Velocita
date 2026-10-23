# CP-026: F5 completion review and current-source REDs

2026-09-23. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; all remediation
uncommitted. Two workers added isolated tests; main reviewed and independently
compiled/ran both. No production protocol or CMake change; no GREEN claim for
either new test. Main plan: `../../F5_COMPLETION_AND_NUC_PLAN.md`.

## Baseline and build provenance

Main first rebuilt the current production/test libraries:

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release \
  --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
```

Build succeeded; registered Release tests passed 59/59. The new REDs deliberately
are not registered. CP-025's sanitizer/tooling results are historical this turn;
no new sanitizer, target, TSan, power-loss or performance evidence is claimed.
Final rebuild after the comment clarification also passed 59/59 (14.66 seconds).
Raw retained outputs: `release-tests.txt`, `catchup-red.txt`, `archive-red.txt`.
The RED outputs were captured again on fresh fixture directories; stderr/stdout
ordering can differ, but both intended failures exited 1. Final independent worker
review found no additional concrete claims defect; main retains responsibility
for the proposed contract and its still-required implementation proof.

Main checked that the Release replication target's flags use
`VELOCITA_TARGET_CACHE_LINE_SIZE=64`; direct-link fixtures use the same ABI flag.
`build/_deps/willemt_raft-src/include` supplies the pinned headers, not its archives.

```sh
c++ -std=c++20 -O1 -DNDEBUG -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Itests -Ibuild/_deps/willemt_raft-src/include \
  tests/v2_snapshot_catchup_red.cpp \
  -L/private/tmp/velocita-full-audit.lYB4P5/release \
  -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator \
  -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread \
  -o /private/tmp/f5-completion-review.HkS34p/catchup-red
```

The archive test uses the same command, replacing source with
`tests/v2_archive_reclamation_red.cpp` and output with
`/private/tmp/f5-completion-review.HkS34p/archive-red`. Both compiles exited 0.
Each executable takes a new fixture-root path and refuses an existing one.

## Original behavioral RED: missing snapshot catch-up

Main run exited 1 with:

```text
snapshot-catchup trace: leader=3 healthy_peer=2 lagger=1 compacted_through=12 lagger_applied=0 lagger_commit=0 append_deliveries=7 failed_append_replies=7
snapshot catch-up test failed: lagging voter could not cross the compacted prefix via snapshot install
```

Healthy quorum and both durable snapshots are asserted before reconnect. Commands
are authored independently; explicit expected kill epoch/state version are 12.
The expected digest uses a separately advanced reducer, suitable as the replication
oracle but not independent proof of reducer economics. A future successful install
need not emit multiple failed replies: that diagnostic precondition is applied
only while the follower remains behind. Main removed arbitrary-root deletion from
the worker's initial fixture and requested the explicit semantic assertions.

This is an intact lagging voter, not unsafe bootstrap of a disk-wiped replacement.
Thus its failure does not conflate missing catch-up with lost voting-memory admission.

## Original behavioral RED: missing audit coverage

Main run exited 1 with:

```text
archive_reclamation included=3 audited_index=1 snapshot_state_kill=2 retained_wal_record=0 retained_raw_bytes_anywhere=0 verified_archive_interface=absent
RED F5/A14: compact removed the last raw committed command/identity bytes without verified archive coverage
```

The 100-byte expected command is extracted from the independent Python-calculated
block-v2 golden in `tests/v2_wal_arena_tests.cpp:102`, excluding record metadata
and outer framing digest. It is not produced by the encoder at test runtime.
Main checked that provenance against the source. The codec must match the literal
before the test commits it; its bound WAL record must exist before compaction.
Two additional commands ensure there is a real compactable prefix (a one-entry
fixture is insufficient for this Raft library).

This negative oracle is intentionally limited: absence of the exact bytes from
all files proves loss in this fixture. Presence in an arbitrary temporary file
would NOT prove durable, verified archival. Before this test becomes a registered
GREEN, add a typed coverage-verification oracle and wrong-lineage/corrupt/incomplete/
unsynced-archive cases. Do not make it pass by dumping an unverified sidecar.

## Review corrections and limits

- The pinned snapshot-load call mutates term/vote/membership immediately; calling
  it directly on the serving owner is not an atomic install.
- A staged-file publication is distinct from the active-generation selector.
  After the latter is durable, failure must not resume the old generation.
- Existing READY notifications are independent values; they survive install but
  do not automatically pin raw history. Covered RESERVED obligations cannot simply
  be discarded or reconstructed by reapplying to final snapshot state.
- Archive-assisted replay is a proposed integration path for those obligations,
  not a new implemented guarantee or a waiver of peer snapshot installation.
- Offline operator-fenced repair is recommended, not yet an approved/proven
  voting-admission protocol. A suffix beyond a lagging commit checkpoint is not
  thereby known safe to discard; preserve it for renewed consensus.

No source freeze, tag, commit, device/venue action, quota increase or acceptance
requirement removal. These REDs establish why fixes are required; they do not
close F5, F6, F7 or the NUC live-deployment checklist.
