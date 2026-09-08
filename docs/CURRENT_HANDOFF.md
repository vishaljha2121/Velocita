# Current development handoff

Updated: 2026-09-08

Purpose: compact working memory for the next agent session. Verify this file
against Git before acting; do not reconstruct the project from chat history.

## Current branch and HEAD

- Branch: `v2/phase-4-risk-orders-outbox`
- Verified development HEAD: `330cfe1b8f63699a22b0ad4ca4ea3de302a0c65e`
  (same commit as the peeled handoff-start tag; Phase 4 changes remain uncommitted).
- Phase 4 base commit: `c25c45fe0d05978b9855685622b39fccc1481b84`
- Handoff checkpoint: annotated tag `v2-phase4-handoff-start`
- Accepted Phase 3 checkpoint: annotated tag `v2-phase3-accepted`, commit
  `8438d0d`

Run `git rev-parse HEAD` and `git show --no-patch --decorate HEAD`. At handoff,
the branch tip must be the commit named by `v2-phase4-handoff-start`.

## Dirty-worktree status

The following pre-existing work is intentionally uncommitted and must be
preserved. It belongs to passive model/market-data research and the proposed
Pi/NUC topology, not to Phase 4:

```text
 M CHANGELOG.md
 M docs/README.md
?? docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md
?? docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md
?? docs/adr/0002-two-machine-pi-nuc-deployment-topology.md
?? model/
```

Do not delete, reset, stage, or include these paths in Phase 4 commits. Inspect
`git status --short` at the start and before every commit. Commit Phase 4 files
by explicit path rather than using `git add .`.

## Last accepted phase

Phase 3—policy artifacts and authoritative decision evaluation—is accepted on
the host and physical Raspberry Pi 5.

Accepted behavior:

- an admitted canonical `DecisionFrame` is evaluated deterministically against
  an atomically activated feature-schema/executable-policy/instrument/venue
  bundle;
- bounded rules use reviewed array-order, first-match precedence;
- strategy control modes dominate economic policy;
- a matched template resolves to an exact `EconomicActionProposal` using
  checked fixed-point values and explicit venue/instrument constraints;
- authoritative and shadow outcomes, provenance, and exact proposals are
  retained in bounded hot/cold decision records;
- canonical command/result/snapshot schema V3, direct apply, snapshot replay,
  and generated-model replay agree;
- authoritative apply remains pure, bounded, transport-free, storage-free, and
  venue-free; and
- cache-line/layout ceilings are enforced, including a 64-byte hot decision
  record.

Acceptance evidence:

- Git tag: `v2-phase3-accepted`
- Evidence document:
  `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md`
- Physical Pi evidence directory:
  `~/.local/share/velocita/v2-phase3-acceptance-pi-gates/20260907T212930Z`
- Accepted source root:
  `f2d350757c9f2f71f0eab4e2600af982bea44d56027082fafc70764c77073f5b`
- Pi result: all Phase 3 suites passed; `throttled=0x0` before/after;
  43.55 C -> 49.60 C.
- In-memory Pi measurements only—no Raft/WAL/network:
  32-rule worst case p50/p99 `222/241 ns`, action resolution `185/204 ns`,
  authoritative reducer `740/778 ns`.

The measurements above are development signals, not end-to-end throughput or
venue-latency claims.

## Current phase

Phase 4—risk reservation, orders, and transactional outbox.

Objective: make an accepted authoritative economic decision atomically create
all four of the following, or none of them:

1. risk reservation;
2. cluster-owned order intent;
3. retained idempotency receipt/result; and
4. committed outbound effect with deterministic `EffectId` and payload digest.

Phase 4 owns end-state guarantees G-010 through G-020 and contributes to
G-021 through G-026 in `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`.

## Implemented behavior relevant to Phase 4

Already available as inputs/foundations:

