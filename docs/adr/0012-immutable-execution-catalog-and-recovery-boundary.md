# ADR 0012: Immutable execution catalog and a fail-closed recovery boundary

- Date: 2026-09-21
- Status: accepted direction, installation/recovery and catalog-v1 byte contract; implementation partial

Implementation checkpoint CP-008: catalog installation, exact row binding,
historical effect binding and recovery/readiness are integrated with kernel,
result and snapshot schema 8. Evidence and limitations are in
`../evidence/f3_binding_2026-09-22/README.md`. The standalone codec evidence remains
in `../evidence/f3_catalog_2026-09-21/README.md`. Real encoder/venue admission,
F4 authority and target acceptance remain open; no live sender is enabled.
- Governing invariants: ADR 0001, especially deterministic inputs, unchanged
  economic intent, replay equivalence and complete snapshots
- Detail: `docs/F3_EXECUTION_METADATA_REVIEW.md`
- Evidence/progress: `docs/evidence/V2_REMEDIATION_CHECKPOINTS.md`

## Context

An effect's price ticks and quantity lots do not identify their exact external
units. Different tick/lot factors can have the same product and thus pass a
notional check while encoding a different price and size. Caller-selected schema
flags and a mutable gateway file cannot establish what metadata the cluster
authorized. The existing unbound body encoders are quarantined, not replaced by
a working adapter. True Markets swaps and perpetuals remain outside this decision.

## Decision

Use one immutable, content-addressed execution catalog per deployment lineage.
Its committed 32-byte root and the proposal's retained spec/capability versions
select exact venue/instrument metadata. Verify canonical bytes before serving;
pass an immutable view explicitly to the reducer. No filesystem lookup, mutable
global fallback, lazy network fetch or allocation-dependent decision inside apply.
The catalog must bind assets/network, exact factors, operations, constraints and
encoder semantics; a digest proves byte identity, not the truth of venue claims.
The owner approved the bounded canonical row schema, exact identity/precision
and account-denomination rules, 8 KiB artifact / 16 KiB context bounds and appended
refusal codes in `docs/F3_CATALOG_V1_BYTE_REVIEW.md`. Real venue mappings remain
unverified; approving structural bytes does not approve a venue capability.

Install once while halted, after nonzero control initialization and before
economic configuration. The approved command direction is kind 20,
`InstallExecutionCatalog`: root (32 bytes), expected kill epoch (u64), expected
executor generation (u64), with existing canonical identity/framing. A repeated
root is idempotent under normal request/control rules; a different root is
refused even after draining. Zero denotes uninstalled, never a default binding.

Missing or corrupt required catalog bytes are a node dependency failure, not a
replicated economic rejection. Stop before advancing applied state, publishing a
snapshot, recording a result or serving authority. Otherwise identical committed
commands could produce different outcomes on replicas with different local files.
Preprovision and verify every configured voter for the static MVP; distribution,
hot rotation and automatic artifact garbage collection are not included.

Preserve OrderEffect's current canonical bytes. Materialize its root and retained
historical versions alongside it in one coherent read, never from the latest
active policy. Copy binding values rather than retain references into compactable
arenas. Keep the immutable catalog alive for every live view and recoverable
history that needs it. A local typed envelope is not authenticated gateway IPC
and does not fix F4's stale-authority problem.

## Compatibility and rollout

The owner approved a fresh pre-live lineage and kernel command/result and state
snapshot schema 7 -> 8. Preserve old storage directories as evidence; no automatic
conversion, clearing, historical root backfill or mixed-version rollout.

Switch the versions with the new root semantics, not ahead of them. A partial
implementation stamped version 8 could itself create economic history lacking
the final prerequisites, recreating the migration problem within one version.
That coherent boundary is implemented in CP-008; schema 7 is historical, not a
supported current lineage. The encoder quarantine remains in force. The earlier
compatibility guard was tested first against an unsupported schema.

