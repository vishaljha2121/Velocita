# CP-015 — approved identity codec and standalone startup owner

2026-09-22. Base HEAD d44a4a9 on v2/phase-5-corrections. Remediation is uncommitted.
The owner approved the exact F5 identity frame, digest domains, modes/epochs and
maximum of three voters after CP-014. No further approval of those bytes is needed.
Two workers handled codec implementation and startup review; main implemented the
startup owner, reviewed both tracks and ran integration/regression checks.

## Scope boundary first

The codec and standalone startup module are GREEN; **ReplicatedKernel does not
yet consume the new token**. Its old raw constructor remains. The original A04
diagnostic was recompiled against this tree and still reports:

```text
membership_drift node=3 became_single_voter_leader=1
RED F5/A04: unchanged durable directory accepted a different static membership
```

This is retained as `kernel-integration-still-red.txt`, outside the passing CTest
inventory. Do not call this checkpoint a repaired kernel restart or full F5.
No economic schema, OrderEffect/marker bytes, V1 startup or live sender changed.

## Codec: what, why and ownership

`static_identity.hpp` / `v2_static_identity.cpp` implement exact V2ID frames:
116/120/124 bytes for one/two/three voters. Inputs are bounded arrays; encoding
uses scratch storage and publishes output only on success. Decoding checks
length, magic, framing schema, reserved bytes, checksum, semantic versions/modes,
strictly sorted positive IDs and local membership before publishing a privately
constructed VerifiedStaticIdentity. Failure preserves the destination. No native
struct padding is serialized; no heap allocation or filesystem access in codec.

The shared manifest hash excludes local voter ID; the complete-file digest
includes it. The derived authority domain is a second domain-separated hash,
never an independently supplied configuration knob. Tests use an independent
literal maximum-size frame and hash constants, not encode/decode as mutual oracles.
Main independently recomputed the hashes with Node's crypto implementation;
`independent-golden-check.txt` agrees with the test constants generated separately.
Valid changes to incarnation, catalog root, voter set and lab/pilot mode change
both shared digests; changing only local ID changes only full-file identity.

One-, two-, three-voter support, malformed signed IDs, foreign local ID, duplicate/
unsorted IDs, zero roots/incarnation, invalid schemas/modes, excess/unused voter
slots, every short buffer length and trailing bytes are covered. Pilot mode is
valid only for two voters; laboratory mode is not production admission. Compile-
time pins to existing 8/8/1/1 constants force review when economic/marker/catalog
schemas change instead of silently advertising obsolete compatibility.

Original codec stub RED: `golden encode refused`; then GREEN. Isolated production
mutants reduce the supported count to two, change the manifest domain literal,
or remove checksum verification. Each fails its intended literal/semantic oracle;
raw logs and exact patches are retained. These mutations never edit shared source.

## Startup owner: publication and lifetime

`identity_startup.hpp` / `v2_identity_startup.cpp` implement explicit bootstrap
and recover returning a move-only VerifiedIdentityStartup. It owns the directory
lock descriptor, verified identity and immutable catalog. Moving transfers the
descriptor and clears the old owner; destruction closes it. No public constructor
can manufacture an owned token. Return-value construction completes before RAII
releases the descriptor, so a path allocation exception cannot leak the lock.

Validate caller identity and required catalog before directory creation. Open an
existing parent; only the requested leaf can be created, with mode0700. Open and
lock the directory inode without a lockfile. Require local effective-UID ownership
and no group/other write permission. All startup file operations are relative to
the locked directory descriptor. Mismatched/missing recovery performs no file
creation or writes; names and bytes are preserved.

Bootstrap accepts an empty directory or exact implementation-owned retry state,
not legacy metadata/WAL, unknown files or two ambiguous identity generations.
Publish an exclusively created temporary file after checked writes and file sync,
then rename and directory sync; parent sync covers the created leaf. An exactly
matching COMPLETE temporary identity can resume. A partial/invalid temporary
file is preserved and refused, not automatically deleted. The availability cost
is operator inspection after some interrupted bootstraps; this avoids guessing
whether ambiguous bytes are safe to discard.

