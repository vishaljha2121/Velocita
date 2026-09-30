# F5 portable archive segment v1 — exact byte review

2026-09-23. **APPROVED after CP-027; standalone implementation at CP-028.** The owner approved CP-026's
offline fenced repair and immutable/no-auto-prune archive policy. This sheet is
the first implementation-sized byte contract beneath that approval. It covers
the portable immutable segment codec, not archive deletion permission, global
disk quotas, export receipts, snapshot transfer/selection or voting admission.

## 1. Recommendation

Use a separate portable segment format with a 96-byte header, ordered typed
records, and a 32-byte segment digest. Each record has a 32-byte header, the
UNCHANGED canonical command/authority-marker bytes, and a 32-byte record digest.
Both digests bind the already-approved shared cluster identity. No economic
command/result, TradingState, local WAL, V1 or peer-message byte changes.

Approved: **4,194,304 bytes maximum per complete segment and 8,192 records maximum**.
The byte limit alone does not bound record work; the record limit alone does not
bound bytes. These are per-object codec ceilings, not a measured retention period
or approval of the older 1-GiB/16-MiB quota candidates. A caller cannot omit records
to fit: split into contiguous segments. Empty segments are invalid.
These are historical records per archive object, NOT a change to live-order or
admission capacity. Four 4-MiB segments can hold one full 8,192-record retained-log
batch even at maximum payload. This is a bounded cold-I/O starting point, not a
claim that target throughput or run duration has validated the chosen size.

Do not extend `BoundFrameKind` or enlarge `BoundFrameScratch`: that scratch already
serves hot peer/local paths. A multi-megabyte archive must not inflate every node's
ordinary decoding workspace. The separate magic/schema makes accidental use as
a local recovery file an explicit refusal.

## 2. Segment header and footer

Integers are little-endian; i32/i64 carry exact two's-complement bit patterns.
Offsets are wire offsets, NEVER native C++ struct offsets. No implicit padding.

| Offset | Bytes | Type/tag | Meaning |
|---:|---:|---|---|
| 0 | 4 | ASCII `V2AS` | Archive segment, not a local WAL/snapshot frame. |
| 4 | 2 | u16 = 1 | Segment schema; also defines the record layout below. |
| 6 | 2 | u16 = 96 | Exact header size. |
| 8 | 4 | u32 | Exact total file bytes, including the trailing digest. |
| 12 | 4 | u32 | Record count, 1..8,192. |
| 16 | 8 | u64 | First included log index, positive and <= INT64_MAX. |
| 24 | 8 | u64 | Last included log index, same range; contiguous count must agree. |
| 32 | 8 | i64 | Term at first_index-1; 0 only when first_index is 1. |
| 40 | 8 | i64 | First record term, positive. |
| 48 | 8 | i64 | Last record term, positive. |
| 56 | 32 | Digest256 | Existing `VerifiedStaticIdentity::manifest_digest()`. |
| 88 | 4 | u32 = 0 | Flags; nonzero is unsupported, not silently ignored. |
| 92 | 4 | u32 = 0 | Reserved; must be zero. |
| 96 | variable | records | Exactly record_count complete records, in index order. |
| total_bytes-32 | 32 | Digest256 | Domain-separated segment digest. |

The existing shared manifest digest binds incarnation, catalog root, all existing
semantic/schema epochs, voter list, durability mode and deployment mode. It does
not include the local voter ID. Require equality to a separately verified expected
identity. Catalog root and authority domain are derived/checked from that identity;
repeating them in this header would add redundant fields with more consistency
checks, not additional lineage protection. Keep the identity artifact itself in
the retained dependency bundle; a digest is not a replacement for its bytes.

`segment_digest = SHA256(ASCII("velocita.v2.archive-segment.v1") || header96 ||
complete_record_region)`. The ASCII domain is exactly 30 bytes, no terminator;
the record region includes every record digest. The final segment digest is not
part of its own input. Hash all defined bytes, not a projection of selected fields.

## 3. Record header and footer

Offsets restart at each record. Record length is derived as `64 + payload_bytes`;
there is no second independently mutable record-length or record-schema field.

| Offset | Bytes | Type/tag | Meaning |
|---:|---:|---|---|
| 0 | 8 | u64 | Full log index; exactly first_index + ordinal. |
| 8 | 8 | i64 | Positive log-entry term. |
| 16 | 4 | i32 | Existing Raft entry ID preserved bit-for-bit. |
| 20 | 4 | i32 | Existing type: 0 command or 101 authority marker. |
| 24 | 4 | u32 | Canonical payload length, <=1,789; inner codec supplies stricter bounds. |
| 28 | 4 | u32 = 0 | Reserved; must be zero. |
| 32 | payload_bytes | exact bytes | Existing canonical schema-8 command or schema-1 marker. |
| 32+payload_bytes | 32 | Digest256 | Record digest. |

