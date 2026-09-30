# F5: identity-bound history and peer traffic — contract review

2026-09-22. **SEMANTICS APPROVED by the owner after CP-017; not implemented.**
Approval covers the six recommendations: manifest reuse, local/portable split,
record-level WAL binding, all-message pre-Raft validation, typed mismatch refusal,
and block-WAL-only fresh bound deployments (no legacy-flat fallback). Exact bytes,
integrity algorithms, numerical budgets and concrete replacement/publication
protocols remain review boundaries. CP-016's startup bytes/token remain approved
and implemented. ADR 0001 and the two-voter fail-closed contract are unchanged.

## 1. The gap and the evidence

Startup currently proves that the directory's identity matches the configured
deployment. It does not prove that every history object inside that directory
belongs to it. A correct label on a folder does not identify substituted contents.

Source inspection at d44a4a9 plus the dirty CP-016 remediation tree:

- `src/v2_replication.cpp:40–76` derives configuration from the owned token, but
  passes only paths to storage and validates recovered typed payloads. A normal
  command's validity is not cluster identity. F4 markers already have their own
  derived authority-domain validation; that does not bind all other records.
- `include/velocita/storage.hpp:12–24` has term/vote/commit metadata and log
  index/term/type/payload, without a cluster or local-voter binding.
- `src/v2_snapshot_store.cpp:126` reads a state/index/term frame without lineage.
  Catalog equality cannot distinguish separate deployments using the same catalog.
- `include/velocita/wire.hpp` carries sender/recipient IDs and a payload but no
  deployment identity. `src/v2_replication.cpp:685–704` checks routes/member IDs,
  then invokes Raft. Another deployment can reuse the same small node numbers.
- `src/v2_wal_arena.cpp:481–513` stops on an invalid block and decodes record
  prefixes. A future identity mismatch must not be collapsed into this generic
  tail/fallback behavior.

These are inspected missing boundaries, not newly executed exploit reproductions.
Permanent current-tree RED traces must precede fixes. CP-016's earlier test results
remain historical evidence; this review does not rerun them or close more gates.

## 2. Recommendation A — reuse shared identity, keep purpose separation

Use the existing 32-byte `manifest_digest` as the history/peer lineage identifier.
It binds incarnation, intended catalog, voter set, mode and compatibility epochs.
Both machines derive the same value. Do not introduce a second configurable
cluster UUID or copy the complete 116–124-byte identity file into every record.

Keep `authority_domain` for the already-approved F4 marker/proof purpose; do not
rename it into a general storage identity. They describe related domains but have
different roles. IP, hostname, current leader/term and build hash are not lineage:
each can change without creating a new economic history.

An envelope must cover its identity, object kind/version, routing or log position,
length and payload with its defined integrity check. A label outside the checked
bytes is insufficient. Exact checksum/hash selection and offsets belong to the
next byte sheet; an unkeyed hash or CRC is NOT sender authentication.

Rejected: identity only in directory name/sidecar, or catalog root alone. The
former misses content substitution; the latter merges unrelated deployments.

## 3. Recommendation B — distinguish local storage from portable content

| Object | Proposed binding | Reason |
|---|---|---|
| Local Raft hard state | Manifest + local voter ID | A vote is a promise by one voter; another voter's term/vote tuple cannot replace it. |
| Each local WAL record in the selected format | Manifest + local voter ID | Reject foreign lineage and raw copies from another voter; cover index, term, entry type and payload together. |
| Local persisted snapshot wrapper | Manifest + local voter ID | Direct file substitution is not an approved snapshot installation transaction. |
| Portable snapshot / archive content | Manifest; producer may be separate provenance | Another voter must be able to import shared history without adopting the source's identity or vote. |
| Every peer request AND response | Manifest + existing sender/recipient route | Requests can change term/log; responses can produce quorum. Both are authoritative inputs. |
| Snapshot-install publication record | Manifest + target voter + exact selected objects | Identity alone cannot select a crash-consistent snapshot/suffix/hard-state generation. |

Portable import validates shared identity and content, then writes a fresh LOCAL
wrapper for the receiving voter through the reviewed install protocol. It never
copies the source voter's hard-state file. Local node binding adds 4 bytes beyond
the 32-byte manifest; neither field belongs in TradingState or economic commands.

