# V1.1 Phase 3 Storage Design Pivot

## Status

Proposed design checkpoint. No production durability behavior changes until the
experiments and protocol changes in this document pass their acceptance gates.

## 1. Decision to make

The Raspberry Pi evidence shows that synchronous per-entry persistence blocks
the single Raft owner thread long enough to miss heartbeats and start elections.
The proposed pivot is to replace per-entry file creation/append/sync work with
fixed-capacity memory blocks owned by each voter, and to persist sealed blocks
in bounded batches.

The decision is not simply "disk or memory." It is where the system places its
durability boundary:

1. volatile append: bytes exist only in process memory;
2. locally durable append: bytes survived an ordered storage flush on one voter;
3. quorum durable append: a voting majority reports locally durable copies;
4. committed: Raft may expose the command to the state machine; and
5. applied/responded: the deterministic effect and receipt are visible.

V1.1 must preserve the rule that a successful client response never overtakes a
quorum-durable log entry. Fixed blocks may stage and batch work, but may not
change that rule implicitly.

## 2. Relationship to the Velocita book

The proposal closely matches two designs described in the book.

Chapter 13 describes a fixed-size, memory-mapped Raft log with:

- a cache-line-aligned header;
- a power-of-two index ring;
- a byte-oriented data ring;
- entries that never straddle the ring seam;
- O(1) append, truncation, and committed-prefix compaction; and
- explicit refusal when the bounded region is full.

The append path is a bounds check, `memcpy`, index store, and position advance.
It has no per-entry syscall. The book is equally explicit about the trade-off:
without per-entry `fsync`/`msync`, a hard power loss may remove entries that the
Raft process believed were present. It calls this an availability-versus-
durability decision rather than free performance.

The same chapter describes snapshots as two copies of FSM state: a live DRAM
arena and a mapped durable arena. Snapshot time copies the live arena into the
durable arena and synchronizes it. That removes mapped-file interaction from
each FSM mutation while retaining an explicit persistence point.

The proposed V1.1 pivot adopts the bounded memory, ownership, ring, and snapshot
ideas. It does **not** adopt an unqualified volatile acknowledgment contract.
The target design adds an asynchronous durability fence before protocol success.

## 3. What fixed per-node blocks solve

Fixed blocks can remove the following costs from the owner-loop append path:

- opening and closing the WAL for every entry;
- extending files and allocating vectors during traffic;
- waiting for `fsync` while heartbeats and ticks are ready;
- rewriting a metadata temporary file per commit;
- renaming that file and synchronizing its parent directory per commit; and
- unbounded memory growth under a slow device.

They also make capacity a first-class state. If all blocks are filling,
flushing, or retained for recovery, the voter refuses new admission with a
storage-backpressure result instead of allocating or blocking indefinitely.

Each voter already runs in a separate process/container and data volume, so its
address space and raw pointers are already isolated from the other voters.
Per-node arenas remain useful, but their principal benefits are bounded
allocation and single-writer ownership, not protection from cross-process
pointer races.

## 4. What fixed memory alone cannot solve

Anonymous memory, `memfd`, and `tmpfs` disappear on power loss. They can prove
that storage is the bottleneck, but they cannot be the authoritative WAL for a
money-bearing system.

A file-backed mapping survives process restart only to the extent that dirty
pages reached stable media. A C++ atomic store or release fence orders CPU
visibility; it does not make bytes durable on disk.

Flushing only when a block is nearly full is also insufficient. At low traffic,
a partially filled block could remain volatile for minutes. The flush policy
must be triggered by both space and time, and successful client acknowledgment
must still wait for the relevant durable-quorum fence.

This pivot can eliminate **owner-loop blocking** and amortize storage barriers.
It cannot eliminate the physical durability latency from a response whose
contract promises stable storage.

## 5. Recommended architecture: bounded segmented WAL with durability fences

### 5.1 Per-voter memory arena

Each voter owns a process-local `WalBlockArena` allocated and pre-faulted during
startup. An initial experiment should use eight 256 KiB blocks per voter (2 MiB
per voter), with the values configurable and reported in run metadata.

Every block has a fixed identity and generation and transitions through:

```text
FREE -> FILLING -> SEALED -> FLUSHING -> DURABLE -> RECLAIMABLE -> FREE
```

Only the owner thread mutates `FILLING`. After publishing `SEALED`, the owner
does not touch the block until the writer returns a completion for that exact
block id and generation.

A record contains at least:

```text
magic | format_version | record_length | crc32
index | term | entry_id | entry_type | request_id | payload
```

No record straddles a block boundary. Tail slack is wasted deliberately so
recovery and direct views never need split-record logic.