Final review corrected a new recovery-window mistake: a valid final file could
be visible after an interrupted rename but before durable directory publication.
Read-only validation alone cannot prove the previous bootstrap returned success.
Matching recovery now resyncs the validated identity, directory and parent before
returning a token. Rejection still precedes those success-path fences. The new
permanent RED states `recovery published ownership without crossing its durability
fence`; injecting interruption at that fence must prevent token publication and
release the lock. This establishes control-flow/error behavior, not sync-sensitive
physical durability: deleting only a sync while retaining its test checkpoint
would require a stronger persistence model to detect. That limitation is explicit.

Additional permanent coverage: exact retry/restart, local-ID/membership drift,
missing/wrong catalog before mkdir, legacy adoption refusal, same-process and
forked independent-open lock contention, lock move/unwind/reacquire, symlink leaf
and identity file, FIFO/nonregular file, hardlink, oversized file and five process
cut checkpoints. The file reader is a fixed125-byte buffer and never opens a FIFO
in blocking mode; ordinary test inventory records nonregular entries without
opening them. A world-writable-directory RED preceded the permission check.

Original startup stub RED: `bootstrap failed to produce a verified directory
owner`. Removing only flock, stored-versus-expected identity equality, or catalog
validation in isolated source copies makes the suite accept the corresponding
forbidden owner/startup (refusal codes3,5,1). These are behavior assertions, not
lock/syscall counters. Main directly compiled the mutants; exact patches/logs
are retained. Later added cases are not mislabeled as original RED-first coverage.

## Explicit limitations and next integration

- Directory flock is advisory/cooperative, verified on this host only. Trusted
  parent components and no noncooperating operator mutation are assumptions;
  O_NOFOLLOW protects the opened parent leaf/deployment leaf/file, not every
  ancestor. Same-UID malicious actors are not fenced. After fork, the child inherits
  descriptors until close/exec/exit; production must not casually fork a live owner.
- `verify_name` detects an already-substituted directory at check time. A later
  pathname reopen can still hit another inode. Kernel/store integration must use
  the retained directory boundary or explicitly retain the trusted stable-path
  assumption; the current token's path getter does not close that race.
- The process-cut matrix does NOT evict OS caches. Startup-specific power-loss/
  syscall-failure modeling and target filesystem/device evidence remain open.
  CP-014 metadata-model evidence does not automatically validate this new module.
- The owner token is cold-start permission, not a Raft quorum proof or permission
  to send. Its small identity records do not bound the kernel queues/history.
- A sidecar does not bind copied foreign WAL/snapshots or stop an older executable
  that ignores it. Shared-domain storage, peer, snapshot/archive/install envelopes
  and final executable admission remain required for full F5/live acceptance.

NEXT: consume the token as the FIRST kernel member; remove the public raw/default
constructor and derive local ID, members, catalog and authority domain solely from
the token. Keep it alive until all stores/writers are destroyed, reject moved-from
tokens before any I/O, and avoid duplicating/aliasing catalog ownership unsafely.
Convert A04's exact diagnostic into typed refusal plus unchanged-tree evidence.

Fixture migration must not make old tests vacuous: bootstrap identity BEFORE
planting unsupported/corrupt WAL/snapshot bytes, so those tests still reach their
intended guard. Missing catalog now refuses at startup; foreign committed installs
still require their before-apply oracle. Marker helpers must use derived domains,
not relabel an arbitrary old digest. Preserve one-/two-/three-voter lab behavior;
no automatic production quorum shrink and no legacy-directory backfill.

## Verification

Final rebuilt host Release46/46 and Debug ASan+UBSan46/46; tools40/40, passive
model109/109; independent inventory46 total/43 V2. Sanitizers use runtime UB halt,
leaks off, existing C++ flags; no new claim for Raft-C instrumentation. Diff check
clean. No target, venue, credentials, source freeze, commit or tag action. All
protected/unrelated work preserved.

Raw direct-build artifacts: `/private/tmp/f5-static-identity.TnVC6D` (codec),
`/private/tmp/f5-startup.PYWcEs` (main startup/regression). Retained evidence below
does not depend on those temporary directories surviving. An intermediate build
hit a constant-name collision after schema headers were added; it was fixed by
renaming the identity-local offset, not counted as a RED. An early startup GREEN
attempt met the still-stub codec and was not claimed as passing. A diagnostic
invocation omitted ROOT and returned usage2; the retained A04 run supplied a new
private root and failed at the intended membership assertion.
