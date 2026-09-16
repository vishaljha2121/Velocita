# Phase 6/7 correction evidence

Base: `8f66dca5435f4e2c866782692a6d901543eb0458`, branch
`v2/phase-5-corrections`. This is correction evidence, NOT acceptance.

## Permanent RED cases (2026-09-15)

Built `v2-replication-tests` before running each named case, with production
sources still at the base for these behaviors:

| Invocation suffix | Exit | Observed failure |
|---|---:|---|
| `suffix-staged` | 1 | rejected staged suffix reappeared on disk |
| `suffix-durable` | 1 | replacement acknowledged using obsolete history durability |
| `compacted-corrupt` | 1 | corrupt compacted snapshot allowed empty recovery |

Commands: `cmake --build build --target v2-replication-tests -j4`, then
`./build/v2-replication-tests <suffix>`.

`suffix-threaded` passed on this schedule before the fix. It is regression
coverage, not claimed RED evidence or a deterministic race reproduction. The
race it was meant to cover is now covered by `writer-truncation-race` below.

The earlier external probes also reproduced membership loss and late-local-sync
quorum stalling; permanent independent suites are being added for these.

## Approved implementation direction

- Restore configured membership after loading the Raft snapshot; no byte change.
- Quiesce and drain the writer before truncation/compaction; rewrite atomically,
  then lower the durable watermark to the surviving log prefix before accepting
  replacement entries. This introduces a rare synchronous maintenance barrier,
  not a claim of a fully non-blocking owner loop.
- Refuse recovery if committed history is missing; preserve files for diagnosis.
  Do not invent an operator acknowledgement or automatically erase history.
- Re-evaluate quorum when local durability advances, retaining Raft's
  current-term restriction and preserving the callback-free V1 path.

Peer snapshot transport requires a separately reviewed material wire contract.
Pi gates remain deferred by user instruction; NUC is the next target gate.

## GREEN, and what each fix is actually held by (2026-09-16)

`cmake --build build -j8` then `ctest --test-dir build`: 34/34, repeated four
times, and again from a clean-slate configure into a separate build directory.

Every fix was then checked by MUTATION - reverting the fix in production source
and requiring a named test to fail. A fix whose mutant survives is not covered,
whatever the suite says. Three of the six survived on first attempt, and the
gaps are recorded here rather than quietly closed, because two of them were in
fixes whose correctness was not in doubt.

| Fix | Mutation applied | Result |
|---|---|---|
| Sync before completion in `WalSegment::flush` | complete inside the write loop, before `sync()` | SURVIVED - no coverage. Closed below. |
| Membership restored after snapshot load | delete the restore loop | KILLED - `v2-replication-tests`, `v2-snapshot-membership-tests` |
| Writer quiesced before truncation | drop `stop_writer()`/`start_writer()` around the rewrite | SURVIVED 40/40 runs of `suffix-threaded` - no coverage. Closed below. |
| Durable watermark rewound to the surviving prefix | - | KILLED - `suffix-durable` (`ack.current_index == 0`) |
| Recovery refuses a missing committed prefix | delete the check | SURVIVED - the sweep still saw a refusal, but an accidental one. Closed below. |
| Quorum re-evaluated on a durability advance | delete the `raft_periodic` re-walk | KILLED - `v2-durable-quorum-tests` only; `v2-replication-tests` stayed green, so the independent suite is the sole coverage |

### Gap 1: the durable index could advance before the sync, untested

The most serious defect in the set, and nothing failed when it was put back.
A crash cannot expose it - `_exit` does not drop the page cache, so the file
comes back complete either way. What distinguishes correct from incorrect is the
durable INDEX at the instant the sync begins, which the `before_file_sync`
checkpoint makes observable.

`test_the_durable_index_does_not_advance_before_the_sync`, in
`v2-wal-arena-tests`, reads `arena.durable_index()` from that checkpoint and
requires it to be zero, and requires the checkpoint to have fired exactly once
so the assertion cannot hold vacuously. Under the mutation:

```
V2 WAL arena test failed: the durable index was already 1 when the sync began,
so a block was credited as durable before its bytes reached the disk
```

### Gap 2: the recovery refusal was covered only by accident

Deleting the explicit check left the power-cut sweep green: `raft_apply_all`
then fails on the entries that are not there, so the node still refuses to
start. But it refuses with `failed to replay committed WAL entries: ` and an
empty cause, after having already set a commit index past the end of its own
log. The reversal from clamping to refusing (ADR 0006's amendment) was made so
that an operator is TOLD what happened; a refusal that says nothing serves
neither half of that.

The sweep now asserts the diagnostic, not merely that something threw. Under the
mutation, at the first cut:

```
recovery refused at cut 0 without naming the missing committed history:
failed to replay committed WAL entries:
```

### Gap 3: the truncation/writer race had no deterministic reproduction

`suffix-threaded` starts the writer and drives a suffix replacement, but flushes
immediately, so nothing is ever in flight when the rewrite happens; the mutant
survived 40 consecutive runs. Running two threads and hoping is not evidence.

`ReplicatedKernel` now takes an optional `StorageCheckpoints` - a test-only
seam, defaulted off, carrying no behaviour. `test_truncation_does_not_race_the_writer`
(`writer-truncation-race`) uses it to PARK the writer inside its drain, at the
point where its bytes are written but not yet synced, and only then delivers the
replacing suffix. The park releases itself after a bounded wait so the correct
implementation - which joins the writer before rewriting - cannot deadlock
against it.

Two entries, not one: the replacement reuses an index, so with a single entry a
stale completion and a correct one name the same number and the invariant cannot
tell them apart. The invariant asserted is that THE DURABLE INDEX MAY NEVER NAME
AN INDEX THE SEGMENT DOES NOT HOLD.

Green 5/5 with the park armed, and 5/5 with it disarmed. Under the mutation,
5/5 failures:

```
raft peer handling failed: -4 cannot rewind durability with outstanding WAL work
```

which is the arena's own guard firing - confirming both that the quiesce is
load-bearing and that the guard behind it works.

## Still open

- ARM64 container gate and the NUC target gate have not been re-run against this
  tree. The source root must be re-frozen and re-pinned first.
- The physical Pi gate remains a carried Phase 5 obligation, deferred by user
  instruction while the machine is offline. It gates the TAG, not HEAD.
- Peer snapshot transport does not exist, so ADR 0006's amendment stands as
  written: a node that loses committed history does not restart without an
  operator, including a follower that could in principle re-fetch it.
