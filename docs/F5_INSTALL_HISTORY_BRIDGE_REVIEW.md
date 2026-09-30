# CP-045: install history across the archive/selector boundary

2026-09-24. **APPROVED — owner explicitly approved the history bridge, candidate
budget and recovery approach.** Implementation and acceptance remain separate.
This enables E1 (actual safe lagger catch-up). CP-036's approved result, authority,
quiesce and acknowledgement rules remain unchanged. No wire or durable encoding
is proposed. The new choice is how to preserve history across two independently
published local authorities, and how to budget the additional disk work.

## 1. Problem and why it matters

Suppose a lagger has committed through2, while its leader has a verified snapshot
and archived commands through12. The lagger must acquire both state and history.
A snapshot alone cannot reproduce the historical command/result evidence.

The approved archive lives at the deployment root, outside replaceable runtime
generations. Its current catalog and the active-generation selector are separate
atomic files. Neither is an atomic transaction with the other.

- Publishing archive coverage12 first changes a dependency of the OLD generation.
  A crash before selecting the candidate leaves old commit2 with archive12. Current
  recovery refuses that mismatch. A future source-generation number also refuses.
- Selecting a new snapshot12 with only WAL13+ first leaves the NEW generation
  without archive/WAL coverage of the missing prefix. Current recovery refuses.
- Staging unlisted objects in the live archive directory is not harmless under
  the approved strict inventory: startup refuses unexpected objects.

These refusals are safety checks, not defects to delete. The integration mistake
would be assuming two individually atomic publications are atomic together. The
existing installer is absent; this is a planning counterexample, not a reproduced
production installer corruption. The real end-to-end lagger still fails at0 after
the healthy peers compact through12.

Source anchors: `ArchiveStore::open` strictly validates the named catalog and
inventory; `ReplicatedKernel` construction rejects future archive generations,
archive coverage beyond recovered commitment, and snapshots with neither archive
nor contiguous retained history. `F5_ARCHIVE_CATALOG_BYTE_REVIEW.md` §4 deliberately
places the archive outside runtime generations. CP-036 requires old authority to
remain recoverable before selection. These requirements must hold together.

## 2. Recommended mechanism: a generation-local WAL history bridge

Let A be the destination's verified archived prefix and S the incoming snapshot
index. Keep the root archive unchanged during preparation. In the isolated new
generation, write the snapshot plus exact verified missing history A+1..S using
the EXISTING bound WAL format, followed by the compatible retained suffix.
This temporary overlap is the history bridge; it is not a new kind of command.

1. Pin a finite authenticated source range and verify shared lineage, committed
   provenance, complete command bytes and dependencies. Check overlap against
   local committed history; a hash or foreign publisher label is insufficient.
2. Stream into destination-owned WAL blocks off-owner; never reinterpret source
   voter-bound files as destination-bound files. Reconstruct outstanding outcomes
   and compare replayed state at S with the canonical incoming snapshot.
3. Prepare/verify the new generation while leaving the root archive unchanged.
   Before selection, a crash still recovers the old generation. The candidate is
   independently complete: archive1..A plus WAL A+1..S plus its retained suffix.
4. Drain/fence old writes, revalidate the full captured tuple/results/leader
   evidence, then publish the already-approved selector. Publication uncertainty
   stops service; never fall back to the old generation after selection may change.
5. Only after durable selection, publish the bridge into the existing archive
   through the trusted local adjudication path. Old catalog plus bridge and new
   catalog plus redundant bridge are both recoverable. Incoming foreign catalog
   bytes do not become a locally authorized catalog merely by changing an ID.
6. Delete no bridge bytes until ordinary archive/snapshot/generation/pin checks
   AND deployment-required independent-copy evidence authorize reclamation.
   Local archive publication alone does not waive live backup requirements.

The bridge contains command history, not a second copy to apply over the final
snapshot. Recovery validates snapshot-covered records but skips them during
state application. Historical replay to reconstruct a missing live response uses
separate scratch; the SAME live ledger and its READY FIFO survive activation.

## 3. Capacity and availability: exact proposal

There is a second integration trap: although recovery streams covered records,
`archive_admission` rejects `log_end - archived > 8192`. Therefore simply writing
a longer bridge does NOT solve far-behind catch-up. The current8,192 guard must
not just be removed or increased globally.

Recommended new bounds/behavior for review:

- Keep the existing8,192 retained-entry and1,024 result limits unchanged.
- Bound the complete candidate WAL by a separate **1 GiB laboratory install
  budget**, counting packed4-KiB blocks, preserved suffix and any preallocated
  tail—not just command payload lengths. A deployment may configure a smaller
  explicit bound; this does not establish live disk sufficiency.