`record_digest = SHA256(ASCII("velocita.v2.archive-record.v1") ||
shared_manifest_digest32 || record_header32 || exact_payload)`.
Domain is exactly 29 bytes, no terminator. The lineage prefix means a standalone
record digest cannot be used as an overlap proof for a different cluster. A record
has the same digest regardless of which voter published it or where segment cuts
occurred. Record digests supplement, not replace, whole-segment validation.

Keep entry IDs even though they are not economic identities; inventing a new ID
constraint or reconstructing IDs would alter archived Raft history. Preserve all
payload bytes, not just a decoded command re-encoded according to a future codec.

## 4. Validation and authority boundaries

Before any visitor sees records: validate header/magic/schema/zeros, length and
count caps, expected lineage, both digest levels, exact consumption, typed payloads,
and every coordinate. Validate positive representable indices, last>=first,
`last-first+1 == count` without overflowing, exact ordinal indices, nondecreasing
positive terms, endpoint terms, and predecessor_term <= first_term. For first=1,
predecessor term is exactly0; otherwise it is positive. Never resynchronize past
an invalid record or turn a truncated archive into a successful shorter one.

Type0 uses the strict existing command decoder. Type101 must have the exact186
marker bytes and pass the current marker codec plus expected authority-domain
binding, matching current WAL admission. Preserve its requested catalog root even
if it differs from the installed catalog: the marker may legitimately yield no
authority proof. An archive must not discard evidence of such a failed request.
Its incarnation identifies a gateway process, NOT the cluster incarnation; do not
require those two independent identities to be equal. Unsupported types/schemas
refuse; they are not skipped as if
non-economic meant irrelevant. Command identity, marker incarnation/request,
effect binding, versions and reserved bytes remain governed by their existing
codecs. Refusal leaves caller-visible decoded metadata unchanged.

**Successful decoding is NOT proof of commitment, durability, independent backup
or permission to reclaim.** A checksum is not a signature or a quorum certificate.
The later archive owner must publish only a verified committed prefix, establish
durable files/directory and dependency coverage, and produce a generation-bound
coverage capability before compaction may consume it. This codec deliberately has
no `is_committed` or `allow_delete` flag that a file can assert about itself.

There is no predecessor SEGMENT digest. Segment cuts may differ across publishers.
A future coverage verifier must prove contiguous ranges, correct boundary terms
and identical record tuples/payloads wherever ranges overlap; reject gaps/forks.
The predecessor term here supports that check but cannot prove it by itself.
No automatic archive pruning or retrospective adoption of arbitrary files follows.

## 5. Audit dependencies and what the segment does not store

Archive every committed entry, including commands that yielded typed refusals and
replication-only authority markers. Preserve the complete ordered input history,
not just orders or state-changing successes. Raw log storage does not contain
ApplyResult records. A historical refusal/result is reconstructed by deterministic
replay from a verified preceding state or genesis with the matching semantics;
this codec does not claim to store a directly emitted result byte-for-byte.

PublishPolicy and StageExecutablePolicy carry their artifact inputs. The execution
catalog installation command carries only a root. Retain and verify the canonical
catalog artifact separately by that root, along with the shared identity artifact
and any other external immutable dependency actually required by replay. A valid
segment without those artifacts has byte integrity, not sufficient audit coverage.
Do not add an unrelated model directory or credentials to the dependency bundle.

## 6. C++ ownership, work and memory

Recommend explicit byte packing/unpacking and a synchronous record visitor, not
`reinterpret_cast<ArchiveHeader*>`, native padding, a `vector<WalRecord>` or a
descriptor per record. A record span borrows immutable input only for that callback;
it cannot outlive the buffer/window or authorize deletion. Decoded metadata is a
value. The durable archive owner, not this pure codec, eventually owns any coverage
token. Mutable source files or buffers invalidate a prior verification.

Use incremental SHA-256 with bounded state; preserve existing one-shot results.
Validate then consume immutable input in two passes. For file-backed streaming,
hold a stable immutable object/version across both passes. A later read or visitor
failure does not roll back an already-consumed prefix: each accepted operation
must retain its own normal durable/result obligations. This is not an atomic
multi-command transaction disguised as a two-pass parser.

Arithmetic independently recomputed by main:

- Segment framing: 96+32 =128 bytes, outside records.
- Maximum record:32+1789+32 =1,853 bytes; record hash input max1,882 bytes.
- At maximum payload, floor((4,194,304-128)/1,853) =2,263 records.
- 8,192 markers occupy128+8,192*(32+186+32) =2,048,128 bytes; the record-count cap
  therefore matters independently of the byte cap.
- A 32-byte descriptor per allowed record would cost262,144 bytes; the recommended
  streaming visitor avoids that array entirely. Input/output buffers, typed command
  scratch and SHA state still count; report actual `sizeof` and peak allocations
  during implementation. Do not claim this arithmetic is a process RSS bound.

Validation/consumption work is O(total bytes + record count); scratch need not grow
with segment length. Publication/hashing/scanning belongs on the cold persistence/
archive side, not in deterministic apply or an unbounded owner turn. This adds disk
and hashing work; it must not add a4-MiB copy to each order admission. Streaming
hash support needs known-vector and varied-chunk tests before use.

## 7. Alternatives and implementation exit

- Existing bound WAL frames verbatim: preserve them as source evidence if needed,
  but not portable canonical content; their owner ID/digest differ across voters.
- A repeated192-byte header/40-byte record prefix was considered. Main recommends
  the smaller layout because shared identity already binds the repeated metadata,
  and segment schema already defines record layout. This removes redundancy, not
  checks on provenance or canonical payloads.
- Only a segment digest saves32 bytes/record, but forces whole-segment context for
  record/overlap verification. Keep both levels for bounded catch-up and coverage.
- Predecessor-segment chaining couples correctness to segment-cut coordination;
  prefer independent segments plus an explicitly verified coverage catalog.
- Enlarging the generic bound codec couples cold archive limits to hot decoding
  RAM. Keep a separate codec and leave approved existing formats untouched.

After owner approval: independent golden vectors; one-/multi-record and mixed
marker/refusal sequences; every truncation boundary of short golden cases and
selected header/record/footer boundaries at maximum size; foreign identity; wrong
types/reserved/count/term/index; digest-only and range-check mutants; both caps;
caller-state/visitor unchanged on invalid input; immutable visitor lifetime and
bounded allocation checks. A component codec GREEN does NOT make the current
archive-loss RED GREEN: durable publication/coverage/reclamation integration
must follow under its own reviewed transaction and quota contract.

**Owner APPROVED:** this exact96/32-byte layout, both digests and the
4-MiB/8,192-record per-object caps for the standalone archive component. Global
retention/backlog quotas, snapshot/repair generation bytes and F6 async fencing
remain outside this approval; A/B policy approval is already recorded.

## 8. Implemented API and the remaining I/O boundary (CP-028)

`archive_segment.hpp` exposes a synchronous pull source for encoding and a
borrowed contiguous input span for decoding. The source returns one record view
at a time, valid until its next call; both passes must see the same immutable
records. This supports a retained-log lookup or reusable staging buffer without
an 8,192-element descriptor array. Output must not alias source payloads. These
are caller lifetime/stability preconditions, not checksums that detect arbitrary
concurrent mutation. No borrowed view may cross a thread/queue boundary.

The encoder preflights the complete stable source and exact required size before
writing. Preflight errors and insufficient output space leave output unchanged.
A source failure on the second pass returns failure and zero publishable bytes;
partial output may exist and must be discarded. The decoder validates all records
before the first visitor, and publishes metadata only after every visitor succeeds.
A later visitor refusal does not undo the previously consumed prefix.

This component does not open, map or stream files. Decoding currently requires
the caller to provide the entire immutable object (at most 4 MiB); its scratch
does not grow with that object. A future file-backed scanner must pin a stable
object/version across validation and consumption. It must not reopen a mutable
path between passes and treat the earlier verification as permission to consume
different bytes. No file-streaming implementation or lifetime enforcement is
claimed by this span API.

Host native sizes with cache-line ABI 64: SHA owner 112 bytes, codec scratch 2,120,
record view 40, pull source 24, metadata 80. The scratch contains the existing
command decoder candidate. A separate decoded command and fixed hashing work
arrays also occupy stack; these sizes exclude stack frames, caller buffers and
any publication owner's future allocations. Codec allocation probes exclude
fixture allocation and check the actual encode/decode calls. No hot reducer,
admission, WAL, snapshot or peer wire path invokes this new archive codec.

Evidence, independent oracles, mutation provenance and remaining integration
REDs: `evidence/f5_archive_codec_2026-09-23/README.md`. In particular, component
verification grants no compaction capability and does not close F5.
