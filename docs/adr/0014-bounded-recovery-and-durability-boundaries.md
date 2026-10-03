# ADR 0014: Bounded recovery and durable protocol boundaries

- Date: 2026-09-22
- Status: **DIRECTION ACCEPTED; startup/history/result/capacity/archive/generation contracts, CP-036 install semantics, CP-045/046 history bridge and CP-048 fresh destination verification accepted. Native async persistence/recovery/backup verification and CP-049 writer handoff implemented; online installer, authenticated deployment integration and target acceptance incomplete. E1/E2/E3 remain OPEN.**
- Scope: roadmap F5/F6; audit A04/A07/A08/A09/A14
- Governing direction: ADR 0001; amendments to ADR 0005/0006/0007
- Detailed reasoning and alternatives: `../F5_F6_RECOVERY_DURABILITY_REVIEW.md`
- Approved startup byte contract: `../F5_STATIC_IDENTITY_BYTE_REVIEW.md`
- Approved history/peer semantics: `../F5_HISTORY_PEER_IDENTITY_REVIEW.md`
- Approved exact framing/initialization: `../F5_BOUND_FRAME_BYTE_REVIEW.md`
- Parallel delivery and MVP deferrals: `../F5_F7_MVP_CLOSURE_PLAN.md`
- Approved outbound/log review: `../F5_OUTBOUND_LOG_CAPACITY_REVIEW.md`
- CP-026 completion/NUC plan (A/B approved; bytes pending): `../F5_COMPLETION_AND_NUC_PLAN.md`
- CP-028 portable archive codec bytes (owner-approved; standalone component): `../F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md`
- CP-029 coordinated closure review (directions subsequently approved; details pending): `../F5_F6_FOUR_LANE_CLOSURE_REVIEW.md`
- CP-031 exact selector bytes (APPROVED; pure codec implemented): `../F5_LOCAL_GENERATION_BYTE_REVIEW.md`
- CP-033 base/layout/startup integration (APPROVED; CP-034 initial publication/cold recovery implemented, live install pending): `../F5_GENERATION_STORAGE_REVIEW.md`

CP-051 scope amendment (2026-09-24, owner decision): the MVP pilot keeps one
voter per host (two voters). With two voters every commit needs both durable
copies and compaction never passes commit, so the online installer guards a
case the pilot cannot reach; it is deferred to the three-voter release with its
CP-036/045 approvals intact. Pilot E1 is native log catch-up, now pinned by
`v2-two-voter-catchup-tests` (mutation-covered), plus the offline lost-voter
repair before live canary. The installer-time old-job drain (E3) moves with the
installer. Evidence: `../evidence/e1_two_voter_2026-09-24/README.md`.

CP-050 handoff clarification (2026-09-24, documentation only): current work is the
integrated E1/E3 installer, not another contract-design phase. CP-036/045 choices
are approved; implementations must preserve their full result, history, lifetime
and uncertainty rules. CP-048 native independent-destination verification exists;
its missing authenticated process/physical-host proof is not missing local codec
work. Read the top CLAUDE HANDOVER in `../CURRENT_HANDOFF.md` for exact next RED,
ownership hazards, build/evidence limits and protected uncommitted work. No new
decision, acceptance promotion or runtime validation is introduced here.

CP-049 implementation amendment: the existing serial writer has a bounded native
target-handoff lane implementing part of the approved CP-036/045 ownership
direction. It finishes old writes, waits for completion dequeue, validates the
expected generation, and rebinds targets without an owner-thread join/I/O. A
post-acquire submission recheck prevents an empty-pop/publication race. The
install owner must still retain targets and apply dequeued completions before
retirement. This grants neither selector nor serving authority; the stable
facade/nonserving candidate and actual online installation remain absent. No new
bytes, guarantees or scope were approved by this amendment. Evidence/limitations:
`../evidence/f5_writer_handoff_2026-09-24/README.md`.

CP-035 adds only native read-only preparation checks under this direction. A
suffix classifier requires exact boundary tuple/payload evidence, not term-only
guessing, before returning the complete suffix including unknown-commit records.
Full live tail must be durable before this complete-view check: flush/retry may
cost availability, while dropping volatile entries could lose obligations. An
installed snapshot is committed prefix evidence independently of an older commit
checkpoint; its term floor therefore cannot depend on checkpoint freshness.