- strong IDs including `DecisionId`, `ClusterOrderId`, and `EffectId`;
- bounded `EconomicActionProposal` created by Phase 3;
- canonical `OrderEffect` contract and payload codec as a boundary type;
- active policy, executor generation, kill epoch, and strategy mode;
- request deduplication and bounded decision hot/cold records;
- canonical kernel command/result/snapshot encoding and deterministic replay;
- compile-time object/payload ceilings and target cache-line reporting.

Phase 4 authorization, canonical V4 state, fill accounting, and the first
cancel/unknown lifecycle slice are GREEN on the host. This remains an
uncommitted development checkpoint, not a completed Phase 4 acceptance claim:

- `AuthorizeDecision` accepts only a retained authoritative Phase 3
  `place_limit` action with an exact valid proposal;
- current risk-policy, risk-state, and position versions must match;
- checked gross quantity, full-margin, and ceiling-rounded fee holds are
  validated before mutation;
- one reservation, cluster order, and pending normalized outbox record are
  created in one non-throwing commit section;
- domain-separated SHA-256 derivation supplies stable 64-bit order/effect IDs,
  with zero/collision failure closed;
- request replay returns the retained identities and exposure, while a
  same-request/different-payload retry remains a typed conflict; and
- risk, staleness, capacity, shadow, no-action, and rejected-decision failures
  leave Phase 4 counts and economic balances unchanged.
- kernel command/result schema V4 explicitly encodes `AuthorizeDecision`,
  `ObserveFill`, `RequestCancel`, `RequestReplace`, and `ObserveOrderEvent`; V3 is not
  reinterpreted;
- snapshot schema V4 retains risk accounts, reservations, orders, outbox
  operation/state, position quantity, and fill/venue-event result identities;
- snapshot decision records omit only identities proven equal to the retained
  frame/hot record, keeping the current worst case at 65,241 bytes under 64 KiB;
- accepted partial fills atomically move signed quantity into position,
  proportionally and conservatively reduce the remaining margin/fee/notional
  holds, and increment `PositionVersion`;
- `(VenueId, ExecutionId)` suppresses duplicate fill economics and conflicts
  on a different retained command digest; accepted fill and venue-event
  identities share a reviewed 64-entry logical horizon inside the existing
  request-receipt arena;
- cancel intent appends a distinct generation-2 cancel effect and remains fully
  risk-bearing; `effect_unknown` also retains the hold; only accepted cancel or
  fill evidence releases residual economics; and
- the tested fill/cancel race preserves a filled terminal result even when
  cancel evidence arrives later.

The focused target is `v2-risk-order-outbox-tests`. Intended compile/runtime
REDs were observed for authorization, V4 codec/snapshot state, fill contracts,
cancel/unknown contracts, and cancelled-quantity snapshot conservation. The
focused suites and all 23 Release tests pass. Current host layout is
81,144-byte `TradingState` under the unchanged 81,920-byte ceiling, with
112-byte risk accounts, 64-byte reservations, 48-byte orders, 64-byte
normalized outbox records, and 776 bytes of remaining provisional headroom.
`ApplyResult` remains 112 bytes, canonical results remain 128 bytes, and the
command ceiling remains 1,920 bytes.

Review corrections implemented through explicit runtime RED/GREEN on 2026-09-08:

- New submit/replace authorization rechecks current strategy mode, kill epoch,
  and executor generation. Historical retries still return historical outcomes.
- Delayed acknowledgements/unknown/rejections cannot regress tested terminal
  orders, partial-fill knowledge, or newer cancel intent.
- Provisional replacement keeps old active terms until acknowledgement and
  holds componentwise maximum old/new exposure; acknowledgement/rejection
  collapses that hold. This is NOT a proven venue-neutral safe replacement:
  maximum is sufficient only if old/new executions are mutually exclusive.
- A fill during pending/unknown replacement preserves risk and marks the
  order/current effect reconciling. Ordinary acknowledgement cannot clear it.
  This defers real fill accounting; reconciliation itself remains unimplemented.
