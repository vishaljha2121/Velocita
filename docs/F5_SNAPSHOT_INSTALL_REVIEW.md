# F5 transactional snapshot installation for an intact lagging voter

2026-09-23. **DIRECTION APPROVED after CP-029; exact new bytes/budgets pending.**
Branch `v2/phase-5-corrections`, baseline HEAD `d44a4a9`; all remediation is
uncommitted. This sheet closes no F5/F6/F7 gate. It changes no current byte,
economic, membership, repair or asynchronous-durability contract.

**CP-036 review supersedes conflicting proposals below:** read
`F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md` for the consolidated decisions, now
owner-approved at CP-037. This does not approve the old draft wire layouts.
The transfer layouts here are historical proposals, NOT ready to encode. Exact
transport bytes await an authenticated transport choice. No extra durable install
receipt is recommended; archive provenance remains separately mandatory. The
two draft correctness errors (ACK boundary and committed suffix results) are
corrected below. CP-036 itself claimed no executed RED; see CP-037 for later work.

CP-034 status clarification: CP-031's exact 160-byte selector and CP-033's
40-byte base seal/layout are subsequently APPROVED, and initial genesis publication
plus selected cold recovery are implemented. References below to a pending shared
selector/base are historical draft status, not a reason to reopen that approval.
Live installation, transfer wire bytes/budgets, coverage evidence and result/suffix
integration remain pending; do not infer their approval from the startup work.

CP-035 implements native-only read-only preparation components: exact-boundary
suffix classification and value-only result-obligation enumeration. They preserve
existing limits/ownership and grant no installation, deletion or serving authority.
The original lagger integration reproducer still fails. Review-driven corrections:
snapshot beyond a complete local tail is ordinary catch-up, and an installed
snapshot remains committed prefix evidence even when metadata commit trails it.
Evidence: `evidence/f5_install_preflight_2026-09-23/README.md`.

## 1. Decision requested

For the smallest safe MVP, use a **fresh destination-local generation** for an
intact voter that still owns valid term/vote metadata. Receive a bounded snapshot
from an authenticated current voter, validate the complete immutable object and
its dependencies, reconstruct result obligations, load a new Raft owner, append
the verified compatible local suffix, and only then quiesce the old owner. Recheck
all captured coordinates and atomically publish a durable selector. After that
selector is durable, the old generation can never serve again.

This is ordinary catch-up for a voter whose local hard state remains intact. It
is not CP-026's separately approved, operator-fenced replacement for lost voting
memory. A missing/corrupt term or vote refuses this protocol and enters the repair
runbook; a snapshot cannot synthesize forgotten election promises.

The requested approval has five material parts:

1. the proposed descriptor, chunk and acknowledgement fields in section 5;
2. the destination-local generation directory and atomic-replace selector in
   section 6;
3. retention of the existing live result ledger and archive-assisted replay rule
   in section 7;
4. the exact bounded budgets and fail-closed saturation policy in section 9; and
5. the authenticated-session and stale-completion rules in sections 4 and 8.

No implementation should infer approval from the instruction to parallelize.

## 2. Why the serving Raft owner cannot be the transaction

The pinned fork's `raft_begin_load_snapshot` is intentionally unsuitable as a
reversible live install primitive. After only two “snapshot is not older” checks,
it immediately:

- assigns `current_term = last_included_term`, which may regress a newer term;
- clears `voted_for`, becomes follower and clears the current leader;
- replaces the log with the snapshot coordinate;
- advances commit and applied to the snapshot index;
- removes every remote member, retaining only self; and
- offers no rollback-load operation.

It also rejects `last_included_index < current log end`. Therefore it cannot
preserve a lagger's compatible or unknown-commit suffix on the serving owner.
The existing startup path is safe only in its narrower context: it invokes the
call on a fresh server, restores membership, then restores durable hard state and
replays a contiguous WAL suffix. Online catch-up must preserve that separation.

Calling the routine first and copying state/result obligations afterward creates
an unrecoverable half-install. Holding an in-memory backup is not a transaction:
a crash loses it. The staged generation must be independently restartable before
the active selector can change.