The per-record WAL recommendation costs more than one file header, but one header
cannot detect a foreign valid record/block spliced after it. Binding each whole
block is a defensible cheaper alternative if no independently accepted unbound
records escape it. Recommend record-level binding initially because archive export,
rewriting and offline record inspection then have one independently checked unit.
The economic command inside remains unchanged; this is a storage record wrapper,
not a source-voter field in the replicated canonical command. A receiving voter
persists its own wrapper. Do not claim the extra redundancy proves freshness.
This choice favors simpler record provenance over best packing efficiency; revisit
only with measured packing pressure and equivalent splice/rewrite tests.
Empty/preallocated storage is not itself a history record; its initialization and
generation selection still require explicit rules in the format/publication review.

Rejected: put the local voter ID in the shared manifest or portable snapshot's
required destination identity. That would make NUC and Pi disagree on legitimate
shared history. Also rejected: accept arbitrary peer directories because the
manifest matches; file copying is not a crash-safe install protocol.

## 4. Recommendation C — validate before mutation, including replies

Peer admission must check fixed framing/version/length bounds, manifest, sender
membership, recipient, and payload/route consistency before any Raft callback,
term/vote change, log staging, commit advancement or authority proof. For example,
a RequestVote candidate must agree with the sender bound to that request. Empty
AppendEntries heartbeats and positive responses receive the same lineage checks.

Check the fixed identity prefix before attacker-controlled count/length allocation;
then decode into bounded owned storage. Checksum/hash work is linear in an already
bounded frame. Transport credentials/session identity must bind the claimed sender
before network deployment. Checking a public digest authenticates nobody. A hello
handshake alone is insufficient unless every subsequent frame is tied to that
immutable authenticated session; propose per-message lineage even with a session.

Use a V2-specific peer boundary; do not change V1's wire contract. The kernel must
not retain a public raw `PeerMessage` receive bypass analogous to the constructor
we just removed. A checked wrapper must own its payload or have a precisely scoped
immutable lifetime; borrowing a span from a recycled receive buffer is unsafe.
Constructing a C++ verified type proves only the validations its factory performs,
not remote authenticity or quorum commitment.

Bad/foreign peer input is refused without protocol mutation and with bounded
diagnostics; it is not permission to erase local history or shrink membership.
Do not let unauthenticated garbage permanently poison otherwise healthy local
storage. Deployment mismatch will cost availability until provisioning is fixed.

## 5. Recommendation D — mismatch is not corruption fallback

Recognized foreign, unsupported or unbound history must refuse with a distinct
typed reason before writable recovery. It is not an empty file, ignorable tail,
or permission to fall back to an older WAL/snapshot. Check all retained supported
sources, including snapshot-covered records and a flat WAL shadowed by block WAL.

**Recommend one active WAL format/generation in the new bound lineage.** Current
startup selects nonempty block history, otherwise flat history. That is not a
generation-selection protocol. New V2 already writes block WAL in `log_offer`;
the flat path is recovery/reclamation compatibility. For fresh bound deployments,
recommend block WAL only and explicit refusal if a legacy flat WAL is present,
not silent preference, merging or conversion. V1 and old evidence remain intact.
If retaining dual-format live recovery is desired, it needs an explicit durable
selection/overlap contract; supporting both is not just one more decoder. This
compatibility removal was approved with the six recommendations after CP-017;
it is not yet implemented. Exact framing still needs review.
Atomic rewrites and future snapshot installation still need crash-consistent
generation selection even after selecting one format.

Keep separate outcomes: absent; verified matching; recognized incompatible/
foreign; malformed or incomplete; I/O failure. Existing allowed torn-tail or
snapshot fallback must still prove a complete valid recovery prefix. It cannot
invent default term/vote, discard acknowledged history, or convert unreadable
storage to an empty node. A malformed checksum is not proof of harmless damage;
when safe recovery cannot be established, preserve files and refuse.

In particular, missing hard state with existing history needs an explicit refusal
test. Current `MetadataStore::load` maps empty input to a default tuple; startup
identity must not turn lost voting memory into a fresh voter. A fresh bootstrap
needs durable initial hard state before participation, with interrupted bootstrap
states distinguished by the reviewed publication protocol. This is a necessary
recovery-ordering dependency, not something a 32-byte identity can solve alone.

## 6. What identity binding still CANNOT prove

- **Freshness/rollback:** an old internally valid file from the SAME lineage and
  voter still matches. Cross-object consistency, install generations and durable
  term/vote rules are separate checks. Complete rollback of all local evidence
  cannot be detected from that evidence alone; recovery admission needs external
  evidence or a separately reviewed operational protocol where required.