- Submit decision retries after full fill return the retained original receipt,
  not the consumed reservation. Replacement decision retries after ack/reject
  likewise return the original effect/exposure without another mutation.
  Receipt lookup is bounded O(receipts * effects), allocation-free, with no
  added resident bytes or canonical schema changes in these retry fixes.

Latest host validation: full `build-phase4` Release build and 23/23 CTest suites
pass. This is development evidence, not Phase 4 acceptance. Existing generated
model coverage is Phase 1–3, not a Phase 4 reference model. The 16-slot arrays
currently bound retained lifetime records, not automatically reclaimed live
orders. Snapshot headroom is only 295 bytes; do not silently expand capacity.

Not implemented yet:

- canonical configuration commands for risk limits;
- final review of the normalized `OrderEffect` boundary produced from retained
  outbox/decision state;
- terminal tombstone compaction and replicated expiry;
- safe replacement and its lifecycle/economic transitions (DEFERRED by the
  2026-09-08 scope decision; not Phase 4 work);
- ordered venue evidence across fills/order events and complete reconciliation;
- position/margin/fee accounting beyond signed filled position quantity and
  remaining open-order holds;
- broader acknowledgement/reject/reconciliation transition coverage;
- Phase 4 model/property/replay corpus and target gate.

An existing `OrderEffect` value/codec is not evidence that an effect can yet be
created authoritatively. It is only a validated boundary contract.

## Decisions that must be reviewed before broad implementation

Keep the Phase 4 design minimal, bounded, and MVP-focused. Present the proposed
contracts to the user before locking them into canonical bytes:

1. exact risk-account dimensions and conservative reservation formula for
   submit, cancel, and replace;
2. minimum cluster order and reservation lifecycle states;
3. deterministic derivation and collision handling for `ClusterOrderId` and
   `EffectId`;
4. bounded capacities for orders, reservations, fill/event receipts, and
   outbox effects, including full behavior and provisional Pi/NUC headroom;
5. replacement semantics that cannot double reserve or briefly under-reserve;
6. partial-fill, cancel/fill-race, and unknown-submission accounting rules;
7. exact additions to `Command`, `ApplyResult`, canonical schema, snapshot, and
   state digest; and
8. which Phase 4 state is hot versus cold and its cache-line/arena budgets.

Prefer the conservative fail-closed choice when evidence is incomplete. Do not
silently clamp an unsafe proposal into an accepted order; return a typed reason.

## Exact next RED test

MVP replacement scope RESOLVED on 2026-09-08: direct amend/replace is DEFERRED.

Rationale, recorded so it is not relitigated: G-018 forbids holding old and new
terms at once; G-011 forbids under-reserving. Whenever the venue can leave both
orders live, `old + new` is the only hold satisfying G-011 and it violates G-018,
while `max(old, new)` satisfies G-018 and under-reserves. The two guarantees are
unsatisfiable together while concurrency is possible, and the proof that a given
venue's replace is atomic is a Phase 5 venue-simulator observable, not a reducer
property. Venue check on 2026-09-08 found no cancel/amend/replace endpoint
published for True Markets on either the CeFi or DeFi path.

Implemented (host GREEN, all 23 CTest suites pass):

- `ApplyCode::replacement_not_supported`: `apply_request_replace` refuses before
  touching economics. Wire kind 16 stays allocated so reintroducing replacement
  needs no renumbering.
- `ApplyCode::order_slot_occupied`: at most one unresolved cluster order per
  `(account, instrument, venue, logical order slot)`, enforced in
  `apply_authorize_decision` before any mutation. Occupancy is defined by a
  reservation that is not `released`, because retained risk is what G-018
  constrains. This closes a real defect: `ClusterOrderId` is derived from the
  varying `DecisionId`, so cancel-then-authorize previously created a second
  order and a second reservation on the same slot - reaching the double-reserved
  state that deferring replacement was meant to prevent. The RED for
  `test_fresh_submit_cannot_bypass_unresolved_cancelled_slot` observed exactly
  that.
