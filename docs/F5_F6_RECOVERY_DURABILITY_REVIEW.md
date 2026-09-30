# F5/F6: bounded recovery and durable protocol state — review

2026-09-22. **DIRECTION APPROVED by the owner after CP-013.**
Approval covers A–F below, including provisional retention of atomic replacement;
it does not approve unspecified bytes, capacities, retention or the concrete
asynchronous Raft protocol. These still require their own review. Existing-contract
fixes and test-only persistence modeling may proceed. ADR 0001 and the original
guarantees are not reduced. `F5_STATIC_IDENTITY_BYTE_REVIEW.md` was subsequently
approved and integrated through CP-016. `F5_HISTORY_PEER_IDENTITY_REVIEW.md` is
the next semantic review, approved after CP-017; its exact new formats are not
approved or encoded.

## 1. Why these tracks run together, but cannot be merged blindly

F5 defines which history belongs to this cluster, how much work can be retained,
and what permits replacement/reclamation. F6 defines which progress survives a
crash and when the node may claim it. A bounded queue carrying an acknowledgement
before its disk fence is unsafe; a perfectly durable unbounded queue can exhaust
the node and strand live risk. Each fixes a different failure.

Original CP-013 seams, with current startup correction noted:

- `ReplicatedKernel` reconstructs membership from constructor arguments; empty
  means self. Neither PersistentMetadata nor StoredSnapshot binds that choice.
  A04 is therefore a recovery identity failure, not an election tuning issue.
  CP-016 CORRECTION: raw construction is removed and startup drift now refuses.
  Persisted history objects themselves still lack the proposed lineage binding.
- Outbound messages, committed results and retained payloads use unbounded STL
  containers. The fixed WAL arena limits unsynced staging only; a fast writer
  replenishes it while a slow consumer's results continue accumulating (A07).
- PeerPayload has no snapshot transfer. Local compact/restart cannot prove that
  a replacement can rejoin after its leader discards the prefix (A08).
- `compact` deletes covered history without archive verification. A current
  snapshot cannot explain every earlier command or reclaimed order (A14).
- Metadata term/vote/commit saves synchronously write/fsync/rename/fsync-dir on
  the protocol owner. CP-006 process cuts do not simulate lost OS cache: removing
  fsync survived those tests. F6 still needs durability-sensitive evidence and
  asynchronous hard-state fencing, not merely another passing process-exit test.

Source routing: include/velocita/v2/replication.hpp, src/v2_replication.cpp,
include/velocita/storage.hpp, src/storage.cpp, include/velocita/wire.hpp,
include/velocita/v2/snapshot_store.hpp; audit A04/A07/A08/A09/A14 and CP-006.

## 2. Decision A — persist identity separately from economic state

Recommend an explicit bootstrap/recover distinction and immutable per-lineage
cluster manifest, verified BEFORE writable recovery or Raft participation.
Semantic fields: independently generated cluster incarnation; sorted unique
positive voter IDs; declared quorum durability mode; protocol/storage semantic
versions and intended immutable execution catalog root. Local node ID is bound
separately to its directory. All configured members must match the shared identity.

Derive F4's authority domain from a domain-separated canonical digest of the
shared manifest. Do not hash the local node ID into it: NUC/Pi must agree on the
same authority domain. Do not use current leader, term, IP address or build hash
as cluster identity. Terms change; endpoints move; identical code rebuilds must
not reinterpret economic history as a new cluster.

Existing nonempty storage without the new binding refuses; preserve it as prior
pre-live evidence. No automatic backfill from today's command-line arguments,
because that would bless exactly the configuration drift the check must detect.
Bootstrap is explicit on a verified empty directory. Keep one-voter laboratory
mode explicit; production configuration remains the approved NUC/Pi two voters.
No dynamic membership or automatic single-voter outage fallback.

