# F5 archive publication, coverage, and reclamation review

CP-048 approval (2026-09-24): owner approved fresh authenticated destination
verification for MVP; persistent receipts deferred. This approves the semantics
and explicit availability cost below, not unspecified authentication wire bytes
or physical-host acceptance. Native integration proceeds under that boundary.

CP-047 (2026-09-24), historical integration review: the local transaction is now
implemented; historical statements below describing its absence are superseded
by CP-039–046 evidence. The independent-destination contract remains UNAPPROVED.
`ArchiveStore::consume_for_reclamation` and `ReplicatedKernel::compact` intentionally
refuse two-host reclamation without it. This is an existing E2 dependency, not a
new feature or a reason to remove those guards.

Owner-approved MVP mechanism: fresh authenticated destination
verification over introducing a separate durable receipt format in this iteration.
After copying existing immutable objects and their exact catalog/dependencies,
the destination must validate the complete pinned set and durably sync its files
and namespace. Only an authenticated, request-correlated completion for the
configured independent destination can produce a one-use local capability.
Bind destination identity, source publisher/manifest, exact catalog digest and
generation, covered range, local storage generation and nonreused attempt identity.
No boolean, rsync status, shared-lineage digest or local second directory suffices.

Do not persist this in-memory permission or add a backup bit to the existing
catalog. After restart, reacquire exact destination verification before the live
recovery barrier clears when reclaimed history relies on that backup; before each
new reclamation, verify the current pinned set again. No automatic pruning at
either destination. Destination loss, missing bytes or uncertain sync leaves the
node nonserving or retains WAL and blocks compaction as applicable. Work runs
off-owner with bounded backlog under the already approved CP-038 limits, never
blocking RPC from Raft apply or granting a hot-path order permission.

Reason for recommending this over a durable receipt: fewer new persistent formats
and no receipt migration/replay authority for the MVP. Cost: availability depends
on the backup service at recovery/reclamation, and exact revalidation can be
expensive. It is not network/I/O-free merely because the owner polls completions.
A durable authenticated receipt remains a deferred alternative, but requires
its own exact bytes, retention and trust contract; it does not itself prove a
backup remains present forever. Defer that optimization/availability extension
under the owner's fresh-verification approval.

CP-048 native implementation: the destination verifier checks the separately
pinned other voter's canonical identity, same manifest, exact source-published
catalog/dependencies/objects and destination durability. It does not relabel the
publisher's archive as the destination's own voter archive. The trusted transport
must own a distinct backup namespace and prove physical-host identity; filesystem
paths or a static-identity digest alone do not prove either authentication or host
independence. The verifier entry is private to that owner and explicit test access.

The native request additionally binds a256-bit challenge. A per-process nonce is
obtained with getentropy during cold construction (failure stops startup), then
domain-separated SHA-256 of nonce and nonwrapping attempt creates each challenge.
The same generation/attempt counters can recur after restart, so counters alone
are not freshness. A permanent RED reused a prior response across restart; fresh
nonce fixes it. This is probabilistic cryptographic freshness, not a durable epoch,
and freezes no transport bytes. No entropy syscall is added to the order path.

Actual compaction now waits between acquired local archive/snapshot completion
and Raft prefix removal for the exact one-use independent evidence. Cold live
recovery with a reclaimed prefix similarly remains quarantined until fresh
verification. A missing pin refuses that startup; absence of a pin continues to
refuse live compaction. Existing laboratory behavior and local catalog bytes stay
unchanged. Host witnesses exercise real WAL removal/restart plus exact remote
payload decoding, using simulated authenticated transport and separate local
directories. They do NOT prove remote transport, independent hardware or power-loss
durability. Evidence: `evidence/f5_destination_verification_2026-09-24/README.md`.

The fresh-verification semantics above are now approved, NOT yet accepted behavior. Concrete
authentication/session/envelope choices still require review before encoding.
Native tests may explicitly simulate the trusted destination boundary; they must
not introduce a production raw-auth bypass or claim physical Pi durability.

2026-09-23. **DIRECTION APPROVED after CP-029; CP-038 local contract approved.** CP-028 approved and implemented
only the portable archive segment bytes and per-object limits. This review does
not approve publication names, quotas, receipts, transfer protocols, new disk
bytes, or compaction wiring. It preserves the already-approved immutable,
no-automatic-prune, fail-closed-full policy and the independent-copy requirement
for live operation.

CP-038 supplies the owner-APPROVED local catalog/publication/quota contract in
`F5_ARCHIVE_CATALOG_BYTE_REVIEW.md`. It replaces the earlier open-ended catalog
design candidates with one flat atomic-replaced catalog and fixed dependencies;
it approves those exact local bytes but does not waive the independent live copy.
The owner approved all five points; CP-028 segment bytes
remain approved and unchanged.