- Deleted: the `max(old, new)` hold in `apply_request_replace` (276 lines) and
  the now-unreachable replace acknowledgement/rejection branch in the observation
  handler (140 lines).
- Deleted: `ClusterOrder::active_filled_baseline`, whose only purpose was
  reconstructing an old residual after a rejected replacement. Every read lived
  in the deleted branch. Removed from the struct, snapshot validation, encode,
  and decode; `kStateSnapshotClusterOrderBytes` 43 -> 35.
- Removed five replacement-economics tests; converted the outbox materialization
  test to a submit/cancel effect pair, and the replacement control-recheck
  assertion to an unconditional-refusal assertion (strictly stronger: no control
  state permits a replacement).

Measured layout after the increment:

```text
ClusterOrder            48 -> 40 bytes
TradingState        81,144 -> 81,016 bytes   (headroom 776 -> 904)
max canonical snapshot 65,241 -> 65,113 bytes (headroom 295 -> 423)
```

The snapshot headroom increase is deliberate and should be spent on B2 below,
not on new capacity.

Two new tests in `v2-risk-order-outbox-tests`:
`test_request_replace_fails_closed_without_economics` and
`test_fresh_submit_cannot_bypass_unresolved_cancelled_slot`. Both assert
economics are byte-unchanged on refusal and hold across snapshot restore. The
second also proves the slot becomes reusable only after `cancel_reconciled`
final evidence releases the residual.

This is unaccepted host development evidence. No sanitizer, ARM64, or physical
Pi gate has been run for it, and no Phase 4 evidence document exists yet.

Exact next RED: B2, terminal tombstone compaction and replicated expiry. The
16-slot order/reservation/outbox arrays bound retained lifetime records, not
reclaimed live orders, so the kernel currently fails closed after 16 lifetime
orders on a slot. Slot exclusivity makes this reachable sooner, because a
requote cycle now always consumes a fresh order slot. Define terminal states,
add a replicated expiry transition (never a wall-clock eviction), and prove no
receipt or decision is reclaimed while an effect still needs it for
`materialize_order_effect`.

Reconciliation contract approved and implemented. The runtime RED
`test_cancel_before_earlier_fill_does_not_lose_fill_evidence` failed with
"earlier venue fill delivered after cancel was lost from accounting".
Venue sequence 1601 is a one-lot fill, 1602 is cancellation; delivery is 1602
then 1601. Cancellation currently releases all remaining reservation; the late
fill is refused with fill_quantity_conflict because the reservation is released.
It is now GREEN: cancellation retains the hold and marks order/cancel effect
reconciling; a valid delayed fill consumes only its quantity and does not reopen
the order. Full fill may finish the order because no unfilled residual remains.

Approved economic contract: distinguish stopped
order from reconciled accounting. Cancellation without evidence of fill
completeness retains unresolved hold and enters reconciling; valid delayed fills
must be applied exactly once without reopening the order. Release unused residual
only after scoped final-order evidence establishes final cumulative filled quantity
and agrees with the locally accepted fills. Implemented `cancel_reconciled` kind
6 in VenueObservation schema V2, still 116 bytes: quantity is explicitly final
cumulative filled quantity for this kind; price/fee/execution ID must be zero,
quantity nonnegative, and venue order identity present. The observation is scoped
by venue, cluster order and current cancel effect. It requires matching locally
filled quantity and a reconciling cancellation (or matching already-full fill).
No position is inferred from a cumulative total. Mismatch returns
venue_observation_not_applicable, retaining risk. Evidence received too early is
not buffered for automatic completion: after missing fills arrive, the caller
must submit a new request; retrying the old request preserves its refusal.

Both plain cancellation and final reconciliation remain venue evidence, whose
authenticity/finality must be established upstream. No contiguous stream watermark
is implemented; a larger sequence alone is NOT proof of completeness. This does
not solve arbitrary contradictory terminal evidence or replacement reconciliation.

