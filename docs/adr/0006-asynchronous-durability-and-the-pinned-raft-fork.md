# ADR 0006: Asynchronous durability, and a pinned Raft fork to keep quorum honest

Date: 2026-09-15
Status: Accepted
Implements: Phase 7.

## Current implementation status — 2026-09-24 (CP-050 handoff)

This supersedes implementation-status statements below, not the retained audit
history. CP-043 moved live term/vote/commit saves and conflicting-suffix replacement
onto the serial WAL worker when `start_writer()` is active. Protocol/apply/output
fences wait for exact acquired completions; explicit cold/manual paths do not
establish a nonblocking live-loop claim. CP-044 added composed durable-image
evidence. Worker exceptions now latch failure rather than escaping the thread.
See `../evidence/f6_async_persistence_2026-09-23/README.md` and
`../evidence/f6_async_crash_model_2026-09-24/README.md` for limits and corrections
to the historical NULL-hook/self-vote claims. Do not reimplement synchronous
metadata removal as though those changes were absent.

CP-049 adds a native serial target handoff after old writes/completions drain;
it is NOT online generation installation. The installer must preserve actual job
lifetimes and same result ledger, publish the selector durably and handle uncertain
selection without fallback. It remains unimplemented. Approved direction and
remaining work are in ADR0014 and the top `../CURRENT_HANDOFF.md` handover.
Current source has not received new target acceptance; E3 is not closed by host
component tests. This amendment changes no bytes or protocol/quorum decision.

## Historical review status — 2026-09-21

The WAL writer is asynchronous; term/vote/commit metadata saves still synchronously
fsync on the owner thread. The reported microbenchmark is not an end-to-end
owner-loop bound. Hard-state atomic replacement can be valid, but its persistence
cut matrix is missing; corrupt-file refusal is not that proof. The fixed staging
arena does not bound the replication queues or retained log map. Current-tree
target acceptance is open. See the full Phase 6–9 audit; do not remove durability
fences or weaken G-043 to restore a phase-complete label.

F3 recovery prerequisite, 2026-09-21: compatibility validation now precedes the
writable WAL open. An unsupported snapshot version is not ordinary corruption
eligible for replay fallback. A shared read-only segment reader lets refusal
preserve the directory inventory as well as existing bytes. This changes no WAL
framing or durability arithmetic; ADR 0012 and CP-005 record scope and evidence.

## Context

CP-013 correction, 2026-09-22: exceptions from synchronous WAL flush and
term/vote/commit metadata persistence now latch the V2 node unusable. Persist a
candidate metadata tuple before publishing its in-memory cache. A post-rename
failure can expose a new named file while Raft has not accepted the callback;
continuing from either assumption is unsafe. Tests check service refusal, not
merely that the first operation threw. Main also found that the pinned election
path ignores its self-vote callback error; owner tick now checks the latch even
after a zero raft_periodic return. No persistent bytes or V1 behavior changed.

This does not make hard state asynchronous or prove power-loss durability.
Background WalWriter exceptions still escape its thread instead of reporting a
structured owner error (source-traced). ADR0014 and the F5/F6 review
separate that protocol from these existing-contract fixes; two-slot versus atomic
replace remains an explicit plan decision, not silently amended by host tests.
The owner subsequently approved the provisional atomic-replacement direction;
the evidence and final plan amendment remain required, not presumed complete.

CP-014, 2026-09-22: a test-only model now executes real metadata save/load with
separate volatile/durable inode bytes and directory publication. Sync-only source
mutants fail independent recovered-value assertions, unlike CP-006 process exits.
This supports the approved provisional choice but does not finalize the plan
amendment: spontaneous writeback, failed-sync partial persistence, exhaustive
sector tears and first-directory creation are outside this model, and target
evidence remains open. See `../evidence/f6_persistence_model_2026-09-22/README.md`.

