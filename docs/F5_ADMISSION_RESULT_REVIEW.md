# F5 next boundary: bounded admission and result ownership

2026-09-23, CP-025. **§7 and §8 APPROVED and kernel-integrated. Separate outbound/log approval is implemented in CP-025; install/archive/async contracts remain draft.**
Local history binding is implemented; this document prevents the next bounded-
container change from silently changing the promises made to callers/followers.
No new bytes; the approved result capacity now covers both kernel admission paths,
recovery and publication. Governing documents: ADR0014,
`F5_F7_MVP_CLOSURE_PLAN.md`, and ADR0001's deterministic apply/ownership rules.

## 1. Recommendation: reserve before accepting, publish without allocation

Use a bounded local result ledger on EVERY replica. Each accepted canonical
COMMAND entry that may later emit a CommittedResult must already own its result
slot. States are free, reserved-uncommitted, and
committed-ready. Bind reservations to log index plus the accepted command's
identity/content, not just a caller ID: a retried command at a different log index
still produces a distinct result notification even when the reducer deduplicates.

Authority-barrier markers intentionally emit no CommittedResult, so they do NOT
reserve a business-result slot. Their log/WAL/outbound ownership and the existing
single pending proof remain bounded separately. This corrects the draft's broader
"every entry" wording: applyable does not necessarily mean result-producing.

Why: checking a queue after apply is too late. The economic transition has already
committed; dropping the result loses an obligation and throwing can strand the
next committed kill. A leader-only check does not bound a slow follower. Reserved
slots make apply a no-fail publication into already-owned storage.

Alternative rejected: overwrite old results, or clear them on election/compaction.
Those events do not establish that the consumer has received the result. Changing
to leader-only publication is a separate semantics change, not a free optimization.

## 2. Exact ownership transitions to design and test

| Boundary | Required ownership action | Reason |
|---|---|---|
| Local proposal | Reserve result + log bytes/entry + staging budget before Raft accepts | Failure must leave no orphan obligation |
| Incoming append batch | Preflight/reserve the complete potential new batch before append callbacks | This Raft implementation may partially append before a later callback fails |
| Duplicate retransmission | Reuse existing reservation for the same accepted log entry | Retry cannot consume capacity repeatedly |
| Conflicting suffix | Keep old ownership until successful durable truncation; release precisely removed uncommitted entries | Early release can make a surviving obligation unowned |
| Committed apply | Fill its reserved result slot; no allocating queue push | Capacity failure is not allowed after economic state changes |
| Subscriber take | Deliver then release exactly its ready slot | The obligation ends at the API's defined delivery boundary |
| Election | Preserve ready results and still-retained log reservations | Leadership is not consumption |
| Snapshot compaction | Release covered log ownership only, not ready result slots | Recovery coverage and subscriber delivery are separate facts |
| Startup | Suppress replayed committed notifications; reserve for retained UNCOMMITTED suffix before serving | Those entries can commit after startup and then require live result publication |

The last row corrects an omission in the worker's initial proposal: “restoring
doesn't emit results” is true only of already-committed replay. It cannot excuse
unreserved entries that become committed later. The replicated request receipt
supports retry/recovery; a volatile notification queue is not an exactly-once
external delivery channel.

## 3. Prefer local backpressure over a new distributed credit protocol

Recommendation: a full replica refuses additional ordinary obligations and relies
on Raft retransmission after its local consumer drains. A leader may therefore
admit work locally which cannot yet reach quorum. This preserves bounded memory
but deliberately does NOT promise immediate quorum availability.

Distributed credits would let a leader predict more, but add stale-grant,
leadership-change and credit-reclamation protocols. Do not add those merely to
replace a deque. First prove local reservation and callback rollback sufficient.
CP-023 implements this for result ownership; CP-025 integrates the separately
approved outbound/log ownership contract.

The implementation seam resolved by approved §8: simply returning capacity_exceeded
BEFORE Raft for every full AppendEntries would also suppress a legitimate HIGHER
TERM. That is unacceptable. The reviewed implementation must still process
authenticated term/heartbeat/control information and return a truthful bounded
reply without admitting unreserved entries. A prefix-only/internal header path
is the implemented design, checked against this pinned Raft library's
success/current_idx/first_idx behavior. Do not acknowledge the
original batch after only processing its header.