Observation boundary/fingerprint explicitly advance to V2: SHA-256 over ASCII
`velocita/venue-observation/v2` without NUL plus canonical V2 bytes. V1 observation
payloads are rejected. Kernel command/result/snapshot remain development V4, and
no fields/capacity were added. Tests prove hold preservation before delayed fills,
snapshot restore while pending, matching/mismatched final totals, request/event
dedup, and byte-identical direct vs encoded reconciliation replay snapshots.
Negative final quantity exposed an additional runtime RED: the typed refusal
could not serialize because kernel codec's ContractError range stopped at an old
enum member. Its range now includes all existing declared ContractError values.
No new error enum was introduced. All current changes remain unaccepted Phase 4
development, not sanitizer/ARM64/Pi acceptance.
Fresh full Release build and 23/23 CTest suites pass after reconciliation and
refusal-codec fixes. Layout report remains 112-byte result, 160-byte receipt,
128-byte wire result, 81,144-byte state and 65,241-byte maximum snapshot;
`git diff --check` is clean. Unrelated pre-existing dirty paths remain untouched.

Latest cross-kind identity checkpoint:
- `test_fill_cannot_reuse_an_order_event_identity` observed runtime RED: an
  acknowledgement and fill reused one venue/session/sequence. GREEN adds an
  existing event-receipt lookup before fill deduplication/economics, returning
  venue_event_identity_conflict. The test also verifies the guard after snapshot
  restore. All 23 Release suites passed before the reverse-direction test below.
- The reverse runtime RED: `test_order_event_cannot_reuse_a_fill_identity` failed
  with "cancel evidence reused a fill venue/session/sequence and released risk".
  A partial fill was accepted, then cancel confirmation reused that fill's stream
  identity and incorrectly released residual risk. This is now GREEN using the
  approved tagged metadata implementation below.
- Approved and implemented result/snapshot contract: use explicitly tagged
  16-byte result metadata for policy ID/version on policy-related results, and
  venue session/sequence on fill results. Preserve strongly typed accessors; do
  not silently put venue IDs in fields still named policy IDs. Existing fill
  results do not use those policy fields. `ApplyResult::metadata_words` stores
  two uint64 words, discriminated by existing detail_kind. Typed policy_id(),
  policy_version(), fill_session_id(), and fill_sequence() accessors return an
  absent ID for the wrong tag. No C++ union aliasing or new tag allocation.
  Canonical codecs read/write the same two positions; successful/duplicate fill
  results now require nonzero session/sequence. A separate codec runtime RED
  proved that missing stream metadata needed rejecting before this validation.
  Old unaccepted V4 fill results without stream identity fail validation; this
  amends development V4 semantics, not accepted V3 bytes. Phase 1–3 regression
  goldens remain unchanged.
- Shared retained-event lookup now covers applied fills and order events.
  Cross-kind conflicts and different executions reusing the same stream identity
  return venue_event_identity_conflict before economics. Execution-ID dedup remains
  independently enforced. Tests prove both directions across snapshot restore and
  distinguish identical sequence numbers in different sessions. One legacy race
  fixture incorrectly reused fill sequence 501 for cancel confirmation; it now
  uses distinct sequence 503 so it tests the intended race, not identity conflict.
- Full Release build and 23/23 suites pass. Measured result/receipt/wire-result
  sizes remain 112/160/128 bytes; resident state remains 81,144 bytes; max snapshot
  remains 65,241 bytes. No added resident capacity, sanitizer or target-gate claim.

Cancel contract approved and GREEN: whole-order cancel uses zero price/quantity,
retains scope/provenance and authorization-time controls, and hashes the full
20-word instruction preimage instead of eight identity/control words. Existing
proposal side/TIF/post-only are retained as metadata, not new trading parameters.
Cancel intent does not release risk. Full-byte tests cover partial fill, cancel
completion, control changes and snapshot restore; boundary validation rejects
nonzero cancel price/quantity. All 23 Release suites passed after this increment.
No additional resident fields or payload bytes were added. Cancel digests (and
snapshots containing them) change from the earlier unaccepted development draft;
old draft cancel digests fail reconstruction rather than being silently rewritten.

