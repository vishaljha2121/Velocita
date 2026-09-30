# F3 execution metadata binding — installation/recovery contract approved

2026-09-21. Owner APPROVED the single immutable committed catalog direction,
then the installation/recovery contract and fresh-lineage schema 8 boundary.
ADR 0012 records the decision. The owner subsequently approved canonical catalog
v1 in `F3_CATALOG_V1_BYTE_REVIEW.md`; implementation remains partial and real venue
mappings remain unverified. This is not authority to trade.
Base d44a4a9 plus uncommitted F1/F2/F3 remediation. ADR 0001 governs;
ADR 0011's swap model remains unapproved. No venue inquiry is authorized.

CP-008 (2026-09-22) implements the approved root installation, mandatory binding,
historical local envelope, schema-8 and dependency/readiness integration. See the
checkpoint log and `evidence/f3_binding_2026-09-22/README.md`. This is not full F3
closure: real encoder/venue admission and target validation remain outstanding.

## Immediate correction versus completed capability

The unbound request-body helpers must refuse without changing output. A caller's
`VenueOrderSchema::verified` boolean cannot bind instrument identity, precision,
asset routing, capability semantics or defaults to committed intent. The market
helper has no legitimate current effect: the reducer authorizes limit submits,
not a zero-price market/swap variant. Refusing both unbound body APIs does not
remove order lifecycle, cancellation, risk or exact decimal arithmetic.

This closes the unsafe emission path, NOT all of F3. Re-enabling an encoder
requires the binding below. No live caller currently uses these helpers.

## What must be bound

An immutable, canonical execution catalog must identify the exact venue/network,
base and quote assets (not just display symbols), instrument, tick quote units,
lot base units and decimals; it must also identify supported operations,
post-only and time-in-force behavior, and the applicable encoder/schema version.
Asset identity and routing details must come from verified venue evidence, not
invented names or a caller-selected schema flag. Capability claims in a digest
still need evidence: hashing an assumption does not make it true.

Gateway inputs must pair the effect with its authoritative binding from one
coherent committed read. Existing cold proposals retain instrument-spec and
capability versions; current `OrderEffect` does not carry them. A new binding
envelope or effect revision must not let the caller attach arbitrary versions.
It must survive policy rotation, outbox retention and snapshot restore without
consulting the currently active policy for an older effect.

Tick × lot agreement remains a useful arithmetic check, not an identity proof:
tick=10000, lot=100000 and tick=100000, lot=10000 both yield 10 quote micros per
tick-lot at 8 decimals. Yet 250 ticks is 2.5 in the first case and 25 in the
second. The encoder must reject the substituted catalog even when notional is
unchanged. It must also preserve the exact TIF/post-only meaning explicitly,
never depend on an unverified venue default.

## Recommendation and alternatives

Recommend a content-addressed catalog with a committed identity. Canonical
catalog bytes are immutable, loaded/verified outside the reducer's hot path and
retained while any pending/unknown effect references them. Missing, mismatched
or unsupported bindings refuse before any transport. No runtime catalog fetch,
floating point, fallback to another version or guessed field is permitted.

Rejected: use only the arithmetic product, a boolean `verified`, or an external
file which can change under the same version. These are precisely the ambiguity
the audit reproduced. An externally pinned deployment manifest alone is lighter
but makes economic meaning depend on operator configuration not recoverable from
the replicated binding; do not silently adopt that weaker trust boundary.

Two implementation shapes were considered; the owner selected option 1:

1. **One immutable catalog root per deployment lineage (MVP preference).** Existing
   spec/capability version IDs select rows in that catalog. This avoids repeating
   a full digest per decision. Catalog replacement is not an ordinary hot update:
   old effects, restart compatibility and lineage transition must be specified.
   The root alone is not enough: staging/authorization and delivery must validate
   the selected row consistently. A binding envelope must include the root and
   historical versions from the same committed state.
2. **Per-effect/per-decision catalog binding.** Supports overlapping old/new
   catalogs and runtime rotation, but requires more retained state, canonical
   payload changes and historical catalog retention. Do not truncate a digest,
   reuse unrelated identity slots or reclaim unresolved effects to make it fit.

The single-root direction and installation/recovery proposal below are approved.
Canonical catalog row bytes are subsequently approved in the catalog-v1 review.
A separate-process binding envelope and any TradingState/snapshot budget increase
remain subject to review. Catalog rotation is not approved.

## Measured budget, not C++ padding guesses

Fresh host layout report, cache line 64:

| Item | Measured | Current bound |
|---|---:|---:|
| TradingState | 81360 bytes | 81920 bytes |
| Maximum canonical snapshot | 65451 bytes | 65536 bytes |
| EconomicActionProposal | 112 bytes | 128 bytes |
| InstrumentExecutionProfile | 72 bytes | 80 bytes |
| Current OrderEffect canonical payload | 166 bytes | Existing schema |

A 32-byte digest in every one of 16 cold proposals adds at least 512 canonical
bytes and exceeds current snapshot headroom. A single 32-byte root could fit
the headline snapshot gap, but its command, validation, migration and retirement
rules still require design; arithmetic fit is not implementation acceptance.
Resident alignment and canonical encoded lengths must be measured separately.
Do not raise ceilings or shrink arenas without an explicit reviewed trade-off.

## Approved increment — installation and recovery boundary

This section is approved, not yet fully implemented. It deliberately separates a
content commitment from local artifact availability. A root commits to bytes;
it neither distributes them nor proves the venue facts those bytes assert.

### 1. Install once, before economic configuration