ResultLedger inspection copies original handles/bindings and full READY values
without mutating slots or creating a second ledger. READY order follows actual
publication FIFO; RESERVED slot enumeration is not apply order. Native structure
is validated before any callback. This adds no persisted result schema, root,
epoch or pool capacity. It is not a concurrent-change detector or authority to
publish/release results; those require quiesce and the final install proof.
Borrowed log ranges must not outlive immutable caller ownership. No helper
invokes Raft, transfers snapshots or activates a generation. The real lagger RED
still fails; component gates cannot close G-040/F5. Evidence and limitations:
`../evidence/f5_install_preflight_2026-09-23/README.md`.

CP-034 integrates fresh genesis publication and selected cold recovery. Retain
root/generations/runtime ownership together and route existing stores through the
selected path; no raw constructor or root-level second metadata authority. Seal
dependencies before publishing the selector, then sync the selector directory
before returning a token. Rename visibility alone is not durable activation.
Initial partial roots stay stopped rather than guessing whether creation finished;
automatic migration/cleanup is deliberately not part of MVP startup.

Final selector remains authoritative even with an unfinished temp. Inspect the
temp for diagnostic status and preserve it; malformed/foreign temp never selects
a generation, while actual inspection I/O failure stops. Blocking on every partial
temp would contradict the approved pre-rename recovery of a still-valid final.
This native diagnostic adds no bytes and does not certify candidate completeness.
Exact selection revalidation also refuses stale tokens after a valid successor,
not merely after file deletion. Snapshot-base floors prevent regression below
installed state without inventing a runtime repair transaction.

Ownership is not serving authority: full mutable history/commit validation stays
in ReplicatedKernel before writable WAL open. Cold recovery can re-sync existing
files before this semantic admission; it does not rewrite payloads. Keeping the
decoder at one boundary avoids competing notions of recoverable history. The
genesis persistence model and four guard mutants are bounded evidence, not live
snapshot install, hardware power loss, archive coverage or F5/F6/F7 closure.
Evidence: `../evidence/f5_generation_startup_2026-09-23/README.md`.

Historically, CP-033 records subsequent owner approval of the exact 40-byte base seal and
minimal startup/layout contract. The codec binds canonical local identity,
generation and full immutable snapshot, validating state/catalog semantics before
returning a summary. Read-only on-disk inspection derives the exact generation
from the final selector and checks its immutable namespace; it does not validate
mutable runtime history, retain locks or grant activation authority. These are
explicitly the next integrations. Automatic legacy migration is deferred, not a
remaining permission blocker for fresh bootstrap. No new generic manifest system.
Evidence: `../evidence/f5_generation_base_2026-09-23/README.md`.

Historically, CP-032 added a transitional old-layout startup guard under existing no-fallback
semantics. A selector, temporary selector or generations entry makes recovery and
ownership-token revalidation refuse before resync; bootstrap retains its generic
unknown-content refusal. Guessing that root-level files remain authoritative is
unsafe before generation-aware startup exists. Three bounded cold-start lookups,
no order-path change. Trusted ownership remains an assumption. The 40-byte seal,
layout and migration proposal subsequently received CP-033 approval; this guard
does not implement it or close F5/F6/F7. RED/GREEN/mutation evidence:
`../evidence/f5_generation_guard_2026-09-23/README.md`.

CP-031 implements the owner-approved160-byte selector codec and pure value
transition guards only. Failure-atomic fixed-byte construction and independent
goldens avoid ABI-dependent serialization. Selection sequence advances exactly
once, while abandoned preparation may skip generation numbers; exhausted counters
remain recoverable but cannot advance. The opaque base digest is not prepared-
base verification, publication durability or startup authority. Those integrations
and their remaining contracts are open. Component67/67 host regressions do not
close F5/F6/F7. Evidence: `../evidence/f5_local_generation_2026-09-23/README.md`.

Owner approved the four CP-029 directions before CP-030. Exact selector/base,
coverage, repair, async continuation and capacity details remain separate reviews.
CP-030 implements only read-only file verification of existing CP-028 archive
objects. It publishes no durable coverage or deletion capability; a verified
opened inode does not establish later pathname binding. Component tests and
guard mutants pass, but original integration REDs remain open. Evidence:
`../evidence/f5_archive_file_2026-09-23/README.md`.

CP-029 adds shared generation/publication/install/repair/async proposals and
permanent current-source diagnostics, not acceptance. Actual metadata fsyncs
still execute on the owner with the WAL worker running. Two byte-identical donor
images can represent different old quorum outcomes, so repair cannot infer suffix
commitment/deletion from donor commit. A full retained log/result ledger can also
block a recovery barrier. Exact control headroom and already-full recovery remain
open decisions. No new persistent result FIFO or automatic fallback selector is
approved. Evidence: `../evidence/f5_f6_parallel_contracts_2026-09-23/README.md`.

