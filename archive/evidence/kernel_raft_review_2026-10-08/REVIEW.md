# Voter kernel and Raft fork: line-by-line review

Reviewed 2026-10-08 at commit `8a15b98`, with fixes landing in `3264351` and `f55209e`.
Scope was the code a voter runs to agree on and apply commands.

## What was read

| File | Lines | Read |
|---|---|---|
| `deploy/patches/0001-raft-durable-quorum.patch` | 422 | All |
| willemt/raft `raft_server.c` (patched) | 1,662 | All |
| willemt/raft `raft_log.c` (patched) | 324 | All |
| willemt/raft `raft_server_properties.c` (patched) | 275 | All |
| `src/v2_replication.cpp` | 1,896 | All |
| `src/v2_kernel.cpp` | 3,406 | About 85%: everything except the digest helpers (215-290), the tail of venue order-event handling with replace and mass cancel (2700-2900), and effect materialization (3160-3260) |

Not read in this pass: `v2_node.cpp`, `v2_wal_arena.cpp`, `v2_archive_store.cpp`, the snapshot and
command codecs beyond the parts changed for ADR 0021, the bound-frame, authentication-envelope and
session-fence code, and willemt/raft `raft_node.c`. These need their own pass.

## Summary

The replication layer is careful. It fences every asynchronous completion against the state that
requested it, refuses to continue on any ownership or storage doubt, and it already guards the
weakest line in the Raft library. The kernel reducer is deterministic, bounded and checks every
arithmetic step.

The review found two real defects, both in code I wrote this week and neither deployed, and fixed
them. It found one gap in the Raft library that the layer above already covers and tests, one liveness
gap with no cover, and a set of authorization gaps that ADR 0020 exists to close.

---

## A. Defects found and fixed

### A1. Data race on archive usage (introduced in `9dcca2c`, fixed in `3264351`)

`ReplicatedKernel::status()` runs on the owner thread every 100 ms to fill the health page. The
archive-usage figures added for the capacity alert read `archive_.logical_bytes()` and
`object_count()` there. During a compaction the writer thread is inside `archive_.publish`, changing
the same fields, and on the first compaction it also sets `archive_open_`. Every other use of the
archive on the owner thread is behind `if (compaction_ ...) return`, which my addition skipped.

Effect: undefined behaviour in the strict sense; in practice a possibly inconsistent number on a
dashboard. Never deployed.

Fix: the figures are captured at the three points where no writer job is running (start,
compaction end, history-recovery end) and `status()` returns the captured values. Three mutants on
the capture points are killed.

### A2. Refused fills were not kept as evidence (introduced in `4c04f75`, fixed in `f55209e`)

`receipt_is_pinned` keeps a refused fill's receipt while its order is unresolved, but only for
`fill_capacity_exceeded` and `fill_quantity_conflict`. The two codes added for ADR 0021,
`fill_price_conflict` and `fill_fee_conflict`, were not in the list. After the gateway acknowledged
past the fill, a later sweep could drop the record of what put the order into conflict.

Effect: the order stayed in conflict, so no money risk, but the state lost the reason. The log
and archive still held it.

Fix: both codes are pinned the same way. The new test fails without the change.

---

## B. Raft fork

### B1. Low: commit index on a follower is bounded by the last log index, not the last new entry

`raft_recv_appendentries` sets `commit = min(leader_commit, last log index)`. The Raft paper says
the index of the last new entry. With the library alone, a follower holding a stale uncommitted
tail could commit it if it accepted an append that did not reach the tail.

The layer above already prevents this: `receive_peer` passes
`leader_commit = min(leader_commit, previous index + entries accepted)`
(`v2_replication.cpp`, in the append-entries branch), including when it drops the entries for
capacity. So the cluster is safe.

The guard is tested. Removing the `std::min` makes `v2-bound-peer-tests` and
`v2-log-saturation-tests` fail (checked by running the whole suite against that mutant). The
library line itself stays wrong, so the guard must never be removed without fixing the library.

### B2. Medium: no leader step-down when it cannot reach a quorum

The library has no check-quorum. A leader that can send but not receive keeps sending heartbeats.
With pre-vote enabled, followers that still hear it refuse every pre-vote, so no election starts,
and the leader never commits. The cluster stalls until a human acts. `VelocitaCommitStalled` would
fire. This is a liveness gap, not a safety one. Fix: the leader steps down when it has not heard
from a majority within an election timeout (Raft thesis 6.2).

### B3. Medium: the library's assertions are compiled out of release builds

Release builds pass `-DNDEBUG` to the C code, so every `assert` in the library is removed, including
`commit index never goes backwards`, `commit index is within the log` and
`a response index is within the leader's log`. In `raft_recv_appendentries_response`, a response
whose index is beyond the leader's log would then set the match index past the log and dereference
a null entry. Peers are authenticated, so this needs a faulty voter, and the result is a crash,
which is fail-stop. But the invariants should stop the node deliberately, not by accident. Fix:
compile the library without `NDEBUG`, or turn the asserts into returned errors that the layer above
already treats as fatal.

