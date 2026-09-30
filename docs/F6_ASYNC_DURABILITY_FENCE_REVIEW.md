# F6: nonblocking owner, durable protocol outputs

2026-09-23. **Direction and §8's five continuation choices APPROVED; CP-043 host implementation below.** Review of the
CP-028 worktree at `d44a4a9`. Existing synchronous persistence is intentionally
not removed. Moving an operation to a worker is not permission to acknowledge it
before its durable completion. This sheet changes no bytes or acceptance status.

## 1. Current failure and what it does NOT prove

`tests/v2_async_hard_state_red.cpp` compiles the actual BoundMetadataStore behind
an observing `fsync` shim that forwards the real syscall. With the WAL writer
running, all three independently exercised paths still sync on the Raft owner:

| Path | Actual metadata sync calls | Calls on owner | Independent prerequisite |
|---|---:|---:|---|
| campaign | 4 | 4 | stored positive term/self vote and matching RequestVote |
| higher-term vote | 4 | 4 | stored term 7/vote 2 and granted response |
| commit checkpoint | 2 | 2 | stored commit 1 and authored kill epoch 1 applied |

Each save syncs a file and its directory. These are syscall counts, not latency
measurements. The assertions fail specifically because the owner performs I/O;
they do not show today's synchronous implementation grants a premature vote.
Zero observed calls also fails, so deleting persistence cannot satisfy the test.
Evidence: `evidence/f5_f6_parallel_contracts_2026-09-23/README.md`.

`persist_vote`, `persist_term` and `persist_commit_index` in
`src/v2_replication.cpp` call the store directly. `start_writer()` moves WAL work,
not those calls. At CP-028, runtime `log_pop`, `log_clear` and `compact` also stop/join or
rewrite storage on the owner. Moving only the three measured saves cannot close
the broader no-blocking-owner requirement.

CP-042 correction: CP-039 moved laboratory compaction onto the worker and CP-041
validated its composed crash ordering. Hard state and suffix deletion remain
synchronous. Do not repeat the historical compaction claim as current status.

## 2. Recommendation: one bounded pending persistence fence

Use one serial persistence owner for live WAL, hard state and generation-changing
storage operations. The Raft owner owns protocol state; the persistence owner
owns disk mutation. Neither mutates the other's objects. Transfer owned fixed-size
requests/completions, never borrowed pointers into mutable Raft arenas.

Start with **one in-flight fence**, not a general concurrent I/O scheduler. While
pending, keep the service loop responsive for health, shutdown requests and
completion polling, but pause protocol mutation, new proposals, application and
new authority proofs. Reject/backpressure new work using reviewed explicit
pending statuses; do not hide an unbounded deferred-message queue.

This sacrifices availability during disk latency. It does not make a quorum vote
or a trade faster merely by moving the wait: the durable prerequisite still has
to finish. The benefit is that health/control work is not trapped inside `fsync`,
and ownership/failure behavior becomes explicit. A stalled disk may stall quorum
indefinitely. Never renew gateway authority to conceal that stall.

## 3. Callback adaptation is the main unresolved proof obligation

The current Raft callbacks mean persistence has completed before success returns.
Simply enqueueing a save and returning zero violates that meaning. The proposed
adapter must quarantine every consequence of a pending transition until durable
completion, or use a separately reviewed resumable library continuation.

Candidate MVP design, requiring an all-call-site proof before encoding:

1. Capture desired term/vote changes during one owner turn in a bounded pending
   tuple, distinct from the last durable tuple. Multiple updates in that turn may
   coalesce only to the final safe tuple; never expose intermediate output.
2. Hold outgoing frames and block apply **before** the library advances its
   applied index. Hold new proofs and new-generation WAL submission too. A WAL
   record from a new term must not become durable ahead of its term metadata:
   current recovery correctly refuses metadata whose term trails the log.
3. End the turn, submit the owned request, and pause further Raft turns until its
   exact completion arrives. A before-apply pause needs an explicit resumable
   status. Do not reinterpret `RAFT_ERR_SHUTDOWN` as ordinary backpressure.
4. Publish the durable tuple only on matching success; revalidate term/generation
   before releasing output and resuming. Failure latches fail-closed behavior;
   it cannot turn into success through a later direct-poll retry.

The library's candidate transition currently ignores the self-vote callback's
error; `raft_periodic` can apply internally; before-apply runs before index
advance. These specific paths require tests, not an assumption that every public
API ends at the same fence. If complete quarantine cannot be demonstrated, use
an explicit optional continuation in the pinned fork and review that patch. Do
not commit a half-working asynchronous adapter to meet a schedule.

