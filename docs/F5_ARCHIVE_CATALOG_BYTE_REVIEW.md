# CP-038: archive catalog and integrated reclamation contract

2026-09-23. **APPROVED — owner explicitly approved all five review points.**
Implementation and runtime acceptance are separate: E1/E2/E3 remain open until
their actual integrated behaviors are demonstrated. No remote receipt or live
independent-copy waiver was approved.
Branch/HEAD verified: `v2/phase-5-corrections` / `d44a4a9`; remediation remains
uncommitted. Two Sol read-only reviews informed this proposal; main selected the
smaller flat design and corrected provenance and quota accounting.

This enables **E2: actual compaction preserves verified historical evidence**,
and supplies E1's historical replay dependency. The actual transaction must use
E3's shared persistence fence. A catalog codec alone is not a delivery milestone.
CP-028 segment bytes and CP-031/033/036 approvals are unchanged.

## 1. Problem, importance and intuition

A snapshot preserves current trading state, not every historical command or its
original result. Current compaction can remove the last command bytes. Merely
putting a checksummed segment beside the WAL is insufficient: the segment could
contain uncommitted records or be an unfinished publication left after a crash.

Use one durable catalog as the archive owner's recorded adjudication: these exact
files were verified as committed history, and their dependencies are present.
Publish the data first, then the catalog, then permit reclamation after the other
snapshot/ownership/durability checks. Restart reads only that named catalog; it
never promotes the highest filename or an unlisted segment.

The checksum protects integrity, NOT authenticity or consensus. Provenance comes
from the trusted publication path checking exact records against Raft, retained
payloads and durable WAL under exclusive ownership, then durably recording that
decision. This is local crash/recovery evidence, not a signed quorum certificate
or protection against an administrator maliciously replacing and rehashing files.

## 2. Recommended scope and alternatives

One self-contained flat catalog describes the complete contiguous prefix
`[1, cut]`, with at most **4,096 segments**. Existing rows are copied byte-for-byte;
new publications only append rows and advance catalog generation by exactly one.
No overlap, gap, reorder, deletion or resegmentation of already-listed rows.
First publication is generation1; overflow refuses, never wraps.

The catalog is replaceable index/commit metadata. **Only that metadata** is
atomically replaced; CP-028 segments and replay dependencies stay immutable and
are never automatically pruned. This is an explicit new filesystem choice for
review, not permission to overwrite archived commands.

Rejected alternatives:

- Catalog chain: adds old catalog files as recovery dependencies and can make
  startup work grow with every update rather than the bounded inventory.
- Merkle tree/pages: unnecessary machinery for this bounded flat list; a hash
  root without the actual membership list cannot recover the files to verify.
- Immutable catalog snapshots plus another head selector: a second publication
  layer and retention problem without a present need for old index versions.
- Snapshot-only starting point: cannot reconstruct pre-snapshot commands/results.
  V1 coverage starts at index1; missing historical bytes refuse, not fabricate a
  successful bootstrap. Existing already-compacted archives cannot be upgraded by
  inventing the missing prefix; stopped migration/repair is separate.

## 3. Exact proposed bytes

Separate magic/schema; do not enlarge hot `BoundFrameScratch`. Explicit
little-endian integers, signed values represented in two's-complement bits.
These are wire/file offsets, NOT native C++ struct offsets.

### Header: 96 bytes

| Offset | Bytes | Tag/type | Meaning |
|---:|---:|---|---|
| 0 | 4 | ASCII `V2AC` | Local archive coverage catalog |
| 4 | 2 | u16 = 1 | Schema |
| 6 | 2 | u16 = 96 | Exact header length |
| 8 | 4 | u32 | Total file length, including footer |
| 12 | 4 | u32 | Segment count, 0..4,096 |
| 16 | 32 | Digest256 | Verified shared manifest digest |
| 48 | 4 | i32 | Publisher/local voter ID; must match expected owner |
| 52 | 4 | u32 = 0 | Flags; nonzero refuses, not a backup bit |
| 56 | 8 | u64 | Nonzero catalog publication generation |
| 64 | 8 | u64 | Local storage generation during this publication |
| 72 | 8 | u64 | Observed committed index at publication |
| 80 | 8 | u64 | Observed locally durable index at publication |
| 88 | 8 | i64 | Observed current Raft term, not the term of the last record |

