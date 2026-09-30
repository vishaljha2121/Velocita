# F5 bound history/peer frame v1 — approved exact bytes

2026-09-22. **APPROVED by the owner after CP-018; implementation in progress.** The six
identity-binding semantics are already approved. This document makes one limited
part concrete: common framing and local metadata/WAL/snapshot/ordinary-peer bodies.
It does NOT freeze archive/install transaction records or asynchronous Raft fences.
No economic command/result, TradingState, F4 marker or V1 bytes change.

CP-020 implementation boundary: common/ordinary-peer framing and sections 2–4
typed local storage/bootstrap/recovery publication are implemented and host-tested.
This does not close whole-node limits, install/archive protocols or target gates.
See `evidence/f5_local_history_2026-09-22/README.md` for tests and explicit limits.

## 1. Recommendation and cost

Use one explicitly typed, length-delimited V2 bound frame for these objects.
Header is 56 bytes, trailing integrity digest 32 bytes: **88 bytes of wrapper**.
The identity fields are the previously discussed 32-byte manifest + 4-byte local
voter slot; those 36 bytes were never the entire envelope overhead.

One bounded verifier avoids separate subtly different lineage checks for each
artifact. Cost: larger metadata/record frames and SHA-256 over framed bytes. The
alternative of specialized minimum-size headers with CRC32 would be smaller;
recommend this uniform form for MVP correctness/review simplicity. SHA-256 is
already used in canonical V2 codecs; it detects equality/integrity, not keyed
authentication or durability. Its runtime cost still needs target measurement.

All integers little-endian. No native-struct dump or implicit padding.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | literal ASCII `V2BF` |
| 4 | 2 | bound-frame schema, exactly 1 |
| 6 | 2 | kind: 1 local hard state; 2 local WAL record; 3 local snapshot; 4 ordinary peer message |
| 8 | 4 | total length, exactly `88 + payload_length` |
| 12 | 4 | payload length |
| 16 | 32 | nonzero shared manifest digest from verified startup |
| 48 | 4 | signed local owner: positive local voter for kinds 1–3; exactly zero for kind 4 |
| 52 | 4 | reserved, exactly zero |
| 56 | P | kind-specific payload |
| 56+P | 32 | SHA-256 described below |

Digest = SHA256(ASCII(`velocita.v2.bound-frame.v1`) || bytes[0,56+P)). Literal
is exactly 26 ASCII bytes with no NUL/newline/length prefix. Digests use raw bytes,
not hexadecimal text; compare all 32 bytes. Check P against its kind's bound
before checked length addition; require input.size() == total exactly.
Unknown kinds/schemas refuse. Kinds 5+ are NOT reserved as accepted objects;
portable snapshot/archive/install schemas will be separately proposed.

Decoder takes an expected kind and verified context, never trusts the frame to
choose its storage operation. Check fixed header, length arithmetic and per-kind
maxima before copying/allocating; verify integrity, manifest and owner before
publishing output or invoking a mutable consumer. Non-success leaves destination
unchanged. A bounded view may reference only immutable caller-owned input whose
lifetime is explicit; queued work must own bytes. A transport label is not a
verified object and a public raw-peer receive overload must not bypass admission.

## 2. Local hard state — kind 1

Payload is exactly 24 bytes, total frame **112 bytes**:

- payload +0: term i64, nonnegative;
- +8: voted_for i32, -1 or a positive configured voter;
- +12: reserved u32 zero;
- +16: commit_index i64, nonnegative.

Term 0 requires voted_for=-1 and commit_index=0. Recovery separately checks commit
against the selected recoverable log/snapshot boundary. Keep the current filename
`v2-metadata.bin`; existing old 36-byte metadata is unsupported in the new bound
lineage, not converted. Atomic temp/file-sync/rename/directory-sync remains the
provisionally accepted publication mechanism; no sync weakening.