Previously READY results can remain readable: they belong to already applied
history. No new result may be published by a pending apply. An already handed-out
authority proof cannot be physically recalled; existing expiry, correlation and
final-send checks still govern it. Local role is not renewed send authority.

## 4. Generation, sequence and operation ownership

Every storage request/completion must identify the local storage generation,
nonwrapping operation sequence and exact operation/tuple being completed. These
are local control identities, not cluster incarnation, archive catalog generation
or transfer session. A late completion from an abandoned generation cannot
release frames, advance durability, authorize deletion or select a generation.

Ignoring a stale completion is insufficient if its worker can still rename over
the active metadata. Requests must target generation-owned files and old writes
must be drained or fenced before selection. Snapshot installation, archive
reclamation and repair must share this rule. Immutable prepared bases are
different from their mutable live WAL/metadata files.

One serial disk owner avoids independently racing metadata and WAL workers.
Current commit checkpoint semantics remain `min(local durable index, raw commit)`;
the snapshot's separately verified committed coverage is not an excuse to write
a speculative commit checkpoint. Exact request/completion C++ types, status
values, ring capacities and selector binding need review before implementation.

## 5. Truncation and reclamation cannot be exceptions

Follower suffix overwrite currently uses synchronous `log_pop`; `log_clear` also
has callback-error limitations. A real async implementation must either prepare
the durable suffix change before permitting the library mutation, or add a
resumable library path. It cannot release result reservations/log cells when a
rewrite has only been queued. Avoid duplicating Raft's conflict logic as a second
unverified consensus implementation.

Compaction requires the archive lane's verified coverage and borrower release,
then a generation-scoped drain/rewrite transaction. No runtime owner-thread
`stop_writer()` join or inline flush fallback may hide behind the async label.
Synchronous bootstrap, offline repair and shutdown can be separate explicit
modes; the live node must not silently fall back to them.

## 6. Required RED/GREEN and mutation evidence

- Retain the three actual-syscall REDs; adapt fixture driving to explicit async
  completions, keeping real disk persistence and non-vacuity assertions.
- Pause the worker before file sync and before directory sync: no vote/election
  frame, durable acknowledgement, apply, result or proof can escape early.
- Deliver stale, duplicated, reordered and failed completions; require exact
  generation/sequence/tuple matching and no state/output unlock.
- Exercise candidate self-vote, higher-term requests/responses, single-voter
  commit, batched commit, stepdown and election timeout while a fence is pending.
- Cut power at each publication step using independently authored old/new durable
  images; reuse the actual-store syscall model, not only orderly process exits.
- Overwrite a conflicting suffix, compact and install a generation with a delayed
  old write. Check bytes, result ownership and recovery, not just enum status.
- Mutation-remove each output/apply/WAL/generation/error guard individually. A
  mutant killed by fixture setup does not prove the intended guard.
- Measure bounded owner work and storage latency on the NUC after correctness;
  host test counts do not establish deployment latency or device durability.

## 7. Approval boundary and rejected shortcuts

Approve the one-pending-fence availability policy and shared generation direction
first. Exact continuation/API and persistent selector/control bytes remain a
separate review slice; none are silently frozen by approving this document.

Rejected: enqueue-and-return-success without quarantine (premature promises);
two independent uncoordinated disk writers (publication races); unbounded queued
turns (memory/latency growth); pre-saving every conceivable vote before Raft
eligibility (duplicates consensus rules); dropping syncs (no durability); and
calling the feature complete after moving only WAL appends (current REDs).

## 8. CP-042 concrete continuation review — subsequently APPROVED

Reviewed against the dirty worktree on `v2/phase-5-corrections`, HEAD `d44a4a9`,
2026-09-23. Two bounded read-only audits covered hard-state and suffix paths;
main independently inspected the callback/apply/delete/worker seams. This is
code-reading evidence and a proposed design, NOT an all-path correctness proof.
No new tests or production changes landed in CP-042. The owner subsequently
approved all five choices; CP-043 implements them as recorded in §9.

### 8.1 The problem and recommended boundary

Today the protocol owner waits inside filesystem calls. Returning from those
callbacks early without fencing consequences would instead let it promise a
vote or publish a result that a power cut could erase. The recommendation is
**one paused protocol turn, with one serial storage operation at a time**.
The owner keeps polling control/health/completions; it does not continue Raft
mutation while that turn's durable prerequisites are unfinished.

The observable sequence is:

`Raft turn -> capture prerequisite -> withhold output/apply -> worker syncs ->
 exact completion checked -> resume continuation -> release eligible output`.

This improves owner responsiveness, not the fundamental durable acknowledgement
latency. It deliberately blocks even unrelated protocol traffic during a disk
stall. Existing READY results remain readable; new results and authority proofs
do not appear. Already issued authority is still governed by its existing expiry
and final-send checks, not magically recalled by this fence.

