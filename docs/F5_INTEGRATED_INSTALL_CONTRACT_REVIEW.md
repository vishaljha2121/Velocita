# CP-036: integrated snapshot installation — remaining decisions

CP-045 integration amendment (2026-09-24), **OWNER APPROVED at CP-046**: the subsequently approved
root-global archive catalog creates a two-publication boundary with the generation
selector. A snapshot+suffix candidate alone may not meet recovery's audit-prefix
rule. `F5_INSTALL_HISTORY_BRIDGE_REVIEW.md` approves retaining missing history in
the candidate WAL until post-selection archive publication, with separately
accounted disk/recovery work. This has separate explicit approval; CP-046 implements
the quarantined cold-recovery side, not online generation activation. The six
approved decisions below remain unchanged.

2026-09-23. **DIRECTION AND SIX DECISIONS APPROVED after the owner explanation.**
The owner replied “lgtm; approved”. This approves sections A–F, including the
bounded-work/finite-range choice, not unspecified transport/archive encodings,
archive quotas, receipt formats or deployment acceptance. No such bytes are
frozen by this approval. CP-037 records the subsequent implementation evidence.
Verified branch/HEAD: `v2/phase-5-corrections` / `d44a4a9`; remediation remains
uncommitted. Two Sol reviewers independently examined transfer and activation;
main reviewed their conclusions against the current implementation and drafts.
This supersedes conflicting proposals in `F5_SNAPSHOT_INSTALL_REVIEW.md`, not
approved CP-031 selector or CP-033 seal/layout contracts.

## 1. What problem remains

The current components can validate a proposed suffix and enumerate pending
responses, but cannot replace a running lagger safely. The integration must
preserve three different things: replicated state, voting promises, and caller
responses. A snapshot contains only the first. Loading it directly into the live
Raft owner destroys information before a replacement is known to be recoverable.

Recommended transaction:

`validate history → prepare isolated owner → quiesce/revalidate → durably select → activate`

Before selection, failure leaves the old owner authoritative. Once selector
rename may have occurred, uncertain publication failure must stop service and
cold-recover from the final selector; it must not resume the old owner. A durable
selection is one-way. This distinction matters at rename-before-directory-sync,
not only after successful publication returns.

## 2. Six approved decisions

### A. Integrate the native transaction; do not freeze a transport prematurely

Implement the internal installation boundary with explicit authenticated-leader
evidence supplied by the eventual transport owner. Host integration may use an
explicitly simulated authentication boundary, but that does not accept live
network catch-up. The existing bound peer digest proves integrity/lineage, not
peer authentication. No caller-controlled boolean should grant production
authentication authority.

The incoming object is canonical schema-8 state, at most 65,483 bytes. The source
voter's 65,587-byte local bound snapshot is not portable to another local owner;
the destination must construct its own bound file. With a proposed 16,384-byte
payload ceiling, four chunks suffice, not five.

Do not freeze draft V2SD/V2SC/V2SA layouts, status numbers, hash domains or sender
session width now. Correlate a native attempt with content, peer/session epoch
and non-reused local attempt identity. The concrete authenticated transport must
own its actual envelope and session guarantees before wire approval.

Remove the generic dependency-set-root proposal from this transfer descriptor:
policy bodies already live in snapshot state, and the execution catalog root is
bound by static identity/state. The actual verified catalog remains mandatory.
Likewise, do not embed unfinished archive catalog/coverage formats in the transfer
descriptor. Verified archive provenance is a separate mandatory input wherever
historical result reconstruction requires it, not an optional hash field.

Cost: real network catch-up remains an explicit deployment gate. Benefit: avoid
freezing a second unauthenticated protocol that must be redesigned later.

### B. Preserve the same ledger; partition pending results correctly

Capture exact value-only obligations and compare the complete inventory again
under final owner quiescence. Keep the SAME live ledger/handles/READY FIFO; do not
invent a ledger digest format, reset it, or transfer results to a fresh ledger.
Let `S` be the incoming snapshot index and `C* = max(S, captured local commit)`.

| Existing obligation | Candidate work | After durable selection/activation |
|---|---|---|
| READY | Preserve value and FIFO position | No replay or second publication |
| RESERVED, matching `i <= S` | Verified historical replay from a predecessor through S; capture its actual outcome | Publish once to exact original handle |
| RESERVED, matching `S < i <= C*` | Apply verified retained suffix from snapshot state; capture its actual outcome | Publish once to exact original handle |
| RESERVED, retained `i > C*` | Preserve exact binding and payload | Remains RESERVED for normal Raft processing |
| RESERVED, divergent and locally uncommitted | Do not invent an outcome | Release only after durable selection excludes its entry |
| Divergence at/below locally known commit | Refuse installation | Preserve evidence; no release |

Captured outcomes are bounded by the existing 1,024 obligations, retain full
identity/digest/term/index/handle binding, and append in increasing apply order
behind existing READY results. Verify replay state against the snapshot at S
BEFORE applying the suffix through C*. Require verified durable bytes through C*;
an undurable tail requires flush/fence/reprepare, not lowering commitment.

**Draft defect:** the former blanket rule that every `i > S` remains RESERVED
strands already-committed suffix responses: startup replay advances applied but
suppresses result publication (`restoring_` in `src/v2_replication.cpp`). The
installer needs bounded outcome capture, not ordinary cold recovery alone.
This is a reviewed integration counterexample, not a newly executed RED test.

CP-037 reachability correction: successful public `propose`, `receive_peer` and
`tick` calls currently apply through commit before returning. Combined with the
install precondition `S > old applied`, this does NOT establish a healthy live
capture with `S < old commit`. The cold-recovery witness below demonstrates the
restoring behavior on a valid disk image, not that live interleaving. Keep the
cohort rule as a defensive contract if asynchronous/bounded apply later exposes
such a boundary; do not manufacture a live bug or add a bypass to exercise it.