## 3. Preconditions and immutable captured coordinates

Let `S` be the incoming snapshot index and `T[S]` its entry term. Preparation
captures the old active generation's tuple:

`C = (selector_generation, hard_term, voted_for, commit, applied, snapshot_index,
snapshot_term, durable_log_end, log_end, suffix_root, live_obligation_inventory)`.

All integer coordinates are positive where Raft requires that; generation and
session counters never wrap. Refuse at `UINT64_MAX` rather than reuse an identity.

The install candidate must satisfy all of the following before any staging file
is publishable:

- exact shared manifest digest and supported snapshot/state schemas;
- sender is a configured voter on an authenticated, route-bound session;
- `S > old snapshot_index` and `S > old applied`; redundant/equal snapshots may
  be acknowledged as already present but never start a new generation;
- snapshot state decodes canonically and its canonical digest equals the declared
  state digest; snapshot term is positive and agrees with verified history at S;
- sender declares a commit index at least S, but that declaration alone is not
  proof: activation requires this descriptor to come from the current
  authenticated leader in the validated current term, as specified in section 8;
- a retained local suffix above S may be spliced onto the incoming prefix only
  when the local entry at S has term T[S] and its available verified tuple agrees
  with the incoming/archive record. The suffix must then be contiguous. If the
  S match is absent or fails, preserve the old bytes as evidence but prepare the
  new generation without that suffix. A conflict at/below a locally known
  committed index refuses;
- local `commit > S` requires verified exact history through local commit and a
  state/result reconstruction path through that index. Never roll commit back;
- all immutable catalog/policy/identity dependencies needed to decode the state
  and replay covered commands are present and content-verified; and
- no external delivery, gateway proof issuance or writable admission occurs in
  the staged generation.

The sender's active log may advance during transfer. It need not freeze globally;
the immutable descriptor fixes one snapshot object, while the final revalidation
detects any local change that affects installation.

## 4. Three identities that must never be conflated

1. **Shared lineage** is the existing `VerifiedStaticIdentity` manifest digest.
   It identifies the cluster contract and is identical across voters. It is not
   a local directory generation and authenticates no network peer by itself.
2. **Local install generation** is a destination-owned, nonzero, monotonically
   increasing u64 naming `generations/<generation>/`. Its prepared base is
   immutable; its WAL/metadata become normally mutable after activation. It never
   appears as cluster identity and is not copied from the sender.
3. **Sender session ID** is a fresh 128-bit value bound by the transport
   authenticator to `(manifest digest, sender voter ID, recipient voter ID,
   connection instance)`. It scopes descriptor/chunk/ack replay only. It is not
   durable voting state and cannot select a local generation.

The transfer also has a stable 32-byte `snapshot_content_digest`, content-derived
only from shared lineage, snapshot index/term, canonical state digest, dependency
root and complete canonical snapshot bytes. It excludes sender/recipient/session,
sender term and sender commit. Retries and different authenticated publishers of
the same object therefore reuse it; a new session may resume only after
reauthentication and equality of all stable content fields. A sender session ID or digest is
never accepted as a signature. MVP requires a mutually authenticated bounded
transport whose peer identity is checked before these messages reach the
installer. The current bound-peer digest supplies integrity/lineage/routing, not
cryptographic peer authentication; without the authenticated transport, catch-up
refuses.

## 5. Proposed transfer protocol and bytes

These are new material wire formats and remain unapproved. All integers are
little-endian, offsets are wire offsets, reserved bytes must be zero, and every
complete message is carried inside the future authenticated transport record.
Do not add these as ordinary Raft log entries or overload `BoundFrameKind`.
The tables are review skeletons for field ownership and bounding, not ready-to-
freeze byte sheets: numeric status/reason assignments, digest-domain literals and
the final relationship to the authenticated transport envelope remain pending.