Alternative: snapshot-only membership. Rejected because first startup, WAL-only
recovery and metadata-only directories would remain unbound. Alternative: local
node ID as whole identity. Rejected because it neither pins the voter set nor
distinguishes two unrelated clusters reusing node numbers. Cost: new versioned
manifest/identity binding and fresh pre-live directories; exact bytes still need
review. This does not require enlarging TradingState or its nearly full snapshot.

## 3. Decision B — bound bytes, entries and work, not just container length

Recommend explicit per-node budgets for in-flight proposal/result obligations,
per-peer outbound bytes/messages, retained log entries/bytes, archive backlog,
snapshot receive staging and per-owner-turn work. Reserve control capacity in
EACH shared resource that can otherwise stop kill, not only WAL blocks.

No guessed capacity is encoded by this document. Derive a reviewed budget table
from maximum canonical frame sizes and the maximum allowed backlog/owner gap:

- admitted response-bearing work <= reserved result slots + durable recoverable
  response obligations under the explicitly supported retry horizon;
- outbound bytes <= descriptor capacity times maximum owned frame bytes, plus
  bounded control/reply reserve;
- retained payload bytes include Raft's entry array AND the owner copy/container
  overhead, not merely sizeof(vector) or WAL staging;
- per-message entry/byte bounds must fit the receiver's admissible batch and
  owner-work budget; peer packet count alone does not bound a huge AppendEntries;
- archive outage budget must leave emergency headroom and refuse new ordinary
  work before disk or retained-history space is exhausted.

Reserve capacity before admission. Do not discover a full result queue inside
apply and either drop a committed result or partially apply. A follower must not
stop applying committed kill because an unused local result subscriber is slow.
Choose response ownership/correlation explicitly; changing all-replica results
to leader-only publication is not a free implementation detail across elections.

Alternative: cap deque.size() after push. Rejected: allocation already occurred
and the work may already be committed. Alternative: drop oldest risk/result
records. Rejected: loses obligations. Retransmittable peer messages may eventually
be regenerated, but only through a specified Raft retry/full policy, not arbitrary
dropping. Allocation-free steady state is the objective; replacing an unbounded
deque with a bounded deque does not itself prove preallocation or bounded time.

## 4. Decision C — separate recovery and audit reclamation boundaries

Recommend a verified immutable archive of the committed typed log prefix before
deleting its last historical copy. Bind cluster/lineage, covered index/term range,
content digest and required catalog/policy dependencies. Archive only committed
history as canonical history; a conflicting uncommitted suffix is not an audit
fact of acceptance. Retain refusal results/causal commands under the same policy.

Reclamation requires BOTH a durable verified recovery snapshot and archive
coverage, plus no live writer/log reference. A returned copy operation, another
live replica, or a current state digest is not archive acknowledgement. Interrupted
publication is retried/verified without erasing the source. Runtime archive
backlog is bounded; when unavailable, stop ordinary admission before reserve is
exhausted. Never silently switch to snapshot-only audit under pressure.

For initial development, a verified durable archive on the same host can establish
the deletion ordering. It does NOT establish host-loss audit durability. The final
NUC/Pi gate must separately declare and test independent-copy/backup requirements;
two live replicas that independently compact are not independent archives.

Alternative: never compact. Rejected as the final design because it moves an
unbounded RAM/disk problem instead of solving it. Alternative: archive only a
snapshot. Rejected because current balances/order state do not preserve all prior
caller actions. Retention duration/export destination are owner/operator choices,
not a default invented by this review.

## 5. Decision D — snapshot installation is a transaction, not a file copy

Recommend bounded, identity-bound snapshot transfer and crash-safe install, with
cluster/membership/semantic identity, covered index/term, exact length and digest,
catalog dependency verification and stale/conflicting-transfer refusal.
The shared snapshot identity must not contain the source voter's local node ID as
the target identity: a legitimate replacement needs its own local directory ID.

