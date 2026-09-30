# F5 outbound and retained-log capacity review — CP-024

Status: **APPROVED by owner after CP-024; CP-025 implemented and host GREEN.**
Approval covers all four choices in §5. Implementation/evidence must be tracked
separately; this approval does not itself close F5 or any target gate. Final
implementation and evidence: `evidence/f5_capacity_integration_2026-09-23/README.md`.
2026-09-23; dirty branch `v2/phase-5-corrections`, base HEAD `d44a4a9`.
This supersedes ONLY the unapproved outbound/log rows in the earlier closure
plan. CP-023 result ownership, canonical frames and economic semantics stay fixed.

## 1. What is actually wrong

`ReplicatedKernel::send_append_entries` copies payloads into nested vectors on
every send; requests and responses accumulate in `outbound_messages_`, an
unbounded deque. A stalled transport can therefore grow RAM without admitting
another order. `log_payloads_` is a map of separately allocated payload vectors:
consuming a result does not release the history needed for replication/replay.
These are different ownership lifetimes and need separate bounds.

Three permanent, standalone diagnostics reproduce the growth:

| Trace | Observed result | Why this is useful |
|---|---|---|
| Receive512 valid empty heartbeats without draining replies |512 replies retained | Control traffic itself can exhaust memory; reserving only data-queue slots is insufficient. |
| Admit16 entries; stall quorum/transport; tick128 times |129 messages and542604 encoded bytes for one peer | Retries duplicate a fixed source history, rather than merely reflecting new admissions. |
| Commit/drain results and messages after each16-entry batch | Ordinary log advances from8128 to8129 | Result-ledger capacity does not bound retained history. |

The thresholds are review candidates, NOT previously accepted quotas. Finite
traces demonstrate growth beyond those candidates; absence of a finite limit
comes from inspecting deque/map insertion paths, not extrapolating a benchmark.
Evidence and reproduction: `evidence/f5_capacity_review_2026-09-23/README.md`.

## 2. Outbound proposal: replaceable packets, never replaceable commands

Allocate FOUR owned mailboxes per remote peer: RequestVote, RequestVoteResponse,
AppendEntries, AppendEntriesResponse. Each contains one complete bound frame.
AppendEntries gets32768 bytes; each other kind gets160 bytes. Current exact
bound-frame sizes are136,113 and129 respectively; empty AppendEntries is140.
CP-025 correction: the original review subtracted four bytes from these sizes;
the actual bound-frame overhead is88, not84. The approved160-byte slots still fit.
At most16 entries still fit the approved32768 frame ceiling (max29084 today).

An unsent message may be replaced ONLY by a newer-generated message for the same
peer and kind whose term is not lower. Replace the entire frame, not individual
fields: combining success/current_idx/first_idx from different responses can
invent an acknowledgement. Cross-kind replacement is forbidden. These are Raft
transport packets, NOT transactional trading outbox effects or accepted log
entries. Raft retransmission remains responsible for delivery after loss.

The newest same-term reply is not necessarily the greatest acknowledged index.
Keeping it may cause backtracking; this is preferable to manufacturing a reply
the follower never issued. Newer terms cannot be displaced by older ones in the
same mailbox. Leadership/term transitions and delayed replies need adversarial
tests; a general packet-loss argument is not enough to claim liveness.

Drain with a persistent fair cursor across peers/kinds. Responses may be visited
first in a sweep, but continuously refilled response slots must not starve requests.
At most one selection per slot per sweep; skip empty slots. The process transport
must also be serviced fairly. No unconditional delivery/latency guarantee follows.

Queue/coalesce returns success to Raft, meaning only local transport ownership.
Several pinned-Raft paths ignore send-callback errors, so a FIFO that simply
returns FULL is not a complete retry protocol. An impossible encoding/size fault
must latch a node fault and deny further authority/admission, not report a
successful acknowledgement or silently treat an implementation bug as fullness.

### C++ ownership and cost

The mailbox copies canonical bytes before the callback returns. No borrowed
`entry.data.buf`, arena index, vector pointer or span escapes into deferred work.
O(frame bytes) encode/copy; O(1) fixed-slot selection. No retry-dependent growth.
Use a caller-owned bounded output span: copy then clear on successful take; an
undersized destination leaves the mailbox unchanged. Zero-copy leases would need
an in-flight bank/release protocol; defer that complexity for MVP.