CP-027, 2026-09-23: the model now has a separate suite for the actual V2
identity-bound metadata implementation. Authored old/new tuples and the real
save/load path are checked with file/name durability separated; main's isolated
file-sync-only and directory-sync-only mutants fail the recovered-value oracle.
The old suite alone did not prove the new implementation. No production order,
format or sync changed. This remains a restricted model, not asynchronous Raft
completion or target power-loss acceptance; atomic replacement stays provisional.
See `../evidence/f6_bound_metadata_model_2026-09-23/README.md`.

F2 remediation, 2026-09-21: the synchronous segment helper now snapshots distinct
sealed descriptors, refuses blocks already submitted to an async writer, and
completes only after the batch sync. Failed write/sync leaves arena ownership
unchanged and rolls back the file append frontier for retry. A consuming iterator
was rejected because its advanced submission cursor would strand failed work.
This fixes the helper's duplicate-front bug; it does not remove synchronous
metadata or establish the missing target/hard-state guarantees. CP-002 records
the permanent failure, retry and mutation evidence.

`WriteAheadLog::append` opened, wrote, fsynced and closed PER RECORD, from inside
a Raft log callback and therefore from inside the owner loop. Every proposal paid
a full sync before anything else could happen.

Making the log asynchronous splits two numbers that were previously identical:
the highest log index a node HOLDS, and the highest index whose bytes are on
stable storage. Raft's safety argument is stated in terms of the second. willemt
assumes the first, in two places that both concern the node's OWN contribution:

- `raft_recv_appendentries_response` starts its quorum count at `votes = 1`,
  counting the leader unconditionally, and exposes no setter for one's own match
  index; and
- `raft_recv_entry` commits immediately when there is a single voting node.

A peer's durability was never the problem - its append-entries response reports
it. Only the local side was missing, and it was not reachable from outside the
library.

## Decision

STAGE, THEN SYNC, OFF THE OWNER'S THREAD. `WalBlockArena` stages records into
fixed 4 KiB blocks; `WalSegment` writes them with `pwrite` and syncs once per
drain; `WalWriter` does that on another thread. The owner owns the arena -
staging, sealing and applying completions all on its thread - and the writer only
READS the bytes of a sealed block, which the generation guard keeps stable for
exactly as long as it holds the descriptor. Descriptors cross in an `SpscRing`
each way.

PIN A RAFT FORK. `deploy/patches/0001-raft-durable-quorum.patch` adds one
OPTIONAL `durable_idx` callback and uses it in both places above, plus a
single-voter re-check in `raft_periodic` - needed because at `raft_recv_entry`
time an asynchronously staged entry is never durable yet and nothing else would
revisit it. With the callback NULL the behaviour is byte-for-byte as before.

CLAMP A FOLLOWER'S ACK. `receive_peer` clamps the reported `current_idx` to the
follower's own durable index. This is the same defect one hop away and the patch
cannot fix it, because the number is produced on this side of the wire.

DECLARED PROTOCOL: a voter contributes to quorum only for entries it has made
durable.

## Consequences

The patch is applied at configure time rather than vendored, so the upstream pin
stays the source of truth and the delta stays reviewable as a diff. Failure to
apply is FATAL: silently building unpatched Raft would silently restore the
defect. A sentinel makes reconfiguring idempotent. Bumping `RAFT_COMMIT` without
regenerating the patch fails the build, which is the desired coupling.

V1's node is unaffected, because it does not set the callback.

Clamping a follower's ack costs a round trip - the leader sees a lower match,
resends, and the follower reports the higher index once its writer catches up.
The alternative is a quorum formed around bytes a power cut would erase.

A recovered commit index past the end of the log REFUSES RECOVERY, preserving the
files for diagnosis. This reverses an earlier decision in this ADR; see the
amendment below.