Receive into owned bounded staging; validate the whole object before replacing
live state. Keep old recoverable generation until new snapshot + compatible suffix
and metadata selection are durable. Preserve a matching suffix when safe; do not
discard acknowledged history or lower term/vote as a side effect of installation.
After install, normal bounded AppendEntries must converge to the leader's suffix.
Temporary files and interrupted/reordered/repeated chunks cannot publish a partial
snapshot or permit delivery. Required artifacts must be ready before participation.

Alternative: copy a peer's whole directory while running. Rejected: inter-file
generations, node identity and concurrent writes are not an atomic install.
Stopped verified manual restore remains a development procedure, not closure of
the peer-install guarantee. Exact envelope/chunk layout and crash publication
protocol are a separate byte review before implementation.

## 6. Decision E — retain atomic replacement provisionally; strengthen its proof

Recommend preserving the current temp-write, file-sync, rename, directory-sync
metadata format while testing the actual persistence ordering. A two-slot journal
is not automatically safer: it adds sequence/wrap/slot-selection recovery rules.
The original plan explicitly names two slots, so retaining atomic replacement
needs an explicit plan amendment supported by evidence, not quiet relabeling.

The next test model must distinguish volatile file bytes, durable file bytes,
volatile directory names and durable publication. Explore power loss at every
operation, including torn writes and failed sync/rename. The expected term/vote
and acknowledged prefix must come from an independent specification. Removing
ONLY a file sync or directory sync must fail the intended durability assertion,
not a checkpoint-count assertion. Then retain target storage/power-cut gates.

Until those tests and the amendment are accepted, F6/G-047 stay open. Do not
remove sync, treat process termination as power loss, or claim that checksums
make a successful write durable. Real device behavior remains outside the model.

## 7. Decision F — asynchronous completion cannot mean early success

The owner must not block on routine metadata I/O, but existing synchronous Raft
callbacks may only return success after their persistence obligation is satisfied.
Simply moving save() to a worker and returning zero would let votes/leadership/
protocol responses outrun hard-state durability. Waiting on a future from the
owner merely renames the original blocking path.

Recommend a bounded persistence owner with explicit request/completion identity
and a reviewed Raft/adapter durability fence. The detailed continuation/output
gating design remains a prerequisite: specify precisely when term/vote becomes
visible, which outputs/apply/proofs may proceed, how higher terms supersede work,
and how delayed completion is prevented from authorizing a different generation.
Do NOT assume a callback error is a resumable asynchronous yield in willemt.

Preserve fail-closed storage-failure latching now under the existing contract.
Stop producing protocol replies/results/proofs after ambiguous persistence errors;
keep diagnosis/recovery possible. Queue-full before mutation is backpressure, not
necessarily disk corruption; an error after partial mutation cannot be treated
as a harmless retry. Stop/fault observations must reach the later M3a health
pipeline without putting formatting or scrape work on the owner.

## 8. Parallel delivery order and approval boundary

1. F5: retain current-tree REDs for membership drift, missing catch-up and audit
   loss; approve A–D direction, then exact manifest/snapshot/archive bytes and a
   quantified budget/full-policy table. Start identity binding before transfer.
2. F6 concurrently: RED-first existing-contract failure-latch fixes; no new bytes.
   Review E's atomic-replace versus two-slot choice; build durability-sensitive
   fault evidence. Review F's concrete async fencing before changing callbacks.
3. Integrate: bounded request/result/control obligations and completion identities
   span both tracks. Main reviews once ownership is stable; no concurrent mutation
   of shared files/build roots. Isolated-source mutants only.
4. Full host tests, then frozen-candidate NUC/Pi gates at the roadmap stage. No
   new target acceptance, live venue or phase-complete claim from these host fixes.

Approval of this direction is NOT approval of unspecified byte layouts, guessed
capacities, archival retention periods or live performance bounds. Each must be
made concrete before encoding/configuring it. Current host implementation work
does not alter the two-voter fail-closed pilot or the mandatory observability gate.