### 5.1 Descriptor (`V2SD`, schema 1, 256 bytes)

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `V2SD` |
| 4 | 2 | schema 1 |
| 6 | 2 | exact header bytes, 256 |
| 8 | 32 | shared manifest digest |
| 40 | 4 | sender voter ID |
| 44 | 4 | recipient voter ID |
| 48 | 16 | sender session ID |
| 64 | 8 | sender Raft term observed when descriptor was frozen |
| 72 | 8 | sender commit index, at least snapshot index |
| 80 | 8 | snapshot last-included index S |
| 88 | 8 | snapshot last-included term T[S] |
| 96 | 8 | canonical snapshot byte count |
| 104 | 4 | chunk size, exactly 16,384 for schema 1 |
| 108 | 4 | exact chunk count, `ceil(bytes/16,384)` |
| 112 | 32 | canonical state digest |
| 144 | 32 | stable snapshot content digest |
| 176 | 32 | required dependency-set root |
| 208 | 32 | portable archive coverage-evidence digest used for replay review |
| 240 | 8 | archive catalog generation named by that portable evidence |
| 248 | 4 | flags, zero for schema 1 |
| 252 | 4 | reserved, zero |

`snapshot_content_digest = SHA256("velocita.v2.snapshot-content.v1" ||
shared_manifest_digest || snapshot_index || snapshot_term || state_digest ||
dependency_set_root || exact canonical snapshot bytes)`. The domain has no
terminator. This stable identity deliberately excludes transient routing, session
and leader observations. It does not itself prove commitment, durability or
source authenticity.

The dependency-set root must name durable immutable verified objects. The archive
field names portable coverage evidence, not the archive owner's local capability.
The recipient independently validates that evidence and then asks its archive
owner to issue/revalidate a local capability. Exact portable evidence bytes remain
pending the archive-publication review; this descriptor does not approve them.

### 5.2 Chunk (`V2SC`, schema 1, 112-byte header plus payload)

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `V2SC` |
| 4 | 2 | schema 1 |
| 6 | 2 | header bytes, 112 |
| 8 | 32 | shared manifest digest |
| 40 | 16 | sender session ID |
| 56 | 32 | stable snapshot content digest |
| 88 | 4 | zero-based chunk ordinal |
| 92 | 4 | exact payload bytes; 1..16,384 |
| 96 | 8 | exact object byte offset |
| 104 | 4 | total frame bytes, header plus payload plus digest |
| 108 | 4 | reserved, zero |
| 112 | n | snapshot bytes |
| 112+n | 32 | SHA-256 over domain, manifest, session, object, ordinal,
payload length, offset and payload |

The chunk domain is `velocita.v2.snapshot-chunk.v1`, no terminator. The maximum
complete chunk frame is 16,528 bytes. Duplicate identical chunks are idempotent;
same ordinal with any different byte refuses and quarantines the staging attempt.
Out-of-order arrival is permitted into preallocated offsets with a fixed bitmap;
acknowledgement never implies full-object verification.

### 5.3 Status/ack (`V2SA`, schema 1, 200 bytes)

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `V2SA` |
| 4 | 2 | schema 1 |
| 6 | 2 | exact bytes, 200 |
| 8 | 32 | shared manifest digest |
| 40 | 4 | sender voter ID |
| 44 | 4 | recipient voter ID |
| 48 | 16 | sender session ID |
| 64 | 32 | stable snapshot content digest |
| 96 | 8 | destination local storage generation |
| 104 | 8 | receiver current term |
| 112 | 4 | status code |
| 116 | 4 | typed refusal reason, zero unless refused |
| 120 | 8 | contiguous received chunk prefix count |
| 128 | 8 | selected/recovered log end, zero before activation |
| 136 | 32 | selected generation-manifest digest, zero before prepared |
| 168 | 32 | domain-separated digest of bytes 0..167 |

Status is one of `descriptor_accepted`, `need_chunks`, `prepared`, `activated`,
or `refused`. Only `activated` means the selector is durable,
the selected generation recovered successfully, and final revalidation passed.
`prepared` is never permission for the sender to discard history. An ack from an
old sender session, old local generation or lower receiver term is stale and has
no effect.