Observation fingerprint contract approved and GREEN. The copied-digest RED above
was observed, then fixed by `observation_fingerprint` in digest.hpp/v2_digest.cpp.
Current preimage (advanced for reconciliation): ASCII `velocita/venue-observation/v2`
without NUL, followed by the 116 canonical VenueObservation V2 bytes, including schema, all IDs/economic
fields, and evidence_digest. SHA-256 covers the entire preimage. Caller/request
identity is excluded so genuine cross-request retries have the same fingerprint.
The allocation-free implementation uses a fixed stack buffer and explicit LE
encoding, tested against the independent existing payload encoder plus SHA-256.

ObserveFill/ObserveOrderEvent producers must set CommandIdentity.payload_digest
to that fingerprint. Kernel apply verifies it before receipt lookup or economic
deduplication; mismatches return invalid_command without advancing state_version
or retaining a receipt. A separate runtime RED exposed wrong command-wrapper
reuse of a valid observation; wrapper/kind compatibility is now checked before
deduplication too. All other command digest semantics remain unchanged.
Tests cover same/cross-request alteration, correctly fingerprinted execution/event
conflicts, valid duplicate receipts after snapshot restore, and existing fill replay.
Full Release build plus all 23 CTest suites pass after this increment.
Resident layout and serialized field layouts are unchanged; observation digest
values now follow this contract rather than arbitrary caller values. Old unaccepted
development snapshots with unchecked observation digests are not migration-tested.
This proves content consistency, not venue authenticity or ordered evidence.

User approved OrderEffect schema V2 on 2026-09-08. The missing-post-only compile
RED is now GREEN: V2 is 166 bytes with the explicit one-byte PostOnlyMode appended
at offset 165. V1 is rejected, not reinterpreted. Unknown enum values and required
post-only with incompatible time-in-force fail validation. The enum moved from
action_template.hpp to contracts.hpp to share one definition without a circular
include; the in-memory field occupies padding beside time-in-force. This boundary
change does not change kernel V4 command/result/snapshot bytes or resident state.

`test_outbox_reconstructs_full_immutable_payload` first failed compilation for
the missing materialization API, then passed with `materialize_order_effect`.
It independently constructs expected submit/replace payloads and compares every
encoded byte in pending, unknown, and acknowledged/control-changed states, also
after snapshot restore at each stage. Corrupt digests, changed proposal post-only,
changed authorization generation, missing identity, and absent authorization
receipts fail closed. The API returns an owning optional value, is noexcept and
allocation-free, and scans O(effects + receipts + decisions), all bounded.
Canonical boundary encoding remains the existing vector-based outer-layer API;
it is not called by the reducer or materializer.

Authorization-time metadata comes from the original applied receipt, not mutable
order/current controls; economics come from the effect's own retained proposal.
The digest is recomputed using the existing 20-word domain-separated semantic
preimage (including post-only), not recursively hashing the serialized digest
field. Full-byte comparisons are additional evidence, not replaced by hash checks.
No retained receipts/decisions may be reclaimed while their effects need them.
Cancel materialization has subsequently been implemented as recorded above.
No sanitizer/ARM64/Pi claim is made.
After this schema/materialization increment, a fresh full Release rebuild and
all 23 CTest suites passed; `git diff --check` was clean. Layout remains
81,144 bytes for TradingState and 65,241 maximum canonical snapshot bytes.

The eight contract decisions were reviewed and approved on 2026-09-07.
User authorized continued development and corrective review. Earlier replacement
wording claiming old-plus-new exposure must never be held was too strong:
coexistence may require both holds. Do not freeze that assumption into canonical
contracts. The exact next RED extends `v2-risk-order-outbox-tests`:

```text
After the MVP scope decision above: if direct replacement is deferred, prove
RequestReplace fails closed without economics/effects, and that a fresh decision
cannot bypass unresolved risk for the cancelled order's slot. If retained, first
specify old/new execution attribution and reconciliation evidence before adding
canonical fields, then test delayed old fills without consuming new terms.
```