Measured on the development host over two thousand command-sized records, owner
loop time: median 21.6 us synchronous against 0.042 us off-thread, max 7136 us
against 358 us - but p99 109 us against 208 us. The design converts an unbounded
per-record stall into a bounded queue-full one. It does not make the disk faster,
and at p99 under sustained overload the owner waits for a block instead of for an
fsync. No claim is made for the Pi or the NUC until the gates run there.

## Alternatives rejected

DO NOT FORK; ENFORCE ONLY WHAT IS REACHABLE FROM OUTSIDE. This was the first
implementation. It clamped the PERSISTED commit index, which prevents the
unrecoverable symptom - a restart reading a commit index past its own log - but
leaves the in-memory commit index able to outrun durability. The exposure was
bounded and was stated honestly, and it was still a weaker guarantee than Raft
promises.

VENDOR MODIFIED RAFT SOURCES. Loses the upstream pin as the source of truth and
makes the delta invisible in review.

KEEP THE SYNC ON THE OWNER'S THREAD, BATCHED. One sync per drain rather than per
record is most of the win and needs no thread. It leaves the owner loop stopping
for the disk, which is the objective sentence of the phase.

## Amendment, 2026-09-16: a missing committed prefix refuses rather than clamps

The original text of this ADR decided that a recovered commit index past the end
of the log should be CLAMPED and reported, on the grounds that refusing turns a
survivable crash into a permanent outage whose only remedy is deleting state.
That decision was wrong, and the reasoning that produced it was wrong in an
instructive way.

The power-cut sweep that motivated it truncates the segment EXTERNALLY. That is
not a thing that can happen while the durability contract holds - with the gate
enforced, commit never exceeds durable, so a commit index past the end of the log
means the storage lost data it had acknowledged. The sweep was simulating a
broken disk, and the response was to teach the node to tolerate a broken disk
silently.

Clamping therefore did not make a survivable crash survivable. It made
unacknowledged data loss invisible, and it did so on the single-voter
configuration that is the MVP deployment shape, where there is no peer to
re-supply what was lost.

DECISION: refuse recovery, name the condition, and leave the files untouched for
diagnosis. Do not invent an operator acknowledgement flow and do not erase
history automatically.

The cost is real and is accepted: a node in this state does not start without a
human. That is the correct cost, because the alternative is a cluster that
continues with a hole in its committed history and no one aware of it.

WHAT WOULD CHANGE THIS. In a multi-voter cluster a follower that lost its tail
should be able to rejoin by re-fetching from the leader rather than requiring an
operator. That needs peer snapshot transport, which is a material wire contract
that has not been reviewed or built. When it exists, this decision should be
revisited for followers specifically - it would remain correct for a single
voter.

## A note on how two of these were found

### 2026-09-21 review: process cuts are not power cuts

F6 now adds deterministic process termination around metadata temporary-file
write, file sync, rename and directory sync. The parent observes the old complete
named frame before rename and the new complete frame after rename; a half-written
temporary cannot become the named state. Default-inert callbacks add no persisted
bytes and leave the production one-write path unchanged.

This does not close G-047 or the owner-thread blocking finding. `_exit` leaves
the operating system and its dirty page cache alive. Direct mutants removing
only file fsync or only directory fsync, while retaining callbacks, BOTH SURVIVE.
An earlier mutant removing directory sync AND its checkpoint died because the
checkpoint was absent; that result is withdrawn as durability-guard evidence.
The matrix proves atomic visibility under process failure, not power-loss
survival. A storage persistence model and target evidence remain necessary;
observing a callback is not proof that a syscall ran or that a device honored it.

The first version of the durability test asserted `commit <= durable` after every
tick, while the harness flushed on every tick. It passed - and it passed with the
gate unwired entirely, because the harness closed the gap before anything could
observe it. `flush_durability` is now separate from `tick` and public, so the
owner loop decides when the disk happens and a test can withhold it.

The power-cut sweep found the fatal-recovered-commit-index defect and the
64 KiB-per-record write amplification on its first run. Both had been present,
green, and invisible to three hand-picked durability points.