**Approved initialization ordering change:** bootstrap durably publishes bound
zero hard state FIRST; immutable identity is published LAST as the initialization
commit record. Only then issue startup ownership usable by a kernel. Existing
CP-016 identity-first initialization is not sufficient once metadata is mandatory.
Identity bytes stay unchanged; the cross-file ordering is a new approval choice.

Under the same exclusive directory ownership, validate expected identity/catalog,
then publish `v2-metadata.bin.tmp` through checked write/file-sync/rename/directory-
sync with exactly (term0, voted_for=-1, commit0). Next publish the identity through
its existing temp/file-sync/rename/directory-sync protocol and sync the parent as
needed for leaf creation. No WAL, snapshot, Raft instance or participation before
the final identity and initial metadata are both durable. No new readiness file
is necessary: the last-published identity fills that role for initial bootstrap.

Explicit bootstrap retry table (all present records must match expected identity
and local node; unknown files or partial/mismatching/ambiguous temps refuse):

| Existing owned artifacts | Explicit bootstrap action |
|---|---|
| Empty leaf | Publish zero metadata, then identity. |
| Complete exact zero metadata temp only | Finish metadata publication/sync, then identity. |
| Complete exact zero metadata final; identity absent or complete exact identity temp | Re-establish metadata durability, finish identity publication. |
| Matching identity final + exact zero metadata final; no temps/history | Re-establish both files/directory/parent durability; exact bootstrap retry may return. |
| Final identity without complete metadata | Refuse; never recreate voting memory from identity. |
| Nonzero metadata, WAL, snapshot, unknown entry or ambiguous temp/final pair | Refuse bootstrap; use validated recovery or operator diagnosis, never reset. |

Recovery NEVER resumes genesis by default: it requires final identity and final
bound metadata. Absent/empty/corrupt metadata is not a default tuple. An identity-
absent partial genesis needs an explicit bootstrap retry under the table. Recovery
with a supported atomic-replacement metadata temp must follow the selected final
metadata, preserve the temp, and validate any recognized temp lineage; it must not
promote a temp or guess a later vote. Detailed cut tests must cover every boundary.

These rules distinguish crash windows under the new publication protocol, not
malicious deletion/complete rollback or authority to reset a used voter. Such
rollback cannot be detected solely from the remaining local public records.

## 3. Local WAL records — kind 2

Payload = 24-byte record prefix followed by unchanged canonical command or F4
marker bytes; total **112 + canonical_payload_bytes**:

- +0: index u64, positive and representable by the Raft index type;
- +8: term i64, positive;
- +16: entry_id i32, existing non-economic Raft hint;
- +20: entry_type i32, only existing normal command or approved authority-marker
  type accepted; inner canonical codecs and marker domain must also validate.

Current directly measured max canonical command is 1,789 bytes; F4 marker is
186 bytes. Assert max(command, marker)==1,789. This gives max bound
record **1,901 bytes**. Existing 4,096-byte block with 32-byte header has 4,064
payload bytes: two max records use 3,802 bytes and still fit. Assert this in code.
Do not increase block size, arena capacity or command budget implicitly.

Keep local block filename `v2-wal-blocks.bin`. Propose block framing version 2
with the existing 32-byte shape/CRC/4 KiB stride, containing ONLY bound records:

| Offset | Bytes | Value |
|---:|---:|---|
| 0 | 4 | ASCII `VWB2` (same magic, explicit version distinguishes format) |
| 4 | 2 | version 2 |
| 6 | 2 | reserved zero |
| 8 | 8 | generation u64, positive |
| 16 | 8 | sequence u64, positive |
| 24 | 4 | used payload length u32, 1..4,064 |
| 28 | 4 | existing CRC32 algorithm over 32-byte header with these four bytes zero plus used payload |
| 32 | used length | complete bound records, exact consumption, no record split |
| remainder | to 4,096 | zero padding; excluded from CRC and required canonical on write |