### 8.2 Five concrete choices for approval

1. **Capture hard state, quarantine the entire existing outbound mailbox.**
   During one turn, callbacks update a desired tuple distinct from durable
   metadata. Coalesce term reset plus self-vote into the final tuple only while
   all consequences remain private. No second mailbox is needed: disable
   `take_outbound_frame` during the fence, including older queued frames. This
   costs some safe traffic availability but avoids another frame pool. The
   single-owner/non-reentrant API is essential; concurrent mailbox consumers
   are not supported by this proposal. `before_apply` must defer BEFORE index
   advancement, for authority markers as well as commands. Require both local
   WAL durability and the durable commit checkpoint to cover an applied index.

2. **A small optional pinned-Raft extension, not a second conflict algorithm.**
   Add a distinct native `RAFT_ERR_PERSIST_PENDING`, propagate self-vote errors,
   and add an optional prepare-truncate callback invoked by `log_delete` once
   with its normalized deletion boundary BEFORE the pop loop. The callback
   returns pending without freeing anything; retry consumes an exact durable
   grant and existing Raft code performs the pops. Per-entry `log_pop` then
   only releases owner-side cells/reservations. Null-hook behavior must retain
   existing semantics; V1 compatibility needs tests, not an assertion based on
   the word optional. The numeric native status must not collide with existing
   library errors; it is not a persisted or wire value.

3. **Exactly one owned retry frame, at the existing 32,768-byte bound.**
   Retain the original validated bound AppendEntries frame when deletion pauses;
   borrowed decoded spans are not safe after `receive_peer` returns. Replay
   that frame after durability through Raft itself, with protocol mutation
   otherwise paused. Do NOT replay an election: repeating a partially completed
   campaign could increment its term again. A higher-term conflicting append
   may require sequential metadata and truncate fences before replay completes;
   never submit two maintenance operations concurrently. Proposed local receive
   status distinguishes an accepted/deferred frame from a newly refused busy
   frame; neither is a peer acknowledgement. This distinction prevents callers
   treating retained work as lost or malformed. Do not overload a codec error
   with scheduling state; use a native receive-result wrapper when implementing.

4. **One typed job/completion on the existing serial WAL worker.**
   Proposed native request fields: operation (`hard_state` or `truncate_suffix`),
   uint64 storage generation, nonwrapping uint64 operation sequence, captured
   durable/staged indices, and an operation-specific owned metadata tuple or
   truncate boundary. Completion returns the full request identity and a
   durable/failed outcome. Validate fields, not struct padding. Job storage
   remains alive until completion; no Raft pointer, result handle, retained-cell
   reference or whole kernel pointer is given to the job. Reuse the worker's
   one-slot maintenance handshake through a typed adapter; do not rewrite the
   tested compaction transport merely for API uniformity. Native struct padding
   is not a new byte contract; report actual sizeof/budget after implementation.
   This adds one 32-KiB retry buffer plus fixed control storage, no new unbounded
   queue and no changes to TradingState or durable/wire encodings.

   Already-submitted older WAL blocks may finish, with owner completion polling
   continuing. Do not submit newly staged higher-term bytes ahead of their
   metadata. Before truncation, drain and durably account all staged work without
   stop/join, then run the rewrite exclusively. Advance/rewind owner arena state
   only after exact completion. A duplicate/stale/wrong-operation completion
   releases nothing; matching failure latches service closed. Sequence exhaustion
   refuses new work rather than wrapping. Worker exceptions must reach the same
   failure latch even if there is no normal successful completion message.

5. **No live clear or generation switch through this fence.**
   Current `log_clear_entries` ignores callback errors, and
   `raft_begin_load_snapshot` changes term/role BEFORE calling it. Adding pending
   inside the per-entry clear callback therefore cannot safely pause installation.
   Current production snapshot-load call is constructor recovery. Keep that
   explicitly cold; reject any non-restoring clear before storage work, latch
   failure, and never claim the library's subsequent memory clear was stopped.
   The E1 installer must quiesce/drain this owner and activate a verified new
   generation using its separately approved installation lifecycle. Do not add a
   general resumable in-place clear API here. Generation selection while a job
   is in flight is forbidden: dropping an old completion alone does not prevent
   its filesystem rename. Shutdown may join for lifetime safety; live progress
   may not use join or inline worker polling as a fallback. Deterministic inline
   test driving must be explicit and must not count as nonblocking evidence.

### 8.3 Why the call sites require this shape