### Each ordered row: 112 bytes

Offsets restart at each row; row ordinal is implicit in its position.

| Offset | Bytes | Tag/type | Meaning |
|---:|---:|---|---|
| 0 | 8 | u64 | First included index |
| 8 | 8 | u64 | Last included index |
| 16 | 8 | i64 | Term at first_index-1 |
| 24 | 8 | i64 | Last included record term |
| 32 | 4 | u32 | Record count |
| 36 | 4 | u32 | Exact CP-028 segment file length |
| 40 | 8 | u64 | Local source-storage generation when this segment was adjudicated |
| 48 | 32 | Digest256 | CP-028 segment digest / immutable object ID |
| 80 | 32 | Digest256 | Last record's existing CP-028 record digest |

The source generation belongs in EVERY row. A header-only source generation would
misattribute old rows after installation changes local storage generation. The
header describes this publication; inherited rows preserve their original facts.
This format records local adjudication, not a remote peer attestation. Import of
a peer's catalog must use the separately authenticated E1 path; foreign publisher
bytes cannot simply be relabelled as a locally adjudicated catalog.

Footer: 32-byte SHA-256 of
`ASCII("velocita.v2.archive-catalog.v1") || header96 || complete_row_region`.
The ASCII domain is exactly30 bytes, no terminator. Footer is excluded from its
own hash. Total length is exactly `128 + 112 * count`; maximum **458,880 bytes**.
Empty catalog is128 bytes, covers nothing and grants no positive deletion cut.

### Validation and fixed dependencies

- Match separately verified shared identity AND local owner; publisher is in the
  configured voter set. Manifest already binds deployment mode, schemas and the
  execution-catalog root, so do not duplicate them in new mutable fields.
- Positive representable record coordinates/terms; nonzero source/publication
  generations. Source generations cannot exceed the publication storage generation
  for this local publisher. Generation mismatch with a later active generation
  does NOT invalidate historical rows.
- First row begins1 with predecessor term0. Each next row starts at previous
  last+1 without overflow, with predecessor term equal to previous last term.
  Count equals inclusive range length and obeys CP-028 record/byte limits.
- Strictly decode EVERY named segment against the expected manifest. Compare its
  range, predecessor/last terms, count, length and segment digest to its row;
  verify the terminal record digest from the actual record bytes. Row metadata
  never substitutes for full segment validation.
- For nonempty catalogs, observed commit and durability cover the final row's
  last index; observed current term is at least its last record term. All index
  values fit Raft's signed range. Empty catalogs allow nonnegative coordinates
  and term but authorize no history regardless of those coordinates.
- Retain exactly the existing canonical static-identity artifact and execution
  catalog artifact, each checked using its existing codec/root. Policy inputs
  already live in archived commands. No generic dependency graph is needed for
  current schemas. A future schema needing another external dependency requires
  an explicit extension, not silently incomplete replay.
- Validate new rows against exact still-owned Raft/retained/WAL tuples and payload
  bytes with commit/durable bounds. Verify prior catalog/objects before carrying
  old rows forward. Do not demand reclaimed old WAL as their new source proof.

The native one-shot reclamation capability binds the CURRENT storage generation,
catalog generation AND digest, verified range/terminal identity and evidence class.
Installation invalidates a capability, not the immutable historical evidence.
The capability is nonserializable; decoding a public struct cannot mint one.

## 4. Directory and publication contract

Propose one new explicitly allowed deployment-root child, `v2-archive/`, outside
all replaceable runtime generations and immutable `base/` directories. Root and
archive directory ownership must be verified and held; do not simply relax the
startup allowlist. Archive restart verifies coverage of any previously reclaimed
prefix before service relies on that history. Missing/corrupt evidence fails
closed; it is never silently treated as an empty new archive.

Names inside that directory:

- `v2-identity.bin`: exact existing identity format, bound to catalog publisher.
- `v2-execution-catalog.bin`: existing canonical artifact, <=8,192 bytes.
- `s-<64 lowercase hex segment-digest>.bin`: immutable CP-028 object.
- `v2-archive-catalog.bin`: only authoritative current catalog.
- `v2-archive-catalog.bin.tmp`: unpublished catalog candidate.
- `v2-archive-segment.bin.tmp`: at most one unpublished segment candidate.

