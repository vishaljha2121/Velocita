# Four-lane F5/F6 closure review

2026-09-23, CP-029 review. **Four design directions APPROVED after CP-029;
exact new bytes and unresolved capacities remain pending. None is closed.**

CP-034 clarification: the selector/base bytes are no longer pending: CP-031 and
CP-033 approved them, and initial publication/selected startup now use them.
This historical review's other exact wire, coverage, capacity and async choices
remain unresolved. Initial startup is not live generation replacement or closure
of any lane below.

User authorized four parallel agents for this task only. Runtime capacity is four
active agents including main: three workers reviewed archive, install and repair;
main owns async durability, cross-review and independent verification. The normal
two-worker limit resumes after this task. No production behavior changed here.

## What the parallel work established

| Item | Permanent evidence / present state | Smallest safe next increment |
|---|---|---|
| 1. Archive publication/reclamation | CP-028 portable codec GREEN; CP-026 last-copy-loss RED still open | Durable coverage provenance plus bounded publication owner; gate compact before any cell release |
| 2. Snapshot catch-up | CP-026 intact lagger RED still open | Shared local-generation selector, staged owner and matching-prefix suffix rule |
| 3. Lost-voter repair | New main-reproduced two-history characterization PASS; no repair API | Offline paired admission with complete uncertain suffix and real fencing |
| 4. Async durability | Three new syscall-observed REDs: owner syncs during campaign, vote and commit | One pending durable fence with explicit output/apply/WAL ordering |

Full fresh Release regression is 63/63, independent of the intentionally failing
diagnostics outside CTest. A green suite is not four repaired capabilities.

## Shared rule: generation is authority to mutate local storage, not consensus

All four lanes must use one destination-owned `LocalStorageGeneration` contract.
It names the selected local storage owner. It is distinct from shared cluster
lineage, archive catalog generation, transfer session and repair transaction.
Prepared immutable base objects and mutable live WAL/hard state are separate.

An old write must not overwrite new-generation files. An old completion must not
release a response, result, proof or reclamation permit. Merely checking a tag in
the completion is insufficient if an old worker already renamed over an active
file. Drain/fence writers and bind their actual paths before selection.

Use one atomically replaced, file-and-directory-synced selector. Before durable
selection, the old generation is authoritative; after it, only the selected new
generation may serve. A damaged selector refuses rather than scanning directories
for the newest valid-looking alternative. Exact selector/seal/body bytes remain
unapproved; the four drafts are not a license to invent them independently.

## Owner-approved directions; explicitly outstanding detailed reviews

### 1. Durable archive coverage, not checksum-based permission

Recommend a generation-bound durable coverage catalog, created only from verified
committed/local-durable source history and verified dependencies. For live audit,
also require the already-approved independent-copy policy with exact object and
destination receipts. A process-local, owner-issued capability gates deletion.

Reason: the archive codec proves integrity and shape, not commitment, successful
publication or a surviving independent copy. Reconstructing permission from any
valid-looking file after restart would adopt orphan/speculative history.

Cost: cold metadata publication and retained disk space; publication failure/full
capacity blocks reclamation and eventually admission. No automatic pruning.
Exact bounded coverage inventory, receipt bytes and disk thresholds need their
own byte/budget sheet; earlier suggested quotas are not approved measurements.

### 2. Transactional local generation and bounded snapshot transfer

Recommend one staged install, authenticated current-leader transfer, 16-KiB
chunks and a single selector transaction. Retain the existing live result ledger;
do not introduce a persistent result FIFO. Reconstruct covered outcomes only from
verified matching historical commands and predecessor state.

Reason: the library snapshot-load call mutates term, vote and log immediately; it
cannot be used as an undoable live transaction. Preserve the local suffix only
when its entry at the snapshot boundary matches the incoming history. Divergent
uncommitted bytes remain evidence but must not be spliced onto a different prefix
or turned into fabricated results. Locally committed conflicts refuse.

Cost: temporary second owner/storage, bounded transfer/replay work and possible
retry if local state changes. Exact process RAM and replay horizon must be sized;
an 8,192-record replay cap can refuse older catch-up and is not a liveness proof.
If necessary history cannot fit the approved bounded process, stay stopped rather
than discard obligations. Protocol tables are skeletons, not frozen wire bytes.