CP-021 resolved one prerequisite, NOT that whole seam. The V2 receiver now caps
the commit passed to pinned Raft at `min(leader_commit, prev + count)`; a zero-entry
message is capped at `prev`. Negative/overflowing prefix indices refuse before
mutation. The pinned library instead uses its whole local log as the ceiling.
Synthetic bound-ingress REDs exposed unchecked-tail apply; independent analysis
did NOT establish an honest current-sender election trace that leaves that tail
present. Its eager backtracking truncation may remove it first. Do not recast the
synthetic regression as a proven production split-brain trace. The guard makes
the receiver's verified-prefix rule explicit before future saturation handling.

A projected header can itself update term/leader, truncate a mismatching prev
suffix, and apply a verified committed prefix. It is NOT a no-mutation capacity
refusal. Its API/counter/response meaning must distinguish valid control processing
from malformed-frame refusal. The envelope's digest is lineage/integrity, not
transport authentication; the future process still needs authenticated peer IDs.

Similarly, reserving room for kill does not reorder the log: a kill behind an
unaccepted ordinary prefix cannot magically leap over that prefix. Reserved lanes
protect resource availability, not unconditional progress under every stalled
consumer/quorum failure. This limitation must be observable and tested.

## 4. Outbound and log capacity must join the same admission transaction

CP-024 review refinement (owner APPROVED, CP-025 implemented): `F5_OUTBOUND_LOG_CAPACITY_REVIEW.md`
provides permanently available coalescing mailboxes, avoiding a fallible outbound
reservation per command. Fixed per-entry cells derive the byte ceiling from the
entry count, rather than imposing a second nonbinding live-byte quota. The older
requirements below describe the hazards, not an approved FIFO implementation.

- Outbound messages need both descriptor and owned-byte limits. Reserve an
  essential response slot before processing a request that must reply. Do not
  label every data-bearing AppendEntries as “control.” Callback refusal must
  preserve a retry path and cannot become a fabricated successful acknowledgement.
- Retained log payloads need both entry and byte limits, including bounded map/
  node-pool overhead. Release only on successful pop/poll/clear under their distinct
  semantics. A fixed WAL staging arena does not bound already-durable history.
- Result/log/outbound/staging reservations must roll back together on admission
  failure. A scoped owner token is appropriate C++; a naked counter increment
  followed by several throwing allocations is not an ownership protocol.
- Every boundary needs an observable refusal reason/counter for the later M3a
  dashboard. No metrics sink, filesystem discovery or network call inside apply.

Two design traps and their current status:

1. If every slot is reserved by an UNCOMMITTED suffix, demanding a spare slot
   before replacing that suffix can deadlock catch-up: no ready result exists for
   the consumer to drain. Never free old slots speculatively. Prepare a bounded
   replacement plan that can transfer precisely those slots AFTER successful
   durable truncation, or explicitly accept/refuse that availability limitation.
   A proposed RAII object is not a rollback of durable term/log transitions.
   CP-023 implements and tests the read-only ownership-transfer plan for results.
2. A bounded outbound reply queue must not turn "full" into ignored higher terms.
   Preallocated per-peer response mailboxes with explicitly reviewed coalescing
   are an alternative to an exhaustible reserve: Raft tolerates transport loss,
   but results cannot be dropped. Coalescing, callback retry behavior and consumer
   ownership need their own tests. No outbound policy is approved here.

## 5. Capacity approval is still separate

Owner approval of §7 covers1,024 result slots (64 protected); §8 was separately
approved and integrated CP-023. CP-024 supersedes the earlier unapproved outbound
FIFO and log byte-budget candidates with four coalescing mailboxes per peer and
8192 retained2048-byte cells (8128 ordinary/64 control). The full review and
backing/Raft growth arithmetic are in `F5_OUTBOUND_LOG_CAPACITY_REVIEW.md`.
Those revised capacities and outbound semantics were approved after CP-024 and
implemented CP-025. Other candidate budgets do not inherit that approval.

Before approval, tabulate allocated backing memory as well as logical used-byte
charges. For example, 64 fixed 32-KiB frame buffers allocate 2 MiB per peer even
if a counter limits live bytes to 256 KiB. Calling that a 256-KiB RAM budget would
be false. Result-ledger metadata also makes each slot larger than the current
176-byte CommittedResult. Derive exact layouts before freezing the budget sheet.
Measured traffic/consumer gaps and target latency remain unknown; saturation must
be safe regardless, and availability claims must stay conditional.