All-zero complete blocks represent preallocation/end, not a valid framed block.
They do not authorize selecting a different WAL. A v2 complete valid block yields
records; recognizable v1/unknown version is typed incompatible; malformed v2 is
corruption/incomplete, not absence. Recovery must not classify version mismatch
as an ordinary tail. Index continuity and the recoverable committed prefix must
be checked independently of block generation/sequence; arena generations are not
cluster rollback counters and can restart across process lifetime/rewrite.
Version 1 and unknown versions are incompatible, not ordinary torn-tail endings.
Zero preallocation remains non-history; partially written/invalid blocks require
the declared valid-prefix rules, never scanning past damage to invent continuity.
All complete records selected for recovery must match local owner and lineage,
including uncommitted and snapshot-covered ones. Foreign frame errors must remain
typed through block decoding and cannot become an empty vector.

Flat `v2-raft.wal` presence in a fresh bound directory is an incompatible legacy
source, even if empty. Do not prefer, merge or delete it. V1's record codec and
old standalone evidence readers remain unchanged; new V2 live recovery does not
fall back to them. Rewrites re-encode verified records with the same binding and
publish atomically; fresh generations/install selection still need reviewed rules.

## 4. Local snapshot — kind 3

Payload = included_index u64 at +0, included_term i64 at +8, then unchanged
canonical state snapshot bytes. Included index/term positive and representable;
state/catalog validation and snapshot/suffix consistency remain mandatory.
Same `v2-snapshot.bin` filename, new bound framing. Old framing is unsupported,
not silently treated as a corrupt optional snapshot.

Current canonical maximum is **65,483 bytes**; full bound local snapshot is
**65,587 bytes**, and a bounded file reader needs **65,588 bytes** including one
oversize lookahead. This is 51 bytes over 64 KiB for the FILE, not an enlargement
of canonical state or TradingState. Approve this explicit outer bound rather than
clamping valid state or quietly overrunning a 64 KiB transport buffer. Portable
snapshot/chunk objects remain a separate contract; do not send local wrappers as
target snapshots or copy the sender's metadata.

## 5. Ordinary peer messages — kind 4

Payload is the existing canonical peer body INCLUDING its existing magic/version,
type and sender/recipient fields, carried only INSIDE the V2 bound frame. Reusing
the inner body avoids changing V1, but the V2 decoder must impose V2 limits before
delegating any vector allocations; the old decoder alone is not bounded enough.

Propose <=16 entries per AppendEntries, each <=1,789 canonical bytes, and **32 KiB
maximum entire bound frame**. At maximum, inner frame is
`52 + 16*(20+1789) = 28,996` bytes; outer total **29,084**, within 32 KiB. One manifest
per message/batch, not per canonical command. Validate each entry's existing
codec/type and any F4 domain as well. Control/empty messages retain their exact
inner lengths and canonical boolean/reserved-field rules. Enforce route, member
and RequestVote candidate/sender consistency before Raft. No trailing bytes.

Both requests AND replies must pass, including empty heartbeats and positive
acks. Identity/authenticated-session checks precede term/vote/log/commit mutation.
This frame supplies lineage equality, NOT sender authentication; deployed peer
transport still needs immutable authenticated sender binding. No TLS/credential
protocol is invented by this byte sheet.

## 6. TDD and approval boundary

Current standalone history/peer/missing-hard-state REDs belong to
`tests/v2_history_identity_red.cpp`; they must become exact typed-refusal tests
with healthy positive counterparts, not broad any-exception PASS tests.
Independent literal byte/hash vectors are required in addition to round trips.
Test every short/oversized buffer, unknown kind/version, lengths, reserved fields,
wrong scope/owner/manifest, corrupt integrity, mutated log coordinates and output
preservation. Then verify integration refuses before writes/Raft, with isolated
comparison/error-classification mutants.

Owner choices in this sheet: common 56+32 wrapper and digest definition; these
four kind payloads; 112/1,901/65,587-byte local bounds; block version 2/no flat
fallback; 16-entry/32 KiB peer limits; mandatory initialized hard state and explicit
bootstrap/recover handling. Exact install/archive framing, rollback admission,
whole-node credits and asynchronous durability protocols remain separate. This
proposal alone cannot close F5/F6 or authorize live/target operations.