### 3. Exactly-two-voter offline repair, with an explicit capacity problem

Recommend the already-approved planned-outage approach, preserving the donor's
complete durable uncertain suffix. Stage both local generations, require both
durable selections before paired release, then a current-term committed barrier
and gateway reconciliation before any new-risk release.

Reason: two real executions left byte-identical survivor files with different old
commit outcomes. Donor commit cannot tell which suffix entries are safe to erase
or apply. Exactly-two-voter quorum intersection plus a non-rolled-back survivor
and actual old-process/session fencing supports re-admission. It does NOT recover
the failed voter's forgotten private numeric terms. This narrower proof needs
explicit acceptance, not a claim that donor term plus one restores lost memory.

Cost: both nodes and gateway are stopped; no single-voter weakening. Authenticated
new sessions and external old-process fencing are deployment prerequisites.
Saturation is still an OPEN design point: a full retained log/result ledger may
leave no space for the recovery barrier. Recommend reserving bounded control
headroom before normal admission, subject to revised budget approval; that does
not retroactively repair already-full histories. Those need a separately reviewed
bounded offline continuation or explicit fail-closed limitation. Neither deleting
the uncertain suffix nor treating a copy as commitment is acceptable.

### 4. One pending durable fence; pause consensus progress, not the service loop

Recommend one serial persistence owner and one pending fence for MVP. While disk
work is pending, stop Raft mutation/admission and withhold dependent output/apply/
proof/WAL progress; keep health/control/completion handling responsive.

Reason: callbacks currently promise completed persistence. Enqueue-and-return-zero
would publish a promise before it is durable. Current real-syscall REDs establish
that merely starting the WAL writer leaves metadata syncs on the owner.

Cost: disk latency still stalls consensus and may stop the two-node quorum.
This deliberately trades availability for a simple provable boundary. Runtime
suffix rewrite and compaction must obey the same fence, not hide an owner join.
Exact resumable callback/API design needs proof and review before implementation.

## Implementation order after the corresponding approvals

1. Main freezes the minimal shared generation/ownership/budget sheet. Distinguish
   approved policy from exact new bytes; do not approve unspecified fields by
   implication. Record the repair capacity decision explicitly.
2. In parallel, archive worker builds publication/coverage; installer worker builds
   generation staging/selector; repair worker builds approved offline validation
   and characterization-to-RED fixtures; main builds the persistence fence/model.
   Give each disjoint implementation files; one owner edits replication glue.
3. Integrate archive permission BEFORE compaction releases cells. Turn the existing
   last-copy-loss RED GREEN with independently verified full archive payloads.
4. Integrate snapshot transfer, prefix/suffix matching and live ledger ownership.
   Turn the actual lagger RED GREEN, including divergent suffix/reserved cases.
5. Compose repair with the same selector/fence, then saturation, stale-output and
   per-publication power-cut sweeps across all four. No subsystem self-certifies
   integration by passing its own local tests.
6. Fresh full Release/sanitizer/model/tool inventory, intended-guard mutations,
   frozen external source-root comparison and NUC gates. Pi stays deferred to the
   final hardware stage, not waived. None of this starts trading automatically.

## Review corrections retained as learning, not hidden

Main rejected draft proposals for a new persistent result-ledger image, redundant
selector/pointer publication, 64-KiB chunks, immutable live WAL file hashes, and
unconditional local-suffix splicing. The first three enlarged MVP contracts without
need; the last two would reject normal operation or combine incompatible histories.
Archive orphan scanning was also rejected as a substitute for durable commitment
provenance. These were draft defects, not bugs shipped in production this turn.

Detailed lane reasoning and test plans:

- `F5_ARCHIVE_PUBLICATION_REVIEW.md`
- `F5_SNAPSHOT_INSTALL_REVIEW.md`
- `F5_LOST_VOTER_REPAIR_REVIEW.md`
- `F6_ASYNC_DURABILITY_FENCE_REVIEW.md`

Evidence and limits: `evidence/f5_f6_parallel_contracts_2026-09-23/README.md`.