Recovery must inspect existing snapshot and WAL compatibility before opening
writable WAL storage. An incompatible snapshot is not an absent snapshot. It
must never fall through to replay under different semantics, even when a complete
WAL is available. Check retained WAL commands including the uncommitted suffix:
those bytes may commit later, and a currently unapplied entry is not harmless.
This check decodes but does not speculatively apply commands.

Same-schema corrupt-snapshot recovery from a complete validated WAL is retained;
this is not permission to reinterpret an unsupported schema. Missing committed
history continues to refuse. Refusal preserves diagnostic file names and bytes.
Read-only preflight assumes exclusive ownership of the deployment directory;
it is not a new defense against concurrent operators replacing files mid-startup.

## Alternatives rejected and costs

- Arithmetic-product or local-flag binding: cannot distinguish the reproduced
  equal-notional/different-price counterexample.
- Per-effect full roots and hot catalog rotation: greater retention/migration
  complexity and at least 512 additional canonical bytes for sixteen decisions,
  exceeding the current 85-byte snapshot headroom. One root adds 32 bytes;
  resident alignment and encoded maxima still need measured gates.
- Missing artifact as an ApplyCode: makes replicated outcomes depend on local
  availability. Refusing the node costs availability but preserves determinism.
- Snapshot-only version bump: an old WAL could replay successful authorizations
  as refusals under the new prerequisite. Both persistence paths need a boundary.
- Automatic migration: old effects cannot prove which exact catalog governed
  them. A fresh pre-live lineage avoids inventing that provenance; existing data
  is retained, not erased. Later migration needs a separate design.
- Validate only on apply: ignores an incompatible uncommitted suffix, and runs
  after writable startup side effects. Preflight adds a linear decode pass at
  startup, not per order; it does not solve F5's unbounded retained-history issue.

## Acceptance, not claims

Permanent RED tests must prove incompatibility refusal and unchanged storage,
then install/dedup/codec/snapshot tests must pin root semantics. Missing-artifact
recovery must stop before any apply. Historical materialization must survive
policy rotation, sweep and restore. Independent mutations must kill the intended
guards, not a preceding unrelated assertion. Full host and target gates, exact
budgets and the remaining binding/recovery integration precede F3 closure.

Implementation review warning: the pinned Raft `raft_apply_entry` increments
`last_applied_idx` before calling `applylog`. Returning shutdown from that callback
alone cannot satisfy the no-index-advance requirement. Artifact dependency checks
must dominate entry into Raft's apply machinery, including constructor recovery
and indirect periodic/peer paths. A permanent missing-artifact test must assert
both the Raft applied index and reducer/result state, not only an error return.

### CP-008 implementation rationale — 2026-09-22

An optional pinned `before_apply` callback now dominates the index increment.
Checking only wrapper tick/propose functions was rejected: peer handling,
periodic auto-apply and begin-snapshot can enter Raft apply indirectly. The hook
receives the exact entry and refuses before index or reducer mutation. NULL
retains the legacy path. Its refusal and misplaced-hook mutations are tested.
No exception crosses the C callback; it returns shutdown and latches node
service refusal. Diagnostic state/status and persistence draining remain usable
for inspection/cleanup, not authority. Reprovisioning requires restart, not hot
mutation of the owned const catalog.

Snapshot hashing and root equality are necessary but insufficient: a structurally
valid snapshot can contain a profile that disagrees with that root's rows.
Recovery therefore checks active/staged profiles, resolved proposals and retained
effects against immutable historical metadata before loading the Raft snapshot.
It refuses rather than falling back to replay for this semantic mismatch, so
inconsistent evidence is not silently hidden. Canonical structural validation is
still separately required; the catalog checker is not a replacement for it.

Gateway admission now consumes copied binding metadata and rechecks the operation
capability. A retained cancel intent on a venue without cancel support remains
retained risk, not permission to send an unsupported operation. The catalog
constructor/codec performs no venue discovery; synthetic fixtures prove mechanics
only. A bare committed-outbox diagnostic view is not a bound sender envelope,
and even a bound envelope is not quorum-fresh authority (F4 remains open).
