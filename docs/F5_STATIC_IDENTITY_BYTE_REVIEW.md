# F5: immutable startup binding — exact byte review

2026-09-22. **APPROVED BYTES — owner approved after CP-014.** Approval includes
this concrete layout, digest definitions, modes/epochs and three-voter limit.
This is the first F5 increment, not closure of recovery, boundedness or archival
guarantees. No economic command, TradingState or schema-8 state payload changes.

CP-015 implemented the codec and startup owner; CP-016 integrated the token as
the kernel's only construction boundary and closed startup membership drift on
the host. See `evidence/f5_kernel_startup_2026-09-22/README.md`. Foreign-history
and peer binding remain open; `F5_HISTORY_PEER_IDENTITY_REVIEW.md` is the next
semantic review (owner-approved after CP-017), not an approved encoding.

## 1. Approved decision

Recommend ONE immutable local identity file with logically separate shared and
local fields. Hash only the shared fields for cluster identity; protect the whole
file, including the local node ID, with a separate trailing SHA-256. Publish the
whole file atomically before opening history writable.

Why one file: two files create a manifest-published/local-binding-missing crash
state without adding identity protection. Logical separation does not require two
filesystem objects. A peer receives shared identity, not a copy of this local file.
Cost: provisioning/export tooling must explicitly extract the shared fields;
copying one voter's whole file to another is invalid. An independent reviewer
challenged this against the two-file alternative and agreed with the narrower
one-file proposal. Neither alternative authenticates a dishonest operator.

Owner approval covers:

1. The exact frame and digest definitions below, including a 16-byte nonzero,
   owner-provisioned deployment incarnation, the intended catalog root's 32-byte
   placement/nonzero rule, and explicit schema/epoch/mode values.
2. A maximum of THREE static voters in this MVP implementation. Codec supports
   one, two and three; production pilot admission requires exactly two. This is
   a bounded supported surface, not a hard-coded two-vote quorum. No auto-shrink.
3. Explicit bootstrap/recover, immutable local binding and fresh pre-live lineage;
   no automatic old-directory adoption. This elaborates the already approved
   policy; the exact atomic single-file publication is the new choice.

Other numeric runtime budgets, storage/peer envelopes and asynchronous fencing
remain separate reviews. Approval here must not be interpreted as approving them.

## 2. Canonical file

Filename: `v2-identity.bin`. Integers are little-endian. Reserved fields
must be zero. Voter IDs are positive signed 32-bit integers, strictly increasing,
unique; reject noncanonical input rather than silently sorting accepted bytes.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | literal ASCII `V2ID` |
| 4 | 4 | total frame length, exactly `112 + 4*N` |
| 8 | 2 | identity framing schema = 1 |
| 10 | 2 | reserved = 0 |
| 12 | 16 | shared deployment incarnation, not all zero |
| 28 | 32 | intended immutable execution catalog SHA-256 root, not all zero |
| 60 | 2 | state snapshot schema = 8 |
| 62 | 2 | command/result schema = 8 |
| 64 | 2 | authority-marker schema = 1 |
| 66 | 2 | execution-catalog schema = 1 |
| 68 | 2 | replication semantics epoch = 1 |
| 70 | 2 | storage lineage epoch = 1 |
| 72 | 1 | quorum durability mode = 1: voter entry acknowledgement counts toward commit only after its local durability fence |
| 73 | 1 | deployment mode: 1 = laboratory, 2 = two-host pilot |
| 74 | 2 | voter count `N`, 1 through 3 |
| 76 | `4*N` | shared ordered voter IDs |
| `76 + 4*N` | 4 | LOCAL node ID, must be in that voter set |
| `80 + 4*N` | 32 | SHA-256 of all preceding file bytes, excluding this digest |

One/two/three-voter frames are 116/120/124 bytes. Decoder reads no more than
124 bytes plus one lookahead and refuses a length mismatch or trailing bytes.
No `reinterpret_cast`/native-struct serialization: alignment and padding are not
the byte contract. Codec failure leaves the caller's output unchanged.

These semantic epochs describe compatibility, NOT executable authenticity or
the Raft term. Epoch 1 will name the reviewed identity-gated implementation;
until its required integrations exist, no live-use readiness is implied. Unknown
epochs/modes/schemas refuse rather than fall back. A future compatible build need
not change identity; an incompatible protocol/storage interpretation does.