## 6. Required proofs before this part of F5 closes

Leader and follower saturation independently; no partial append on batch-capacity
failure; higher-term handling while full; control reserve exhaustion; restored
uncommitted suffix followed by live commit; result retention across elections and
compaction; same-entry retry versus later-index duplicate command; conflicting
suffix release; callback/allocation failure rollback; consumer drain/retry progress.
Mutations must remove each guard and fail for the intended observable effect.

Also preserve decoder arithmetic checks separately from quota refusal. Independent
review noted additional signed peer-coordinate gaps. CP-022 now rejects negative
terms/commit/log coordinates, nonpositive entry terms and invalid response indices
before Raft. Fresh regression and a guard-removal mutant verify the boundary;
the worker's original stale-library runs are not credited as current-tree REDs.
These are basic field checks, not comprehensive cross-field Raft validation.

The remaining F5 dependencies are unchanged: separately reviewed replacement
fencing/admission and transactional snapshot install/suffix catch-up; verified
archive before last-copy reclamation; integrated evidence. F6 asynchronous fences
and the final frozen target gates are not closed by bounded containers.

## 7. Narrow owner review now: result-ledger contract only

Owner approved the following local result contract and1024/64 budget on2026-09-23,
then requested a C++ implementation explanation BEFORE code work. CP-022 implemented
the standalone ledger; CP-023 integrates it after the separate §8 approval.
This is NOT approval of outbound/log quotas, a wire schema, the header-only
capacity protocol, archive/install bytes, or F5 closure. Integration still has to
prove the ownership transitions above; landing a container alone is insufficient.

1. **1,024 result slots on each voter: 960 ordinary plus64 protected control.**
   Control-eligible commands take the protected pool first, then ordinary space;
   ordinary commands can never borrow the protected pool. Derive eligibility from
   the same decoded command/operator-identity predicate as the existing WAL
   control-lane check (AdvanceKillEpoch, SetExecutorGeneration, SetStrategyMode,
   RequestMassCancel); do not trust a caller's lane label or require a new peer
   lane field. This means eligibility is reproducible on a follower as well.
   There is no result slot for an authority marker.
2. **Reserve before accepting; retain ready results until taken.** Elections and
   log compaction are not consumption. Publish into owned storage without a heap
   allocation. This is a volatile, per-replica notification API, not durable
   exactly-once client delivery: already-committed replay remains suppressed and
   caller recovery uses the replicated request receipt. Recovered UNCOMMITTED
   command entries need reservations before the node serves; an oversized suffix
   refuses startup unchanged rather than discarding obligations.
3. **Stop admission when the applicable pool is full; never overwrite results.**
   Local refusal occurs before accepting the command. Follower refusal requires
   complete-batch planning and separately proven higher-term/control processing.
   No distributed credits for MVP. A leader may have room while its quorum does
   not; an admitted proposal is NOT a committed decision. On the NUC/Pi two-voter
   topology a stalled consumer on either machine can ultimately stop new commits.
   Sixty-four control slots buy finite room, not permission to bypass an earlier
   missing ordinary log entry. Do not promise a time horizon without measured
   traffic and consumer service rates.

### Native layout and cost (not persisted or canonical)

Fresh host C++20/cacheline64 probe, using current CommittedResult definitions:
source/output retained as `evidence/f5_verified_prefix_2026-09-23/result-ledger-layout-proposal.*`.
Main inspected the probe and reran its binary. CP-022/023 implement that native
layout; it is not target acceptance or canonical serialization.

| Component within one proposed slot | Offset | Bytes | Meaning |
|---|---:|---:|---|
| CommittedResult | 0 | 176 | Identity/result plus term/index; term/index bind a reservation before publication |
| Canonical command digest | 176 | 32 | Binding to the accepted content, not only a caller-supplied identity |
| Generation | 208 | 8 | A recycled slot is not the old reservation |
| Next slot | 216 | 2 | Free-list or ready-FIFO linkage, never both simultaneously |
| State | 218 | 1 | Free / reserved / ready |
| Pool class | 219 | 1 | Ordinary or protected; derivable from slot range if omitted |
| Alignment padding | 220 | 4 | Native padding only; must not be serialized or compared with memcmp |