CP-028 implements the subsequently approved portable archive component: 96-byte
segment header, 32-byte record header, separate record/segment SHA-256 digests,
4-MiB complete-object and 8,192-record caps. Shared manifest binding excludes
local publisher ID. Strict canonical payload validation preserves refused commands
and failed authority requests; it does not re-adjudicate them. The two-pass codec
uses immutable borrowed input, fixed scratch and a pull source rather than a
per-record descriptor array. Contiguous input/output storage is caller-owned;
file streaming/publication and compaction authorization are not implemented here.
No existing economic, WAL, peer or snapshot byte contract changes. Incremental
SHA preserves the existing one-shot digest bytes. Evidence and limits:
`../evidence/f5_archive_codec_2026-09-23/README.md`.

Owner approved all four CP-024 decisions before CP-025. Mailbox and retained-cell
ownership, local/follower/startup bounds and streaming recovery/rewrites are now
host GREEN. Keep the existing bounded-by-admission Raft ring growth, explicitly
budgeting its transient peak; no new allocator fork for MVP. Packet coalescing
does not apply to trading effects; quota exhaustion never authorizes history
eviction.59/59 Release and C++ sanitizer suites do not close archive/install or
target obligations. Exact evidence, prototype corrections and RED provenance:
`../evidence/f5_capacity_integration_2026-09-23/README.md`.

Historical CP-024 was a REVIEW, not an amendment accepting budgets. Original diagnostic REDs
demonstrate outbound retry/reply growth and retained history growth independent
of results. Proposed same-kind packet coalescing never applies to trading effects;
fixed log ownership never authorizes last-copy deletion. The review supersedes
earlier unapproved outbound/log candidates, explains actual backing and library
resize RAM, requires bounded recovery/rewrite scratch, and recommends deferring
a new Raft allocation API for MVP. Approval was subsequently obtained for CP-025
as recorded above; this paragraph describes the earlier review-only checkpoint.

CP-026 adds current-source catch-up and audit-loss REDs and a dependency-ordered
completion plan. The owner subsequently approved offline operator-fenced repair
and immutable/no-auto-prune archives with fail-closed fullness and independent
live audit copies. Exact admission, storage/transfer bytes and quotas are still
pending; this semantic approval is not permission to infer those contracts.
Snapshot installation must preserve existing result obligations as well as state;
durable active-generation selection is a point of no return, not a reversible
in-memory swap. Exact bytes/budgets and the asynchronous fence remain open.

CP-016: the CP-015 identity owner is now the kernel's ONLY construction boundary.
Raw/default arguments are removed; the first member retains directory ownership
through all store/writer destruction. ID, voters, catalog and authority domain
derive from that owner. A04 startup drift is host GREEN with typed refusal,
unchanged storage and healthy original-state recovery. Success-path recovery
re-establishes identity publication durability after validation; visibility alone
is not proof of sync. No new bytes beyond the approved identity frame. Evidence:
`../evidence/f5_kernel_startup_2026-09-22/README.md`. Path-based storage still
requires trusted stable paths after pre-open inode revalidation. Foreign-history
binding and full F5/F6/target acceptance remain open.

CP-017 was review-only; the owner subsequently approved local-voter-bound storage
wrappers, portable
shared-lineage content, all-peer-message admission and one active block-WAL format
for fresh bound deployments, including record-level WAL binding and typed mismatch
refusal. Exact bytes are not frozen and implementation is pending. Static identity
cannot establish safe voting after lost hard state or detect complete same-node
rollback; replacement admission must not be confused with genesis bootstrap.

CP-018 adds existing-contract worker-error capture and owner-boundary refusal,
including a main-discovered direct-poll retry bypass with original RED/GREEN.
Five history/peer/missing-hard-state diagnostics now reproduce the open failures.
Exact bound-frame bytes and metadata-first/identity-last genesis publication are
PROPOSED, not implemented or approved by the earlier semantic approval. Host
regression and focused concurrency evidence do not close F5/F6/F7 target gates.