Production encoding should write existing canonical bytes from borrowed bounded
entry views directly into fixed scratch/mailboxes, without constructing nested
dynamic vectors. Preserve golden bytes; this is an ownership/API change, not a
wire-format change. Test harness adapters may decode to convenient dynamic values,
but production egress must not reintroduce an unbounded downstream queue.

Measured PROTOTYPE native mailbox owner33336 bytes/peer on this host (includes
four term/length fields, two counters, cursor and padding). Two remote peers:
66672 bytes. One additional32768-byte egress buffer per peer gives132208 bytes
combined at three voters; at the two-voter pilot it is66104. Excludes codec
scratch, network/kernel buffers and future transport state. Prototype is not a
shipping layout or a whole-process bound; target sizeof must be measured again.

CP-025 actual layout supersedes that prototype: one mailbox object always holds
TWO remote-peer slot sets and occupies66680 bytes, including on two voters. Add
the future transport's egress/scratch separately; do not halve the shipping object
by voter count. Slot allocation is deliberately fixed at the supported maximum.

Rejected alternatives: unbounded deque fails under stalled drain; bounded FIFO
still needs a full/control/retry protocol on every ignored-callback path; one
mailbox for all kinds lets bulk traffic overwrite a vote/response; max-field
response merging is not truthful; borrowed payloads reintroduce lifetime bugs.

## 3. Retained-log proposal: fixed owned cells and explicit admission tokens

Recommend8192 total retained entries:8128 ordinary tokens and64 protected control
tokens. Preserve the existing operator-caller AND command-kind classifier:
AdvanceKillEpoch, SetExecutorGeneration, SetStrategyMode, RequestMassCancel.
Control uses protected tokens first, then ordinary. This is admission priority,
not a claim that every parameterization is risk-reducing. Authority markers use
ORDINARY log capacity: they enable a new-risk attempt, even though they emit no
result. Results retain their independent approved1024/64 pool.

Each retained payload occupies one2048-byte cell allocated at startup. Today's
maximum command1789 and authority marker186 both fit. Physical backing is16 MiB,
NOT a16-MiB live-payload quota. Actual live canonical bytes are at most
8192*1789=14655488; the old16-MiB/256-KiB byte quotas cannot bind before the old
8192/64 entry quotas. Retire those misleading independent-byte-limit claims.
Still enforce canonical per-entry maxima and track actual live bytes for metrics.

Store per-entry full index, length, actual pool and optional result handle in a
fixed descriptor table. A bounded contiguous retained index interval permits
index-modulo8192 cell lookup with FULL INDEX verification. Pool tokens are
independent of physical cell numbers, so a modulo position never accidentally
forces ordinary work into a protected pool. Payload addresses stay stable until
Raft relinquishes the entry. No compaction/movement of still-referenced cells.
Allocate the slab once on the heap, not on a16-MiB stack or inline in a kernel
value. Refuse startup if allocation fails; do not partially activate the node.

### Backing RAM, including the library

| Component | Host bytes | Qualification |
|---|---:|---|
|8192 payload cells at2048 |16777216 | Proposed fixed backing, including internal slack |
|8192 descriptors at measured prototype40 |327680 | Includes optional result handle; do not count it again |
| Pinned Raft ring at capacity10240, entries32 |327680 | Current doubling sequence starts at10 |
| Last Raft resize old5120+new10240 |491520 | Transient ring peak, replaces preceding row |
| Payload+descriptor+steady ring subtotal |17432576 | Not process RSS or allocator-inclusive total |
| Same subtotal during last resize |17596416 | Excludes owner headers, ledger, WAL, scratch, allocator overhead |

MVP recommendation: keep the pinned Raft ring implementation, enforce entry
admission before entering Raft, and account for its bounded growth/resize peak.
Do NOT claim allocation-free admission or a worst-case latency bound. A fixed
8192-entry Raft constructor would save65536 steady bytes and remove growth jitter,
but expands the consensus fork for an optimization, so defer it. If later pursued,
leave V1's default constructor unchanged and extend patch/source attestation to
raft_log.c, not merely raft.h/raft_server.c. No global INITIAL_CAPACITY edit.

The slab wastes at most259 bytes per maximum-size command and more for small
ones. This buys straightforward lifetime/budget proofs without a variable-size
allocator. Packing payloads or changing library allocation is not required for
MVP correctness. These are initial conservative limits, not traffic-derived
uptime guarantees; retained history must eventually be safely reclaimed.

## 4. Full means refusal, not eviction