Slot size224, alignment8. Array:1,024*224=229,376 bytes. A24-byte proposed header
contains the next generation and two free-list heads, ready head/tail and counts;
total **229,400 bytes per voter**, about224 KiB. Allocate once during startup;
the owner keeps an8-byte unique_ptr on this host rather than embedding the array
inside TradingState. A generation/slot handle occupies16 bytes and must live
with each retained command's ownership. That handle storage, mapping, allocator
metadata, other queues and Raft allocations are NOT included in229,400. This is
a ledger allocation budget, not a whole-process memory ceiling or target ABI
measurement. Omitting the pool byte does not shrink this slot because of padding.

Two free lists and one ready FIFO make reserve/publish/take O(1); computing the
binding digest separately at admission is O(command bytes), bounded by the
approved command maximum. A generation-
checked handle prevents an old owner from publishing into a newly reused slot;
index/term/content checks still matter. Never wrap the generation counter: refuse
further reservations at exhaustion while existing results remain drainable.
Replacing an uncommitted suffix needs an ownership-transfer plan, not speculative
free-list reuse. A failed durable truncation must leave old ownership intact.

Alternatives rejected: an unbounded deque violates the memory guarantee; dropping
or overwriting after apply loses an accepted obligation; leader-only publication
changes the existing replica/result contract; increasing the slot count merely
delays the same saturation problem. The proposed number is a conservative pilot
starting point, NOT throughput evidence. Adjust later only with measurements and
explicit configuration/contract review.

## 8. Follower saturation protocol — approved 2026-09-23

The owner separately approved this protocol after CP-022. The earlier §7 approval
did not imply it. Integrate both admission paths and recovered suffix ownership;
do not switch only the leader path merely to make its capacity test pass.

Approved behavior: if a whole incoming batch cannot obtain result reservations,
invoke the existing Raft handler once with its original term/prev fields but no
new entries, and cap leader_commit at prev. Normal Raft handling can learn the
newer term, step down, verify the previous entry, or durably truncate a conflicting
UNCOMMITTED suffix. Its response must cover only the actually verified durable
prefix, never omitted entries. A conflicting committed prefix is still fatal.
Record a distinct result-capacity refusal count. A valid frame whose control
portion was processed returns frame-error NONE, not a malformed/no-mutation
capacity error. Do not immediately retry the omitted batch after that handler
mutates state. Ordinary Raft retransmission retries later; capacity returning does
not promise zero-delay progress, especially if an unchanged match index does not
trigger the library's aggressive-send branch.

Why not reject before Raft: a full result pool would hide a legitimate higher
term. Why not let log_offer fail partway through: earlier entries can already
have appended. Why not drop a ready result: it violates the approved notification
obligation. Why not invent distributed credits now: stale grants and failover
would introduce another protocol without removing local ownership requirements.

For feasible replacements, read-only planning identifies the first missing or
term-conflicting entry exactly as pinned Raft does. Existing matching entries
need no new reservation. A full uncommitted suffix may supply replacement capacity
ONLY after successful durable deletion, never from ready results or committed
entries. Plan by actual pool ownership: a control command may have borrowed an
ordinary slot. Raft deletes tail-first before appending a replacement; an I/O
failure partway through deletion leaves already-deleted and surviving portions
with different ownership. RAII cleanup must reflect actual successful transitions,
not pretend the whole Raft call was atomic. Capacity failure must be preflighted;
other storage/allocation failures are not magically eliminated by result planning.

Outbound queue bounds/coalescing and retained-log quotas received separate
approval and CP-025 implementation. Neither this result contract nor that slice
makes a whole-process boundedness claim. Header-only processing is not sender authentication; deployment still needs
an authenticated transport bound to the verified peer identity.

CP-023 integration evidence: `evidence/f5_result_integration_2026-09-23/README.md`.
The implementation uses read-only capacity/generation simulation rather than
pre-reserving spare slots. During the synchronous single-owner Raft call, only
successful suffix deletion and planned offers change reservations; there is no
interleaving subscriber operation. Offers reserve before Raft accepts the entry.
An unexpected failure of this capacity proof is fatal, not ordinary backpressure.
CP-023 native kernel inline size was91472; CP-025 is91424. Ledger229400 lives
separately. Each retained log descriptor includes24 bytes of optional-handle
storage, even after publication clears its value; CP-025's40-byte descriptor
already counts it. See the updated memory atlas, not this historical size alone.