## 1. Smallest correct MVP

Add one archive owner beside, not inside, Raft apply. It owns the archive
directory, bounded publication scratch, startup verification, quota accounting,
and short-lived coverage capabilities. It consumes copied committed log records
and emits the already-approved CP-028 objects. It never borrows a retained-log
cell or WAL decoder view across an owner call.

CP-028 objects alone are insufficient to recover deletion authority. On startup,
enumerating and decoding them proves only object integrity and lineage; after the
source WAL has been deleted there is no remaining fact from which to infer that
the records were committed or independently copied. The smallest restart-safe
MVP therefore needs a generation-bound durable coverage catalog/commit artifact.
Its exact bytes, maximum inventory entries/pages, root construction, and update
protocol are new material contracts and remain unapproved and not ready to
encode. Avoid an unbounded mutable per-object database: publish immutable objects
first, then atomically publish a bounded inventory commitment naming the exact
contiguous admitted object/dependency set and its provenance. Startup trusts no
discovered object absent from that committed inventory.

The archive owner returns an in-memory `ArchiveCoverageCapability`, conceptually:

```
manifest_digest, archive_catalog_generation, local_storage_generation,
first_index, last_index,
term_at_first_minus_one, last_term, terminal_record_digest,
durable_coverage_record_digest, local_publication_durable,
independent_copy_durable
```

This is a typed, non-serializable capability, not another persisted wire format.
It is useful only to the current archive owner and current active storage
generation. The owner validates it again immediately before reclamation. A
copied struct, pathname, checksum, snapshot coordinate, metadata commit index,
or peer's active WAL is not a capability.

### Why not the simpler alternatives

- Snapshot-only compaction is recovery-safe but destroys ordered command and
  caller evidence. The current implementation demonstrates exactly this loss.
- A successfully decoded CP-028 file can contain uncommitted or independently
  fabricated canonical records. Integrity and lineage are not commitment.
- The local metadata commit checkpoint is clamped to the locally durable index,
  but can trail a newer durable snapshot. It is neither proof of exact archived
  bytes nor a deletion watermark by itself.
- Another voter's active WAL can compact the same prefix and is not an audit
  copy.
- Adopting every valid object found during a directory scan invents commit
  provenance. A durable coverage record is necessary once the source history is
  reclaimed; it must be published after its dependencies and never turn an
  unlisted file into coverage.
- Publishing by final-name write exposes partial bytes; rename without syncing
  the directory does not prove the final name survived a crash.
- Automatically deleting the oldest archive object at quota does not solve
  pressure: it silently trades audit retention for availability and violates
  the approved policy.

## 2. Record selection and commitment proof

The archive source range must be a contiguous prefix whose every record is:

1. still owned by the current retained-log/WAL generation;
2. locally durable (`index <= durable_index`);
3. known committed by the current Raft owner (`index <= commit_index`); and
4. coordinate-identical across the retained cell, Raft entry, and durable WAL
   record (index, term, entry ID, entry type, and exact payload).

Capture the range, current local-storage generation, and current archive-catalog
generation while the single
owner cannot interleave log mutation. Copy each complete record into bounded
archive-owned storage, then release the source view before encoding or I/O. If
the range cannot fit one CP-028 object, split it into contiguous objects; never
omit a record to meet a cap. Revalidate the generation and the committed/durable
upper bound before publication and again before granting coverage.

Commit proof begins as a live owner fact bound to the copied bytes, not a field
inferred from the segment. Before reclamation, the owner durably records that
adjudication in the coverage artifact, binding the verified shared identity,
archive-catalog generation, local-storage generation, contiguous range,
predecessor/last terms, exact ordered object digests, commit coordinate/term,
required dependency digests, and (for live) exact independent-copy receipt
digests. The inventory must be explicitly bounded by an approved maximum object
and dependency count. If an ordered digest root is used, its leaf/domain/proof
bytes and bounded inventory pages are part of the still-unapproved persistent
contract; a root without recoverable exact membership is insufficient. Startup
restores only the prefix authorized by this artifact after strict verification
of every named dependency. A crash after object publication but before coverage-
artifact directory sync leaves an orphan object, never coverage. Exact catalog,
coverage, inventory/root, and receipt layouts require separate owner approval.

Failed commands and failed authority requests remain archive records. Result
readiness does not decide archive retention; unresolved RESERVED obligations and
unknown-commit suffixes independently prevent an install/repair cut even when
the command bytes are archived.

## 3. Immutable publication transaction

For one already-encoded, fully verified object:

1. Reserve quota for the worst-case complete object plus one bounded temporary
   and any explicitly approved independent-copy backlog. Refuse before starting
   if reservation fails.