- **Safe replacement voting:** bootstrap under an old voter ID is not recovery
  from lost hard state. That voter may have already voted in a term it forgot.
  Shared/local identity equality does not authorize it to vote again. Replacement
  admission must establish safe hard-state/fencing conditions before participation;
  do not implement catch-up by starting an empty normal voter and hoping Raft heals
  it. The final replacement protocol remains separately reviewable.
  First-ever genesis bootstrap is not a reset operation for an existing voter.
  Replacement-pending must not campaign, vote or accept ordinary Raft traffic;
  a distinct recovery path must establish admission first. In a fixed two-voter
  pilot, losing one voter's durable identity/history can make safe automated
  replacement unavailable. No auto-shrink, donor-vote copying or unsupported
  claim that a term bump alone fences the old instance. Keep the existing catch-up
  RED as evidence, but its future GREEN must use admitted replacement, not the
  diagnostic's current empty ordinary-voter shortcut.
- **Authenticity:** anyone able to rewrite files/messages can copy a public digest
  and recompute an unkeyed checksum. This proposal protects provenance consistency
  under trusted ownership, not Byzantine voters or hostile same-UID actors.
- **Durability:** matching bytes can still exist only in OS cache. Sync ordering,
  atomic publication, bounded completion identities and target gates remain F6.
- **Authority:** a matching snapshot or peer frame is not a fresh permission to
  send an order. F4 quorum/request/generation/kill checks still apply independently.

## 7. Cost and compatibility

Identity fields alone are 32 bytes for shared content, 36 for local content.
These are NOT final total-envelope overhead numbers: framing, integrity and
generation fields will be enumerated separately. No 36-byte in-memory struct
size is assumed; C++ padding/alignment and wire layout are independent.

Recommend fixed arrays and explicit endian codecs, no serialization of native
structs, bounded scratch and unchanged outputs on decode failure. Metadata is
cold/small. WAL record headers grow and may reduce records per 4 KiB block; assert
the maximum legal command STILL fits without splitting and measure packing cost.
Do not enlarge the block or arena silently. Snapshot wrapping must stay outside
the canonical state payload; recheck the complete file/transfer buffer bounds.
No additional fsync is justified merely to compare identity; initialization and
publication ordering may nevertheless require real durability fences.

Use distinct versioned V2 formats. Preserve V1 codecs/behavior and old evidence.
No automatic in-place blessing of pre-binding history. Fresh pre-live lineage is
already approved, but destructive cleanup is not: old directories remain intact.
Old binaries may ignore new filenames and recreate empty files, so new magic alone
is NOT an anti-rollback gate; executable/directory admission remains necessary.

## 8. TDD order and approval boundary

1. Freeze semantic scope, then publish exact local metadata/WAL/snapshot and peer
   frame sheets with maxima, integrity domains, version handling and bootstrap
   crash states. Do not treat this document as approval of omitted layout details.
2. Preserve RED traces using two valid deployments with the SAME catalog/voter
   numbers but different incarnations: swap metadata, WAL record/block and
   snapshot independently. Assert typed refusal and unchanged files/state.
3. Same manifest, different local voter: reject copied local hard state and raw
   local files; later positively test valid portable snapshot installation.
4. Foreign high-term RequestVote, heartbeat, vote reply and append reply must
   leave term/vote/log/commit/apply/proofs unchanged. Tests must exercise real
   encoded ingress plus prohibit a raw in-process bypass.
5. Mixed-source, snapshot-covered and uncommitted foreign records must not hide
   behind fallback. Missing/truncated metadata cannot silently reset voting state.
6. Kill isolated mutants for manifest comparison, local-node comparison, reply
   validation and typed-error preservation. Keep healthy replay/restart/proof
   positives so unconditional refusal cannot satisfy the suite.
7. Then layer bounded snapshot installation and archive-before-reclamation; these
   do not close from lineage equality alone. Quantified queues and F6 async fences
   retain their own review gates. Rebuild host tests before later frozen NUC/Pi gates.

Owner approved: reuse manifest identity; local-versus-portable split; record-
level local WAL binding; all-message pre-mutation validation; typed mismatch
refusal/no migration; one active block-WAL format for fresh bound deployments.
One independent agent reviewed the storage/peer/replacement seams. Main retained
record-level binding over its lower-overhead block-level alternative for the
provenance simplicity stated above, and incorporated its dual-format and lost-
voting-memory objections. Do not ask again for these six semantic choices. No
code work or acceptance promotion in this checkpoint; next review exact bytes.