Fingerprint, cross-kind identity, and first delayed-cancel reconciliation REDs
above are GREEN. Fill receipts retain session/sequence and execution identity.
Terminal evidence permutations are GREEN; next resolve the replacement scope choice;
no extra state-capacity budget is approved.
Review any material
new byte-contract choice with the user before freezing it. Then prioritize
observation-content identity, safe replacement/reconciliation, snapshot invariants,
and a real Phase 4 model/replay corpus. No gateway, venue adapter, Raft, WAL,
netting, strategy, or optional V2.2 work belongs in these increments.

## Acceptance evidence required for Phase 4

The phase cannot close until retained evidence proves:

- no effect without reservation and order intent;
- duplicate request/decision returns the same effect, never a second mutation;
- insufficient or stale risk creates no order/effect;
- reservation/filled/remaining quantities conserve under partial fills;
- unknown submission and cancel-pending remain risk-bearing;
- replacement never double-reserves or under-reserves (satisfied by refusing
  direct replacement and enforcing one unresolved order per logical slot);
- replay reconstructs byte-identical pending/unknown effects;
- snapshot retains receipts/effects through the declared retry horizon;
- bounded full policies fail atomically; and
- fault/property/model tests cannot create an unreserved effect, negative
  reservation, duplicate economics, or replay mismatch.

Evidence must identify commit/source root, canonical schema/digest changes,
Release and sanitizer results, layout/capacity report, ARM64 container result,
physical Pi result, power/temperature state, limitations, and rollback point.

## Unresolved failures and provisional limits

- No known failing accepted Phase 3 test or physical Pi gate.
- Phase 4 is in progress; the host RED/GREEN evidence above is not yet the
  sanitizer/ARM64/physical-Pi acceptance gate.
- The `TradingState` 80-KiB ceiling and current capacities are provisional until
  deferred NUC capacity validation; do not increase them silently.
- Phase 3's benchmark excludes Raft, WAL, networking, and venue I/O.
- TrueMarkets resting-order/post-only/order-book capability remains unconfirmed;
  that blocks adapter/live claims, not Phase 4's venue-neutral OMS work.
- The proposed two-machine topology is not accepted high availability.
- A logical order slot held by an order stuck in `reconciling` is not
  recoverable without operator action, and no operator action exists yet. There
  is no replicated timeout, no escalation, and no override command. The slot
  silently stops quoting and is indistinguishable from a quiet market until the
  Phase 4 metrics in C4 exist. Tracked as G-074a in
  `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`; the resolution is an operator-
  driven replicated transition plus a runbook entry, owned by Phases 8 and 10.
  Do not "fix" this in Phase 4 with a wall-clock timeout in the reducer: that
  breaks determinism, and releasing a hold without venue evidence of final
  cumulative filled quantity is the G-011 violation reconciliation exists to
  prevent.

## Important ADRs and contracts

Read only these first; chase additional references only when the current task
requires them:

1. `docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`, especially sections 2,
   3, 4, and 9 (Phase 4).
2. `docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md`, especially
   transactional outbox, authority, persistence, and non-negotiable invariants.
3. `docs/V2_ARCHITECTURE_CONTRACTS.md`, especially sections 3 and 7–10.
4. `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`, especially G-010–G-026 and
   deviation tripwires.
5. `docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md` for the accepted input boundary.
6. `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md` for the exact
   accepted checkpoint and limitations.
7. `docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md` only to avoid pulling deferred
   work into V2.

Primary C++ starting points:

- `include/velocita/v2/kernel.hpp`
- `src/v2_kernel.cpp`
- `include/velocita/v2/action_template.hpp`
- `include/velocita/v2/contracts.hpp`
- `include/velocita/v2/kernel_codec.hpp`
- `src/v2_kernel_codec.cpp`
- `include/velocita/v2/state_snapshot.hpp`
- `src/v2_state_snapshot.cpp`
- `tests/v2_decision_kernel_tests.cpp`
- `tests/v2_replay_tests.cpp`
- `tests/v2_model_replay_tests.cpp`
- `tests/v2_layout_tests.cpp`