2. Create a new, singly linked regular temporary in the archive directory with
   no symlink following. Never truncate an existing name.
3. Write all bytes, handling short writes and `EINTR`; `fsync` the file.
4. Re-read through the file descriptor and strictly decode the complete CP-028
   object against the expected identity and captured range.
5. Install at a deterministic final name using an atomic no-replace operation.
   An existing name succeeds only after its complete bytes decode to the exact
   same object; otherwise latch an archive conflict and fail closed.
6. `fsync` the archive directory. Only this cut establishes durable local
   publication.
7. Reopen the final name without following links, verify regular/single-link
   identity, and strictly decode again. Only then may local coverage advance.
8. In live mode, copy the immutable object to the independently administered Pi
   archive, obtain destination-local durable publication evidence, and verify
   the complete object there. Its receipt must bind the destination replica/host
   archive identity, archive-catalog generation, exact object digest/range, and
   identity/dependency digests; shared cluster identity alone is insufficient.
9. Atomically publish and directory-sync the bounded coverage artifact after every named
   object and required receipt is durable. Reopen and verify it and all named
   dependencies. Only this cut can grant a restartable capability.

The exact catalog/coverage/inventory/root/final-name grammar, no-replace
portability mechanism, independent-copy receipt bytes, retry protocol, and every
numerical inventory/backlog bound are material contracts still requiring
approval and are not ready to encode. NUC shadow may use local-only coverage if its deployment mode
explicitly says so; live must not silently downgrade. Partial temporaries and
conflicting finals are retained for diagnosis and never count as coverage.

## 4. Coverage and dependency ownership

Coverage is a graph, not one high-water integer:

```
verified static identity
  -> immutable CP-028 object(s), contiguous and overlap-consistent
  -> local directory-durable publication
  -> independent durable copy (mandatory in live)
  -> durable generation-bound coverage record
  -> archive coverage capability for current archive and storage generations

verified durable snapshot through cut
  + archive coverage capability through cut
  + zero source borrows / no pending archive read of reclaimed cells
  + no unresolved result or repair obligation that pins the prefix
  -> reclamation authorization
```

The archive owner owns immutable base descriptors, archive objects, coverage
artifacts, verification state, quota reservation, and
capabilities. The replication owner owns commit/applied/durable coordinates,
retained cells, Raft entries, result obligations, and the mutable live WAL and
hard-state generation. The snapshot owner owns immutable snapshot generations
and proves recovery-state durability only. The repair or install owner selects a
new `LocalStorageGeneration`; it does not create an archive-catalog generation.
It may consume verified archive records, but cannot mint archive coverage.

`ArchiveCatalogGeneration` is distinct from `LocalStorageGeneration` (the active
install/repair selector) and from any sender transfer session. Snapshot install,
repair selector switch, restart without verified catalog recovery, or replacement
invalidates the local-storage generation; catalog replacement invalidates the
archive generation. Outstanding source tokens and capabilities must match both.
Counters must be
non-wrapping; exhaustion is a stopped-service diagnostic, not reuse. No payload
span, Raft entry pointer, retained descriptor, WAL visitor reference, or archive
decoder view survives the callback that produced it.

## 5. Reclamation cut

`compact(cut)` must be a fail-closed transaction coordinated by the replication
owner:

1. Bound `cut` by applied, committed, and locally durable history.
2. Obtain durable-catalog-backed archive coverage through `cut` for this identity and both generations
   (including the independent-copy bit in live mode).
3. Durably publish and reload a snapshot through `cut`.
4. Revalidate identity, generation, commit/durable bounds, archive capability,
   result/repair pins, and zero live archive-source borrows.
5. Ask Raft to discard its prefix and release retained cells only under a
   no-fail callback, or first refactor the callback ownership so a later failure
   cannot leave Raft and retained ownership divergent.
6. Request an F6 asynchronous writer drain/fence and return control to the owner
   loop. Only a matched, successful completion for the current storage generation
   may authorize durable WAL-prefix rewrite, which remains last. Runtime owner
   code must not stop/join the writer; join is permitted only during fenced
   offline shutdown.
7. Invalidate the one-shot capability after successful rewrite and resume gated
   admission.

The present implementation does not meet step 2 and erases retained cells in
`log_poll` during `raft_end_snapshot` before `WalSegment::discard_through`. It
also has no archive borrow/generation gate. These are code flaws relative to the
already accepted archive-before-last-copy direction, but production changes must
wait for approval of this publication contract.

Failure before step 5 leaves redundant history. Failure during/after Raft prefix
removal must stop service unless ownership and WAL state can be proven aligned;
it must never continue by treating the snapshot as audit coverage. WAL deletion
is last. Archive objects are never automatically pruned.

## 6. Quota and full admission