- Before writing, reserve the exact candidate budget and separately prove that
  completing its local archive publication fits the existing Q/object/catalog
  quotas, including pending retained suffix obligations and publication scratch.
  Do not assume the WAL and archive encodings have equal physical sizes.
- Treat already-committed snapshot-covered bridge records as accounted recovery
  work, NOT live retained-log cells. Keep new admission and consensus service
  gated until their local archive catalog publication completes. Health and
  bounded completion polling stay responsive. Restart recognizes/verifies the
  same overlap from existing snapshot/WAL/catalog coordinates and finishes that
  recovery work; no new durable progress flag or temporary-file promotion.
- One install attempt/candidate at a time. A partial candidate is preserved and
  blocks another attempt pending fenced operator handling. Old generations are
  also preserved; there is no automatic cleanup. The1-GiB bound is incremental
  candidate storage, NOT a whole-root bound: old generations, root archive and
  filesystem overhead consume additional space and must be included in deployment
  capacity/admission checks. Do not promise indefinite unattended installs.

This changes admission/accounting only for independently verified, already
committed covered history. Uncommitted suffixes still consume the ordinary fixed
limits. Missing provenance, budget exhaustion or a changing pinned interval
refuses before selection. Missing independent-copy evidence prevents deletion,
not local catalog publication; retained duplicate WAL may subsequently cause
disk backpressure. These are explicit availability costs, not reasons to weaken
the evidence requirements.

Correction at CP-046: streaming the WAL itself is linear, but total catch-up
time is NOT established as O(history bytes + replay records). Existing archive
publication re-verifies the growing archive for each segment; total verification
I/O can be the sum of those growing prefixes. This conservative cold-path cost
is retained for MVP; no latency bound is claimed. The stream has a finite pinned end, with the previously approved
per-owner-turn replay limits and one bounded archive segment input. A disk budget
can refuse an excessively distant catch-up, but unlike the old8,192 test it does
not confuse historical distance with the capacity of the live log arena.

## 4. C++ ownership and latency implications

Keep a stable facade owning the live `ResultLedger` and serial persistence owner.
The replaceable generation owns its Raft server, reducer state, stores and retained
payloads. A prepared generation must not expose serving APIs. Current stores and
`WalWriter` borrow a specific segment/arena, and the startup token is nonassignable;
field-by-field reassignment or placement-new under those references is unsafe.
Refactor ownership deliberately instead of bypassing the verified startup token.

Jobs own their input or carry a generation-owned storage lifetime. No borrowed
record span crosses a worker turn. Completion checks generation, attempt and
nonwrapping sequence; final quiesce drains actual old writes before selection.
Ignoring an old completion cannot undo a stale write to a reused path. Old
objects are retired only once all references/jobs are finished. No live owner
stop/join or fsync fallback is introduced.

This adds cold-path disk writes and preparation time, not an extra per-order
snapshot copy or a larger hot message. Acknowledgements still wait for durability;
the owner staying responsive is not the same as the voter remaining available
for consensus while its installation fence is held. Two voters on NUC/Pi cannot
make quorum when one is unavailable. The three-voter host witness demonstrates
catch-up mechanics, not continued trading availability for that deployment.

## 5. Alternatives and reasons to reject them for MVP

- **Weaken recovery checks:** permits incomplete/ahead evidence and masks the
  ordering problem. Rejected on correctness grounds.
- **Publish the two heads in the opposite order:** moves, rather than removes,
  the crash window. Rejected unless a bridge or new coordination contract exists.
- **Move catalogs/archives inside every generation:** can make selection coherent
  but changes the approved storage namespace, import/migration and accounting
  contracts. It may be a future alternative; it is more change than WAL overlap.
- **Add a multi-object transaction journal/install receipt:** adds a new durable
  recovery authority and encoding contrary to the approved minimal receipt-free
  direction. Not needed if existing overlap semantics suffice.
- **Allow only gaps up to8,192:** makes a useful test pass but does not implement
  the approved finite-history streaming design. Do not advertise it as full E1.
- **Unlimited bridge disk:** replaces a bounded live-memory issue with unbounded
  storage liability. Rejected; capacity is a fail-closed admission condition.

## 6. Approval boundary and next implementation

The owner approved the bridge ordering, separate1-GiB lab candidate budget with exact quota
reservation, and accounted/nonserving recovery of snapshot-covered bridge debt.
This does NOT approve transport wire formats, authentication booleans, foreign
catalog relabeling, independent-copy waivers, old-generation deletion or live
deployment. The native test may simulate authenticated transport explicitly;
no production raw-auth bypass may be exposed.

Remaining implementation: preserve the actual lagger RED; integrate the stable ownership,
worker-backed candidate and archive bridge; prove actual catch-up with unchanged
ledger handles/results; then attack every publication/activation cut and stale
write. Do not count the cold-layout contract witnesses as an installed voter.