The recommended 16-KiB payload fits below the existing 32-KiB peer frame ceiling
without claiming these transfer messages are ordinary Raft peer frames. It also
matches the earlier bounded-transfer candidate and limits cut-loss retry. A larger
64/128-KiB chunk would need separate transport and scratch justification.
Alternative in-order-only chunks remove the
bitmap but make every lost/reordered frame restart more work; the fixed bitmap is
small and improves bounded retry.

## 6. Durable local generation and selector transaction

Each generation has an immutable prepared **base** and, after activation, normal
mutable runtime storage. CP-033 supersedes this draft's earlier broader base:
`base/` contains exactly the destination-bound `v2-snapshot.bin`; the generation
holds the approved 40-byte `v2-base-seal.bin`. Identity remains in the root.
The seal binds generation, canonical local identity and the complete bound
snapshot. Captured term/vote, transfer descriptor and archive replay provenance
are preparation inputs, NOT additional files silently authorized in `base/`.
Any durable install receipt/coverage object needs its separately approved format
and location. Adding those files now would violate the implemented closed base
namespace, not complete it. See `F5_GENERATION_STORAGE_REVIEW.md`.

The candidate also contains initial mutable runtime files: local bound metadata
and, only after the matching-point test in section 3 succeeds, the preserved
compatible/unknown local WAL suffix beginning at S+1. They are validated as part
of preparation but are not immutable base objects. A divergent old suffix remains
preserved solely in the old generation/evidence; it is not attached to the new
prefix.

The preparation seal is checked while the candidate is still unselected and once
again immediately before selector publication. It proves that the base used to
construct/test the fresh Raft owner is the base being selected. It does **not**
freeze the live bound WAL or bound metadata file. Once selected and activated,
ordinary Raft operation appends/rewrites the WAL and persists term/vote/commit
through their existing approved framing, binding, durability and recovery checks.
Restart validates the immutable base against the selected generation and then
validates/replays the current mutable WAL/metadata using those existing contracts;
it does not compare mutable files to their preparation-time lengths or digests.

Directory ownership, the preparation seal and mutable-file binding are approved
and initial startup is implemented in CP-034. Live replacement still needs its
owner/quiesce/result integration. Do not copy/seal a whole new generation per append,
nor treat a changed valid WAL or hard-state record as base corruption.

Preparation writes a new directory, syncs every file, syncs that directory, then
publishes a sealed-generation marker and syncs its parent. It never mutates files
in the active generation. The fresh Raft owner is created from that directory:
load snapshot, restore configured membership, restore the captured current term
and vote, append the compatible suffix, set commit to `max(S, captured commit)`
only when all bytes through it were verified, and apply only that committed
range. Any `raft_begin_load_snapshot` failure destroys/quarantines the candidate,
not the serving owner.

Reuse the approved CP-031 160-byte `V2LG` selector and atomic-replace persistence
pattern; the earlier draft name `V2GS` is superseded, not a second format.
The selector contains shared manifest, local owner, generation, prepared-base
digest, previous generation, selection sequence, zero flags/reserved and its
domain-separated digest. Write a temp, sync it, rename over the selector, and sync
the selector directory. Startup reads only that named selector and the exactly
named generation; malformed/mismatched bytes refuse. It never scans for or falls
back to the newest valid generation directory.

Immediately before selector publication, quiesce owner input and drain/capture
outbound Raft packets without external delivery. Re-read C and require exact
equality for selector generation, hard term/vote, commit/applied, durable log end,
suffix root and exact live obligation inventory. A harmless log extension is not silently
folded in: abort and prepare again, because it may carry a new reservation or an
unknown-commit entry. Higher term or changed vote always aborts.

Every asynchronous storage completion consumed during preparation carries this
local storage generation and its operation sequence. A completion for the old or
an abandoned generation cannot publish metadata, release a pin, expose an output,
apply an entry or reopen service. This is the install-facing identity rule; the
exact asynchronous fence protocol remains the separate F6 review.

The durable selector is the point of no return. Before it, restart uses the old
generation. After it, restart uses or fail-closes on the new generation; it must
never resume the old owner. The process activates the already validated new owner
only after re-reading the selector. A failure after publication is recovery work,
not rollback authority.

