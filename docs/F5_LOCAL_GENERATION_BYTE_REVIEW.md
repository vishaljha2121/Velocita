# F5 local storage-generation selector v1 — exact byte review

2026-09-23. **APPROVED after CP-030 and the owner explanation.** Approval covers
the exact selector bytes, strict selection/no-fallback rules and the recommended
exclusive-directory/seal-last direction. Prepared-base bytes, actual migration,
startup integration and the other protocols below remain separate. This document is
local storage selection only. It does not define snapshot transfer, archive
catalog/coverage, repair certificates, result persistence or voting admission.

Subsequent CP-033 status: `F5_GENERATION_STORAGE_REVIEW.md` now approves the exact
base seal, fixed layout, startup direction and automatic-migration deferral.
Standalone base codec/read-only file validation are implemented. References below
to those choices as pending describe the CP-031 dependency at that time; actual
startup/publication integration, not another approval, is now the next step.

## 1. Approved selector contract

Add one fixed 160-byte selector in the already locked voter deployment root. It
binds one destination-owned generation to both the existing shared manifest and
the existing local voter ID. A generation number names storage; a separate
selection sequence orders selector publications even when failed preparation
burns generation numbers. Both are nonzero and nonwrapping.

The selector names an immutable prepared-base digest. It does **not** hash the
live WAL or term/vote/commit metadata forever: those files become mutable after
activation and retain their existing bound-frame recovery rules.
The codec proves only well-formed bound bytes. It is not selection authority:
root-lock ownership, prepared-base validation, transition checks and durable
publication are mandatory caller responsibilities.

Owner-approved choices:

1. approve the 160-byte record in section 3 and validation in section 4;
2. approve one atomic-replaced named selector, with no scan/fallback;
3. approve fail-closed treatment of old nonempty unselected roots pending an
   explicit stopped migration; and
4. choose the portable generation-directory publication mechanism in section 6.

## 2. Relationship to existing source contracts

The current `VerifiedStaticIdentity` has two relevant identities:

- `manifest_digest()` deliberately excludes local voter ID and local framing;
- `value().local_node_id` binds one directory to one configured voter.

The selector therefore stores both. Storing only the manifest would allow a
selector copied from voter 1 to validate on voter 2. Storing only local ID would
lose cluster/schema/catalog lineage. The manifest already binds incarnation,
catalog root, schemas/epochs, voters and durability/deployment modes, so this
record does not repeat those fields.

Existing local metadata/WAL/snapshot bound frames already include manifest and
local-owner binding. Reusing `BoundFrameKind` is inappropriate: its enum has no
generation-selector kind, its metadata payload capacity is only 24 bytes, and
adding a kind changes an approved general framing contract. Use a separate magic
and digest domain, but reuse the same explicit little-endian and strict-zero
coding discipline.

Current `VerifiedIdentityStartup` locks the deployment directory, rejects
unexpected bootstrap contents, verifies the directory inode/name and uses
temp-write, file sync, rename and directory sync. Generation selection must occur
only while that same ownership token remains alive. This sheet does not weaken
identity-first validation or allow a selector to choose a foreign directory.

## 3. Exact selector bytes

Approved selector filenames:

- final: `v2-active-generation.bin`
- temporary: `v2-active-generation.bin.tmp`

Integers are little-endian. Signed local ID uses its exact i32 two's-complement
bits. Offsets are wire offsets, never native struct offsets. The complete file is
exactly 160 bytes; short or trailing bytes refuse.

| Offset | Bytes | Type/value | Meaning |
|---:|---:|---|---|
| 0 | 4 | ASCII `V2LG` | Local generation selector, not a bound frame. |
| 4 | 2 | u16 = 1 | Selector schema. |
| 6 | 2 | u16 = 128 | Bytes before trailing digest. |
| 8 | 4 | u32 = 160 | Exact total file bytes. |
| 12 | 4 | u32 = 0 | Flags; nonzero unsupported. |
| 16 | 32 | Digest256 | Existing shared manifest digest. |
| 48 | 4 | i32 | Existing positive local voter ID. |
| 52 | 4 | u32 = 0 | Reserved; must be zero. |
| 56 | 8 | u64 | Selected local storage generation, nonzero. |
| 64 | 8 | u64 | Selection sequence, nonzero. |
| 72 | 8 | u64 | Previously selected generation; zero only on first selection (genesis or reviewed migration). |
| 80 | 32 | Digest256 | Immutable prepared-base manifest digest. |
| 112 | 16 | zero | Reserved; must be zero. |
| 128 | 32 | Digest256 | Selector digest defined below. |

