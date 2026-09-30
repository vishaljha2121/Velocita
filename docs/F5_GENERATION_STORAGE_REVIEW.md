# F5 generation storage: minimal remaining integration contract

2026-09-23, CP-032 review. **OWNER APPROVED after the CP-032 explanation.**
Approval covers the exact seal/digest, fixed layout/root ownership, runtime
snapshot floor/refusal policy and deferral of automatic migration below. The
160-byte selector was already approved and implemented; this does not reopen it.
Approval is not implementation or acceptance evidence.

## 1. MVP decision

Reuse the existing identity, bound snapshot, metadata and WAL formats. Add one
40-byte base seal, not a generic manifest database, file inventory tree, new
snapshot format, or recovery framework. Keep one candidate generation at a time.
Defer an automatic legacy-directory migration tool; preserve old directories and
refuse to adopt them implicitly. This does not authorize resetting a participating
voter or bootstrapping its old identity after losing voting memory.

The seal verifies an exact immutable base for a particular local generation.
It does **not** establish Raft commitment, archive coverage, independent backup,
result reconstruction, repair admission, selector durability or send authority.

## 2. One owner and one selected runtime directory

```
deployment-root/                         held root lock, immutable identity
  v2-identity.bin                        unchanged approved identity format
  v2-active-generation.bin               unchanged approved 160-byte selector
  v2-generations/
    g-0000000000000008/                  held selected-directory lock
      v2-base-seal.bin                   approved 40-byte seal, written last
      base/
        v2-snapshot.bin                  unchanged bound snapshot, immutable
      v2-metadata.bin                    unchanged mutable hard state
      ...existing WAL files...           unchanged mutable bound WAL
      v2-snapshot.bin                    unchanged mutable runtime snapshot
```

For a genesis base, `base/` exists but is empty. For a snapshot base, its only
entry is `v2-snapshot.bin`. Refuse unexpected entries/symlinks in this immutable
namespace. Mutable WAL files are not part of a permanently frozen digest.
No hard links between immutable and mutable snapshots.

Adapt the existing non-publicly-constructible `VerifiedIdentityStartup` boundary:
hold root and selected-generation ownership until all stores/workers die;
`directory()` routes consumers to the selected runtime directory. Do not add a
raw path constructor or two independently writable metadata authorities. Validate
root identity/catalog first, then selector, canonical derived directory name,
base seal/snapshot, current runtime metadata/WAL/snapshot, then writable startup.
Use descriptor-relative non-following validation; preserve existing trusted-path
requirements for path-based stores until their ownership-safe integration is done.

## 3. Exact approved seal and digest

Filename `v2-base-seal.bin`, exactly 40 bytes; explicit little-endian integers.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `V2GB` |
| 4 | 2 | schema = 1 |
| 6 | 1 | kind 0 = genesis; kind 1 = snapshot; others refuse |
| 7 | 1 | reserved = 0 |
| 8 | 32 | base digest below |

Digest preimage, in this exact order:

1. ASCII `velocita.v2.local-generation-base.v1`, no NUL/newline/length prefix;
2. the first 8 seal bytes;
3. local generation as LE u64, derived from selector and its canonical directory;
4. the exact validated canonical root identity file bytes (existing 116..124-byte,
   self-sized format; includes local owner and shared lineage);
5. immutable bound snapshot file length as LE u32;
6. those exact immutable bound snapshot bytes, including existing framing/hash.

SHA-256 of that preimage is both seal bytes 8..39 and the approved selector's
`prepared_base_digest`. No second manifest digest or new `BoundFrameKind`.
Identity bytes are length-delimited by their existing canonical format. Snapshot
length is 0 for genesis with no file; snapshot kind requires a valid full bound
snapshot of at most 65,587 bytes with positive included index/term. Validate its
canonical state and catalog dependencies, not only its hash. Bound lengths before
reading/hashing. Recomputing from a caller-supplied unrelated generation is invalid:
the verifier must bind the opened directory, selector and generation argument.

## 4. Preparation, activation and restart

Create the canonical generation directory exclusively, without replacing any
existing name. Partial unselected directories remain evidence and grant nothing.
Construct and sync immutable base plus initial runtime files. Snapshot kind seeds
the runtime snapshot with a separately stored byte-identical copy of the base;
genesis has no snapshot and initially requires exact zero hard state and empty
WAL. These genesis checks apply at creation, NOT after normal operation advances.