## 7. READY and RESERVED result obligations

The state snapshot is not a result ledger. Current approved semantics require a
reservation before accepting each result-bearing entry, preserve READY FIFO
notifications independently of log compaction, and release a conflicting
uncommitted RESERVED slot only after durable suffix deletion. Installation must
not reset the 1,024-slot ledger or silently reorder/reissue results.

The smallest MVP retains the **same live `ResultLedger` owner** while replacing
only the Raft owner/state/storage generation. READY slots stay in their existing
FIFO and are neither copied nor reapplied. This avoids inventing a persistent
result format and does not change restart semantics: current startup committed
replay produces no notification, and the in-memory ledger does not promise FIFO
survival across process crash. On restart, existing caller retry/deduplication and
committed-replay behavior continue to govern.

The ledger is quiesced with the old Raft owner during final validation. Compare
the exact value-only inventory of occupied handles, bindings, results and READY
FIFO order; do not introduce a ledger-root format. This is not a global revision
counter or concurrent mutation detector. The owner excludes mutation during the
comparison and keeps the same ledger, including its free-slot generations.

For a RESERVED entry at index `i <= S`, first require exact membership in the
verified committed incoming history: index, term, entry type, identity, command
digest and payload bytes must match. Only then reconstruct its historical outcome
by verified **contiguous archive replay** from a verified predecessor state (or
genesis) through i, using matching catalog/policy dependencies. Replay invokes
the existing deterministic reducer with external delivery disabled. Preparation
retains the outcomes in bounded scratch; after selector publication and in-memory
state activation, it publishes each outcome exactly once into the matching
existing RESERVED handle. CP-036 recommends no separate durable install receipt:
selected history governs recovery and live FIFO does not survive a process crash.
Archive provenance remains mandatory and separate; no receipt belongs in the base.

A RESERVED handle whose local entry is divergent from the committed incoming
prefix represents a losing uncommitted command. Never replay it and never
manufacture a result for it. Release it only after selector publication makes the
new generation's durable history exclude that entry, using the same durable-
displacement ordering as ordinary Raft suffix overwrite. Missing history,
dependency, capacity or digest refuses before selection.

Replay must not replace the candidate final state. Independently compare the
replayed state digest at S with the incoming canonical snapshot state digest;
then discard replay scratch and install the decoded snapshot as state. This both
preserves historical results and detects a snapshot/history divergence.

For RESERVED `S < i <= C*`, where `C*=max(S,captured commit)`, capture the actual
outcome during staged committed-suffix apply and publish once after activation,
in apply order behind existing READY results. Ordinary startup replay suppresses
publication and is insufficient for this live obligation. Only retained `i>C*`
stays RESERVED for normal Raft commit or overwrite; release remains ordered
after durable suffix removal. When the
S match failed and the suffix was not spliced, those handles are released only
after durable selection/displacement. Any conflict at/below captured commit
refuses and preserves both generations.

Authority markers emit no result and require no slot, but remain mandatory in
archive replay because they are ordered audit evidence. Unread READY values do
not pin raw log cells; the existing ledger owns their values during live install.

## 8. Concurrent change, activation and protocol acknowledgement

Preparation is cold work and may overlap normal Raft traffic on the old owner.
It owns no authority. At quiesce, all of these must still hold:

- exact captured local tuple C;
- active selector still names the captured generation;
- authenticated sender session is live and still bound to the current leader;
- receiver current term equals the descriptor term, the authenticated sender is
  still the current leader in that term, and its commit evidence still covers S;
- staged suffix ends at the exact captured durable end and has no gap/fork;
- the live obligation inventory still equals the value captured before
  preparation; consumer takes that change it cause preparation to restart; and
- no outstanding borrowed log/payload/archive view names storage to be retired.