Local command/marker admission must preflight all applicable log/result/WAL
resources before Raft mutation. Append a distinct local log-capacity refusal
without renumbering existing statuses. Outbound slots need no per-command
reservation because bounded replacement is always available for valid messages.

Follower planning examines the WHOLE incoming batch read-only, including actual
pool tags of conflicting suffix entries. Reclaim credit is hypothetical until
each durable deletion succeeds. Do not release ownership early; on partial
failure, surviving history and reservations must remain exact. Marker and
command entries both consume log tokens. Match existing entries without charging
twice; protect the full stored index on modulo reuse.

If the batch cannot fit, reuse approved CP-023 control-only handling: process the
original term/prev with zero new entries, commit capped at min(advertised,prev),
and respond only for the verified durable prefix. This CAN change term, truncate
a conflicting uncommitted suffix or apply an existing prefix. It is not a
no-mutation malformed-frame rejection and not an acknowledgement of omitted data.
Counters distinguish result, log and WAL pressure. Unexpected callback admission
failure still fails closed; preflight is not a promise that disk cannot fail.

Startup reconstructs pool ownership oldest-first from retained history before
writable store creation. Count ALL retained entries, including committed ones;
result recovery only reserves the uncommitted subset. Oversized retained history
refuses unchanged, rather than deleting data or accepting a differently sized
node. Physical records already covered by a verified snapshot still require
validation but need not occupy retained-log cells.

Current recovery and WalSegment suffix/discard rewriting materialize entire
record vectors. Bounding only the runtime map does not bound these paths. Replace
them with bounded validation/streaming scratch before claiming this component's
memory closure. Recovery may use two passes (validate/count, then populate),
retaining the exclusive verified startup owner. Keep existing durability ordering
and bytes. Tail-by-tail streaming rewrite may remain quadratic in suffix length;
bounded memory is required now, batching that I/O is a separate optimization.

### Availability limits that must remain visible

- Consuming results or applying an entry does NOT free retained history.
-64 reserved log slots are finite. They do not create quorum, jump an earlier
  uncommitted prefix, reserve WAL/result capacity automatically, or guarantee a
  deadline for kill/cancel. All owners must have space for the action to commit.
- Venue observations remain ordinary under this proposal. At ordinary saturation,
  fresh fill evidence can be backpressured too; live risk must stay held and fresh
  risk must remain fenced. This proposal does NOT establish uninterrupted venue
  evidence ingestion. Safe reclamation/operational headroom must precede live use.
- No automatic snapshot/prefix deletion is added to escape fullness. The current
  compact path does not enforce the pending archive-before-last-copy guarantee.
  Archive/install/replacement admission remain separate F5 obligations. A full
  node can remain stopped until a safe recovery/reclamation path exists.
- Local memory quotas do not bound WAL/archive disk usage or transport/OS buffers.

## 5. Approval requested and implementation order

Approve (A) four coalescing owned mailboxes/peer with fair draining and bounded
copy-out; (B)8192 retained entries with8128/64 tokens and2048-byte cells, authority
markers ordinary; (C) explicit stop-on-full/control-only follower semantics and
startup refusal, with no unsafe reclamation; (D) keep and budget the existing
bounded-by-admission Raft growth for MVP, rather than expand the fork now.

After approval, TDD in this sequence:

1. Mailbox component: exact bytes, all four kinds, cross-kind independence, lower
   term rejection, same-term success/failure replacement, fair drain under refill,
   undersized copy-out and original response/retry growth traces.
2. Kernel egress integration: callbacks, durable truthful replies, elections and
   catch-up after packet replacement/loss; byte ownership after log truncation;
   ordinary prefix before kill. Mutate guards and identify the failing assertion.
3. Fixed log component and all admission paths: exact-bound+one, protected spill,
   markers, modulo wrap, partial durable truncation failure, simultaneous result/
   log/WAL pressure, whole follower batch, no false acknowledgement.
4. Bounded recovery and rewrites: committed and uncommitted overcapacity refuse
   unchanged; snapshot-overlap history, restart, truncation, failure, and stable
   payload lifetime. Inspect actual transient allocations, not only final counts.
5. Register approved REDs as GREEN CTest suites and independent gate inventory;
   fresh full Release/sanitizers, checkpoint/evidence/atlas. Targets remain pending.

Keep outstanding install/archive/async-fence contracts separate. No tuning,
network process, Grafana, venue adapter, source freeze or deployment in this slice.