Mode 2 is structurally valid only with two voters. Laboratory mode supports all
three counts so one-voter unit tests and three-voter failure tests remain useful.
Production admission must reject laboratory mode; a CLI must not reinterpret it
as pilot mode. A mode change is a fresh reviewed lineage, not an in-place toggle.

## 3. Exact digest domains and ownership

Let `S` be the exact shared byte span `[12, 76 + 4*N)` above; neither local ID,
framing length nor file checksum belongs to it. Identity schema 1 fixes its
interpretation and is named in the domain separator.
Validate magic, exact length, frame schema, reserved fields and whole-file digest
before constructing any verified shared identity. In particular, never accept a
v1 digest computed from an unvalidated future-version frame with the same `S`.

- `manifest_digest = SHA256(ASCII("velocita.v2.cluster-manifest.v1") || S)`.
- `authority_domain = SHA256(ASCII("velocita.v2.authority-domain.v1") || manifest_digest)`.
- ASCII literals have no terminating NUL, length prefix or newline. Digest inputs
  use the 32 raw digest bytes, not hexadecimal strings. The file checksum is the
  separate unprefixed SHA-256 specified in the table.

The shared digest/domain match across NUC/Pi; complete-file checksums differ due
to local IDs. Neither domain is accepted as a second independently configurable
value. F4's approved type-101/schema-1 marker remains unchanged: its existing
32-byte authority-domain field must equal the derived value.

The incarnation is generated once outside the deterministic reducer using a
secure random source and provisioned identically on both machines. Do not derive
it from a hostname, IP, term, local ID or build hash. This avoids reusing identity
across unrelated deployments while keeping replay deterministic. It is public
identity, not a signing key. Hashes detect corruption/equality, not authorization.

Catalog availability is checked before participation: the supplied verified
artifact must hash to the intended root. TradingState may initially have no
installed catalog; the existing replicated install must install exactly this
intended root. Rootless tests need explicit small test catalogs, not a production
zero-root bypass. This changes fixture setup and startup validation, not the
approved economic catalog encoding or command kind.

## 4. Bootstrap, recovery and failure order

Read-only preflight must happen BEFORE any side-effecting storage constructor.
Today MetadataStore and WriteAheadLog create directories in member initializers;
a check only in ReplicatedKernel's constructor body would already be too late.
Use an explicitly verified startup context/factory consumed by a private writable
constructor; do not leave a second public raw-configuration bypass.

Bootstrap and recovery have different authority:

- Recovery never creates missing identity, changes membership or guesses a local
  node ID. Missing/invalid/mismatched binding refuses without altering files.
  An absent directory is an error, not implicit single-voter bootstrap.
- Explicit bootstrap requires a new or verified empty exclusively owned directory.
  Publish the complete canonical record via owned temp, checked write, file sync,
  atomic rename and directory sync. Synchronize directory creation in its parent
  as necessary; syncing only a child directory does not prove its parent entry.
  Only then may metadata/WAL/snapshot state be opened writable.
- Exact bootstrap retry may confirm an already complete identical binding only
  when there is no history; starting an existing lineage uses recover explicitly.
  Different shared fields or local ID refuse, even if nothing traded yet.
- A crash before publication may leave an implementation-owned temp. Retry must
  validate the exact temp and absence of history before safely superseding it;
  unknown files or ambiguous leftovers refuse. No directory-wide cleanup.
- After rename but before directory sync, modeled loss may reveal no binding or
  the complete binding. Both retries follow the preceding rules; no partial
  identity becomes participation permission. No new risk before completed startup.
- Schema-8 legacy history without binding remains preserved prior evidence. Do
  not adopt it by signing today's startup arguments: that would certify the very
  membership drift this guard is intended to reject.

The final implementation must also exclude concurrent owners of one directory;
atomic publication alone does not prevent two processes racing validation. A
process-lifetime directory lock/exclusive ownership check is an integration
prerequisite, not a claim provided by the checksum. Its failure must precede
writable history access. No implicit lock-stealing or automatic membership repair.
Acquire ownership before the empty-directory/bootstrap check and retain it for
the process lifetime. Byte approval does not approve an unspecified lockfile:
creating one during refused recovery would violate unchanged-tree evidence.
The implementation review must choose a non-mutating recovery ownership mechanism
and cover missing-directory/bootstrap races before claiming this ordering complete.