Any mismatch aborts without selector mutation. The receiver sends `prepared`
only after durable staging, and `activated` only after selector sync, selected-
generation recovery, state/suffix/result checks and owner activation. The sender
may acknowledge replication only through S and resumes ordinary AppendEntries
verification at S+1. A retained local end is diagnostic only, never matchIndex
authority: the retained entries may diverge from the leader's suffix. Neither
ack is archive deletion permission; working-history reclamation consumes the
separate generation-bound coverage capability.

On activation the node starts as follower, sends no gateway effect, discards all
old-generation outbound Raft packets, and requires fresh current-term consensus
traffic. A stale descriptor/chunk/ack cannot renew authority or mutate a selected
generation.

## 9. Explicit bounds and fail-closed saturation

Proposed MVP limits, not yet approved:

| Resource | Hard bound | Full behavior |
|---|---:|---|
| canonical snapshot bytes | existing maximum 65,483 | refuse descriptor |
| chunk payload | 16,384 | refuse frame |
| chunks/object | 4 for canonical schema-8 bytes (CP-036 proposal) | refuse descriptor |
| simultaneous inbound install attempts | 1 per voter | keep active owner; busy |
| staged unselected generations | 1 | refuse until operator removes quarantined evidence |
| result obligations | existing 1,024, including 64 protected | refuse; never drop READY |
| compatible suffix entries | existing retained-log 8,192 cells | refuse; preserve old owner |
| archive replay segment | approved 4 MiB / 8,192 records | bounded scan; refuse gap/full |
| replay records per install | finite pinned authorized range (CP-036 proposal supersedes total8,192 cap) | refuse missing/changed proof; no growing-tail chase or partial publication |
| install owner turns | max 16 records or 64 KiB per poll | yield; no owner-loop bulk scan |

Peak incremental resident ownership includes one existing active kernel; one
staged Raft/state/storage owner that reuses the existing live result ledger;
the staged owner's mailboxes and Raft library allocations; one canonical snapshot
buffer <=65,483; two chunk/transport buffers <=33,056 total; one approved archive
input object <=4 MiB if the current contiguous decoder is used; and fixed replay/
codec scratch. There are no old/new persisted ledger images. This is deliberately
**not** a whole-process byte claim:
the fresh Raft server/library heap and exact generation manifest must
be measured before approval. A future file-streaming archive scanner can remove
the 4-MiB resident input but cannot weaken two-pass immutability.

No elapsed-time guarantee follows from bounded work. Record bytes, chunk count,
replay entries, fsync calls, retries and quiesce duration receive counters. Each
owner turn is bounded as above; a wall-clock watchdog aborts preparation but never
publishes partial state. Repeated saturation stops catch-up and eventually
ordinary admission under the separately approved fail-closed archive policy; it
never evicts history or results.

## 10. RED, GREEN, cut, saturation and mutation exits

Preserve the original `tests/v2_snapshot_catchup_red.cpp` unchanged until the
real integration turns its final applied/digest assertions GREEN. It proves an
intact lagger currently remains at zero after healthy peers compact through 12;
it is not evidence for crash atomicity, result obligations or authentication.

Before registration as a passing target, require independent tests for:

**RED/GREEN behavior**

- original three-voter lagger catch-up with independently authored state oracle;
- READY + exact covered RESERVED + compatible unknown-commit suffix in one
  install, plus a divergent RESERVED entry that is displaced without a result;
- snapshot behind a compatible suffix and snapshot/history digest agreement;
- archive replay reproduces historical refused and applied results in FIFO order;
- missing archive/dependency/authentication and foreign lineage refuse unchanged;
- higher term, changed vote, appended suffix or consumed READY during preparation
  invalidates the candidate rather than overwriting the newer fact; and
- duplicate/reordered chunks and retried descriptor/ack are idempotent, while a
  same-ordinal different chunk quarantines the attempt.

**Crash/cut matrix**

Cut before/after every write, file sync, rename, directory sync, sealed marker,
inactive-selector publication, pointer publication, selector re-read, activation
and acknowledgement. Restart must select exactly old or new; after the selector
point it must never serve old. For process-crash cuts, assert existing restart
semantics rather than newly requiring READY FIFO persistence. For non-crash live
cuts, READY and RESERVED ownership must remain intact. Include failure to
construct/load the staged Raft owner and failure after selector publication.

