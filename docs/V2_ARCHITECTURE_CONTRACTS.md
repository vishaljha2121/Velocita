# V2 architecture contracts

Status: proposed implementation contract under accepted ADR 0001. A contract
is not an implementation claim. Field sizes and limits become wire/storage
compatibility promises only after their Phase 1 golden-corpus gate is accepted.

Architecture decision:
[`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)

Execution plan:
[`V2 TDD implementation and validation plan`](V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md)

## 1. Purpose

This document defines the boundary that V2 code must obey. It tells an
implementer what is authoritative, which messages cross each boundary, which
state must survive replay, and what failure means. It intentionally precedes
the trading strategy. A strategy may change its economics later without
changing order identity, risk accounting, external-effect delivery, or
reconciliation semantics.

The architecture is:

```text
raw venue data -> normalizer/model -> DecisionFrame + PolicyArtifact
                                      |
                                      v
                              Decision/OMS Raft FSM
                                      |
                                  OutboxEffect
                                      |
                                      v
                              execution gateway -> venue
                                      |
                               VenueObservation
                                      |
                                      +-------> Decision/OMS Raft FSM
```

## 2. Vocabulary and authority

- **Policy artifact:** immutable versioned rules or model output used to make
  decisions. Publication does not activate it.
- **Decision frame:** bounded canonical feature/input set proposed for one
  authoritative evaluation.
- **Directive:** deterministic result of evaluating a frame against activated
  policy and current replicated state.
- **Order intent:** cluster-owned desired economic action.
- **Outbox effect:** committed instruction that permits a gateway to attempt an
  external action.
- **Venue observation:** typed evidence produced by the gateway from venue
  messages or reconciliation queries.
- **Unknown outcome:** an attempted external effect whose existence or terminal
  result cannot yet be proved.
- **Reconciliation watermark:** explicit boundary at which venue and local
  evidence were compared.
- **Executor generation:** monotonically advancing authority generation for the
  logically active gateway.
- **Kill epoch:** monotonically advancing global risk-control generation.

| Fact | Authority | Disposable copies |
|---|---|---|
| Activated policy | Decision/OMS cluster | model cache, dashboard |
| Strategy mode and kill epoch | Decision/OMS cluster | gateway cache |
| Order intent and reservation | Decision/OMS cluster | gateway work queue |
| Whether an external order/fill exists | venue evidence accepted by cluster | raw response cache |
| Position and fill ledger | Decision/OMS cluster | SQL/report projection |
| Socket/session state | execution gateway | none |
| Raw book state | market-data edge | replay archive |
| Metrics and charts | no trading authority | Prometheus/Grafana |

## 3. Identity contract

IDs are domain types, not interchangeable integers. C++ uses strong wrappers so
an `EffectId` cannot accidentally be passed where an `OrderId` is required.

Minimum identities:

- `CallerId + RequestSequence`: idempotent cluster command identity;
- `PolicyId + PolicyVersion`: immutable policy identity;
- `FeatureSchemaId`: semantic units and ordering of decision-frame features;
- `DecisionId`: one accepted policy evaluation;
- `ClusterOrderId`: immutable internal order identity;
- `EffectId`: immutable external-effect identity, derived from committed order
  identity and effect generation rather than process randomness;
- `ExecutorGeneration`: current sender authority;
- `KillEpoch`: current risk-control authority;
- `VenueSessionId + VenueSequence`: source ordering when supplied;
- `VenueOrderId`: venue-assigned order identity; and
- `VenueId + ExecutionId`: preferred fill deduplication identity.

Identity retention must exceed the maximum retry, recovery, snapshot restore,
and reconciliation horizon. Expiry is a replicated transition, never a local
wall-clock eviction.

## 4. Common command envelope

The first implementation shall define an explicit byte encoding with:

```text
magic | schema_version | message_kind | flags | payload_length
caller_id | request_sequence | correlation_id
source_id | source_generation | source_sequence
event_time | receive_time | payload | checksum/digest
```

Rules:

- integers have fixed width and declared byte order;
- no C++ object layout, padding, pointer, `std::string`, or container layout is
  copied directly to the wire or snapshot;
- unknown versions and unknown mandatory flags fail closed;
- all lengths are bounded before allocation or copy;
- trailing undeclared bytes are rejected;
- money, price, size, and fees use checked scaled integers;
- times are values supplied by an edge command, not clocks read during apply;
- decode/validation errors are typed and never partially mutate state; and
- an accepted duplicate must have the same payload digest and returns the
  retained original outcome.

## 5. Policy artifact contract

An artifact includes at least:

```text
policy_id | policy_version | schema_version | feature_schema_id
instrument/account scope | model digest | artifact digest
validity boundary | risk-policy version | bounded rules/actions
```

Lifecycle:

```text
PROPOSED -> VALIDATED -> ACTIVE -> SUPERSEDED
     |          |
     +-> REJECTED
                +-> EXPIRED
```

Publication, validation, and activation are separate commands. Activation
requires every voter to possess or deterministically decode the exact artifact
identified by its digest. A compact artifact may be stored directly in the log
and snapshot. A large artifact may use content-addressed immutable storage, but
activation cannot succeed until every voter proves the same digest is locally
available.

Floating-point fitting may occur outside the core. Values used by the reducer
must have a pinned representation and deterministic comparison semantics. V2's
initial live policy evaluator uses fixed-point features and thresholds.

## 6. Decision-frame contract

A frame contains only the bounded inputs required for one decision:

```text
policy identity and feature schema
venue/session/source sequence
event and receive timestamps
instrument/account identity
fixed-point feature vector
market-data readiness and age
position/risk source versions when proposed externally
raw evidence digest
```

The frame is rejected if:

- its policy or feature schema is inactive or unknown;
- source identity regresses without an explicit new generation;
- required data is stale, recovering, crossed, incomplete, or otherwise not
  ready under policy;
- a value cannot be represented in its declared scale;
- its evidence digest or payload is malformed;
- the strategy mode prohibits new risk; or
- required order, position, reservation, or reconciliation state is unknown.

A prefilter may suppress frames that obviously cannot act. It may not authorize
an effect. The final predicate is evaluated in the cluster.

## 7. Deterministic reducer contract

Conceptually:

```cpp
ApplyResult Apply(TradingState& state, const Command& command,
                  ApplyScratch& scratch);
```

The concrete C++ contract must satisfy:

- one owner thread mutates `TradingState`;
- commands are immutable after decoding;
- scratch memory is preallocated and bounded;
- apply performs no external I/O or service call;
- apply does not read a clock or random source;
- invalid business inputs return typed results rather than relying on an
  exception to express normal rejection;
- `noexcept` is used only after every reachable operation proves it cannot
  throw; and
- the same state bytes and command bytes produce the same new state digest,
  outcome, and outbox bytes.

Candidate command family:

- `PublishPolicy`, `ActivatePolicy`, `SupersedePolicy`;
- `EvaluateDecisionFrame`;
- `SetStrategyMode`, `AdvanceKillEpoch`, `SetExecutorGeneration`;
- `ObserveOrderAck`, `ObserveOrderReject`, `ObserveFill`;
- `ObserveCancel`, `ObserveEffectUnknown`;
- `ApplyReconciliationResult`;
- `ExpireIdentity`, `Checkpoint`, and versioned administrative corrections.

## 8. Order, reservation, and position contracts

Minimum order knowledge states:

```text
DESIRED -> INTENT_COMMITTED -> DISPATCH_AUTHORIZED
                                |       |
                                |       +-> SUBMISSION_UNKNOWN -> RECONCILING
                                v
                         ACKNOWLEDGED -> LIVE -> PARTIAL -> FILLED
                                           |
                                           +-> CANCEL_PENDING -> CANCELLED
                                           +-> REPLACE_PENDING -> LIVE/UNKNOWN
```

Rules:

- intent and actual venue evidence are retained separately;
- unknown submission retains the full applicable reservation;
- cancel-pending residual quantity remains risk-bearing;
- a partial fill moves only executed quantity into position and releases only
  the policy-approved unused reservation;
- fill/cancel races are applied by venue sequence or explicit reconciliation
  order and never release the same hold twice;
- only accepted fill or correction evidence changes position; and
- a correction is a new attributable command, never history mutation.

The minimum reservation record retains original, filled, remaining, fee/margin
hold, account, instrument, order, command, policy, and position versions.

### 8.1 Replicated risk-limit configuration (B1, approved 2026-09-09)

This is the authoritative reference for later operator/user documentation.
`ConfigureRiskLimits` replaces all six settings for one `(AccountId, InstrumentId)`
in one reducer transition. It does not configure a venue account, authenticate an
operator, import external positions, or send an external effect.

- `max_order_quantity`: per-order quantity ceiling, positive and no larger than
  `max_reserved_quantity`, the positive aggregate reserved-quantity ceiling.
- `max_order_gross_notional`: positive per-order gross notional ceiling.
- `max_reserved_exposure`: positive aggregate `reserved_margin + reserved_fee`
  hold budget, NOT aggregate gross order notional. There is deliberately no
  comparison requiring per-order gross notional to be below this hold budget.
- `initial_margin_ppm`: 1 through 1,000,000; `maximum_fee_ppm`: 0 through 1,000,000.
  All arithmetic is checked; invalid settings are refused, never clamped.

Creation requires an absent key and `expected_risk_version = 0`; it initializes
RiskVersion and PositionVersion to 1 and all economic balances to zero. This is
an empty kernel ledger, not evidence that a real trading account has no position.
Update requires exact expected/current RiskVersion equality; the kernel advances
by exactly one, including fresh-request no-op updates. Overflow refuses rather
than wrapping. There is no delete/reset command and no caller-chosen next version.
Expected kill epoch and executor generation must match current state; configuration
is allowed while halted. Matching generations do not replace operator authentication.

Updates preserve position, reserved quantity, reserved margin/fees, and
PositionVersion. Caps are prospective: changing per-order caps neither cancels
old orders nor rewrites their effects. Aggregate caps must still cover current
holds. Changing either rate requires a strictly newer RiskPolicyVersion; policy
versions cannot regress. Rate or policy-version changes require no unresolved
reservation for that account/instrument, including unknown/reconciling/conflict
orders. Released reservations and nonzero filled position alone do not block
configuration; B1 does not revalue position margin. A policy-version change may
require a matching policy deployment before new authorization can resume.

The single reducer owner validates a complete local candidate before assigning
it to TradingState; receipt capacity is checked before this commit. Other threads
must not read mutable TradingState concurrently. This is transaction atomicity,
not an 88-byte CPU atomic store or a durability claim. A mask/patch could also be
atomic, but full replacement avoids partial-update semantics on an infrequent
control path. The configuration cannot carry balance fields at all.

Exact request retries return the original result without another mutation.
Same-request/different-digest retries conflict; fresh stale updates refuse.
Admitted refusals can retain receipts and advance global StateVersion without
changing risk configuration or economics. Account/receipt capacity exhaustion
fails closed. New typed outcomes append `risk_account_capacity_exceeded`,
`risk_version_exhausted`, and `risk_reconfiguration_blocked`; existing outcomes
cover invalid settings, stale control/risk state, arithmetic overflow and
insufficient capacity.

Canonical kernel schema V5 command kind 17 has an 88-byte little-endian body:
account ID, instrument ID, risk-policy version, expected risk version, expected
kill epoch, expected executor generation (six u64); two quantities and two money
limits (four i64, in the order listed above); margin ppm and fee ppm (two u32).
With existing identity/framing/digest this is 180 bytes. Result detail tag 5 uses
the existing three u64 words for account ID, instrument ID and resulting risk
version. Snapshot schema V5 retains this new receipt variant; V4 is not silently
reinterpreted and no migration is supplied. OrderEffect bytes and schema remain
unchanged. No risk-account capacity or resident-state layout increase is required.

### 8.2 Resolved versus acknowledged (approved 2026-09-09)

These are different properties and the difference decides what may be reclaimed.

- **Acknowledged** is evidence about ONE instruction: the venue told us how a
  specific effect ended.
- **Resolved** is a property of an ORDER's risk: its reservation is released and
  its exposure is fully accounted for.

They usually coincide and must not be assumed to. `conflict` is terminal but
never resolved - internal state and venue evidence disagree, so exposure is of
unknown size. `reconciling` is likewise unresolved while final evidence is
outstanding. Reclamation and cleanup therefore test whether RISK is resolved, not
whether a state machine stopped; any future terminal-but-unresolved state then
inherits the correct behaviour without being added to a list.

The converse also occurs: an order can be resolved while one of its own effects
was never acknowledged. Once the order is closed, no future observation can be
meaningfully scoped to that effect, so it is ORPHANED. It is settled to
`OutboxState::incomplete`, which records that the outcome was never established.
It is deliberately not recorded as `acknowledged`, which would claim evidence the
venue never gave.

`pending` does not mean "never sent". `pending`, `claimed`, and `attempted` are
gateway-side bookkeeping the cluster learns about asynchronously, so a venue
response can legitimately arrive before the cluster is told the effect was
attempted. Venue evidence for a pending effect is therefore valid and must stay
accepted.

Orphan settlement runs only on DEFINITIVE evidence: final cancel reconciliation,
or a rejected submit the venue never accepted. A full fill is excluded even
though it is terminal, because a cancel that was in flight when the fill landed
is still owed a response ("too late, already filled"). That effect is awaiting
evidence, not orphaned, and auto-settling it would discard a response we have
every reason to expect. If that response never arrives it is the G-074a operator
situation, not something to heal automatically.

Without this, an order whose sibling effect never received evidence stays
permanently unreclaimable, and enough such groups wedge the bounded arenas:
evaluation fails closed and cannot recover even after the order reconciles.

`TradingState::settled_incomplete_effect_count` is a monotonic count of orphan
settlements. A rising count means the gateway is losing effect outcomes even
though reclamation remains healthy.

**Phase 8 dependency.** Reclaiming an `incomplete` effect makes its canonical
bytes unreconstructable, so a late gateway send against a closed order must be
rejected by executor-generation and kill-epoch fencing plus stable `EffectId`
idempotency rather than by retaining bytes. That hardening is a Phase 8
acceptance requirement; see G-057a.

### 8.3 V5 snapshot header growth (2026-09-09, for review)

Recorded explicitly because it falsifies a property a previous increment pinned
on purpose, and a reviewer should be able to judge the trade rather than discover
it as a broken assertion.

**What changed.** The canonical snapshot header gained
`settled_incomplete_effect_count`, a u64 appended after the existing control
prefix. `kStateSnapshotEmptyPayloadBytes` moves 53 -> 61; maximum canonical
snapshot 65,121 -> 65,129 bytes; resident `TradingState` 81,024 -> 81,032.
Headroom becomes 407 snapshot bytes and 888 resident bytes, both still inside
their reviewed ceilings. No per-record layout, capacity, or `OrderEffect` byte
changed.

**Property invalidated.** A guard asserted that V5's empty payload was
byte-identical to V4's apart from the schema byte, which documented that B1 added
no header field. That is now deliberately false. The guard was re-purposed to pin
the empty-payload SIZE rather than re-hash the payload, because hashing it again
would only restate the empty-state golden digest immediately above it and would
not independently detect anything.

**Why a header field rather than the alternatives.**

- A live gauge over effects currently in `incomplete` was rejected: settlement
  exists precisely so those records become reclaimable, so under the arena
  pressure that makes the signal interesting the gauge trends to zero while
  incompletes churn through it. It would read healthiest exactly when it matters
  most.
- A volatile, non-replicated counter was rejected because it would not survive
  snapshot restore and could differ between replicas, which makes it useless as
  evidence in a phase whose whole discipline is byte-identical replay.
- Deriving it from the WAL was rejected as circular: Phase 7 does not exist, and
  the counter's purpose is to be readable now.

**Why the cost is acceptable.** Eight bytes against 415 bytes of snapshot
headroom. The signal it carries - the gateway is losing effect outcomes even
though reclamation stays healthy - is not otherwise observable, and its absence
was the failure mode that wedged the arenas in the first place. The alternative
to spending these bytes is an operational blind spot on the exact condition Phase
4 just learned it must watch.

**What a reviewer should check.** That the counter is monotonic and never
decremented; that it is written and read at the same header position on both
codec sides; that it participates in the state digest so replay divergence is
detectable; and that no per-account or per-record structure grew, since those
multiply by capacity while a header field does not.

### 8.4 Unauthorized-decision retention (approved 2026-09-09)

A live maker evaluates continuously and most frames produce `no_action`, so the
decision arena fills with records no order will ever reference. Reclaiming order
groups does not help, because in that situation there is no resolved group to
free. Two rules make retained decisions reclaimable, with different
justifications.

**Structurally non-authorizable.** Every condition in the authorization
predicate - evaluation mode, decision code, effective directive, action
resolution code, proposal validity, and self-consistent proposal identity - is an
immutable property of the retained record. None depends on mutable state, so a
record failing any of them can never become authorizable however the world
changes. Reclaiming it needs no horizon and no clock. This covers `no_action`,
shadow, control-suppressed, and rejected evaluations, which is the dominant
volume.

**Superseded.** An actionable decision that was never authorized, for a slot
where a NEWER actionable decision exists, is reclaimable. A maker only ever wants
the latest quote for a slot; resurrecting an older proposal would rest a price
computed from older market data, which is the same objection that already
justifies refusing a stale `RiskVersion`.

Ordering is by ARRAY POSITION, not `DecisionId`. Decisions are appended and
compaction is stable, so a higher index is unambiguously later on every replica.
`DecisionId` is caller-chosen and must never be used for ordering.

**Strategy assumption, stated explicitly.** Supersession means the only way to
authorize a decision is promptly, before the edge produces a newer proposal for
that slot. That is correct for a market maker driven by continuous market data
and level-triggered quoting. It would be WRONG for a venue or strategy that
deliberately queues intents for later execution. A strategy of that shape needs a
different rule, not a tuned horizon.

**Identity is not lost.** Applied evaluation receipts outlive the decision arena
and retain the `DecisionId`, the payload digest, and the original outcome, so
re-presenting a reclaimed `DecisionId` with a different payload still returns
`decision_id_conflict` and an exact replay still returns the retained result.
Receipts hold 256 slots against the decision arena's 16.

**A referenced decision is never reclaimed** under either rule: the existing
reference pass un-marks any decision a surviving order or outbox effect still
names, so `materialize_order_effect` can always reproduce historical bytes.

**Implementation hazard.** `decision_is_authorizable` mirrors the predicate in
`apply_authorize_decision`. If that gains a condition and the mirror does not,
the sweep will reclaim records the authorizer would still have accepted. The two
must change together; there is no compiler check for this.

**Deferred to a later version: a state-version horizon.** Reclaiming any
unreferenced decision older than N state versions was considered and deferred.
It is deterministic - state versions, never wall-clock - and would bound
retention even for strategies that do not fit the supersession assumption. It was
not adopted because N cannot be justified today: the gateway retry horizon and
the venue reconciliation window are both unknown until Phase 5 supplies a venue
simulator. Revisit it there, alongside the reference-horizon question already
deferred from the reclamation contract.

### 8.5 Phase 4 metrics (approved 2026-09-09)

Two constraints from ADR 0001 decide the whole shape.

**Metrics are disposable projections and never trading authority.**
`collect_metrics` is therefore a PURE FUNCTION of `TradingState`. It adds no
field, participates in no digest, and cannot be read back by the reducer.
Anything a metric would need that state does not already carry is a signal to
question the metric, not to grow authoritative state.

**Apply reads no clock.** Section 15 asks for "oldest pending age" and
"decision-to-effect latency", which are durations, and a duration needs a clock
apply is forbidden to have. Every age is therefore measured in STATE VERSIONS -
a monotonic count of applied commands - so the quantity is exact, deterministic,
and identical on every replica. Converting to wall-clock time is the exporter's
job, on the exporter's clock, outside the authority boundary.

That is arguably the better operational measure and not merely a workaround.
State-version age answers "how many decisions has the cluster made while this sat
unresolved", which is load-relative: a stuck slot looks equally stuck in a fast
market and a quiet one, and the number cannot drift or jump when a host's clock
is corrected. A wall-clock age would do both.

Reported: arena occupancy against compile-time capacities; orders, effects and
reservations by state, indexed by raw enum value; counts and oldest ages for
unresolved orders, unsettled effects, reconciling orders (G-074a) and conflicted
orders; the maximum decision-to-authorization distance; the retained reclamation
and orphan-settlement counters; control state; and per risk account the reserved
exposure, position, versions, and headroom against the aggregate ceilings.

`unresolved` keys on RETAINED RISK, not on lifecycle state, matching the
reclamation rule. A metric counting "not terminal" would omit a conflicted order,
which is terminal and simultaneously the most important record on the dashboard.

An age for a record authorized at or after the current state version reports
zero rather than wrapping. The subtraction is unsigned, so a future-dated record
would otherwise appear maximally stale and turn a corrupt input into a page.

An effect record carries no version of its own, so its age is that of the order
that authorized it. That is the correct provenance: the effect has existed since
its order was authorized.

**`OutcomeCounters` is separate and deliberately not in state.** Duplicate and
refusal counts describe COMMAND OUTCOMES rather than retained records - a
duplicate that returned its retained result leaves no trace in state, which is
precisely what makes it a duplicate. They are accumulated by an observer as
results flow past. Putting them in `TradingState` would make an observability
counter part of the state digest and therefore of consensus.

### 8.6 Finite identity horizons (approved 2026-09-09)

Before this, nothing ever reclaimed a request receipt. Every command retains one,
refusals included, so the kernel accepted exactly `kMaxRequestReceipts` commands
in its lifetime and then refused everything permanently with no recovery path. A
second ceiling sat inside the same arena: at `kMaxVenueEventReceipts` accepted
venue events, fills stopped being accepted, which strands existing risk rather
than merely stopping new work. Both are availability defects, not tuning
questions.

#### The safety tension

G-023 requires a duplicate accepted request to return its retained result and
EffectId. G-022 requires at-most-once economic mutation. Forgetting a receipt
breaks G-023 as literally written. Whether it breaks G-022 depends entirely on
what a retry then does: re-executing would be a second economic mutation and is
unacceptable, while a typed refusal preserves G-022 and merely loses the original
answer. Any expiry design must therefore guarantee REFUSAL, never re-execution,
which means forgetting individual receipts while remembering enough per caller to
tell "expired" apart from "never seen".

#### Request receipts: per-caller watermark

Each caller has a retained cursor holding the highest `RequestSequence` whose
receipt has actually been RECLAIMED for that caller - deliberately not its
highest observed sequence.
Receipts may be forgotten freely. A request at or below its caller's watermark
with no retained receipt returns the typed refusal `request_expired` and mutates
nothing. A request above the watermark is fresh and executes normally.

Refusing on a highest-observed watermark was implemented first and rejected: it
refuses a low sequence the cluster never held, which enforces per-caller
monotonicity as a side effect of expiry. That is a stricter and different
contract, and it broke fixtures that had every right to number their requests
freely. The kernel must refuse only what it actually forgot.

A fourth arena needed the same treatment. Source-event receipts already have
their watermark in `SourceCursor`, and admission independently refuses a
regression, so a receipt at or below its cursor is no longer load-bearing: a
redelivery is refused as a regression rather than re-admitted. They are reclaimed
on the same fail-closed pattern.

Receipts are pinned in two families. AUTHORIZATION receipts are the provenance
`materialize_order_effect` uses to rebuild an effect's historical bytes, so
dropping one while its effect lives would strand a pending external effect and
break G-020; they become reclaimable once their order group is gone. FILL and
VENUE-EVENT receipts are pinned unconditionally for now, because a redelivered
PARTIAL fill would book twice and reservation arithmetic does not catch that -
filled plus remaining stays within the original, so the double booking looks
legitimate. Everything else, including every refusal, is reclaimable, and in a
live maker those are the overwhelming majority.

This needs no clock and no horizon constant. Rejected alternatives: a
state-version horizon requires an N that cannot be justified until Phase 5
supplies a venue simulator, and here a wrong N fails in the UNSAFE direction by
permitting re-execution; caller-driven expiry alone leaves the wedge in place
whenever the caller does not cooperate.

**`CallerId` must be a stable logical identity assigned by configuration, and
`RequestSequence` must be monotonic per caller and must not regress across
restarts.** A caller that returns from a restart with a fresh `CallerId` would
fill the cursor arena with dead entries and rebuild the same wedge one level up.
This is a deployment requirement, not an implementation detail; the alternative
is caller eviction, which would need its own horizon and reintroduces the problem
it was meant to solve.

#### What a caller does with `request_expired`

Reconcile against state; never retry blindly. In practice the loss is small,
because almost every command already carries a second, state-based deduplication
that survives receipt expiry entirely:

| Command | Survives receipt loss via |
|---|---|
| `AuthorizeDecision` | the existing order for that `DecisionId`, returning `already_applied` with its retained identities |
| `ObserveFill` | `(VenueId, ExecutionId)` identity, then the order's own existence |
| `ObserveOrderEvent` | venue-event identity and effect scoping |
| `ConfigureRiskLimits` | `expected_risk_version`, so a stale retry refuses |
| `RequestCancel` | order state, so cancelling a cancelled order refuses |
| `EvaluateDecisionFrame` | the decision record, or its retained evaluation receipt |

`request_expired` is therefore mainly an honesty signal - the cluster can no
longer say what happened, and the answer is in state - rather than a loss of
economic protection. The economic protection was never the receipt; it was the
state.

#### Venue-event identities: reclaimed with their order

A fill or venue-event identity is reclaimable EXACTLY when its cluster order
group is reclaimed, and is pinned for as long as that order exists.

This is safe by construction rather than by bookkeeping. A duplicate fill whose
order is gone fails to resolve an order at all and returns
`venue_observation_not_applicable` before touching economics, so the ABSENCE OF
THE ORDER IS ITSELF THE DEDUPLICATION. No amount of redelivery can book against a
record that no longer exists.

NOT YET IMPLEMENTED, and pinned by a failing-loud test. A fill receipt carries
`{venue_id, execution_id, filled_quantity}` in its detail and
`{session_id, venue_sequence}` in its metadata - all five slots used, none naming
the cluster order. Tying expiry to order-group reclamation therefore requires the
canonical result to carry a cluster order id, and the result is currently 126 of
its 128 bytes. That is a byte-contract change awaiting review, so the
`kMaxVenueEventReceipts` bound still stops fills being accepted and still strands
existing risk. `test_venue_event_horizon_still_bounds_order_completion` asserts
the bound so the limitation is visible rather than silently absent, and instructs
its own deletion once the bound is lifted.

An earlier proposal to pin fill identities against expiry indefinitely was
withdrawn: it preserved the same safety at strictly greater cost and left the
`kMaxVenueEventReceipts` wedge in place, whereas tying expiry to order-group
reclamation removes that wedge using machinery already proven economically inert.

The 64-entry venue-event horizon is a reviewed capacity, not a derived one. No
derivation is claimed for it.

#### Deferred

Caller-driven `ExpireIdentity`, already named in the section 7 candidate command
family, is deferred to Phase 5 review. It is the safest possible expiry, since
the party that would retry declares it is finished, but it cannot be the only
mechanism because an uncooperative or crashed caller would leave the wedge. It
belongs alongside the gateway that would send it.

### 8.7 Venue-event horizon: proposal awaiting review (2026-09-09)

Raised for Codex review. Nothing here is implemented and no byte contract has
changed. The recommendation deliberately costs zero bytes; the alternatives that
cost bytes were considered and rejected, and the reasoning is recorded so the
choice can be re-argued rather than rediscovered.

#### The situation

`apply_observe_fill` refuses with `fill_capacity_exceeded` once
`accepted_venue_event_count` reaches `kMaxVenueEventReceipts` (64). Fill and
venue-event receipts are pinned unconditionally by §8.6, because a redelivered
PARTIAL fill would book twice and reservation arithmetic does not catch it:
filled plus remaining stays within the original quantity, so the double booking
looks legitimate. Nothing reclaims them.

The bound is not dangerous because it exists. It is dangerous because it fails in
the WRONG DIRECTION. Every other bounded arena stops NEW work when full; this one
stops EXISTING orders from completing, so live risk is stranded with no path to
resolution. That is the one failure mode this phase has consistently treated as
unacceptable.

`test_venue_event_horizon_still_bounds_order_completion` pins the bound so it
stays visible, and instructs its own deletion once lifted.

#### Why the approved §8.6 rule is unimplementable as written

§8.6 approved reclaiming a fill identity exactly when its order group is
reclaimed. A fill receipt cannot be traced to its order. Its detail words hold
`{venue_id, execution_id, filled_quantity}` and its metadata words hold
`{session_id, venue_sequence}` - five slots, all used, none naming the cluster
order. The canonical result is at 126 of its 128 bytes, so carrying a cluster
order id means growing the result.

Repurposing a slot was examined and rejected:

- `filled_quantity` is returned to the caller and is genuinely needed;
- `session_id` and `venue_sequence` are load-bearing for the cross-kind venue
  event identity conflict check; and
- `venue_id` looks redundant because `VenueSessionId` implies a venue, but
  swapping the deduplication key from `(VenueId, ExecutionId)` to
  `(SessionId, ExecutionId)` is a SAFETY REGRESSION. A redelivery arriving after
  a gateway reconnect carries a NEW session id, so a session-scoped key would
  fail to catch precisely the duplicate that matters most.

#### Recommendation: reserve completion capacity, do not reclaim

Keep the 64-entry horizon. Move the pressure from fills to AUTHORIZATION:

> Refuse a new `AuthorizeDecision` when remaining venue-event capacity would not
> cover completing the orders already live. Fills for existing orders are always
> admitted.

The failure mode becomes "the maker stops quoting" instead of "the maker cannot
get out", which is the trade every other arena already makes and the one an
operator can survive.

This is not a new idea in the design. §11 already reserves bounded capacity for
the operations that reduce risk: normal and emergency traffic have separate
bounded quota lanes, and emergency behaviour is limited to pre-authorized
stop/cancel/reconcile actions. Reserving completion capacity applies the same
principle one layer down.

Cost: one predicate in `apply_authorize_decision`. No schema change, no new
arena, no canonical result growth, no additional resident bytes.

Pair it with one free self-heal: a fill receipt is reclaimable once NO reservation
is unreleased. With every reservation released no fill can book - a redelivery
meets `fill_quantity_conflict` on a released reservation - so the receipts protect
nothing. Coarse, but free, safe, and it drains the arena whenever a maker goes
momentarily flat between requotes.

#### Deferred alternative: a venue-session watermark

If true per-order reclamation is wanted later, the clean route is the same
pattern as the caller cursor in §8.6: track per `(venue, session)` the highest
`venue_sequence` whose fill receipt was reclaimed, and refuse an at-or-below
arrival that has no retained receipt. Both fields already sit in the result's
metadata words, so this needs no result change - only a small arena, roughly 96
bytes at four sessions against 277 bytes of snapshot headroom.

It is deferred because it depends on whether the venue actually supplies
monotonic per-session sequences, which is unknown until a venue exists to ask.
Review it in Phase 5 alongside the three horizon questions already parked there:
caller-driven `ExpireIdentity` (§8.6), the state-version horizon for unreferenced
decisions (§8.4), and the reference horizon deferred from the reclamation
contract. All four are the same question asked about different arenas.

#### What a reviewer should decide

1. Whether converting a risk-stranding failure into a stop-quoting failure is the
   right trade, or whether the venue-event horizon should instead be raised or
   reclaimed properly now.
2. How much completion capacity to reserve. A defensible starting rule is two
   venue events per live order, since a submit acknowledgement plus one terminal
   event is the minimum an order needs to close.
3. Whether the flat-state reclaim is worth having given how rarely a continuously
   quoting maker is genuinely flat.

## 9. Outbox and execution contract

Outbox lifecycle:

```text
PENDING -> CLAIMED -> ATTEMPTED -> ACKNOWLEDGED/REJECTED
                       |
                       +-> UNKNOWN -> RECONCILING -> ACKNOWLEDGED/REJECTED/
                                                    CONFLICT/INCOMPLETE
```

An effect includes:

```text
effect_id | cluster_order_id | committed directive index
executor_generation | kill_epoch | venue/account/instrument
operation | side | fixed price/quantity | TIF/capabilities
policy/decision/risk/source versions | payload digest
```

The executor:

- consumes committed effects only;
- validates generation, kill epoch, expiry, venue capability, and quota;
- may refuse or defer, but never expand or economically change the effect;
- uses the same stable identity for every permitted retry;
- records success, duplicate, rejection, transport failure, and timeout
  distinctly;
- marks timeout/connection loss after send as unknown, not failed;
- reconciles before resending an unknown effect; and
- publishes canonical observations back through the cluster command path.

An effect is not considered delivered merely because a local socket write
succeeded. Delivery and venue state are separate facts.

## 10. Reconciliation contract

Reconciliation compares:

- client and venue order IDs;
- side, price, original, filled, and remaining quantity;
- order state and replacement lineage;
- execution IDs, price, quantity, fee, and currency;
- positions, balances, and settlement state; and
- query window, cursor, session, and venue watermark.

Typed outcomes:

- `Match`: internal state and venue evidence agree;
- `Adopt`: venue evidence can be safely associated with a known intent;
- `Conflict`: identity or economics disagree;
- `Incomplete`: query boundary is not yet authoritative; and
- `Unavailable`: a trustworthy query cannot be completed.

Only `Match` and policy-approved `Adopt` clear a recovery gate. Other outcomes
force a bounded mode such as no-new-risk, cancel-only, reduce-only, or halted.

## 11. Execution authority and kill contract

Every sender tracks a committed `ExecutorGeneration` and `KillEpoch`. It rejects
new-risk effects carrying older values. Promotion of a standby requires:

- a new committed generation;
- old-sender fencing evidence;
- complete venue reconciliation;
- empty or adopted unknown-effect set; and
- an explicit transition out of no-new-risk mode.

Normal traffic and emergency traffic have separate bounded quota lanes.
Emergency behavior is limited to pre-authorized stop/cancel/reconcile actions.
Failure to contact the Decision/OMS authority causes the executor to stop new
risk before its authority lease expires.

## 12. Persistence, replay, and snapshot contract

The replicated storage coordinates are distinct:

```text
memory_appended_index
local_durable_index
peer_durable_match_index
commit_index
applied_index
outbox_delivered/reconciled watermark
```

Client success and risk-increasing external delivery remain behind committed
and applied state. A follower does not acknowledge entries as replicated merely
because they are staged in volatile memory.

Snapshots include:

- last included term/index and membership;
- policy catalog and active policy;
- strategy, executor, and kill state;
- orders, positions, reservations, and fill ledger;
- request/effect/fill/event deduplication receipts;
- full pending/unknown outbox state;
- reconciliation and source-sequence watermarks; and
- canonical state digest and schema version.

Replay uses the same reducer with effect delivery disabled. Reconstructed
pending effects retain byte-identical identities. Snapshot replacement is
atomic under the declared storage contract, and WAL prefixes are reclaimable
only after the replacement snapshot is durable and verified.

## 13. Thread and process ownership

- one Raft owner thread owns protocol state and the trading reducer;
- one persistence owner handles sealed WAL blocks and reports durability
  completions through bounded descriptors;
- market-data and model workers publish immutable proposals;
- the gateway event loop owns sockets, authentication, rate limits, and venue
  mappings;
- callbacks translate into immutable messages and never mutate cluster views;
- cross-thread queues carry values or stable arena descriptors, not borrowed
  pointers; and
- release/acquire ordering proves memory visibility, not disk durability.

All queues have a capacity, full policy, metric, and test. Query and monitoring
consumers may shed/rebuild; risk and external-effect queues fail closed rather
than overwrite authoritative work.

## 14. Failure behavior matrix

| Failure | Required behavior |
|---|---|
| No Raft quorum | no new authoritative risk; preserve/reconcile known effects |
| Leader changes after decision commit | replay retains the same outbox identity |
| Leader fails before decision commit | no executor-visible effect exists |
| Executor fails before send | pending effect remains recoverable |
| Executor fails after venue acceptance | effect becomes unknown and reconciles before retry |
| Duplicate venue acknowledgement | idempotent evidence update |
| Duplicate fill | one position transition or visible conflicting duplicate |
| Fill races cancel | apply both in venue/reconciliation order; residual remains risk-bearing |
| Market-data gap/staleness | stop new risk and preserve emergency cancel capacity |
| Persistence writer stalls | bounded admission backpressure; no volatile success |
| Disk full/corrupt/torn tail | storage fault, valid-prefix recovery, no fabricated suffix |
| Old executor wakes | stale generation/kill epoch rejected and external path fenced |
| Dashboard/exporter fails | no effect on decision or execution authority |

## 15. Required operational metrics

- decision frames proposed, accepted, stale, invalid, and no-action;
- policy publication/activation/rejection by bounded reason;
- decision-to-commit and commit-to-outbox-visible latency;
- outbox state counts, oldest pending/unknown age, and delivery attempts;
- external retry amplification: transport attempts per logical effect;
- order counts by lifecycle state;
- unknown submission and reconciliation count, outcome, and duration;
- risk reserved, released, and venue-open exposure by bounded scope;
- duplicate/conflicting request, effect, event, and fill counts;
- executor generation, kill epoch, lease freshness, and emergency-lane capacity;
- Raft term/leader, durable/commit/apply index, state digest, and lag;
- WAL block occupancy, flush latency, durable-index lag, and storage errors; and
- market-data source generation, sequence, readiness, age, and gap count.

Request/order/effect IDs, raw error strings, symbols, URLs, and unbounded model
names are evidence fields, not Prometheus labels.
