# CP-048: fresh destination verification in native compaction/recovery

2026-09-24. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation
uncommitted. Owner approved fresh authenticated destination verification for
MVP, deferring persistent receipts. This implements the native E2 transaction,
not an authenticated network service or physical two-host acceptance.

## Implemented boundary

`ArchiveDestinationVerifier` has no public raw verification/mint entry. Its
private entry belongs to the future authenticated transport and explicit test
friend. It validates the separately pinned other voter's canonical identity,
same manifest, expected publisher, exact catalog digest/generation/range and
source publication generation, every named object and both dependencies. It
uses existing archive file safety/ownership/full-decode/file-and-directory-sync
rules. The publisher's catalog is copied exactly, never relabelled as a locally
adjudicated catalog for the backup voter. Backup namespace is separate from
that voter's own active archive; transport must hold exclusive root ownership.

The returned evidence owns VALUES, no filesystem/record borrows. It is move-only,
nonserializable and one-use. A request includes current local storage generation,
publication generation, monotonic attempt and fresh256-bit challenge. The kernel
issues requests but cannot turn a boolean or caller-supplied request into valid
evidence. Only exact pending completion can authorize progress.

Two-host compaction now captures/acquires its existing local worker transaction,
then waits for independent evidence BEFORE Raft prefix removal and WAL rewrite.
No pin means no live compaction, preserving the previous refusal test. Local
coverage and exact independent evidence are consumed together; the WAL rewrite
still runs on the serial worker. Duplicate completion does nothing; a mismatched
pending completion latches closed with history retained. Outbound frames, results,
authority and protocol mutation stay gated during the wait.

Cold two-host recovery with archived/reclaimed prefix refuses without a pin and
otherwise starts quarantined with a fresh request. Only matched fresh evidence
releases the recovery barrier. Missing/corrupt backup does not become a healthy
empty archive. No persistent receipt, catalog flag, wire encoding, automatic
cleanup, venue behavior or capacity increase was introduced.

## RED / GREEN and draft correction

`live-red.txt`: with API skeleton and unchanged compaction behavior, the real
two-host fixture fails `configured two-host compaction never requested fresh
destination verification`. The following GREEN uses actual WAL removal, not a
mock success or capability counter.

Review found per-process attempt numbers repeat after restart. `replay-red.txt`
reuses a prior response after destroying/reconstructing the kernel; the constant
session witness fails `pre-restart backup response released fresh recovery
barrier`. The fix gets a fresh nonzero32-byte nonce during cold construction and
hashes a domain, nonce and attempt for each request. Hashing requires no entropy
syscall on an order or compaction-owner turn. Entropy failure stops construction;
the native request is not a frozen transport format. Freshness is cryptographic,
not an impossible mathematical guarantee against random collisions.

## Permanent tests and oracles

`tests/v2_archive_destination_tests.cpp`:

- Independently decode exact authored index/term/entry ID/type/full command bytes.
- Refuse wrong/same/foreign destination, absent archive, stale catalog generation
  or digest, publication/current generation, attempt and challenge mismatch.
- Consume permission once; a moved-from or mismatched permission cannot be reused.
- Corrupt actual command payload, or present a self-consistent foreign dependency.
- Inject separate actual file-fsync and directory-fsync EIO through the real store
  implementation; neither mints permission. Assert source archive/WAL bytes are
  unchanged. Verification itself does not rewrite destination content.

`tests/v2_archive_live_verification_tests.cpp`:

- Canonically authored committed commands1..3, exact kill epoch3/state version3.
  Start two-host-mode compaction, prove WAL/retained cells remain while waiting;
  provide verifier-produced evidence through an explicitly simulated trusted
  transport boundary; finish real WAL removal and independently decode commands
  1..3 from the copied archive. Destroy/restart and verify the new recovery gate.
- Changed attempt refuses without deleting history; duplicate acceptance is false.
- A prior-run response cannot release restarted recovery even when generation and
  attempt counters repeat. Freshly verified response releases exact recovered state.
- Delete or payload-corrupt the backup AFTER successful compaction. Recovery
  remains pending: tick, higher-term peer input, proposal, output, proof and result
  checks stay gated with unchanged state. Missing data never yields evidence.
- Inject actual cold getentropy EIO via private compilation: constructor refuses
  and durable history bytes remain unchanged. No production failure-injection API.

The snapshot/WAL fixture is a valid cold committed image, not a live election
trace. Backup copying is a test-driver filesystem copy; the evidence comes from
subsequent complete destination verification/sync, NOT the copy return value.
Separate directories on this host do NOT prove independent hardware. Destination
identity pinning is NOT authentication: real transport/session binding is still
required before production can mint/use these capabilities across processes.

## Mutation evidence and review corrections

- Remove exact catalog-digest equality: stale request wrongly mints evidence;
  intended assertion fails (mutation2).
- Remove archive-recovery pending from common persistence gate: intended
  `reclaimed live recovery served without fresh backup evidence` assertion fails.
- Ignore entropy failure and continue with zero nonce: intended
  `entropy failure started a node or changed durable history` assertion fails.

Only scratch source copies were mutated. Initial failed builds used a wrong
WalSegment helper name and a missing POSIX entropy header; neither is claimed as
an intended RED. Main corrected tests that labelled header corruption as payload
corruption: both now flip `segment_header96 + record_header32`, the first actual
payload byte. Full payload equality remains independently asserted.

## Scope, resource cost, and remaining exits

Native host sizes: ReplicatedKernel124904 (CP-046124440, +464), request184,
evidence192 bytes. These exclude archive scan buffers, heap, thread stacks and
total RSS. The verifier currently fully opens/verifies the archive twice; I/O is
bounded by existing configured archive quota, not by a latency promise. Keep
that conservative cost for MVP; no new optimization workstream.

Approval explicitly accepts backup-service availability as a dependency of
reclamation and relevant recovery. The owner only copies bounded request values
and validates completion; destination filesystem/network work belongs off-owner.
The test driver joins its verification thread; no protocol-owner API does so.
No fresh process/target latency, RSS, TSan or physical power-cut result is claimed.

The real lagger test remains RED (freshly built `lagger-red.txt`): compacted12,
applied0, seven failed replies. E1 online generation selection, same ledger and
actual stale-write draining remain next; E3 activation composition stays open.
E2 native compaction/recovery is demonstrated, but authenticated copy transport,
crash/physical destination evidence, bounded transfer integration and final NUC/Pi
gates are not complete. No E1/E2/E3/F5/F6/F7 closure or live trading follows.
Pi remains deferred, not waived. Protected paths/tags untouched; no staging,
commit, device, venue, credential or deployment actions.

Commands/results and sanitizer limitations are in `verification.txt`. Scratch
sources/binaries retained in `/private/tmp/velocita-cp048.rDj2EZ` and the test
agent's `/private/tmp/velocita-cp047-e2` (the latter name is not a checkpoint claim).