**Saturation and bounded work**

Exact snapshot, chunk, result, suffix, archive-record and generation limits;
one-over each; protected/ordinary slot exhaustion; full archive input; stalled
consumer; repeated transfer retry; preallocated bitmap and allocation probes;
bounded records/bytes per poll; disk full/short write/EIO at every staged object
and selector path. Fullness leaves the active generation usable or fail-closed,
never partly switched.

**Direct single-condition mutations**

- load the serving owner instead of the staged owner;
- restore term/vote before `raft_begin_load_snapshot`;
- omit one membership restoration;
- accept local tuple change at final revalidation;
- resume old generation after selector publication failure;
- use sender session or shared manifest as local generation;
- skip object or chunk digest / accept foreign manifest;
- apply covered RESERVED command directly to final snapshot state;
- replace/reset the live ledger during the owner swap, or publish a reconstructed
  RESERVED outcome twice;
- release conflicting reservation before durable suffix removal;
- splice a suffix despite a mismatching S entry, or destroy the displaced old
  suffix/evidence merely because persisted commit trails it; and
- acknowledge activated before selector directory sync/recovery validation.

Each mutation must fail an intended assertion, with retained diff and raw log.
Sanitizers, nonrecovering UBSan, allocation probes and fresh inventory remain
required; host success makes no target/power-loss/latency claim.

## 11. Alternatives and non-goals

- **Destructive live load plus rollback:** rejected; the pinned fork has no
  rollback and mutates election/log/membership state immediately.
- **Drop local suffix:** rejected; entries above a stale persisted commit may
  have committed elsewhere, and compatible uncommitted entries still own results.
- **Apply every covered command to the final snapshot:** rejected; double-mutates
  state and cannot reproduce the command's historical result.
- **Persist native `ResultLedger`:** rejected; native layout and handles are not
  a stable byte contract.
- **Snapshot-only historical result schema:** deferred; verified archive replay
  reuses canonical commands and deterministic semantics at bounded extra cost.
- **One mutable `current` symlink/file:** rejected; ambiguous crash windows and
  newest-directory guessing are not durable selection.
- **Snapshot digest as authentication/commit proof:** rejected; use authenticated
  transport and current consensus evidence for source/commit, respectively.
- **Automatic lost-voter replacement:** out of scope; CP-026 selected a stopped,
  operator-fenced repair path with separate admission review.

## 12. Integration boundary

The archive-publication lane owns `ArchiveCatalogGeneration` (nonzero and
nonwrapping), distinct from this document's `LocalStorageGeneration`. Its
nonserializable, one-use coverage capability is revalidated by the archive owner
and contains: shared manifest digest, archive catalog generation, source local
storage generation, first/last index, term at first-1, last term, terminal record
digest, durable coverage-record digest, local-publication-durable and
independent-copy-durable. Archive-assisted replay requests immutable contiguous
decoded records through that capability and copies each payload before a borrowed
view expires. The installer cannot mint, widen or extend coverage.

The descriptor proposal carries a portable coverage-evidence digest and archive
catalog generation, not a serialized capability. Their exact portable evidence
contract remains owned by the archive-publication review. Preparation reacquires
and revalidates a local capability. Selecting a new local storage generation
invalidates capabilities tied to an older source generation unless the archive
owner explicitly reissues them from its durable catalog. Independent-copy evidence
also binds destination replica/host/archive identity plus exact object and
dependency digests; matching shared lineage alone is insufficient.

Snapshot install consumes coverage for replay; it does not reinterpret “file
exists” as coverage or authorize archive reclamation. The lost-voter repair lane may reuse
the immutable generation builder and selector machinery only after its separate
fencing/admission proof; it may not reuse intact-voter hard state assumptions.

Main integration owns replication callbacks, quiesce, result-ledger transfer,
selector activation and the final shared byte/budget review. No production or
CMake edit should begin until the owner approves the material choices and freezes
the still-pending exact selector and portable coverage-evidence formats. No new
persistent result-obligation representation is proposed by this MVP.