`selector_digest = SHA256(ASCII("velocita.v2.local-generation-selector.v1") ||
bytes[0..127])`.

The ASCII domain is exactly the shown characters with no NUL or length prefix.
The digest is integrity/domain separation, not authentication. Local directory
ownership and permissions remain required.

Generation directory names are mechanically derived, not carried in the record:
`v2-generations/g-` followed by exactly 16 lowercase hexadecimal digits for the
u64 generation. No signs, prefixes, variable width, symlinks or path separators.

## 4. Codec validation and startup authority guards

Decode into a temporary value and leave the caller's destination unchanged on
all errors. The standalone codec validates only:

1. exact 160-byte length, magic, schema/header/total sizes, flags and all reserved
   zeros;
2. trailing digest using a constant-time digest comparison;
3. nonzero manifest/base digests, generation and selection sequence;
4. manifest equality with a separately decoded `VerifiedStaticIdentity`;
5. exact positive local voter ID equality with that identity;
6. first-selection relation: selection sequence 1 requires previous generation
   0, whether this is empty genesis or an explicitly reviewed legacy migration;
   later sequences require `0 < previous_generation < generation`.

The startup/transition validator, not the pure codec, additionally requires:

- the derived generation path is a real owner-controlled directory opened with
  non-following directory-descriptor operations beneath the locked deployment
  root; normal POSIX directory link counts are not constrained to one;
- every regular selector/base file is non-symlink, owner-only and single-link;
- the sealed immutable prepared-base manifest hashes to the selected base digest
  and independently binds the same manifest/local owner/generation; and
- the transition from the currently selected record satisfies the rules below.

For an update from an already valid selector:

- `new.selection_sequence == old.selection_sequence + 1` without wrap;
- `new.generation > old.generation`, is nonzero, and has never been selected;
- `new.previous_generation == old.generation`; and
- the old selector and root-directory ownership are revalidated immediately
  before rename.

Refuse when either old counter is `UINT64_MAX`; never reset/reuse it. Abandoned
prepared generations may create gaps in generation number. Sequence has no gaps
because it counts only selector publications. A selector is not valid merely
because it has the numerically largest generation.

## 5. Immutable base versus mutable live files

The prepared-base manifest is a separate, still-pending local generation format.
At minimum its digest must cover the immutable installation-base snapshot and the
immutable identity/catalog/base descriptors used to build the candidate. Exact
manifest bytes, filenames and optional install/repair receipts are dependencies
of this selector review, not silently defined here.

The prepared candidate also has initial bound WAL and bound hard-state metadata.
They are validated before selection, but are **not** members whose original bytes
must match the prepared-base digest forever. After activation, ordinary Raft
appends/truncates WAL and replaces term/vote/commit metadata. On restart:

1. decode exactly the named selector;
2. validate the named generation and immutable prepared base;
3. validate current mutable metadata, WAL and snapshot through their existing
   local-owner/manifest framing and recovery invariants; and
4. refuse any gap, foreign owner/lineage or inconsistent hard state normally.

Do not copy or reseal a generation on every append. Do not treat a valid changed
WAL/metadata file as prepared-base corruption. A later normal compaction writes
the existing mutable runtime snapshot; it does not replace or mutate the sealed
installation-base snapshot. Conversely, mutable files cannot change the selected
generation or base digest.

## 6. Publication and crash semantics

Selector replacement uses the existing pattern under the held deployment-root
lock:

1. completely prepare and validate an unselected generation;
2. sync every required base and initial runtime file;
3. publish/sync its sealed-base marker and sync the generation directory and its
   parent;
4. exclusively create the selector temp, write exactly 160 bytes, sync and close;
5. revalidate old selector, ownership and preparation inputs;
6. atomically rename the temp over the final selector;
7. sync the deployment root; then reread selector and selected generation; and
8. activate only after the reread succeeds.

Before step 6, restart selects the old generation. After successful step 7, it
selects the new generation. A process failure after rename but before directory
sync may recover either old or new after an actual power loss; this is the known
rename-persistence uncertainty, so runtime must latch/fail closed and must not
resume the old owner after observing rename success. Acceptance needs the same
sync-sensitive cut model used for bound metadata, not `_exit` alone.