Keep the already approved semantic policy: immutable/no automatic pruning and
fail-closed full. Do not freeze the older 1-GiB archive or 16-MiB backlog numbers
without measurement and owner approval.

The numerical review must separately bound:

- local immutable bytes and object count;
- temporary publication bytes (at most one complete 4-MiB CP-028 object per
  publisher unless a different concurrency budget is approved);
- independent-copy backlog bytes and objects;
- startup catalog dependency/object count, inventory pages, scan work, and time;
- quarantine/conflict bytes; and
- bounded copy/encode/decode scratch.

Admission reserves the complete worst-case object before accepting history that
would require it. At the ordinary high-water, stop ordinary proposals while
retaining separately approved capacity for control actions and for finishing an
already accepted publication. At hard full, do not compact, prune, overwrite,
weaken live independent-copy requirements, or accept economic work whose audit
dependency cannot be stored. Surface a typed capacity refusal and an operator
alarm. Operator export/removal is a separate, fenced procedure and is not part
of this MVP.

## 7. Required RED tests before implementation

Keep `tests/v2_archive_reclamation_red.cpp` outside CTest until the contract is
approved. Its current literal command proves that snapshot-only compaction loses
the last canonical audit bytes. The GREEN must replace its presence-anywhere
oracle with the typed capability and independently decode the published object.

Add permanent tests for:

1. valid CP-028 object fabricated from uncommitted entries grants no coverage;
2. valid local object without directory fsync grants no coverage after every
   publication crash cut;
3. short write, file-fsync failure, rename failure, directory-fsync failure, and
   post-rename corruption leave WAL/retained prefix intact;
4. existing identical final is idempotently verified; conflicting final stops
   service unchanged;
5. gap, overlap disagreement, wrong predecessor term, wrong manifest, wrong
   record coordinate/type, truncation, trailing bytes, and digest mismatch do
   not advance coverage;
6. snapshot without archive, archive without snapshot, and checksum without
   commit proof each refuse reclamation;
7. capability copied before suffix truncation, restart, install, or repair
   generation switch is stale and cannot reclaim;
8. a held source borrow blocks reclamation; release permits it; no visitor span
   remains usable as authority after callback return;
9. local copy succeeds but live independent copy is absent/corrupt/not
   destination-durable: live reclamation refuses; NUC behavior follows its
   explicit deployment mode;
10. quota reservation at exact boundary succeeds; one byte/object beyond stops
    ordinary admission; no file is pruned or overwritten; reserved completion
    work can finish within its separately approved reserve;
11. crash at every snapshot/archive/Raft-poll/WAL-rewrite cut recovers either the
    old redundant prefix or the new fully covered state, never an audit gap;
12. concurrent writer drain cannot report durability or reuse a WAL block from
    a stale generation;
13. unread READY results survive compaction, covered RESERVED results are not
    silently canceled, and unknown-commit suffix remains intact;
14. startup bounded scan refuses excess objects/work rather than allocating or
    trusting a highest filename; and
15. no archive codec or filesystem work runs from deterministic apply.

Meaningful mutants must remove each fsync, move coverage before directory sync,
replace no-replace with truncation, accept one gap, omit generation checking,
skip the independent-live bit, bypass quota reservation, and move WAL deletion
before snapshot/archive proof. Each must fail one named assertion.

## 8. Acceptance boundary

Archive publication/reclamation is GREEN only after owner approval of the open
material choices; fresh Release and C++ sanitizer suites; retained raw RED/GREEN
and crash-cut logs; direct single-condition mutants; bounded-memory evidence;
restart reconstruction; and a live-mode test using a genuinely independent Pi
filesystem. Host tests cannot claim Pi power-loss durability. Passing this slice
does not approve snapshot transfer/install, repair admission, asynchronous Raft
persistence, deployment, or trading.

### Choices requiring owner review

1. Capability fields and distinct local-storage/archive-catalog generation
   ownership above.
2. Exact durable catalog/coverage/inventory/root bytes, atomic publication
   protocol, maximum object/dependency entries and pages, and orphan-object
   diagnosis (required; CP-028 scan alone is deliberately insufficient).
3. Final-name grammar and portable atomic no-replace primitive.
4. Exact local quota, object-count/startup-work bound, ordinary high-water,
   completion reserve, and independent-copy backlog.
5. Independent-copy durable receipt/verification semantics, destination identity,
   and whether local-only
   NUC coverage is enabled by deployment mode.
6. Whether Raft prefix polling is refactored to be provably no-fail before WAL
   rewrite, or reclamation latches service on any post-poll failure.

None of the unresolved persistent layouts, inventory/root rules, receipt bytes,
generation encodings, filesystem names, or numerical budgets in this review is
ready to encode before explicit owner approval.