Before sealing/selecting, validate the complete runtime tuple using existing
recovery rules: no foreign/unsupported bytes, term covering snapshot/WAL terms,
compatible contiguous retained history, all unknown-commit suffix records retained,
and commit covered by verified history. Do not set commit to the copied tail.
Snapshot committed coverage may exceed an older metadata commit checkpoint.
Snapshot installation additionally needs its existing source-authority and result-
obligation checks; a seal is not a substitute for them.

Write the seal only after these prerequisites are durable, sync it and its
directory/parent. Then execute the approved selector temp/write/file-sync/rename/
directory-sync/recheck protocol. After a successful rename but failed sync, stop;
do not resume the old runtime as though selection never happened.

On EVERY restart, verify immutable base and current mutable runtime independently.
For a snapshot base at S, require a valid runtime snapshot at or above S; at the
same S require matching term and canonical state digest. Missing/corrupt/older
runtime snapshot refuses in this MVP; do not silently recreate it from the base.
Above S, existing snapshot/WAL/metadata/catalog consistency still applies. This
floor prevents recovery below the installed base; it is not a general complete-
directory rollback detector. A genesis base permits normal later snapshots.
No fallback to another generation, temp selector, or largest directory number.

## 5. Bootstrap/migration and explicit costs

Fresh generation-aware bootstrap creates initial runtime hard state, then the
immutable root identity, the seal and selector in durable dependency order before
returning an owning token. Root has no second mutable metadata copy. An interrupted
initialization stays stopped for diagnosis; no implicit promotion of a partial
generation or unselected old root. Exact init cut tests must pin these states.

For MVP, support only genuinely fresh deployment bootstrap and recovery of valid
selected roots. Do not ship an automatic old-layout migration tool now. Existing
test/lab evidence remains preserved; participating histories needing migration
remain stopped until a separately reviewed offline copy/admission procedure is
available. New bootstrap is not a way around lost-voter repair.

Costs: one extra immutable snapshot copy (<=65,587 bytes) plus 40 seal bytes per
generation; full staging also retains its WAL/metadata and previous generation,
so these numbers are NOT total disk/RAM budgets. Sync/validation cost occurs at
preparation/recovery, not every order. No automatic cleanup. Missing runtime
snapshot or failed initialization sacrifices availability instead of inventing
another repair transaction. F6 must keep live publication I/O off the Raft owner.

## 6. Required evidence and scope boundary

TDD the actual selected-startup path: untouched old generation on failed prepare;
wrong root/voter/generation/base/length refused before writable open; publication
cuts and EIO/short-write/directory-sync failures; root/name substitution; missing
runtime snapshot and below-base recovery; stale selector/completion refusal;
unknown suffix and READY/RESERVED obligations retained during live install.
Use independently authored durable images, not process-exit alone.

CP-032 adds only a transitional guard to the current old-layout startup: any
selector/temp/generation entry must refuse rather than be ignored. Healthy roots
with no generation artifacts keep current behavior until the new startup path is
implemented. Bootstrap retains its existing generic unknown-content refusal;
the new dedicated error applies to recovery and ownership-token revalidation.
All checks assume the existing trusted directory and cooperative lock ownership,
not protection against a same-user process ignoring the lock and modifying files.
This guard is not generation-aware startup, publication or F5 closure.

Approved as ONE integration choice: this exact 40-byte seal/digest,
fixed layout and root-owned startup direction, runtime snapshot floor and explicit
refusal policy, and deferral of automatic migration. Archive coverage, transport,
repair and async contracts remain outside this document.

## CP-034 implementation status

Initial empty-root genesis publication and final-selector cold recovery now use
the approved layout. Root, generations and runtime locks stay with the owning
token; existing stores receive its selected runtime path. Snapshot-base floor,
exact stale-selection validation and diagnostic-only temporary inspection are
implemented. Full history validation remains in the kernel before writable WAL
open; the token is not serving authority. Recovery may re-sync existing files
before that semantic admission, without rewriting them.

The durability model covers genesis publication; valid snapshot-base fixtures
exercise cold recovery, not a live install implementation. Nonempty interrupted
initialization/legacy migration remain explicitly unavailable. Next is actual
snapshot-generation installation, including unknown suffix and result obligations;
the complete required evidence in section 6 is not yet satisfied. Evidence:
`evidence/f5_generation_startup_2026-09-23/README.md`.

## CP-033 implementation status (historical)

Exact seal codec and read-only on-disk immutable-base verification are implemented
and tested. Full snapshot bytes and semantic catalog validation are required;
successful inspection is not startup authority. Generation preparation, selector
publication, selected-runtime startup, runtime snapshot floor and their durable-cut
tests remain to implement under this approval. CP-032's refusal guard is still
active. Evidence: `evidence/f5_generation_base_2026-09-23/README.md`.