If the final selector is missing, malformed, foreign or names an invalid/missing
generation, startup refuses and preserves everything. It never scans generation
directories, chooses the greatest valid number, follows `previous_generation`,
or falls back to an old selector/temp. A leftover selector temp is evidence of an
interrupted attempt: validate it for diagnosis but do not promote it implicitly.

### Generation-directory publication

Portable POSIX `rename()` replaces an existing destination; Linux
`renameat2(RENAME_NOREPLACE)` is not portable to the macOS development host.
Generation publication must nevertheless prevent collision/reuse.

Recommended MVP: create the final derived generation directory exclusively
while holding the root lock, populate it under its final name, and publish a
sealed-base marker last. An incomplete directory is never selectable; preserve it
and burn that generation number. Visibility of a partial unselected directory
grants zero authority and is diagnostically useful.

Alternative: stage a directory and require a narrow platform abstraction with
true atomic no-replace publication: Linux `renameat2(..., RENAME_NOREPLACE)` and
macOS `renameatx_np(..., RENAME_EXCL)`. This adds platform/filesystem behavior and
cut tests without improving the selector's authority boundary, so it is deferred
for MVP. Never fall back to clobbering `rename`, and do not introduce a hard-link/
unlink publication protocol.

## 7. Genesis, old unselected roots and migration

For a truly empty bootstrap, sequence 1 / previous 0 is the first selection. The
current bootstrap publishes root metadata then identity, while the proposed
generation layout wants selected runtime files under a generation. Exactly how
generation 1 is initialized without duplicating or exposing two writable metadata
owners remains an integration blocker; this sheet does not invent that move.

An existing nonempty voter root with no selector is **legacy unselected**, not
generation 0 and not implicit generation 1. Recovery refuses before writable
open. Do not move/adopt files, synthesize a selector, or pick a directory merely
because existing bound history validates.

A one-time migration requires a separately reviewed stopped-node procedure that
preserves original bytes, validates all existing history and hard state, creates
and syncs the first generation, and publishes sequence 1. Lost-hard-state repair
cannot masquerade as this migration. No migration byte or operator certificate is
defined here.

This strict rule means the current source cannot consume this selector without a
migration/layout decision: `VerifiedIdentityStartup` currently expects identity,
metadata and history directly in one deployment root and bootstrap rejects
unknown contents. That is a real integration dependency, not a codec bug to hide
with directory scanning.

## 8. Dependency separation and acceptance exits

This selector depends on, but does not define:

- prepared-base manifest bytes and directory ownership;
- snapshot install transfer/content bytes;
- archive catalog generation or portable coverage evidence;
- stopped-cluster repair fencing/admission certificates;
- F6 asynchronous completion/fence bytes; and
- one-time migration mechanics.

Future async completions must carry local generation identity so stale completions
cannot mutate/reopen a selected generation, but that protocol is not encoded here.
Archive/repair generations remain separate types and namespaces.

Implementation acceptance requires literal independent golden bytes and tests
for every field/reserved byte, truncation and trailing data, foreign manifest and
owner, zero/wrapped counters, stale/out-of-order sequence, wrong previous, base
digest mismatch, symlink/hard-link/path substitution, temp-only/final-only/both,
missing generation, partial unsealed generation, and no directory-scan fallback.

Cut tests cover every generation file/marker sync and selector write/sync/close/
rename/directory-sync/reread boundary. Direct mutations must fail intended
assertions: omit owner binding, omit base binding, accept greatest directory,
promote temp, follow previous on corrupt current, reuse generation, allow sequence
gap/wrap, accept mutable-file digest as permanent base, activate before directory
sync, and resume old owner after observing selector rename.

CP-031 implements the standalone selector codec and pure transition checks with
independent literal bytes, corruption/shape/ownership tests and guard mutations.
It does NOT implement the filesystem/startup portions of this contract. A complete
startup transaction still needs root-layout/genesis/runtime integration and
publication tests. CP-033 implements its prepared-base validator; automatic legacy
migration is explicitly deferred rather than required for fresh deployment.
Source/CMake tests now include the codec;
no deployment or whole-F5/F6 acceptance is claimed. Evidence:
`evidence/f5_local_generation_2026-09-23/README.md`.