Owner approval after CP-018 freezes that byte sheet, including metadata-FIRST /
identity-LAST bootstrap ordering. CP-019 implements common framing and bounded
ordinary-peer admission only. The public raw-message receive overload is removed;
all requests/replies now pass lineage/integrity/route/typed-payload checks before
Raft. AppendEntries sends at most sixteen entries and the pinned Raft reply path
advances through remaining suffix batches. Local hard-state/WAL/snapshot runtime
binding and genesis crash windows are STILL pending, despite their approved bytes.
Generic framing success is not typed storage validation. Evidence and limits:
`../evidence/f5_bound_peer_2026-09-22/README.md`.

## Problem

CP-020 implements approved typed metadata, block-v2 WAL and local snapshot binding
plus metadata-first/identity-last genesis. Foreign/unsupported history refuses
before writable recovery. Missing hard state is never defaulted; partial metadata
temps require diagnosis, complete matching temps are validated without promotion.
All retained records validate even under snapshot coverage. Full corrupt blocks
refuse; only complete valid prefix before a physical short tail can be recovered,
subject to committed coverage. Metadata term cannot trail WAL/snapshot term.
A snapshot may legitimately exceed the older metadata COMMIT checkpoint because
snapshot durability establishes that committed prefix; term and commit are not
interchangeable freshness signals. No byte/economic decision beyond the approved
sheet. Current host evidence: `../evidence/f5_local_history_2026-09-22/README.md`.
The old syscall metadata model covers the previous generic implementation only.
CP-027 adds a separate actual BoundMetadataStore model with independent authored
tuples, real load cross-check, cut/failure sweeps and two meaningful sync-only
mutants. This addresses the port gap, not device power loss, asynchronous fences
or the final plan amendment. Evidence: `../evidence/f6_bound_metadata_model_2026-09-23/README.md`.

Local snapshot recovery is not peer replacement. WAL staging bounds are not
whole-node bounds. Current state is not audit history. A synchronous persistence
callback cannot safely become asynchronous merely by returning earlier. These
distinctions underlie the remaining F5/F6 findings and remain MVP requirements.

## Accepted direction

1. Explicit bootstrap/recover and immutable shared cluster manifest; separate
   local node binding. Derive F4 authority domain from shared canonical identity.
   Refuse drift and unbound nonempty legacy directories; preserve their files.
2. Pre-admission byte/entry/work budgets with control reserves and explicit full
   policies; no dropped economic/result obligations or capacity checks after apply.
3. Separate recovery and audit-reclamation boundaries: durable verified snapshot
   AND verified archive coverage before last-copy history deletion.
4. Identity-bound, bounded snapshot transfer and transactional installation,
   followed by compatible suffix convergence without term/vote regression.
5. Provisionally retain atomic metadata replacement while adding sync-sensitive
   power-loss modeling; the owner approved this provisional choice, with evidence
   still required before amending the plan's literal two-slot prescription. No
   fsync weakening or power-cut claim by `_exit`.
6. Explicit asynchronous persistence fences and bounded completions, with detailed
   Raft integration review before implementation. Existing ambiguous failure paths
   may be fixed now by stopping service; that changes no durable framing.

## Costs and rejected shortcuts

Identity binding requires reviewed storage and peer envelopes/fresh pre-live
lineage. Bounded capacity and unavailable archives reduce availability. Snapshot
install and async hard-state need crash/ordering tests, not only happy-path tests.
These costs preserve the original guarantees; they are not optional optimization.

Rejected: infer membership from startup arguments; regard replicas as an archive;
drop committed results to free space; copy a running peer's directory; return
from persistence callbacks before their durability fence; count process exits as
proof of power-loss safety. Detailed alternatives are adjacent to each decision
in the review document.

## CP-036 install refinement (owner-approved at CP-037)

See `../F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md`. The draft must not acknowledge
an unverified retained local suffix as leader replication, nor leave committed
suffix caller results RESERVED after staged replay advances applied. The review
records both counterexamples, exact result cohorts, same-ledger ownership and
one-way selection. It proposes no extra persistent install receipt and a finite
pinned historical replay range instead of a total8192-record cutoff. Transport
wire, durable archive provenance and quotas remain separate approval boundaries.
Owner approved all six direction/semantic decisions after the plain-language
and C++ explanation. This does not approve unspecified archive/transport bytes
or quotas. Existing approved selector/base contracts stand unchanged.

## CP-038 local archive catalog (owner approved 2026-09-23)

`../F5_ARCHIVE_CATALOG_BYTE_REVIEW.md` proposes the concrete bounded local
adjudication record: a flat genesis-prefix catalog atomically replaces its prior
index metadata, while segment/dependency objects remain immutable. This avoids a
catalog chain, pages and another selector. Source-storage generation is per row
so later local replacement cannot rewrite old provenance; live native capabilities
bind current generations separately. Early publication cuts must reaccount the
remaining byte/object/backlog reservations before consuming them.