Approved: `InstallExecutionCatalog`, appended command kind 20. Its body is the
32-byte nonzero root, expected kill epoch (u64), then expected executor generation
(u64), using the existing little-endian canonical primitives. Both controls must
be nonzero and match committed state. Installation requires halted mode and no
policy records or bundles, decisions, risk accounts, reservations, orders or outbox
records.
Control initialization may precede installation; economic configuration may not.
No bytes are copied from the gateway to supply an omitted binding.

The install uses normal request identity/deduplication. Once installed, the same
root is an idempotent no-op under the request/control rules; a different root is
refused permanently, even if all arenas later drain. A conflicting replacement
does not require loading the proposed replacement artifact merely to refuse it.
Operator-only ingress must eventually be authenticated: an enum is not proof of
who sent a command. This increment does not claim the missing process/auth layer.

At 12-byte frame header + 48-byte request identity + 48-byte body + 32-byte frame
digest, the candidate command is 140 bytes, below the existing 1789-byte maximum.
The state stores one digest; zero means uninstalled, never a default catalog.
Snapshot payload gains exactly 32 bytes, nominally 65483 total (53 bytes spare).
Resident size, including alignment, must still be measured; no ceiling increase
is proposed. Appended refusal order and catalog-image bytes are subsequently
approved in the catalog-v1 review; activate them with the coherent root semantics.

### 2. Availability is a node prerequisite, not an economic answer

Load, structurally validate and hash a bounded canonical image before serving.
Pass a const immutable verified view explicitly into reducer work; do not look
up files, consult a mutable global registry or lazily fetch inside apply.
Stage/authorization check the unique catalog row selected by venue, instrument,
instrument-spec version and capability version. The row must match the supplied
profile/capabilities, including exact factors, not merely their product.

If the committed root's bytes are missing or corrupt locally, refuse startup or
stop that node before further apply/serving. Do not emit an ApplyCode, receipt,
state-version increment or applied-index advance for the failed dependency.
Otherwise one replica could reject a trade that another accepts solely because
their disks differ. A leader admission check alone is insufficient: replay and
snapshot installation need the same dependency check before publishing state.
For this static MVP, verify provisioned bytes on every configured voter before
installation; a future distribution/attestation protocol is not implied here.
Loss of a required voter may stop progress; this is explicit fail-closed behavior,
not an availability guarantee. No hot swapping of the verified view is allowed.

### 3. Fail closed across the semantic upgrade

Use a fresh deployment lineage for this pre-live change, preserving old
directories and evidence, rather than an automatic migration. Existing success
records lack the root; attaching today's catalog retrospectively would invent
which metadata authorized them. Merely bumping the snapshot schema is not enough:
`ReplicatedKernel` restores snapshots and then replays WAL through the current
reducer. Replaying an old successful authorization under a new catalog prerequisite
could turn it into a refusal and lose the old economic outcome.

Approved: kernel command/result codec 7 -> 8 (they share a version constant today)
and snapshot schema 7 -> 8. Keep existing kind numbers and body layouts; append
kind 20 and the snapshot root. Old persistence must fail explicit compatibility
preflight before new durable writes, never be treated as an empty deployment or
silently replayed under new semantics. An unsupported snapshot must not fall
through to WAL recovery. No mixed-version rollout or old-directory conversion is
claimed. The exact durable protocol check is part of implementation/tests, not
permission to erase history. A later migration would require its own design.
Implement the read-only compatibility guard first. Switch both version constants
with the root semantics, not ahead of them; intermediate code must not produce
nominal version-8 history that the final implementation reinterprets.

### 4. Keep the existing effect; bind historical meaning alongside it

Retain the current OrderEffect bytes and bounded outbox records. Materialization
can pair an effect with the state's immutable root and the retained cold proposal's
spec/capability versions in one coherent read. Do not borrow these from the active
policy: it may have rotated since authorization. Copy the small binding values;
never retain pointers into compactable state arrays across apply/sweep. The loaded
immutable image must outlive every view into it.

This is initially a local typed interface, NOT proof that a separate-process
gateway has received authoritative bytes. A transport envelope and F4's quorum
authority remain separate reviewed work. Retain catalog bytes as long as any
recoverable snapshot/WAL/effect in the lineage needs them, not merely until the
current outbox empties. No automatic catalog garbage collection in the MVP.

### Implementation order and first RED

First pin the operational boundary: startup with legacy rootless persisted
economic history must refuse without rewriting it; a catalog-bearing recovery
must not advance when its exact artifact is unavailable. Expected outcomes come
from the contract above, not from re-encoding the implementation's own answer.
Then add install/dedup/codec/snapshot tests, deterministic row binding, historical
materialization and independent mutation tests. Budget checks precede expansion
to full host and target gates. Document RED output before implementing each guard.
The separately reviewed catalog-v1 schema is now approved for encoding; no live
venue mapping or guessed routing is approved by this section.

## Acceptance tests before enabling encoding

- Wrong venue/instrument/assets/network, stale version, digest substitution and
  same-product/different-factor substitution all refuse without output.
- Supported post-only and TIF are encoded exactly; every unsupported combination
  refuses, including malformed enums and conflicting schema fields.
- Authorized payload and binding reconstruct after policy change, delayed gateway
  response, snapshot restore and replay; missing historical catalogs never fall
  back to the latest one.
- Refusals leave output buffers unchanged and produce no transport call.
- Mutation tests remove each binding check and fail at the relevant assertion.
- Canonical codec/snapshot/layout budgets and full host/target gates pass.

F4's quorum-authority defect remains independent. A correctly bound stale effect
is still unsafe to send. This review does not approve True Markets swaps,
perpetuals, external calls or live deployment.