All files are owner-only regular single-link files, opened without following
symlinks. Catalog has exact length; dependencies have existing codec bounds.
Final-name derivation is fixed, not arbitrary paths from decoded bytes.

Segment publication: exclusive temp creation, complete checked writes, file
sync, complete re-read/validation, atomic **no-replace** rename, directory sync,
then reopen/verify final identity/content. EEXIST is idempotent only after full
exact-object validation; a conflict stops publication. Use Linux
`renameat2(RENAME_NOREPLACE)` / macOS `renameatx_np(RENAME_EXCL)` behind a narrow
platform wrapper; implementation must validate actual platform/filesystem support
and fail closed if unavailable. Do not substitute check-then-rename. Hard-link
publication is rejected because a crash can leave two links, violating the
single-link reader invariant.

Catalog publication: after all named objects/dependencies are durable, write the
exclusive temp, file-sync, atomically rename over the named current catalog,
directory-sync, reopen and fully verify. No extra selector. A final catalog
observed after an interrupted rename must be fully validated and re-synced before
use. A malformed final refuses; never fall back to a temp or highest generation.
Old or new complete catalog can survive a crash; WAL deletion was not authorized
until the new publication completed. Uncertain post-rename failure latches service
until recovery; do not continue from an in-memory old catalog assumption.

Unexpected temps/unlisted finals are preserved for diagnosis, charged against
capacity and stop new publication until explicit resolution. No retry loop may
accumulate unbounded quarantine files; no automatic orphan adoption/deletion.
Directory enumeration is for bounded validation/accounting, NEVER authority.
Stop on excess entries rather than scanning an arbitrarily large directory.

## 5. Proposed bounds and honest availability cost

The following are **laboratory starting limits for approval**, not measured live
retention hours or a promise that a device has sufficient free space:

| Resource | Proposed limit / policy |
|---|---|
| Published immutable segment logical bytes | Q=1 GiB laboratory default; explicit bounded deployment configuration before live |
| Segment inventory | 4,096 rows/files maximum; may fill before byte quota |
| Ordinary high-water | Q minus16 MiB; 4,092 rows, evaluated WITH unfinished reservations |
| Protected completion/control margin | Remaining16 MiB and4 rows; only already-admitted completion and existing control-eligible work, not ordinary traffic |
| Concurrent publication | One segment and one catalog transaction; no concurrent alternate candidates |
| Temp/dependency/catalog accounting | One4-MiB segment temp + two458,880-byte catalogs + existing identity<=124 and execution artifact<=8,192: **5,120,380 bytes** separate accounting reserve |
| Independent-copy backlog (when transport exists) | Normal combined bound4 objects/16 MiB; hard combined bound5 objects/20 MiB, including unfinished reservations; final margin only completion/control |
| Startup verification | At most4,096 named objects, bounded catalog and two fixed artifacts; byte work bounded by configured Q, not a time guarantee |

Q counts immutable segment logical lengths; the separate reserve must also be
budgeted by deployment storage checks. Neither covers filesystem block/inode
overhead nor reserves physical capacity against other users. Actual ENOSPC/EIO
still fail closed with the WAL retained. Directory entry validation has a bound
of4,102 non-dot entries (inventory plus fixed/temp names and excess detection);
an excess/unknown entry refuses rather than allocating an unbounded inventory.

### Completion reservations — bytes AND objects

Reserve capacity before accepting each record that creates an audit obligation,
including refused commands and authority markers. No result-slot exemption means
an archive exemption. Count accepted-but-not-catalogued records plus any in-flight
candidate exactly once throughout ownership transfer.

At the existing largest record1853 bytes, a4-MiB object holds at least
`floor((4194304-128)/1853)=2263` records when packed to capacity. Thus a fully
packed plan for the retained8,192-record bound needs at most4 objects/16 MiB.
This is a packing bound, NOT permission to publish arbitrary tiny objects using
that same reservation.

Before any early/underfilled publication cut, atomically check:

`published + actual_candidate + reservation_for_remaining_records <= limit`