### 5.2 Thread ownership and C++ memory ordering

The Raft owner and persistence writer communicate through two bounded SPSC
rings:

```text
owner --sealed block descriptor--> writer
owner <--durable completion-------- writer
```

Descriptors use `{block_id, generation, first_index, last_index, used_bytes}`
rather than heap ownership. The backing arena has process lifetime, so no block
can be freed while either thread can name it.

The producer publishes a fully initialized descriptor with release ordering;
the consumer reads it with acquire ordering. Queue head and tail counters are
placed on separate cache lines to avoid false sharing. A generation number
prevents an old completion from being mistaken for a newly reused block.

Release/acquire proves cross-thread visibility only. Durability is represented
by a separate writer completion emitted after the storage flush succeeds.

### 5.3 Flush triggers

A filling block is sealed by the first applicable trigger:

- insufficient space for the next bounded record;
- byte or entry batch threshold;
- maximum durability age;
- explicit shutdown/checkpoint request; or
- a protocol event requiring an immediate hard-state fence.

"Near overflow" is therefore one trigger, not the durability policy. The
maximum-age trigger bounds low-rate exposure; the size trigger enables group
commit during bursts.

Initial values are hypotheses, not production defaults:

| Parameter | Initial experiment | Purpose |
|---|---:|---|
| Block bytes | 256 KiB | Avoid tiny writes while keeping bounded latency |
| Blocks per voter | 8 | One filling, one flushing, and bounded spare capacity |
| Seal age | 1 ms | Bound low-rate batching delay |
| Admission high watermark | 6 occupied blocks | Shed load before complete exhaustion |
| Hard stop | 8 occupied blocks | Never overwrite recovery state |

### 5.4 Persistence writer

The writer uses one pre-opened, preallocated segment file per voter. The first
implementation should use ordered `pwrite`/`pwritev` followed by `fdatasync` on
the writer thread. Direct I/O and `io_uring` remain later experiments; they add
alignment and completion complexity without changing the protocol invariant.

After the flush completes, the writer publishes a completion containing the
highest contiguous durable index. Errors are terminal storage states and return
to the owner; they are never converted into successful acknowledgments.

### 5.5 Raft durability fence

The owner tracks separate coordinates:

```text
memory_appended_index
local_durable_index
peer_durable_match_index[peer]
commit_index
applied_index
```

A follower may copy AppendEntries into its memory arena immediately, but it
does not send a successful AppendEntries response covering those entries until
the writer reports them durable. The leader counts a peer only to the durable
index reported by that response.

The leader may advance `commit_index` only when a voting majority has durable
copies under the Raft term rule. FSM apply and the final client response remain
behind commit.

This is the change that preserves safety while removing storage wait from the
control-plane loop.

### 5.6 Hard state and commit metadata

Term and vote changes are rare and safety-critical. V1.1 should retain a
durability fence before sending the corresponding vote/protocol response. The
current temporary-file/rename/directory-sync sequence should be replaced by a
small preallocated two-slot hard-state journal with sequence number and CRC.
The newest valid slot wins during recovery.

Persisting `commit_index` through a separately renamed metadata file on every
command is the largest avoidable barrier. Before removing it, recovery must be
changed and proven. Two valid approaches are:

1. record commit/checkpoint markers in the ordered WAL batches; or
2. restore snapshot plus log, elect a leader, establish a current-term commit,
   and only then apply the newly proven committed suffix.

V1.1 should not simply stop persisting the commit index while continuing to
replay it as if it were durable.

## 6. Required change to the pinned Raft library contract