Owner explicitly approved all five points: exact96/112-byte layout, namespace,
quotas and catalog-metadata replacement. Local coverage does not assert independent backup; live deletion
still requires authenticated destination-durable evidence. E2 must be demonstrated
through actual compaction and restart with E3 fencing; a new codec is not closure.

## CP-041 composed persistence evidence (2026-09-23)

Historical CP-042 review note: `../F6_ASYNC_DURABILITY_FENCE_REVIEW.md` §8 proposed the
exact native async continuation after call-site inspection. At CP-042 it was
not yet accepted or implemented. It preserves the approved one-fence availability policy and
adds a proposed library-selected whole-cut preparation hook, one bounded owned
retry packet, and typed completion identity on the existing serial worker.
Cold snapshot clear is explicitly not a resumable installer. This avoids
duplicating consensus rules or expanding E3 into a general scheduler. No wire,
economic or durable-byte amendment was made by that review.

CP-043: the owner subsequently approved all five choices. Worker-mode hard state
and whole-suffix deletion now implement this continuation. Raft's optional
prepare hook chooses the cut; staged WAL drains before one worker rewrite;
exact completion and replay consume a one-shot target grant before memory/result
release. An unconsumed grant fails closed. Runtime clear is forbidden rather
than falsely advertised as resumable; actual E1 activation remains open. Existing
READY results stay readable during a fence, but new apply/proofs/output do not.
Native owned retry32KiB and heap job168 bytes do not amend durable encodings or
TradingState. Allocation per job is bounded, not an allocation-free owner claim.
See `../evidence/f6_async_persistence_2026-09-23/README.md` for tests, mutations,
fixture corrections and remaining composed-crash/target obligations. No phase
acceptance or F5/F6/F7 closure follows from the host implementation.

CP-044 (2026-09-24) adds composed current-generation metadata/WAL durable-image
coverage without changing this decision.75 reached cuts across three namespace
schedules, zero-event recovery and real promise baselines compare exact authored
tuples/payloads before startup can re-sync. Vote-only coverage prevents cross-file
directory sync from hiding omitted metadata durability. Negative controls remove
metadata directory sync or WAL replacement-file sync and fail intended promise
checks. See `../evidence/f6_async_crash_model_2026-09-24/README.md`.
Actual E1 activation and stale writes across generations still require integrated
tests; these host schedules are not target or physical power-loss acceptance.

The actual laboratory compaction transaction now has a bounded durable-image
sweep across archive publication, snapshot publication and WAL replacement:
243 reached cuts across first/replacement compaction and three name-persistence
schedules. This supersedes CP-040's archive-only model gap, not target gates or
independent-copy requirements. Production behavior and contracts are unchanged.

A successful final image must recover the exact new snapshot, full archived
history and retained durable uncommitted suffix. Every intermediate image is
checked before recovery: either the complete original WAL survives or, when its
prefix is absent, matching snapshot and archive prerequisites are already durable.
Refusal cannot excuse violation of that ordering. Accepted recovery must also
match every original tuple/payload; recovery refusal is not a catch-all for
failed post-admission assertions.

The selective schedule allows the WAL rename to persist without an unsynced
snapshot rename in their shared directory. Omitting snapshot directory sync
passes simpler lazy/eager schedules but fails this one. Omitting WAL file sync
and dropping its first retained suffix record also fail intended assertions.
This demonstrates why individually correct component tests and a successful
end-state restart did not by themselves establish transaction ordering.

This model assumes known durable initial state, atomic rename and successful
sync semantics; it is not all filesystem writeback schedules or physical-device
power-loss evidence. Details and development-oracle corrections:
`../evidence/f5_composed_persistence_2026-09-23/README.md`.
Next is actual E3 hard-state/truncation adaptation, then E1 installation;
E2 independent-copy/target obligations and broad F5/F6/F7 remain OPEN.

## CP-040 persistence evidence boundary (2026-09-23, historical)

The CP-039 implementation now has actual archive syscall injection and a
sync-sensitive archive durable-image model, plus nine integrated snapshot/WAL
I/O failures. This adds evidence under the approved contract, not new bytes or
reclamation authority. Accepted recovery checks full historical tuples/payloads;
ambiguous images may stop without rewriting evidence. Availability is deliberately
weaker than safety: an orphan is not silently adopted just because it decodes.