### B4. Low: other library notes

- `__should_grant_vote` has no "already voted for this candidate" case. A repeated vote request from
  the same candidate after a lost reply is refused, costing one election round.
- `log_clear_entries` walks one slot too far and passes indices shifted by one. Harmless here: the
  callback ignores the index and refuses to run outside start-up, when the log is empty.
- `__log` formats into a fixed 1,024-byte buffer with `vsprintf`. No log callback is installed.
- The log buffer grows by doubling with `calloc`. It reaches its working size early and stays there,
  but it is the one unbounded allocation on the commit path.
- The pre-vote change is sound as written. A leader is its own `current_leader` and resets its timer
  on each heartbeat, so it always refuses; a grant changes nothing at the voter; a stale grant
  cannot start an election because it must name exactly the next term.

---

## C. Replication layer (`v2_replication.cpp`)

### C1. Low: the node does not tick while it compacts or persists

`tick`, `propose` and `receive_peer` all return early while a compaction or a hard-state write is
pending. A leader sends no heartbeats during a compaction. Measured pauses are 0.13 to 0.6 s
against a 1 s election timeout; `VelocitaCompactionPauseLong` watches this. The Pi reached 0.61 s on
2026-10-08. If the archive grows, the scrub that runs inside the pause must stay bounded (it is:
two segments per verify).

### C2. Low: smaller points

- `propose` copies each command into a `std::vector`, and a compaction copies its records into
  vectors. Bounded in size, but heap allocation on the commit path.
- `status()` encodes and hashes the whole state ten times a second for the health page.
- `before_apply` reports any exception as a catalog-dependency failure, which would mislead whoever
  reads the log.
- An unused variable (`submitted_any`) in `flush_durability`.

### C3. What is solid here

- A committed index is never reported, applied or persisted beyond what is durable locally.
- Every asynchronous completion (hard state, truncation, compaction, history recovery) is checked
  against the generation, sequence and captured indices of the request; a mismatch stops the node.
- Start-up cross-checks snapshot, archive, write-ahead log and metadata terms and refuses on any
  disagreement.
- An authority proof is re-validated against current state every time it is read and revoked on any
  change of term, kill epoch, executor generation, mode or the effect itself.

---

## D. Kernel reducer (`v2_kernel.cpp`)

### D1. High for live: most commands are not restricted by caller

`apply` restricts five commands to the operator band: kill epoch, executor generation, strategy
mode, catalog install and risk limits. Everything else is accepted from any authenticated caller:

- publish, validate, activate and supersede a policy; stage and activate an executable policy;
- observe a fill or an order event;
- authorize a decision, request a cancel or a mass cancel, acknowledge gateway results.

So the strategy's key could install a different policy, and it could report a fill that never
happened. With ADR 0021 a fabricated fill at a good price would be booked as profit. This is the
gap ADR 0020 describes; this list is what it has to cover.

### D2. Medium for live: an order in conflict has no way out

A fill that fails the quantity, price or fee check freezes its order as `conflict`. That is the
right first reaction. But nothing in the kernel ever leaves that state: the order is never swept,
its reservation is held, and `SetStrategyMode(normal)` is refused while it exists. With limits sized
for one order, one conflict stops the account for the life of the incarnation. The book (ch. 50,
break workflow) wants an append-only correction that records both values and closes the break.
Needed: an operator command that resolves a conflicted order against stated venue evidence.

### D3. Medium for live: no flatten path

`reduce_only`, `no_new_risk` and `cancel_only` suppress every proposed order at evaluation,
including one that would close the position. Already recorded in `LIVE_TRADING_JOURNEY.md` step 4.3.
The loss limit added in ADR 0021 does let closing orders through, so it is not affected.

### D4. Low: one active executable policy for the whole kernel

`active_executable_policy` is a single slot. A second account or instrument cannot run beside the
first. Known and intended for now.

### D5. What is solid here

- Every multiplication and addition on money or quantity is checked; an overflow is a refusal.
- Identity is deduplicated at three levels (request, venue event, execution) and conflicts are
  distinguished from replays.
- Capacity is checked before any mutation, with sweeps that keep whatever an unresolved order
  still needs.
- The ADR 0021 ledger: 50 mutants killed, hand-computed sequences including a reversal through
  zero.

---

## E. Next actions

1. Compile the Raft library with its assertions, or return errors instead (B3).
2. Leader step-down without a quorum (B2).
3. ADR 0020 with the list in D1, and an operator command to resolve a conflict (D2), before the
   next format change.
4. Review the files listed under "Not read".