No separate persistent install receipt is needed for MVP. The approved selector,
sealed base and bound mutable history determine recovery; archive provenance
remains the archive owner's responsibility. The live ledger already makes no
promise of FIFO survival across process crash. Adding a receipt would introduce
a second recovery authority without fulfilling an existing guarantee.

### C. Acknowledge only the installed snapshot boundary

An activated acknowledgement proves replication only through S. Resume ordinary
AppendEntries verification from S+1; never set sender matchIndex to a returned
local suffix end merely because the receiver retained that suffix.

**Draft defect:** receiver installs S=12 and retains entries 13..20. Its entries
15..20 can differ from the leader's. A snapshot acknowledgement of local end20
would let the leader count unverified entries toward quorum. Local suffix end
may be diagnostic, not replication authority. This follows the existing
verified-prefix rule; it is not a relaxation of suffix retention.

Leader evidence must come from an authenticated configured voter acting as the
current leader in the validated term, observed after this attempt starts, with
committed index >= S and revalidated at quiesce. A snapshot descriptor/hash alone
is insufficient. Do not require a successful local AppendEntries prefix match
to bootstrap catch-up: missing that prefix is precisely why a snapshot is needed.
This assumes honest crash-fault Raft peers; it is not a Byzantine certificate or
a quorum-fresh read/send authorization.

### D. Use the existing asynchronous durability direction

One serial disk owner and bounded requests/completions; bind each operation to
destination generation, local attempt epoch and nonwrapping operation sequence.
Final quiesce pauses protocol mutation, admission, result takes and delivery/proof
issuance while health/completion handling remains responsive. Drain/fence actual
old-generation writes before selection; ignoring a stale callback cannot undo a
write that already targeted a file. No owner-thread fsync or stop/join shortcut.

Revalidate the exact local tuple, complete ledger inventory and leader evidence
before selection. A change aborts/reprepares rather than silently absorbing newer
work. Partial live publication failure after selection stops/cold-recovers; there
is no fallback to the old owner. This accepts an explicit availability pause in
exchange for a single authoritative history. It adds no per-command snapshot copy.

### E. Bound resident work, not historical distance by log capacity

Keep one active attempt, one staged generation, the existing 8,192-entry retained
suffix limit, existing 1,024-result limit and approved 4 MiB/8,192-record archive
segment limit. Propose at most 16 records or 64 KiB of replay work per owner turn.
Whole-segment decode/hash passes run off-owner; the existing decoder is not made
incremental merely by placing a small bound on reducer calls.

Replace the draft **8,192 total replay records per install** with a finite pinned
archive interval/catalog membership fixed before replay. Stream one approved
segment at a time, reusing input/state scratch; retain only the bounded matching
outcomes. Never chase a growing tail or accept newly discovered objects into the
attempt. Check counters for overflow and refuse missing/changed evidence.

Reason: retained-log capacity bounds memory, not the distance from a verified
historical predecessor. A total 8,192 rule could permanently block an otherwise
valid far-behind voter. Cost: total disk I/O and completion time are not bounded
by that number. Cancellation/watchdog leaves old state authoritative before
selection; repeated timeouts can still prevent progress. Durable archive quota
and catalog bounds remain a separate approval, not an invented unlimited archive.

Do not claim a whole-process memory bound: canonical bytes + two draft transport
buffers + one 4-MiB segment alone total 4,292,843 bytes, EXCLUDING the staged
16-MiB payload slab, Raft/log allocations, inventory, outcomes, mailboxes and codec
scratch. Measure the integrated peak before target acceptance. The reused ledger
is not an additional allocation.

### F. Keep archive publication and reclamation authority separate

Installation that preserves the old generation does not itself delete audit
history. It need not wait for an unrelated Pi replica solely to select a new
generation. But any replay must use authorized committed provenance, and live
reclamation still requires the approved independent durable copy plus coverage,
resolved pins and zero borrowed views. This does not waive the Pi gate or permit
cleanup of old generations. Exact durable catalog/coverage bytes, quotas and
destination receipts remain unapproved in `F5_ARCHIVE_PUBLICATION_REVIEW.md`.

## 3. Implementation and acceptance sequence after review

1. Turn both draft counterexamples above into permanent intended REDs, including
   mixed READY/covered RESERVED/committed suffix/uncommitted suffix obligations.
   Preserve the original lagger RED; no invented cold-start response promise.
2. Settle the concrete archive provenance/publication bytes and bounds before
   encoding them. Integrate the archive owner and asynchronous durability fence
   with the staged-owner transaction, not disconnected acceptance substitutes.
3. Make the real native end-to-end lagger trace GREEN using the same live ledger,
   exact suffix and final generation selection. Simulated authentication must be
   labelled and cannot enter a production serving API as a bypass.
4. Exercise every write/sync/rename/activation cut, changes at final validation,
   stale completions that attempt real writes, output failure, saturation and
   replay cancellation. Mutate each decisive guard and check which assertion
   kills it; run rebuilt focused/full regressions.
5. Integrate the chosen authenticated deployment transport and run target gates
   with externally held source pin and complete metadata. F5/F6/F7 remain open
   until their actual acceptance exits are satisfied; Pi is deferred, not waived.

## 4. Original CP-036 evidence status (historical)

Documentation-only review; no new build, RED, mutation or runtime measurements
were run. CP-035 remains the last verified 74/74 Release and C++ sanitizer
checkpoint with its stated limitations, not a fresh result for this review.
The original lagger/archive-loss/owner-fsync integration failures remain open.