Check bytes, object/catalog slots and (when applicable) the combined unconfirmed
backup backlog. If the cut creates another required file and no extra capacity
exists, refuse that cut, retain the WAL, and pack a larger batch when possible.
Never consume the reservation first and discover missing capacity afterward.
Ordinary work must fit below its high-water; protected work still fits below the
hard limit. A crash/orphan may require operator intervention; the reservation
guarantees bounded planned resource liability, NOT eventual progress through
disk faults or a disconnected destination.

Example rejected by review:2263 pending records reserve one object. Publishing
only the first record leaves2262 records needing a SECOND object. The original
one-object calculation is no longer sufficient. A smaller file is not always
cheaper when file-count limits and reserved future work also matter.

## 6. Local coverage is not independent-copy evidence

This catalog deliberately records **local** durable committed coverage. It has
no placeholder receipt hash or self-asserted `independent=true` field. Existing
manifest deployment mode is authoritative: laboratory may consume local coverage;
two-host pilot may not reclaim without separately verified destination durability.
No runtime switch may silently downgrade that mode.

Destination receipt identity/authentication/wire remains a real unapproved
transport dependency. Until implemented, live reclamation MUST refuse. A local
host integration test can prove E2's local transaction, not complete live E2 or
the Pi gate. This is implementation sequencing, not removing independent backup
from MVP. Later evidence must bind destination identity, exact catalog/object and
dependency set, and destination file/directory durability through authenticated
responses; an rsync exit or sender-created checksum is not that evidence.

## 7. Integrated C++ transaction and verification exit

Use explicit encoding into bounded cold-side buffers, never write a native struct
with padding or `reinterpret_cast` disk bytes. Copy source records into owned
bounded buffers while their identities are stable; no `span` into retained/WAL
storage crosses a thread or outlives its callback. Arrays bound inventory memory;
move-only ownership and a generation/sequence-checked completion fence enforce
lifetimes. Codec success grants no reclamation authority.

Full catalog/object verification is O(total named bytes + records), off the
protocol owner; it may be expensive at Q. This is the conservative initial
implementation, not a per-command scan or a latency claim. Catalog rewrite is
O(number of rows) cold work. Measure integrated peak memory rather than presenting
the458,880-byte file maximum as process RSS; segment input/output and retained
owners dominate it.

After archive publication, durably publish/reload the state snapshot. Revalidate
commit/applied/durable bounds, current catalog/storage generations and all result/
repair pins/borrows. The shared F6 storage owner performs the fenced WAL rewrite;
the protocol owner polls bounded completions and remains health-responsive.
No owner-thread fsync or stop/join fallback. Preserve payload/result ownership
until the transaction permits release; late failure cannot be treated as rollback
after irreversible Raft polling. Any post-mutation ambiguity latches service.

Do NOT invent a second consensus algorithm to schedule truncation. Integrate a
reviewed continuation/no-fail release ordering with the pinned Raft callbacks.
The exact library adaptation is an implementation proof obligation, not something
this disk schema magically supplies. Do not report E3 closed after only WAL work
moves to a thread.

Acceptance starts from the original archive-loss RED. Make actual compaction
GREEN, restart, and independently recover the exact authored command from the
named catalog while proving the old WAL prefix was reclaimed only with coverage.
Then cut/fail every object/catalog/snapshot/rewrite sync and rename; test stale
generations, held borrows, unread results, malformed/unlisted objects, quota cuts
and missing live-copy evidence. Mutate decisive checks and verify the intended
assertion. No standalone codec-only milestone; E1/E2/E3 remain the exits.

## 8. Approval requested and evidence status

Approve together: exact96/112-byte catalog and fixed dependencies; replaceable
catalog metadata versus immutable segments; namespace/no-replace publication;
bounded quota/reservation policy and laboratory defaults; and the explicit local-
versus-live evidence boundary. This approves no destination attestation bytes,
peer transport, deployment, credentials, archive cleanup, or weakened live gate.

This checkpoint is design review only. No C++ changes, new test runs, performance
measurements or gate acceptance. CP-037 is the last verified runtime checkpoint.
All three exits remain open. The original archive-loss RED remains the integration
oracle, not a catalog decoder count.