## Commands needed to continue

Verify the handoff:

```bash
cd /Users/vishaljha/aeron-raft-pi
git status --short
git branch --show-current
git rev-parse HEAD
git rev-parse v2-phase4-handoff-start
git log --oneline --decorate -8
git diff --check
```

Read the bounded context:

```bash
sed -n '1,260p' docs/CURRENT_HANDOFF.md
sed -n '442,484p' docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
sed -n '1,240p' docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md
sed -n '1,340p' docs/V2_ARCHITECTURE_CONTRACTS.md
sed -n '45,115p' docs/V2_END_STATE_GUARANTEE_CHECKLIST.md
```

Run the existing host regression baseline using an out-of-tree build directory
(do not overwrite an existing user build):

```bash
cmake -S . -B build-phase4 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-phase4 -j2
ctest --test-dir build-phase4 --output-on-failure
```

Before a physical Pi gate, synchronize explicitly while preserving Pi-local
Git/build/evidence state:

```bash
rsync -av --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'build-*/' \
  ./ bhaikapi@100.84.151.116:~/aeron-raft-pi/
```

Do not invent a Phase 4 Pi acceptance command until its gate script and
evidence contract exist. The previous accepted Phase 3 gate is
`deploy/run-v2-phase3-acceptance-pi-gate.sh`; it is a regression reference, not
a Phase 4 acceptance gate.

## Explicit non-goals for Phase 4

- no execution gateway or external venue calls (Phase 5);
- no generic V2 Raft integration (Phase 6);
- no asynchronous WAL, block arena, snapshot publication, or compaction work
  beyond extending deterministic in-memory snapshot bytes (Phase 7);
- no executor fencing, automatic promotion, kill transport, or emergency lane
  implementation (Phase 8);
- no TrueMarkets order adapter, credentials, or live/paper order submission
  (Phase 9);
- no Grafana/release-operating expansion beyond metrics contracts (Phase 10);
- no strategy fitting, profitability claim, leaderboard claim, or real-money
  activation (Phase 11);
- no cross-strategy portfolio netting; it is explicitly deferred to V2.1;
- no `valid_state` performance rewrite without measurement; deferred to V2.1;
- no good-to-have feature that is not necessary for the Phase 4 invariants;
  defer such work to V2.2.

## New-session bootstrap prompt

Copy the prompt below into the new session:

```text
Work in /Users/vishaljha/aeron-raft-pi on Phase 4: risk reservation, cluster
orders, and the transactional outbox.

Read completely:
- AGENTS.md, if present
- docs/CURRENT_HANDOFF.md
- sections 2, 3, 4, and 9 of docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
- the directly referenced ADR/contracts listed in the handoff

Inspect Git status, branch, HEAD, the v2-phase4-handoff-start tag, and recent
commits to verify the handoff. Preserve every pre-existing dirty path listed in
the handoff. Never use git add . and do not reset or delete unrelated work.

Do not reconstruct the project from historical chat. Git, tests, ADRs,
contracts, evidence, and docs/CURRENT_HANDOFF.md are authoritative memory.

First explain the eight Phase 4 contract decisions in CTO-level and deep C++
terms, including rejected alternatives, and ask me to review material choices
before freezing canonical bytes. Then continue by TDD from the exact next RED
test in docs/CURRENT_HANDOFF.md. Show RED for the intended reason, implement the
smallest GREEN increment, run focused and regression tests, and keep me educated
on what, why, how, ownership, memory layout, complexity, failure behavior, and
trade-offs.

Maintain MVP focus. Phase 4 must not pull gateway, Raft, WAL, venue adapter,
strategy, netting, or optional V2.2 work forward. Update CURRENT_HANDOFF.md as
the phase advances.
```