File contents and directory names are separate durability obligations. A model
that always keeps host-visible writes after process death cannot establish this
ordering; the bounded model reconstructs only its persisted graph. Removing
either file-sync or directory-sync classes fails the successful-publication
restart assertion. This is not a proof over all filesystems or all individual
sync sites. The composed archive/snapshot/WAL crash-image sweep remains open;
process-visible EIO tests cannot substitute for it.

No production defect or production behavior change was established in CP-040.
Review corrected harness reachability/retry/oracle errors rather than modifying
Raft based on a scheduler-dependent timeout. Exact scope, commands, failed
test-development claims and final results:
`../evidence/f5_archive_faults_2026-09-23/README.md`.
E1/E2/E3 and F5/F6/F7 remain OPEN; target gates and independent-copy requirements
are unchanged.

## CP-039 implemented local subset (2026-09-23)

The native kernel now wires the approved local archive into a two-phase
compaction transaction for **laboratory deployment only**. This is deliberately
smaller than the complete F5/F6 result: it establishes a concrete local ordering
without pretending that local files are an independently administered durable
copy.

The protocol owner first requires applied and committed history to meet at the
same positive cut, at or below local durability, with no staged volatile tail.
A durable but not-yet-committed suffix may remain and retains its archive-capacity
reservation. While it still
owns Raft and the retained log, it copies every not-yet-archived record through
that cut into bounded job-owned storage and captures the state, term, storage
generation and commit/durable coordinates. No retained-cell span or Raft entry
pointer crosses to the worker. The job is limited by the retained-history bound;
archive encoding uses at most four 4-MiB segment buffers plus the bounded flat
catalog and heap-backed row scratch. Snapshot save/load also use fixed-size
heap workspaces after worker-stack overflow was reproduced under ASan; the file
read lookahead remains 65,588 bytes regardless of file length. This costs cold-side memory and duplicate
verification work in exchange for keeping filesystem latency and borrowed hot
storage off the protocol owner.

The WAL maintenance worker then performs the first durable phase. It compares
the owned copies with the existing durable WAL, publishes and reopens the
immutable archive segments and single catalog, and obtains the store's opaque
one-shot local coverage capability. It next saves the captured snapshot and
reloads it, checking the exact cut, term and state digest. Capacity pressure is
a deferral: the worker marks the job deferred, the owner cancels the begun Raft
snapshot, and the original WAL and retained history remain. Integrity,
ownership, I/O or ambiguous publication failures are not availability signals;
they fail the storage path closed rather than authorizing reclamation.

After worker completion, the protocol owner rechecks the captured storage
generation, commit, durable and current-term coordinates and that the cut is
still the applied index. It also preflights every retained-cell/Raft callback
relationship before the first irreversible poll. Only then does it consume the
one-shot archive capability and finish the Raft snapshot, releasing the covered
prefix. A second maintenance submission performs `WalSegment::discard_through`.
Thus durable WAL prefix rewrite is **last**: after verified archive publication,
verified snapshot reload, owner-fence revalidation, capability consumption and
Raft prefix release. The owner polls bounded completions and does not run archive
I/O, snapshot fsync or WAL rewrite inline.

This ordering matters because a snapshot alone preserves current state but not
the exact historical command/result evidence, while an archive object alone
does not prove local commitment. Conversely, deleting WAL before the owner has
revalidated the completed worker job would let stale asynchronous work reclaim
the wrong generation. The continuation has an asymmetric failure policy: lack
of quota may safely leave redundant history and retry later; faults after
publication or irreversible owner mutation cannot be treated as rollback and
stop the storage path.

The implementation does **not** add destination receipts, authentication or an
independent-copy capability. Non-laboratory deployment therefore refuses this
reclamation path. It also does not prove every filesystem/power-loss cut, target
durability, live retention sufficiency, peer historical transfer, or snapshot
installation. The broader E1 safe lagger catch-up, E2 live evidence-preserving
compaction and E3 general nonblocking persistence guarantees remain open, as do
their target and adversarial acceptance gates.

## Acceptance boundary

CP-048 (2026-09-24), owner-approved fresh independent-destination verification:
defer persistent receipts; reacquire authenticated exact-set destination evidence
at each live reclamation and after restart where recovery depends on reclaimed
history. Destination outage or failed validation costs availability, never history.
Existing catalog/segment bytes stay unchanged; no backup flag is added. The native
verifier privately mints move-only one-use evidence bound to publisher, destination,
manifest, catalog digest/generation, publication/current storage generations, range,
attempt and fresh challenge. Static-identity digest is a PIN, not authentication;
the eventual transport must establish that pin against an independent host and
hold destination-root ownership. Separate local directories in a test prove none
of those physical/authentication facts.