## 5. What this stage DOES NOT prove

A correct sidecar cannot identify independently substituted same-schema WAL,
metadata or snapshots from another directory. Both the one-file and two-file
proposals have this limit: neither changes the bytes of those artifacts. This
stage is **immutable startup-configuration binding**, not cryptographically bound
durable history. A matching file also does not show that every peer was provisioned
correctly or that the caller is authenticated.

Before full F5/live acceptance, further reviewed framing must bind shared identity
to metadata, both WAL formats, local/portable snapshots, archive and installation
records. Peer requests AND replies must be identity-validated before any Raft
term/vote/log mutation. A handshake alone is insufficient if subsequent frames
are not bound to its authenticated immutable session. Portable snapshots exclude
the source node's local binding; the target verifies its own local ID separately.

Old binaries can ignore a new sidecar. Therefore this file alone is NOT an
anti-rollback fence: executable admission/new framing and final gates must prevent
old code from reopening the new live lineage. Existing historical accepted tags
are not moved or erased. This limitation must remain visible in acceptance notes.

## 6. RED/GREEN plan after approval

1. Independent literal-byte golden vector and maximum-length refusal tests.
   Round-trip alone can share an encoder/decoder bug. Cover every enum/reserved
   field, ID ordering/zero/duplicate/local mismatch, digest and trailing bytes.
2. Same shared fields with different valid local IDs produce identical shared
   digest and authority domain, different full-file checksums. Mutate each shared
   field and verify the binding changes or is refused.
3. Convert the CP-013 three-voter-to-one-voter recovery diagnostic into exact
   refusal plus byte-for-byte unchanged tree. Preserve a healthy same-identity
   restart and explicit one/two/three-voter laboratory fixtures.
4. Prove failure precedes mkdir/open-writable/Raft mutation, including missing
   catalog, absent binding, wrong local ID, attempted legacy adoption and a
   second local owner. Exercise bootstrap publication cuts and exact retries.
5. Keep cross-artifact substitution and peer/snapshot lineage binding as explicit
   OPEN obligations; retain a failing diagnostic before implementing those next
   contracts. Do not rename a startup pass as full recovery acceptance.

All work is host-only until the frozen-candidate NUC/Pi gates. Availability cost:
an ambiguous identity/storage directory refuses to start instead of guessing.
For two voters, losing either voter still prevents new quorum-backed risk. Neither
operator impatience nor a healthy dashboard is permission to shrink that quorum.

## 7. CP-015 implementation choices and limits

Use a retained directory descriptor and nonblocking advisory flock, not a mutable
lockfile. Require local effective-UID ownership and no group/other directory write
permission. Startup I/O uses openat/renameat relative to that descriptor; require
an existing parent and create only the requested leaf. Trusted/cooperative local
owners and ancestor components are assumptions, not authentication or hostile
same-UID protection. A future pathname-based store can still race an operator's
directory replacement; token integration must address or explicitly bound that.

Complete canonical matching temporary records are resumable; partial records or
two candidate generations refuse and remain available for inspection. No cleanup
policy authorizes deleting an ambiguous bootstrap record automatically.

Recovery VALIDATION is read-only. A successful matching recovery then resyncs the
validated identity, its directory and parent before issuing ownership: a final
name alone may have come from a failed post-rename/pre-directory-sync bootstrap.
Without that fence, recovery could allow new history before identity publication
was durable. Refusal for mismatch still precedes sync and changes no names/bytes.
The added tests pin token suppression on interruption, not physical disk behavior;
startup-specific sync-sensitive modeling and target gates are still required.

## 8. CP-016 kernel integration

The raw/default ReplicatedKernel constructor is removed. The only construction
path consumes VerifiedIdentityStartup by value, rechecks ownership/name/identity
before any store construction and keeps it as the first member, destroyed last.
All static configuration derives from it. A catalog reference points into this
owned token, not the constructor parameter or caller. No second resident catalog
copy is needed. Member-order and failure-unwind tests protect lock lifetime.

This closes startup membership drift, not foreign-history substitution. Changed
path detection is a pre-open check, not atomic fencing of subsequent pathname
operations. Cooperative local ownership and stable trusted paths remain explicit
deployment assumptions. Refusal sacrifices startup availability instead of
adopting history or shrinking quorum. Evidence and migration/mutation details:
`evidence/f5_kernel_startup_2026-09-22/README.md`.