| Current source seam | Observed behavior | Required protection |
|---|---|---|
| `raft_become_candidate` | Term save, then self vote whose return is ignored, then RequestVote sends | Propagate real failure; capture tuple; withhold sends; never restart campaign |
| Higher-term vote/append requests and responses | Term transition may precede further mutation in the same call | Fence every public turn, not only outbound-producing requests |
| `raft_periodic` / `raft_apply_entry` | Periodic can apply internally; before_apply precedes index increment | Dedicated pending return, not shutdown; checkpoint/local-durable coverage |
| `receive_peer` | Queues response before its final result check | No response construction/publication for interrupted append; check callback failure first |
| `log_offer` | Owns payload and stages WAL bytes | No higher-term WAL submission before metadata; retain result ownership |
| `log_delete` | Pops descending; callback sees each tail index, not the whole intended cut | Prepare once at library-selected boundary, before any pop |
| `WalWriter::poll` | Processes submitted blocks before maintenance | Drain old blocks without waiting on owner; gate new submissions |
| Snapshot load / clear | Changes state and ignores per-entry clear errors | Cold-only in this increment; not an asynchronous installer |

Primary references: `src/v2_replication.cpp` functions named above;
`src/v2_wal_arena.cpp` maintenance/poll; pinned
`build/_deps/willemt_raft-src/src/raft_server.c`, `raft_log.c`,
`raft_server_properties.c`; reproducible fork changes belong in
`deploy/patches/0001-raft-durable-quorum.patch`, not only generated build sources.

### 8.4 Implementation order after approval

1. Permanent worker-paused election/vote/commit REDs: observe real sync thread,
   durable tuple, absent frames, unchanged applied/result/proof state. Keep the
   existing synchronous diagnostic evidence; adapt fixture pumping explicitly.
2. Minimal hard-state fence GREEN, then failure/stale-completion/all-public-turn
   tests. Include single voter, higher-term responses, batched commitment and
   timers while pending. Remove each decisive guard in isolation and require
   the intended assertion, not fixture failure.
3. Permanent two-voter conflicting-suffix RED with real staged and durable tail;
   exact cut chosen by Raft, no cells or RESERVED results released early. READY
   results must come from committed history and cannot legitimately be placed in
   that uncommitted conflicting suffix. Implement prepare/drain/rewrite/replay;
   check both prev-term and incoming-entry conflict paths.
4. Compose failures/crash cuts and generation exclusion with existing compaction.
   Check exact restart bytes and independently authored outcomes, not only tags.
   Build then run focused/full Release and sanitizers; target timing later.

Rejected: per-tail-entry asynchronous rewrites (unnecessary I/O and harder partial
ownership), a wrapper-side conflict algorithm (duplicates Raft), a general task
scheduler (unneeded scope), and pretending cold-only clear closes E1 installation.
E3 is not complete until hard state AND runtime suffix truncation pass; no phase
acceptance or target performance claim follows from this review.

## 9. CP-043 host implementation, not deployment acceptance

Worker-mode hard state and whole-suffix truncation now use the approved fence.
The fork owns cut selection; one owned frame survives asynchronous replay, and
per-entry pops release obligations only after the whole-cut worker rewrite.
Replay must consume its exact grant or fail closed. Metadata completion validates
the full native identity/tuple before owner publication; marker and command apply
share the before-index-advance gate. All READY results remain readable while
pending; no new results, proof or outbound drain is released early.

The native receive result separates scheduling status from frame errors. The
one-slot maintenance transport is reused through typed job/completion contents;
worker exceptions latch failure even without a success completion. The job is
168 host bytes (bound256), allocated per fence; deferred input is32,768 inline
bytes. This is bounded, not allocation-free, and no canonical bytes changed.

Runtime clear remains forbidden and E1 installation remains separate. The
no-worker path is explicitly deterministic/cold testing, not a production
fallback. Live integration must call start_writer. Worker shutdown still joins
for lifetime safety and cannot be used to make a pending live fence progress.
Evidence, negative controls, corrected fixtures and precise limits:
`evidence/f6_async_persistence_2026-09-23/README.md`.
This does not close composed async power-cut, actual installation, independent
copy or target obligations, nor F5/F6/F7 or phase acceptance.

## 10. CP-044 composed host crash model (2026-09-24)

The current-generation metadata-plus-WAL model obligation above now has75
reached syscall cuts, three seed recoveries and six successful baselines. Exact
authored metadata tuples and WAL bytes are checked before recovery; standalone
vote coverage prevents a later WAL directory sync from masking omitted metadata
durability. Two direct-source mutants fail the intended promise assertions.
See `evidence/f6_async_crash_model_2026-09-24/README.md` for review corrections,
raw evidence and model limits. This extends evidence, not the approved contract.
Actual E1 generation activation/delayed old writes, independent-copy E2 and
target durability remain unproven. No physical power-loss or broad E3 closure.