The pinned [`willemt/raft` commit](https://github.com/willemt/raft/tree/e428eeb921a014192d1d703dd317f3f29f5916c5)
API says that `log_offer`, `log_poll`, `log_pop`, `persist_vote`, and
`persist_term` must flush before returning. Its receive-entry and
receive-AppendEntries calls are explicitly allowed to block for that flush.

Consequently, these implementations are rejected:

- enqueue a block and return success from `log_offer` before it is durable;
- move `fsync` to a worker but wait for that worker inside `log_offer`; or
- send a successful follower AppendEntries response for merely staged bytes.

The first and third violate durability. The second preserves durability but
does not remove owner-loop starvation.

The safe design requires a pinned local fork or replacement adapter that adds a
two-phase operation:

```text
append/stage -> persistence pending -> persistence complete -> protocol success
```

This is the principal engineering cost and release risk of the pivot. The fork
must be small, documented, fuzzed, and checked against the upstream commit.

## 7. Snapshot and compaction relationship

Flushing a WAL block is not a snapshot. A WAL is the ordered history of
commands; a snapshot is the deterministic FSM state at one committed index.

Let `S` be a durable snapshot's last included index and term. Only after the
snapshot file, checksum, metadata, and directory entry are durable may WAL
segments whose last index is at or below `S` become reclaimable. The previous
known-good snapshot remains authoritative until the replacement is complete.

Compaction must start before the ring is nearly full:

- start snapshot/compaction around 70% occupancy;
- alert and reduce admission around 80%;
- refuse durable work before 100%; and
- never overwrite an entry required by recovery or a lagging voter.

For the current counter FSM, snapshot serialization is small. The design must
still include request deduplication receipts, membership/configuration, last
included index/term, and state digest so it scales to the trading FSM.

## 8. Alternatives considered

### 8.1 Pure volatile quorum

All voters acknowledge memory append and flush only near capacity. This gives
the lowest latency but can lose acknowledged commands after correlated power
loss. Because all three laboratory voters share one Pi and one power source,
that failure is correlated by construction. Rejected for the default contract.

### 8.2 Book-style mapped ring without per-entry sync

This is close to the reference design and is feasible with the current callback
shape. It improves append latency dramatically, but its power-loss contract is
kernel dirty-page writeback rather than quorum-stable media. Keep it as an
explicit benchmark/experimental mode, not as a silent production change.

### 8.3 Writer thread with synchronous wait

Easy to implement and safe, but the owner still waits for every flush. It does
not address missed ticks. Rejected as the target design.

### 8.4 Faster durable media with current synchronous callbacks

NVMe or separate hosts/devices may make the current design acceptable with much
less software risk. This remains a required comparison. It does not fix the
architectural fact that a tail flush can stop the control plane, but it may be
the correct V1.1 product decision while asynchronous persistence matures.

## 9. Staged feasibility experiments

### Experiment A: causal storage isolation

Run the existing instrumented cluster with identical fixed-capacity data paths
on SD-backed storage and tmpfs. Tmpfs results are performance evidence only.

Acceptance signal:

- tmpfs retry amplification at 5/s and 20/s is near 1.00;
- terms remain stable and exactly one leader exists;
- owner-loop maximum gap stays below the election-risk budget; and
- SD mode reproduces elevated I/O PSI and gaps.

### Experiment B: synchronous mapped ring

Implement a benchmark-only fixed mapped ring that matches the book's layout,
including CRC framing and bounded refusal. Compare append, recovery, and I/O
pressure without claiming hard-power durability.

### Experiment C: asynchronous block writer prototype

Build the arena, SPSC queues, preallocated segment writer, and durable-index
completion path independently of Raft. Stress it with variable records, forced
short writes, injected sync failures, wraparound, stale generations, and queue
exhaustion.

### Experiment D: two-phase Raft integration

Integrate deferred durable completion into the pinned Raft fork. Verify that no
AppendEntries success, commit, apply, or client success can cross a missing
durability completion.

### Experiment E: snapshot and power-cut recovery

Test empty recovery, WAL-only recovery, snapshot-only recovery, snapshot plus
tail, torn final record, failure during snapshot replacement, failure during
compaction, and repeated request IDs after restart. The decisive oracle is the
last acknowledged request ID and state digest, not clean shutdown output.

## 10. Metrics and acceptance gates

Export raw fixed-layout counters; formatting remains outside the hot path:

- memory-appended, local-durable, committed, and applied indexes;
- WAL block occupancy by state and high watermark;
- oldest unflushed age;
- blocks and bytes per flush;
- flush duration distribution and failures;
- storage-backpressure admission rejections;
- snapshot index, age, bytes, duration, and failures;
- owner-loop maximum gap and tick age;
- term and leader changes; and
- retry amplification.

The pivot is accepted only when:

1. all correctness and recovery tests pass under fault injection;
2. a power-cut drill never loses a successfully acknowledged command;
3. 5/s and 20/s target runs keep one leader and a converged term;
4. steady retry amplification is at or below 1.05 without injected failure;
5. owner-loop gaps remain below the agreed election-risk budget;
6. queues and arenas remain bounded under a stalled writer; and
7. overload produces explicit storage-backpressure refusal, not memory growth,
   silent data loss, or heartbeat starvation.

## 11. Recommendation

Adopt the fixed-block idea as a **bounded staging and batching layer**, not as a
replacement for durable WAL semantics. Begin with storage-isolation and an
independent asynchronous writer prototype. Do not alter production
acknowledgment behavior until the two-phase Raft contract and power-cut recovery
proof exist.

In parallel, benchmark NVMe/separate-host durability. If faster independent
media meets the V1.1 latency and liveness targets with the current synchronous
library, ship that lower-risk topology first and continue the two-phase storage
engine as a later version.