An acquired local compaction completion no longer suffices in two-host mode.
The owner waits with WAL/retained history intact, then consumes local coverage and
exact independent evidence together before the existing prefix-removal/rewrite
continuation. Relevant cold startup is nonserving until fresh evidence arrives.
Native request/completion calls do no filesystem/network work; destination reads,
file/directory sync and complete object/dependency verification belong off-owner.
Never turn rsync status or an unauthenticated response into this capability.

Counter reset after restart was reproduced as a draft freshness defect. A cold
getentropy256-bit nonce plus per-process nonwrapping attempt now derives a
domain-separated challenge; old responses cannot satisfy a new session except
with negligible cryptographic collision probability. Entropy failure refuses;
no time/PID/constant fallback. No replicated state or durable format changes.
Evidence: `../evidence/f5_destination_verification_2026-09-24/README.md`.
Native host integration is not E2 deployment acceptance. E1/E3 generation switch,
authenticated process transport and NUC/Pi gates still remain.

CP-045/046 (2026-09-24), **owner-approved amendment, partially implemented**: the shared
archive catalog and generation selector cannot be treated as one atomic head.
Existing startup refusals make both naive publication orders unsafe for the
approved before-selection recoverability rule. A destination-bound WAL history
overlap bridges the two publications without new formats. The live8192-entry
bound remains; separately accounted snapshot-covered recovery debt no longer
has to occupy that live capacity. See
`../F5_INSTALL_HISTORY_BRIDGE_REVIEW.md` for the separate candidate-disk/accounting
and nonserving-recovery decision. CP-046 implements worker-backed recovery of the
selected generation's bridge, with a configurable positive cap up to1GiB,
whole-plan archive quota checks, and exact completion fencing. The budget counts
the entire retained WAL file, including suffix and preallocation. The worker
validates all records but publishes only missing `(archive_cut,snapshot_cut]`;
already archived overlap is not charged again. Pending live suffix records retain
their existing worst-case quota reservation. No WAL deletion is authorized.

Before serving, the owner must acquire the serial worker completion and match
generation, nonwrapping sequence, cut, archive coverage/term and unchanged
Raft/metadata coordinates. Worker failure or mismatched completion latches closed.
Owner polling is nonblocking; the voter is unavailable for consensus throughout
recovery. Two-voter NUC/Pi therefore loses quorum during this interval. Cold
constructor verification remains synchronous; no startup latency claim follows.
Memory holds one segment's input payload plus descriptors, encoder and verification
scratch, not the whole WAL; it is not a4MiB process-memory claim. Per-segment
archive reverification can reread growing prefixes, so total I/O is not proven
linear. Native recovery job8920 bytes on this host, excluding dynamic allocations.

Cold-layout witnesses/guard mutations are NOT evidence that an actual lagger
installs. Stable facade/generation-core ownership, live result-ledger preservation,
candidate construction/selection and stale-write fencing at activation remain
unimplemented. Evidence: `../evidence/f5_history_recovery_2026-09-24/README.md`.

CP-023 supersedes the CP-022 integration pause below. Owner approved §8 separately;
both kernel admission paths and recovery now own bounded result reservations.
Read-only batch simulation is equivalent to later slot allocation because the
single owner cannot interleave other result users during synchronous Raft handling.
Actual suffix slot pools and nonwrapping generation capacity are included; releases
happen only after successful durable deletion. Each successful transition, not a
fictional whole-Raft-call rollback, determines cleanup. Full followers process the
term/prev header without entries, cap commitment at min(advertised,prev), and never
acknowledge omitted entries. Fixed diagnostic counts distinguish capacity pressure
from malformed input. All other outbound/log budgets remain pending review.
See `../evidence/f5_result_integration_2026-09-23/README.md` for permanent RED/GREEN,
mutations and the explicit two-voter slow-consumer availability cost.

Historical CP-022 boundary:

CP-022 implements the approved1024/64 ledger component, not its kernel integration.
Generation-checked slots prevent stale reuse; ready results are never canceled to
make space. Basic signed peer coordinates now refuse before Raft. Fresh host
Release/sanitized52/52 and targeted mutants are component/guard evidence, not
whole-node bounds. The header-only follower fallback remains a SEPARATE pending
protocol decision in F5_ADMISSION_RESULT_REVIEW §8: withholding entries must not
hide higher terms, but normal header processing can mutate term/log/commit.
This must not be reported as malformed-input/no-mutation refusal. Evidence,
remaining integration REDs and stale-library verification correction:
`../evidence/f5_result_ledger_2026-09-23/README.md`.

CP-021 makes the V2 receiver's verified-prefix rule explicit before adding bounded
admission: pass min(leader_commit, prev+count) to the pinned library and reject
unrepresentable prefix arithmetic. Synthetic ingress REDs verify this guard;
honest current-sender divergent-tail reachability was not established. V1 and
the fork are unchanged. This is an invariant enforcement, not a new wire/economic
decision. Evidence: `../evidence/f5_verified_prefix_2026-09-23/README.md`.
The owner approved the local result-ledger contract and1024/64 budget in
`../F5_ADMISSION_RESULT_REVIEW.md` §7 after CP-021, requesting the C++ explanation
before implementation. Other quota/protocol approvals do not follow from this.
Markers do not emit
business results. Full uncommitted suffixes require planned ownership transfer
after durable truncation; speculative release and unbounded emergency capacity
are not acceptable shortcuts.

No unspecified encoding, chosen capacity, retention horizon or asynchronous
callback semantic is approved by the direction approval. Existing-contract fixes
and test-only persistence modeling can proceed in
parallel. F5/F6 remain open until their contracts, permanent adversarial tests,
meaningful mutations and final target evidence exist. Two-voter NUC/Pi topology,
no automatic promotion and the required observability gate are unchanged.

## Amendment 2026-10-03 (CP-079): the compaction path verifies the archive incrementally

**What was observed** (M3 deployment log, finding 53). Every compaction called
`ArchiveStore::verify_current()` twice, once before and once after publishing. Each call reread,
re-hashed and fsynced **every** archived segment. The text above already said that "archive
reverification can reread growing prefixes, so total I/O is not proven linear"; in production it
was quadratic.
- With one segment per compaction and a compaction every ~32 minutes, the pause grew by about
  45 ms per archived segment on nuc1: 0.8 s on day 0, 4.8 s after 90 compactions.
- Past the 1–2 s election timeout, most compactions caused an election (term 1 → 65 in 48 hours).

**Decision.** The archive check is split in three:

| Check | What it does | Cost | Where it runs |
|---|---|---|---|
| `verify_layout` | The directory is ours; the dependency files and catalog on disk equal memory; the directory holds exactly the catalog's files; **every catalogued segment is an owned regular file of the catalogued length** (a stat, no read). | one stat per segment | every compaction, before and after publishing |
| `verify_incremental` | `verify_layout`, plus a **rotating scrub**: reread, re-hash and fsync `kArchiveScrubSegmentsPerVerify` (2) segments, continuing from where the last call stopped. | constant | every compaction, before publishing |
| `verify_current` | `verify_layout`, plus every segment's bytes. | linear in the archive | startup (`open` already does this), and history recovery, where the voter is not serving consensus |

A new segment is still read back and compared in full before its catalog row is published. That
is unchanged.

**What is given up, and why it is acceptable.**
- **Before:** a silent change of an old segment's *contents*, at the same length, was found at the
  next compaction.
- **Now:** it is found when the scrub reaches that segment, which is within
  `ceil(segments / 2)` compactions, or at the next start, whichever is first.
  - With 90 segments, that is about one day.
  - At the catalog's ceiling of 4,096 segments, it is about 43 days.
- **Still found at the next compaction:** a missing, truncated, extended, re-permissioned,
  hard-linked or replaced-by-symlink segment, and any unexpected file.
- **Why the delay costs nothing in safety.** The log prefix an old segment covers was reclaimed
  when that segment was published. Finding its corruption one compaction later, instead of some
  compactions later, recovers nothing on this node either way: the repair is the same (the other
  voters' archives). What the check provides is *detection*, and detection is kept, on a bounded
  delay.

**Not addressed here.**
- The compaction's base cost remains: reading the WAL back, saving and reloading the snapshot, and
  rewriting the WAL. It was 0.2–1.0 s per host on day 0.
- All three voters compact at the same log index, so they pause together.
- The archive is never pruned: at one segment per compaction it reaches the 4,096-row catalog
  ceiling in about 85 days at the current tick rate, after which compaction defers and the
  retained log fills. An archive retention or offload decision is needed before then.

