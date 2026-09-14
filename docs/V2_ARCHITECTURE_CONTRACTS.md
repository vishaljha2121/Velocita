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

Authorization enforces the same rule, not only reclamation: the chosen decision
must still match the currently active policy and executable bundle, instrument
specification, venue-capability version, and scope, and no newer structurally
authorizable proposal may exist for its logical slot. Immutable proposal bytes
prove provenance; they do not preserve authority after policy rotation.

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
and identical on every replica. A single StateVersion sample cannot be converted
to wall-clock time. An exporter that needs durations must retain its own
timestamped StateVersion observation timeline outside the authority boundary.

That is arguably the better operational measure and not merely a workaround.
State-version age answers "how many decisions has the cluster made while this sat
unresolved", which is load-relative: a stuck slot looks equally stuck in a fast
market and a quiet one, and the number cannot drift or jump when a host's clock
is corrected. A wall-clock age would do both.

Reported: arena occupancy and headroom against compile-time capacities, including
accepted venue-event identities and caller cursors; orders, effects and
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

An effect record carries no version of its own, but its pinned authorization
receipt does. Unsettled-effect age is derived from the receipt naming the exact
`EffectId`, so a later cancel is not falsely aged from its parent order's older
authorization. `oldest_reconciling_order_age_since_authorization` is explicitly
an upper bound: Phase 4 does not add a transition timestamp to authoritative
state merely to improve a metric.

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

Each caller has a retained conservative PREFIX watermark. `expired_below = N`
means the cluster no longer promises an individual answer for any sequence at or
below N. A retained exact receipt still wins; otherwise a request in the prefix
returns `request_expired` and mutates nothing. This necessarily classifies an
unseen lower number as expired too, which is why `RequestSequence` is required to
be monotonic per stable caller rather than freely permuted.

The cursor is registered atomically on the caller's first admitted request with
`expired_below = 0`. The ninth distinct caller is refused before StateVersion,
receipt, or cursor mutation. Snapshot validation bounds the cursor count, rejects
zero/duplicate caller identities, and accepts zero only for the watermark value.

A fourth arena needed the same treatment. Source-event receipts already have
their watermark in `SourceCursor`, and admission independently refuses a
regression, so a receipt at or below its cursor is no longer load-bearing: a
redelivery is refused as a regression rather than re-admitted. They are reclaimed
on the same fail-closed pattern.

Receipts are pinned in two families. AUTHORIZATION receipts are the provenance
`materialize_order_effect` uses to rebuild an effect's historical bytes, so
dropping one while that exact effect lives would strand a pending external effect
and break G-020. A definitively rejected cancel record may be reused for its next
generation; the superseded authorization receipt then becomes reclaimable.
FILL and VENUE-EVENT receipts are pinned while their owning order exists, because
a redelivered PARTIAL fill would otherwise book twice. Everything else, including
every refusal, is reclaimable, and in a live maker those are the overwhelming
majority.

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

Typed refusals are themselves canonical state. Successful observation results
require complete stream, execution, and owning-decision identity; a refused
malformed observation may echo a partial non-empty detail so the diagnostic can
be encoded. The result codec and snapshot validator apply the same rule, ensuring
that an admitted `invalid_command` can never make the next snapshot invalid.

#### Venue-event identities: reclaimed with their order

A fill or venue-event identity is reclaimable EXACTLY when its cluster order
group is reclaimed, and is pinned for as long as that order exists.

This is safe by construction rather than by bookkeeping. A duplicate fill whose
order is gone fails to resolve an order at all and returns
`venue_observation_not_applicable` before touching economics, so the ABSENCE OF
THE ORDER IS ITSELF THE DEDUPLICATION. No amount of redelivery can book against a
record that no longer exists.

Implemented without growing the 128-byte result. Candidate schema V5 already
contains a `DecisionId` field outside the five tagged detail/metadata words.
Applied fill and venue-event results now populate it; the order's retained
`DecisionId` is therefore the ownership edge. Applied observation results require
that owner in codec and snapshot validation. Old unaccepted development V5
snapshots whose applied observation results left it zero are unsupported.

When event capacity is under pressure, the reducer first reclaims risk-resolved
order groups, then sweeps the now-unpinned receipts, before resolving any arena
index or borrowing a reference. `test_resolved_orders_release_venue_identity_capacity`
drives 119 complete orders through the 64-entry horizon and proves both sustained
completion and the retained ownership identity.

An earlier proposal to pin fill identities against expiry indefinitely was
withdrawn: it preserved the same safety at strictly greater cost and left the
`kMaxVenueEventReceipts` wedge in place, whereas tying expiry to order-group
reclamation removes that wedge using machinery already proven economically inert.

The 64-entry venue-event horizon remains a reviewed capacity. Admission derives
whether a proposed order fits inside it; no average fill size is assumed.

#### Deferred

Caller-driven `ExpireIdentity`, already named in the section 7 candidate command
family, is deferred to Phase 5 review. It is the safest possible expiry, since
the party that would retry declares it is finished, but it cannot be the only
mechanism because an uncooperative or crashed caller would leave the wedge. It
belongs alongside the gateway that would send it.

### 8.7 Venue-event and cancel headroom (approved and implemented 2026-09-10)

The reviewed direction is fail-closed at NEW RISK, never at completion. The old
rule admitted orders until 64 observation identities already existed, then
refused fills for those live orders. The bound therefore failed in the wrong
direction: it stranded existing exposure instead of stopping new quoting.

`AuthorizeDecision` now proves the following conservative capacity inequality
before creating any economics:

```text
accepted observation identities
+ maximum future one-atom fills for every unresolved order
+ bounded remaining submit/cancel/reconciliation transitions
+ the same budget for the proposed order
<= kMaxVenueEventReceipts (64)
```

Maximum future fills is `remaining_quantity.atoms()`, not an average or one
terminal fill per order. A two-event rule was rejected as only a lower bound: a
Q-atom order may legally complete as Q distinct one-atom fills. A newly
authorized order receives five control slots: submit unknown, submit ack, cancel
unknown, cancellation, and final reconciliation. As its state advances, the
remaining control allowance decreases. Overflow or a disabled legacy replace
state fails closed as unbudgetable.

This deliberately converts pressure into `venue_event_capacity_exceeded` at
authorization. Observation handlers still sweep resolved groups and their
receipts before refusing at the hard horizon. The result is no new resident
state and no result growth.

The outbox has the same directional rule. Every unresolved order needs its
retained submit effect plus one cancel slot. New authorization is refused unless
both remain representable, which currently bounds simultaneous unresolved orders
to eight even though the order arena holds sixteen. A definitively REJECTED
cancel record may be overwritten by the next cancel generation in the same slot:
the old effect is settled and no longer deliverable, while the new EffectId is
derived from a strictly increasing generation. Pending, unknown, reconciling,
conflicting, and incomplete effects are never overwritten. This prevents venue
cancel rejections from consuming the outbox for the lifetime of the process.

`order_acknowledged` is submit-only. Applying it to a cancel effect previously
resolved the effect while leaving the order cancel-pending, which made later
valid cancellation evidence inapplicable. The mismatch is now refused before
mutation. A semantically repeated `effect_unknown` returns `already_applied` and
does not consume another accepted venue transition.

Fill evidence is likewise submit-scoped. A fill whose `EffectId` names a cancel
is refused before economic mutation even when it names the correct cluster order.
A partial fill naming a submit proves that submit reached the venue, so it changes
`submission_unknown` to `partially_filled` and acknowledges the submit effect. If
the current unknown effect is a later cancel, the same historical-submit fill is
booked exactly once but the order remains `submission_unknown` and the cancel
effect remains `unknown`; execution evidence does not invent cancel-delivery
evidence. A terminal fill may still make the order `filled`, but any outstanding
cancel remains unsettled until its own venue evidence arrives.

#### Rejected alternatives and deferred work

Repurposing `venue_id` to make fill deduplication session-scoped remains rejected:
a redelivery after reconnect has a new session and would escape the identity key.
Raising capacities merely moves the wedge. A per-venue-session sequence watermark
is deferred to Phase 5 because it depends on a real venue providing monotonic
session sequences. Caller-driven expiry and a general decision/reference horizon
remain parked with it; none gets an invented wall-clock timeout in Phase 4.

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

### 9.1 Phase 5 gateway contract decisions (approved 2026-09-10)

Sections 9 through 15 are the primary gateway contract and, unlike 8.1 through
8.7, had never passed an approval gate: the document header still reads
"proposed". A review before Phase 5 code found thirteen contradictions or gaps
inside them. The five that blocked the first line of code are resolved here. The
reasoning is recorded so each can be re-argued rather than rediscovered.

#### 9.1.1 "Committed" collapses to "applied" until Phase 6

G-027 requires delivery only from "committed and applied" outbox state, and ADR
0001 says the gateway "consumes committed outbox effects after apply". Raft
integration is Phase 6, so during Phase 5 there is no commit index and the phrase
has no referent.

For Phase 5, committed means APPLIED: the gateway reads a post-apply
`TradingState` by direct in-process call. Phase 6 re-tightens this to genuine
quorum commitment and must re-run the Phase 5 delivery gates against the stricter
definition.

Justification: the alternative is to invent a commit abstraction in Phase 5 that
Phase 6 would immediately replace, and to test delivery against a fiction. Naming
the interim honestly costs one paragraph; a placeholder abstraction would cost a
migration.

#### 9.1.2 `claimed` and `attempted` are gateway-local, not consensus state

Section 9 draws the lifecycle as `PENDING -> CLAIMED -> ATTEMPTED -> ...` as
though all four were cluster states. Section 8.2 says the opposite - that
"`pending`, `claimed`, and `attempted` are gateway-side bookkeeping the cluster
learns about asynchronously". The two sections contradicted each other, and no
command exists to write either value.

Section 8.2 wins. Delivery bookkeeping is gateway-local. The cluster learns only
terminal outcomes plus `unknown`, all of which already have observation kinds.

Justification: recording a claim or an attempt in replicated state puts a quorum
round trip on the critical send path, and buys visibility the metrics projection
can supply without it. ADR 0001 already separates these concerns: "An effect is
not considered delivered merely because a local socket write succeeded. Delivery
and venue state are separate facts."

The enum values are RETAINED rather than renumbered, because renumbering is a
schema change for no behavioural gain and would leave a gap in the contiguous
`known()` range. Instead snapshot validation must REJECT `claimed` and
`attempted`, since the reducer can never legitimately produce either. That is a
tightening of an existing check, not a schema change. `conflict` is equally
unreachable today and is retained for the reconciliation work that will produce
it.

Consequence to accept deliberately: replicated state cannot distinguish an unsent
effect from an in-flight one. That distinction lives in the gateway, and is
recovered after a gateway restart by reconciliation rather than by reading it out
of the cluster.

#### 9.1.3 Expiry means control-generation staleness, not a deadline

Section 9 and G-029 both require the pre-send guard to validate "expiry". The
effect field list in the same section contains no expiry, deadline, or validity
field, and `OrderEffect` has none.

Expiry is DEFINED as control-generation staleness: an effect is expired when the
`executor_generation` or `kill_epoch` recorded at its authorization no longer
match current replicated control state. No wall-clock deadline exists and none is
added.

Justification: adding a deadline field would put a clock in the authority path,
which every other part of this design refuses, and would grow a boundary value
that Phase 4 already showed is byte-constrained. The staleness check is one the
guard performs anyway under G-029 and G-054, so this removes a phantom field
rather than adding a real one. A venue-side time-in-force remains an economic
property of the order, carried in `time_in_force`, and is unrelated.

#### 9.1.4 Lowering is representation only, never economics

G-028 permits the gateway to "lower" an effect. No document defines the boundary
between permitted lowering and the forbidden "expand or economically change the
effect". This was the largest under-specified permission in the contract and the
most dangerous, because rounding a price reads as lowering to one implementer and
as an economic change to another.

Permitted lowering is REPRESENTATION ONLY:

- encoding the effect into a venue's wire format;
- mapping `ClusterOrderId`/`EffectId` onto a venue client-order identifier; and
- transport framing, authentication, and session mechanics.

Forbidden, without exception:

- rounding or adjusting price or quantity;
- substituting or weakening time-in-force;
- downgrading or dropping post-only;
- splitting one effect into several venue orders, or combining several into one.

If a venue cannot represent the exact economic intent, the gateway REFUSES the
effect and reports it. It never sends an approximation.

Justification: this is reject-not-clamp, already settled in Phase 3D for the
policy envelope and restated in 8.7 for adapters, which say an adapter "may
reject but never silently downgrade intent such as post-only". Extending the same
rule to the gateway keeps one principle instead of two, and it is the only
version that preserves ADR 0001's invariant that the executor never economically
modifies an order.

#### 9.1.5 Process boundary, tested in-process

Section 13 lists "the gateway event loop" alongside the Raft and persistence
owners as though intra-process. ADR 0002 co-locates the gateway with voter 1 on
the NUC. G-066 requires credentials never to enter logs, test fixtures, or
evidence bundles. The thread-versus-process choice was never made, and it decides
whether G-066 is achievable at all.

The gateway is a SEPARATE PROCESS in deployment. Phase 5 exercises it in-process
for determinism, with the transport interface as the seam, and that seam is
designed so a process boundary can be introduced without reshaping the core.

Justification: credentials and session material cannot be isolated inside a
process that also runs the reducer and its test fixtures. Deciding the boundary
now and testing across the seam avoids discovering in Phase 9 that G-066 requires
an architectural change.

#### 9.1.6 Decisions that shape the work but do not block it

- EMERGENCY LANE. TDD Phase 5 requires it; G-057 and G-058 assign emergency
  semantics to Phase 8. Phase 5 builds the bounded lane required by G-034 and
  claims no emergency guarantee.
- AUTHORITY LEASE. Section 11 requires stopping new risk "before its authority
  lease expires" and section 15 wants a lease-freshness metric, but no document
  defines the mechanism, duration, or issuer. Phase 5 treats authority as
  "control generation matches at pre-send"; the lease is Phase 8.
- CRASH-CUT. Send, then reconcile before resending; not record before send. This
  is what G-032 and ADR 0001 already mandate, and it is why the plan gives the
  simulator a query API. Recording intent before sending would collapse into
  9.1.2 and pay the same quorum latency.
- CRASH SCOPE. Nothing is durable until Phase 7, so a whole-cluster crash cannot
  be tested meaningfully. G-031 is scoped to GATEWAY crash with cluster state
  intact, which is the realistic case given the process boundary in 9.1.5.
- SIMULATOR. Test-only for now. Promoting a test double to a product is easier
  than the reverse, and G-084's ladder places the simulator well before any
  paper venue.
- VIRTUAL TIME. Every timeout and rate-limit test is driven by an injected
  virtual clock. No test sleeps. This follows `OwnerLoopScheduler`, which already
  takes `now` as a parameter rather than reading a clock.
- PARKED HORIZONS. The five deferred retention questions become answerable once
  the simulator can supply retry and reconciliation numbers. Phase 5 is not
  blocked on answering them.

#### 9.1.7 Open obligation inherited from Phase 4

Section 3 requires that "identity retention must exceed the maximum retry,
recovery, snapshot restore, and reconciliation horizon". Sections 8.6 and 8.7
made retention finite and capacity-driven, while 8.4 records that the retry and
reconciliation horizons are unknown until Phase 5. Phase 4 was therefore accepted
with a retention policy that cannot yet be SHOWN to satisfy section 3.

This is an open obligation, not a defect: every arena fails closed rather than
silently dropping evidence. Phase 5 should discharge it by measuring the actual
horizons its simulator produces and comparing them against the accepted
capacities. If they do not fit, the capacities move, not the invariant.

#### 9.1.8 A trap in the tree

`include/velocita/v2/wire.hpp` defines a `VLT2` envelope with a `MessageKind`
enumeration including `order_effect`, `observe_fill`, and `apply_reconciliation`.
Nothing maps those onto `KernelCommandKind`, the numbering does not correspond,
and the only callers are Phase 1 contract tests and a benchmark. It is an
unwired Phase 1 vocabulary, not a transport. Phase 5 must not assume it is one.

### 9.2 Delivery-loop contract decisions (approved 2026-09-10)

Two decisions were forced by writing the delivery loop's tests rather than by
reviewing the document. Both are recorded here because both change what the
gateway is permitted to do, and neither is obvious from the guarantee list.

#### 9.2.1 A resend requires PROOF that nothing reached the venue

The first delivery planner refused to reschedule any effect that had ever been
attempted. That is safe and wrong. It conflates two situations the transport
already reports separately:

- the send never left us - `pre_send_failure`, or `rate_limited`, where the venue
  refused to accept for pacing before reading anything. Nothing is resting there.
  The next send is a FIRST send, not a duplicate, and needs no reconciliation.
- the send may have arrived - `accepted`, `ambiguous`, `venue_duplicate`, or a
  business rejection. Something may be resting at the venue. A resend here is the
  gateway's own duplicate, which is precisely what G-025 and the Phase 5 gate
  exist to exclude.

Collapsing these means one transient rate limit wedges the logical order slot
until reconciliation - and reconciliation would then query a venue that has never
heard of the order, which is the worst possible way to discover the truth.

`DeliveryAttempt` therefore carries `resend_permitted`, false by default. Only
`record_unsent` sets it, and callers must reserve that call for the two outcomes
that carry the proof. `record_response` deliberately leaves it false: the venue
answered, so whether to send again is reconciliation's judgement, not the
planner's. `record_send` clears it, so no stale permission can survive into the
next tick.

Justification: the default has to be the safe answer, because the failure modes
are not symmetric. Guessing "resend permitted" wrongly places a second real
order. Guessing "resend forbidden" wrongly blocks a slot, which is visible in
metrics and recoverable. The asymmetry is why the strong claim must be asserted
by a caller that saw the evidence rather than inferred by the planner.

#### 9.2.2 Provably-unsent retries are bounded

`GatewayTimeouts::max_send_attempts` (3) bounds how many times one effect may be
re-sent under 9.2.1. Without it, a venue that rate-limits indefinitely becomes an
unbounded retry loop.

On exhaustion the planner simply stops scheduling the effect. It is NOT marked
ambiguous - nothing ever left the process, so calling the outcome unknown would
be a lie that reconciliation would then have to chase. The effect stays `pending`
and unsettled, so it appears in `unsettled_effects` with a rising
`oldest_unsettled_effect_age`, and its slot stays blocked. That is the same
fail-closed, operator-visible shape as G-074a's stuck reconciliation, and it is
deliberately the same shape: one escalation path, not two.

Open: the bound is a guess. Section 9.1.7's obligation applies here too - the
simulator can measure how often three attempts is too few, and the number should
move on evidence rather than on taste.

### 9.3 What the Phase 5 delivery gate actually asserts

The gate is stated in the TDD plan as three absolutes. `v2-gateway-delivery-tests`
drives the real kernel, the real guard and planner, and the adversarial venue
through the fault matrix, and makes each one checkable:

- NO DUPLICATE VENUE ORDER. The simulator creates a resting order only for a
  cluster order identity it has never seen and answers a repeat with
  `venue_duplicate`. So the count of orders it created must equal the number of
  DISTINCT identities that were accepted. A retry that mutated its identity makes
  this strictly larger. The harness separately asserts that one cluster order
  identity is never sent under two different effect identities.
- NO LOST ACCEPTED FILL. Every distinct execution the venue reported is booked
  exactly once, or refused with a code that loses nothing - idempotent
  suppression, or a sweep of a group that had already fully resolved. Any other
  refusal fails the gate, including a capacity refusal: that would be fail-closed
  but is still a sizing problem worth failing on.
- NO UNRESOLVED STATE PRESENTED AS HEALTHY. The projection's `unresolved_orders`
  must equal what a direct walk of the reservation arena finds, and anything
  unresolved must carry a nonzero age so it cannot look permanently fresh. Slot
  exclusivity is asserted directly: the fixture trades one logical slot, so at
  most one order there may ever hold unresolved risk.

The matrix also asserts its own non-vacuity - that it sent, placed, timed out,
stranded a slot, and suppressed a duplicate fill at least once - because a fault
matrix that silently stopped reaching its hard paths would keep passing.

One coverage gap was found and closed while writing this: the `duplication` fault
duplicated only acknowledgements, never fills. A duplicated fill is the
economically dangerous case, since booking it twice doubles the position and no
downstream reservation arithmetic can undo it. Duplicating only the harmless
observation would have tested the identity check on the case that does not
matter.

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
- `Incomplete`: the query's window is not yet known to be complete and
  comparable; and
- `Unavailable`: a trustworthy query cannot be completed.

Only `Match` and policy-approved `Adopt` clear a recovery gate. Other outcomes
force a bounded mode such as no-new-risk, cancel-only, reduce-only, or halted.

### 10.1 Phase 5 reconciliation contract review (2026-09-10)

Section 10 is in the same never-approved block as section 9 and was reviewed the
same way before any code. Six findings. Five are resolved here with reasoning;
one is a material byte-contract choice and is held for explicit approval, so the
gateway-side half was built and the cluster-side half was not.

#### 10.1.1 Half the compare list does not exist

Section 10 requires reconciliation to compare "positions, balances, and
settlement state" and "currency". Positions exist as
`RiskAccount::position_quantity`. Balances, settlement state, and currency exist
NOWHERE - not in `TradingState`, not in `OrderEffect`, not in
`VenueObservation`, and `FeeMicros` carries no currency tag. Phase 4 explicitly
accepted `ConfigureRiskLimits` with no balance overwrite.

Resolved: Phase 5 compares what exists - cluster and venue order identity, side,
price, original/filled/remaining quantity, order state, execution identity,
quantity, and fee. Balances, settlement state, and currency are deferred, and
this paragraph is the record that they were dropped deliberately rather than
overlooked.

Justification: the alternative is to grow the byte contract in Phase 5 to carry
fields nothing produces and nothing consumes, so that a comparison can be written
against them. That is cost with no evidence behind it. When a venue integration
actually supplies settlement state, the comparison can be added with a real
counterparty to test it against.

#### 10.1.2 The gateway may not classify; the reducer may not query

Section 10 lists five typed outcomes without saying who computes them. Both
obvious answers are wrong.

If the GATEWAY computes `Adopt` and the cluster believes it, the gateway has
become an economic authority, which ADR 0001 forbids: it "may refuse or defer an
unsafe effect but may not originate or economically modify an order". If the
REDUCER computes everything, it needs the query - and apply performs no I/O.

Resolved by splitting along the line that already exists. The gateway decides
only whether it holds a TRUSTWORTHY ANSWER; the reducer decides what a
trustworthy answer MEANS. This is not a new pattern: `cancel_reconciled` already
works exactly this way, arriving as raw observation carrying final cumulative
quantity for apply to adjudicate.

The split is enforced by the type system rather than by a comment.
`QueryDisposition` - the gateway's output - has no way to spell `adopt` or
`conflict`. A gateway that wanted to classify economics could not express it.

#### 10.1.3 Two of the five outcomes are not cluster facts

`Incomplete` and `Unavailable` do not describe the order. They describe the
QUERY: its window was not complete, or it could not be completed at all. The
reducer cannot observe either, because it never sees a query, and storing them
would put a fact about the gateway's network into replicated economic state.

Resolved: `Match`, `Adopt`, and `Conflict` are reducer classifications.
`Incomplete` and `Unavailable` are gateway-local and mean "submit nothing, ask
again". A fourth gateway-local value, `exhausted`, is added for the case where
attempts are spent - the gateway stops asking and the order stays visibly
unresolved, which is the G-074a operator situation rather than something to heal
by guessing.

`not_found` stays distinct from `unavailable` throughout, and this is the whole
reason reconciliation is worth building. "The venue answered that it has no such
order" resolves a `submission_unknown` and releases its reservation. "The venue
would not answer" concludes nothing. Treating silence as absence is precisely how
a gateway places a second real order.

#### 10.1.4 "Policy-approved Adopt" names a policy that does not exist

Resolved by derivation rather than by new machinery. An adopt is safe exactly
when the venue's evidence fits INSIDE the existing reservation: filled quantity
within the reserved original, price within the authorized terms, side matching
the proposal. Anything outside is `Conflict`. Every one of those checks already
exists in Phase 4's fill path.

Justification: a separate adopt policy artifact would be a second place for the
economics to be stated, and two statements of the same rule eventually disagree.

#### 10.1.5 Section 10 would let one bad order halt the cluster

Section 10 says non-`Match` outcomes "force a bounded mode such as no-new-risk,
cancel-only, reduce-only, or halted". Phase 4, which IS accepted, made `conflict`
a per-order terminal state that holds its risk and blocks its logical slot while
the cluster keeps trading every other slot; slot exclusivity is scoped to
(account, instrument, venue, order slot).

These contradict. The accepted artifact wins: a conflict blocks its own slot. A
cluster-wide mode change stays an operator action under G-074a, owned by Phases 8
and 10.

Justification: the section 10 reading means one venue disagreeing about one order
stops all trading on every instrument. That is a larger blast radius than the
evidence supports, and it converts a contained fault into an outage. Phase 4's
scoping already fails closed on the thing that actually holds risk.

Flagged for review: this is the one place where the review overrules the written
contract using an accepted phase rather than an argument from first principles.
If the intent really was cluster-wide, section 10 should say so and Phase 4's
acceptance needs revisiting.

#### 10.1.6 A query answer has no event identity (approved 2026-09-10)

`ObserveFill` and `ObserveOrderEvent` deduplicate on venue event identity -
`(venue_id, venue_session_id, venue_sequence)`. A query answer is a SNAPSHOT, not
an event: it has no venue sequence. Worse, session-scoped identity is unsafe
across reconnects, and a disconnect is exactly when reconciliation runs.

APPROVED: one new `VenueObservationKind`, `order_reconciled = 7`, inside the
existing `VenueObservation`. Zero new bytes, no new command kind, no new struct.
The alternative - a new command kind with content-addressed idempotency - would
have cost a command kind plus a body in the snapshot and receipt paths to buy a
property cumulative evidence already has.

The safety argument is not about identity at all, and this is worth stating
precisely because the weaker version of it is the one that first comes to mind.

The weak version: quantity is FINAL CUMULATIVE filled rather than an increment,
so applying the same answer twice is a no-op by value. True, but in practice the
applicability check fires first, so replay protection is not what this buys.

The strong version, which is what actually matters: because the answer is a TOTAL
compared for EQUALITY against what the cluster has already booked, the verdict is
a pure function of `(answer, currently booked)`. It does not depend on how many
answers preceded it, or in what order they arrived, or whether any of them were
seen twice. THE ADJUDICATION NEEDS NO HISTORY - which is precisely why it needs no
reliable identity. An incremental encoding would have to know whether this
particular message had already been counted, and that question is unanswerable
without exactly the identity a snapshot cannot carry.

A zero `venue_order_id` means the venue answered that it holds no such order.
`validate` enforces that absence implies a zero fill, so "the venue denies this
order but reports it filled" is refused at the contract boundary rather than
adjudicated deeper in, where the two halves would have to be reconciled against
each other. Price, fee, and execution identity must all be zero: a snapshot is not
an execution, and carrying an average price would invite it to be booked as one.

The change is purely ADDITIVE. No existing golden changes, no existing kind is
touched, and the frame stays 116 bytes. The one real cost is that a decoder which
rejects unknown kinds will reject kind 7. The model package needs no change and
must not receive one: it deliberately implements no `VenueObservation`, and its
safety interlock fails the build if the name appears at all.

#### 10.1.7 There is no `Adopt` in Phase 5, and that is the correct answer

Section 10 lists `Adopt` - associating venue evidence with a known intent - and
10.1.4 derived when it would be safe. Implementing it revealed that the question
is moot for a query answer, and the reason is worth recording rather than
rediscovering.

If the venue reports MORE cumulative filled than the cluster has booked, those are
fills the cluster has never seen. Booking them means creating a position movement.
But a snapshot carries no execution identity, no price, and no fee - which are
exactly the fields a fill requires, and exactly the fields Phase 4's fill path
checks. Adopting would mean inventing all three.

So the divergence is a `Conflict`, not an `Adopt`. Both directions are:

- below what is booked, the venue has lost fills the cluster accepted, and a fill
  is never un-booked; and
- above it, the cluster refuses to manufacture a position movement from a total.

The order goes terminal-but-unresolved, retains its reservation, and blocks its
own slot. That is the G-074a operator path, and it is the honest place for
economics the cluster cannot settle from the evidence it holds.

`Adopt` returns when reconciliation can obtain per-execution evidence - a fills
query with execution identities - rather than an order-level snapshot. That is a
venue-integration question, not a Phase 5 one.

#### 10.1.8 What `order_reconciled` deliberately does not answer

It resolves exactly one question: did the submit reach the venue, and how much has
it filled. It is applicable only to a submit effect on an order in
`submission_unknown` or `dispatch_authorized`, with its effect still `pending` or
`unknown`.

Not answered, and deferred with reasons:

- WHETHER A RESTING ORDER IS ALSO CANCELLED. The observation has no field for it,
  and the existing `cancelled` kind requires a cancel effect the cluster may never
  have sent. A venue-initiated cancellation cannot be reported at all today; that
  is a pre-existing gap, not one this creates.
- A `partially_filled` ORDER THAT WENT SILENT. Its state is not
  `submission_unknown`, so it is out of the applicable set. The order is known
  live; only its subsequent fills are missing.
- POSITION MOVEMENT. See 10.1.7. Agreement resolves the effect and never touches
  the position.

### 10.2 What the reconciliation planner decides

`plan_reconciliation` targets exactly the orders holding exposure the cluster
cannot resolve from evidence it already has. Three needs:

- `submission_unknown` - a submit the venue never answered;
- `cancel_unconfirmed` - a cancel acknowledged but never finally reconciled; and
- `delivery_exhausted` - send attempts spent under 9.2.2 with nothing having
  reached the venue.

The third exists because the retry bound alone would trade an unbounded retry
loop for a permanently wedged slot. A query breaks the tie: the venue can say it
never had the order, and the reservation is released. It is a target ONLY once
attempts are spent - an order still eligible to be sent must not be queried, or
the query races the send and the venue is asked about an order the gateway is
about to place. Whether attempts are spent is gateway-local by 9.1.2, which is
why the planner reads `GatewayDeliveryState` as well as `TradingState`.

A `live` order with no fill is deliberately not a target. Without a clock there
is no way to distinguish "its fill was lost" from "it has not traded yet", so
querying every resting order would be continuous noise. A resting order is
reconciled when it is cancelled, or at session recovery under G-032.

One outstanding query per order. Two in flight can answer out of order and let a
stale view overwrite a fresh one, which is a conflict the cluster would then have
to adjudicate for no reason.

Reconciliation is fenced by `executor_generation` but NOT by kill epoch or
strategy mode. A superseded gateway must not write venue evidence into consensus
at all. A halt stops new risk; reconciliation originates none, and a halted
cluster holding a blocked slot is precisely when resolving it matters most.

Measured against the fault matrix, before and after the cluster half existed.
BEFORE: sixteen of thirty-two seeds ended with a stranded slot, every one in
`submission_unknown`. AFTER: none do. Eleven end with the venue confirming it
holds a resting, unfilled order - exposure that is real and correct rather than
stranded - one ends in a genuine `conflict` for an operator, and the rest resolve
completely.

The matrix asserts the sharp form of that: after reconciliation quiesces, NO order
may remain in `submission_unknown`. The cluster is allowed to hold exposure; it is
not allowed to still be wondering whether its order exists. It also asserts that
every order still holding exposure sits in a state something can act on - either
queryable, or already adjudicated - so exposure with no path forward fails the
build.

The matrix's own non-vacuity is asserted too: it must reach the `unavailable`
branch at least once, because silence is the branch where reading an absent answer
as an absent order would place a second real order. Making that reachable required
one simulator fix - a query now runs over the same session a send does and is
subject to the same disconnect, without which `QueryOutcome::unavailable` was
unreachable and the classifier's most safety-critical branch was never exercised.

### 10.3 The cancel path through reconciliation (2026-09-10)

Four findings, all inside existing contracts. No byte change.

#### 10.3.1 The gateway refused to cancel in every mode that matters

`evaluate_pre_send` gated every effect on `control_permits_new_risk(mode)`, which
returns true only for `normal`. A cancel is an effect, so a cancel was refused in
`no_new_risk`, `cancel_only`, `reduce_only`, and `halted`.

`cancel_only` is a mode whose own guard refused the only thing it exists to
allow. `halted` means originate nothing new, not abandon the orders already
working. The gateway declined to get out of a position in exactly the modes an
operator reaches for in order to get out of one.

The defect is a shape worth naming, because it is easy to reproduce elsewhere: a
predicate named for a QUESTION ("does control permit new risk") applied to a
SUBJECT it does not describe (every effect, risk-increasing or not). The name was
accurate and the usage was not, and no test caught it because Phase 5 had only
ever delivered submits.

Resolved: the predicate takes the effect. A cancel is permitted in every strategy
mode; everything else still requires `normal`. `executor_generation` and
`kill_epoch` remain the real fences and are still checked first, so a cancel
authorized under a superseded epoch is still refused.

#### 10.3.2 `submission_unknown` names the order but describes its effect

When a cancel effect times out, `effect_unknown` moves the ORDER to
`submission_unknown` - even though the submission is not in doubt. The overload is
deliberate and `apply_request_cancel` reads the state the same way, so this is a
naming problem rather than a logic bug. But it has teeth: `need_of` could not tell
a stuck cancel from a stuck submit by order state alone, so the planner would
reconcile a submission that was never in doubt, submit an `order_reconciled` the
reducer correctly refuses, and burn its query budget while the cancel stayed
stuck.

Resolved: `need_of` reads the CURRENT EFFECT's operation, which is the only place
the truth actually lives.

#### 10.3.3 Final cancel evidence was only reachable from `reconciling`

The `cancel_reconciled` gate required `effect->state == reconciling` and
`order.state == reconciling`, which is reachable only after a `cancelled`
observation has arrived. A cancel that simply went silent never gets one, so the
only path out of the state was closed and the order held its risk and its slot
with nothing able to resolve it.

Resolved: the gate also accepts a cancel effect in `unknown` with its order in
`submission_unknown`, which is exactly the shape 10.3.2 produces.

#### 10.3.4 Disagreement is not symmetric, and treating it as such freezes
recoverable orders

This one was got wrong first, caught by an accepted Phase 4 test, and is the most
transferable lesson in the increment.

The first attempt made any quantity disagreement a `Conflict`, mirroring what
10.1.7 had done for the submit path. That broke
`test_cancel_fill_final_evidence_permutations`, which applies a fill, a
cancellation, and final reconciliation in all six orders and requires every one to
converge. Under a reordering transport, final evidence routinely arrives before
the fill it accounts for.

The two directions have different causes and must have different answers:

- the venue reporting MORE than the cluster booked is MISSING EVIDENCE. Fills that
  close the gap may still be in flight. Refuse, change nothing, let it settle and
  be re-presented. The order stays queryable and recoverable.
- the venue reporting LESS is a genuine CONFLICT. No future evidence reduces a
  fill the cluster has already booked, so the gap can never close and waiting for
  it to is a lie.

This rule now governs BOTH the cancel path and the submit path. 10.1.7's statement
that both directions of `order_reconciled` divergence are conflicts was wrong in
the "above" direction and is corrected here rather than left standing.

The cost is real and worth stating: an order whose fills were genuinely lost stays
unresolved rather than becoming a conflict. That is the better trade. Unresolved is
recoverable if the evidence does arrive and stays visible with a rising age;
`conflict` is terminal and cannot be recovered automatically. Do not spend a
recoverable state on evidence that might have been one tick away.

#### 10.3.5 A cancel may be redelivered without proof (approved 2026-09-10)

Amends 9.2.1. That rule - a resend requires proof nothing reached the venue -
exists because a duplicate SUBMIT creates a second order. A duplicate CANCEL
creates nothing: it can only reduce exposure or leave it unchanged, and the venue
deduplicates it by the same stable identity. So the rule was strictest exactly
where its own justification does not apply, and strictest exactly when an operator
is trying to get flat.

APPROVED: a cancel effect may be redelivered after its response deadline passes,
without reconciliation, bounded by the same `max_send_attempts`. `unknown` also
becomes a deliverable state for a cancel and only for a cancel - an unknown cancel
is precisely the effect most worth sending again. `reconciling` stays undeliverable
for both: it means final evidence is owed and the cluster is mid-adjudication, so
sending then would race the answer.

Two mechanisms are deliberately kept distinct, because conflating them is how one
intent becomes two orders:

- REDELIVERY is the same `EffectId`, at-least-once, deduplicated by the venue.
  This is what a timeout triggers.
- RE-AUTHORIZATION mints a fresh cancel effect with a new identity, and is the
  reducer's business. It only makes sense once the old cancel is known not to have
  taken effect.

ORDERING: redelivery FIRST, reconciliation second. `need_of` makes a silent cancel
a reconciliation target only once its send attempts are spent. The cheap
idempotent action is tried before the round trip that would most likely just say
"send it again"; reconciliation takes over at the point where it starts adding
information.

#### 10.3.6 A rejected cancel does not prove the order is live

The rider on 10.3.5, and it is not optional.

`order_rejected` on a cancel effect returned the order to `live`. A venue refuses
a cancel for "already cancelled", "already filled", and "no such order" as readily
as for "still working" - and once cancels may be redelivered, a rejection of the
DUPLICATE is the common case. Under a reordering transport that rejection can
arrive before the original's `cancelled` observation, flipping a genuinely
cancelled order back to working, after which its real cancellation is refused
because the effect is already resolved.

A rejected cancel therefore means the lifecycle is UNKNOWN, not live: the order
moves to `reconciling`.

That made `reconciling` a dead end, because `cancel_reconciled` can only say the
cancel DID take effect. So `order_reconciled` was widened to speak about a cancel
effect, where it means the venue still holds the order and the cancel did not take.
The cancel effect becomes `rejected` - no economic result - and the order returns
to `live`, which is also what lets `apply_request_cancel` reuse the record rather
than consume an arena slot. The gateway chooses which kind to submit from the
query's `cancelled` flag; both kinds already existed, so this costs no bytes.

ACCEPTED-BEHAVIOUR CHANGE: Phase 4's outbox-reuse test previously rejected a
cancel and immediately re-requested one. It now reconciles in between. The
arena-pressure guarantee the test exists for is unchanged - a reconciliation is an
observation and allocates no outbox slot - it just takes one more step to reach.

### 10.4 Two test-integrity defects found while doing this

Recorded because both are the kind of thing that makes a green suite worthless.

THE HARNESS WAS CHEATING. It set `VenueQueryView::authoritative` from the
simulator's `inconsistent` flag - which is the simulator telling the TEST what it
did, not something any real gateway could know. The gateway was being handed
ground truth, so a corrupted answer never reached the reducer and the reducer's
refusal to guess was never exercised. The field now models only what a venue says
about its own query window, and a corrupted answer reaches the reducer to be
caught there. Section 10.9 renames it `window_complete`, because "authoritative"
was still claiming more than a venue can actually say.

THE FAULT CORRUPTED IN THE HARMLESS DIRECTION. `inconsistent_query` reported
`original + 1` - above what the cluster booked, which 10.3.4 establishes is the
recoverable direction. Its own comment claimed it tested that reconciliation
"never overwrites conflicting evidence to make a dashboard green". It now denies a
fill it has already made, which is the unclosable direction and the one that
claim is about.

A THIRD, found by exercising cancels end to end in the matrix rather than only in
unit tests. The simulator attributed a cancellation to the SUBMIT's effect
identity, because `base_observation` carries the resting order's originating
effect - right for an acknowledgement or a fill, wrong for a cancellation, which
resolves the cancel effect. The gateway therefore never saw a response to its
cancel and timed it out, while the reducer refused the evidence as inapplicable to
a submit. Unit tests could not find this: they construct observations directly and
so had the identity right by construction. Only the end-to-end path had it wrong.

Known coverage boundary, stated rather than papered over: the matrix cannot
produce a reconciliation conflict. That needs an order which went unknown while
already holding booked fills, and the fault model applies one fault per operation
so a partial fill and a lost response never combine. The branch is covered by
direct unit tests instead. Closing it properly means letting faults compose, which
is a simulator change worth making on its own.

### 10.5 Discharging 9.1.7: the measured retention horizons (2026-09-10)

Section 3 requires that identity retention exceed the maximum retry, recovery,
snapshot restore, and reconciliation horizon. Phase 4 was accepted unable to show
that: 8.6 and 8.7 made retention finite and capacity-driven, and 8.4 recorded the
horizons as unknown until a gateway existed to produce them. They exist now, and
these are the numbers.

Measured by `v2-gateway-delivery-tests` driving the real kernel, guard, planner,
and adversarial venue. Reported by setting `VELOCITA_HORIZON_REPORT=1`.

#### 10.5.1 Peak occupancy is the WRONG measure, and this was got wrong first

The first version asserted that no arena reaches its ceiling. It failed
immediately on the decision arena at 16 of 16 - under NOMINAL faults, with zero
conflicts.

That is not a defect. The decision arena sweeps ON ADMISSION WHEN FULL, which is
the reclamation trigger Phase 4 deliberately chose over a background sweep.
Reaching the ceiling is how reclamation starts. An assertion on occupancy
condemns a sweep-on-full arena for working exactly as designed.

The liveness property lives one level down: not "no arena is ever full" but NO
ADMISSION WAS EVER REFUSED FOR WANT OF ROOM. If the sweep frees enough, capacity
genuinely exceeds the horizon; if it does not, admissions are refused and the
capacity is wrong. The measurement now counts capacity refusals across every
command the harness issues.

#### 10.5.2 Nominal faults: 48 seeds, 40 cycles

| arena | capacity | peak | margin |
|---|---:|---:|---:|
| decisions | 16 | 16 | 0 |
| orders | 16 | 12 | 4 |
| reservations | 16 | 12 | 4 |
| outbox effects | 16 | 14 | 2 |
| request receipts | 256 | 189 | 67 |
| venue events | 64 | 51 | 13 |
| caller cursors | 8 | 1 | 7 |
| source events | 128 | 40 | 88 |

- capacity refusals: **zero**;
- orders reclaimed while holding unresolved exposure: **zero**;
- order groups reclaimed: 24, so the run completed full create-resolve-sweep
  cycles rather than only filling;
- maximum order age: 105 state versions; maximum unresolved and unsettled age:
  105;
- send attempts used 3 of 3; query attempts used 4 of 4 - both bounds are reached,
  so neither is an untested guess.

VERDICT: for nominal adversarial operation, the accepted capacities exceed the
measured horizon. Section 3 is satisfied and 9.1.7 is discharged for this load.

#### 10.5.3 Conflict-inducing faults, and the operational bound they expose

Adding `fill_after_cancel` and `inconsistent_query` raises the maximum order age
to 178 state versions against a run length of 191 - an order that lives almost the
entire run. That is a conflict, and a conflict is terminal-but-unresolved by
design, so it is never reclaimed.

The consequence is worth stating plainly, because it is the most operationally
important number in this section: EVERY UNRESOLVED CONFLICT PERMANENTLY CONSUMES
ONE SLOT IN THE DECISION, ORDER, RESERVATION AND OUTBOX ARENAS AT ONCE. With
sixteen slots each, sixteen simultaneous conflicts saturate the cluster and it
stops accepting new decisions.

That is correct behaviour - refusing is the only alternative to freeing capacity
against exposure that may still exist - but it makes the G-074a operator
escalation path LOAD-BEARING rather than a convenience. A cluster with no operator
path has a hard ceiling of sixteen lifetime unresolved conflicts.

The capacity cannot simply be raised to soften it. `TradingState` is 81,168 bytes
against the accepted 81,920 ceiling, and one more decision record costs 704 of the
752 bytes remaining. Exactly one more slot fits, which changes nothing. Any real
increase requires moving the state ceiling, which is itself provisional pending
NUC validation. This is a bound to characterise and to staff around, not a number
to move.

#### 10.5.4 The safety property, which holds at any capacity

Saturation is a liveness bound and is acceptable. Premature reclamation is a
correctness failure and is not. The harness tracks every order across ticks and
counts any that leaves the arena while still holding unresolved exposure.

Measured zero under both loads. That is the property section 3 actually protects,
and it holds independently of whether the capacities are generous.

#### 10.5.5 Two defects the measurement found before it could report anything

THE GATEWAY'S OWN BOOKKEEPING NEVER SHRANK. `GatewayDeliveryState` and
`GatewayReconciliationState` are sized like the reducer's arenas but only ever
appended, while the reducer reclaims resolved groups. A gateway that ran long
enough exhausted its attempt arena and began refusing to record sends on a cluster
with plenty of room. No short run shows it; the first 40-cycle run hit it
immediately. `reclaim_delivery_attempts` and `reclaim_reconciliation_attempts` fix
it, keyed on records the cluster still holds and deliberately kept out of
`plan_delivery` so that function stays pure.

A FILL THAT CANNOT BE BOOKED WAS REFUSED SILENTLY. With `fill_after_cancel`
enabled, a venue reports a fill after the cluster has released the reservation on
final cancel evidence. `apply` refused it with `fill_quantity_conflict` and
changed nothing, so the venue traded and the cluster's position simply did not
reflect it.

This was unreachable before Phase 5: nothing produced `cancel_reconciled`, so a
released reservation could never be contradicted. A reconciliation query answer is
a SNAPSHOT, and a venue can still report a fill that was already executed when it
called the order cancelled - so the gateway asserts a finality it does not have.
The fill still cannot be booked, because booking it would exceed authorized risk.
What changes is that the order now goes to `conflict`, which is
terminal-but-unresolved, so the group is not reclaimed and the divergence is
visible to the operator who has to settle it. The gate was tightened to match: a
fill may be refused as unbookable, but only if a conflicted order is left behind.

### 10.6 An invariant that decides which conflict branches exist (2026-09-10)

Found while adding snapshot coverage for Phase 5 states, and worth stating
because two tests here were originally written against a state it forbids.

`apply_observe_fill` marks the submit effect `acknowledged`: a fill proves the
submit reached the venue. A resolved effect cannot be declared unknown again.
Therefore A SUBMIT EFFECT IN `submission_unknown` ALWAYS HAS ZERO BOOKED
QUANTITY, and `order_reconciled` about a submit can only agree with the cluster
or be ahead of it - never behind.

The conflict branches are still live, but only through the CANCEL path. A
partially filled order can have its cancel go silent, and the order is then in
`submission_unknown` with fills already booked, its current effect being the
cancel. That is the only way an order holds booked fills while its current
effect's outcome is unknown, and it is now how every conflict test reaches the
state it asserts on.

The corrected fixtures also record an ordering constraint that is easy to miss:
a fill must be booked BEFORE a cancel is requested. Fill evidence is scoped to
the order's submit effect, and `RequestCancel` makes the cancel effect current,
so a fill offered afterwards names the wrong effect and is correctly refused.

### 10.7 Phase 5 states survive snapshot restore, and how that is checked

Every Phase 5 path ends somewhere the reducer may be asked to persist, and the
snapshot codec was written and accepted before any of those paths existed. A
state that cannot round trip is a state the cluster cannot restart from, which
makes every guarantee above it conditional on never restarting.

Four states are now covered: a submission the venue denied, a conflict reached
through reconciliation, a silent cancel finally reconciled, and a cancel rejected
then restored to working by reconciliation - the last of which did not exist at
all before this phase.

The equality used is RE-ENCODE AND COMPARE BYTES, not `memcmp` of the struct, and
the distinction matters. `TradingState` has padding whose contents depend on how
the value was built: a state assembled by applying commands carries different
padding residue from one produced by `decode_state_snapshot` into a
value-initialised object. `memcmp` reports that as a difference when nothing
canonical has changed. The snapshot IS the canonical form, so re-encoding the
restored state and comparing bytes is the equality that actually means "the
cluster restarts here". This follows the convention the existing snapshot suite
already uses.

Note the narrower `memcmp` uses elsewhere in these tests remain correct: they
compare a state against a COPY OF ITSELF to prove a function mutated nothing, and
there both sides share provenance and therefore padding.

### 10.8 Reviewing the lifecycle by enumeration (2026-09-10)

The handoff named `apply_observe_order_event` as the highest-risk area in Phase 5:
nine interacting predicates feeding a guard disjunction with negations. Reading
that code is how it was written, so reading it again is a poor way to find what is
wrong with it.

`v2-lifecycle-reachability-tests` instead DRIVES the kernel breadth-first through
every command it accepts and records what actually happens. It converges at 27
reachable lifecycle positions from depth 6 onward, and it found three defects that
review-by-reading did not.

The tool asserts three things permanently:

1. THE INVENTORY. All 27 positions are listed and asserted. An edit that creates
   or removes a reachable position fails the test, so the author has to justify it
   rather than discover it later.
2. `conflict` IS A SINK. Nothing may carry an order out of it.
3. NO UNSETTLED EFFECT IS A DEAD END. Every position whose effect is still
   unsettled must have at least one accepted transition.

Its fixture is built independently of the delivery tests' fixture, on purpose -
the same discipline Phase 4 used for its property corpus. A shared helper would
hide exactly the kind of fixture error that has already bitten this phase twice.

#### 10.8.1 A fill could erase a conflict

`apply_observe_order_event` refuses everything from `conflict`. `apply_observe_fill`
did not. So a conflicted order - terminal-but-unresolved, retained precisely so an
operator can settle a disagreement - accepted a later fill, moved to `filled`,
RELEASED ITS RESERVATION, and vanished from the conflict projection entirely.

That is ADR 0001's "never overwrites conflicting evidence to make a dashboard
green", performed by the reducer itself. The fill path now refuses a conflicted
order, matching the venue-event path.

The asymmetry is the lesson: two functions that must agree about a lifecycle rule
had no structural reason to agree, and nothing compared them. Enumeration does.

#### 10.8.2 A pending cancel could be reconciled away

`reconciled_cancel` accepted a cancel effect in `pending`. A pending cancel has not
been delivered yet, so its outcome is not in doubt - and answering "the order is
still live" about it is true, useless, and destructive: it marked the cancel
`rejected` and returned the order to `live`, silently abandoning an intent the
operator had asked for.

Reconciliation about a cancel now requires its DELIVERY to be uncertain -
`unknown`, `rejected`, or `reconciling`.

Recorded consequence: a cancel whose send attempts are exhausted cannot yet be
reconciled, because its effect is still `pending`. The useful answer there is
"re-authorize a fresh cancel" and nothing does that. Opening the state up needs a
re-authorization story first, and inventing one here to close a gap the tool found
would be the wrong order of work.

#### 10.8.3 A cancel could become permanently unsettleable

An order that fills while its cancel is in flight leaves the cancel effect
`pending`. From there: the gateway redelivers until its attempts are spent; every
`order_rejected` the venue sends is refused because the order is terminal;
`need_of` never targets a finished order. The effect is never settled, so the
group is never reclaimable - one slot in each of four bounded arenas pinned
forever, while the order reads as healthily `filled`.

A venue refusing a cancel IS definitive evidence about that cancel, even when it
says nothing about an order whose outcome is already known. A cancel rejection
against a terminal order now settles the EFFECT and leaves the order untouched.

This one is worth noting as a class: the bug needed a fill, a cancel, a specific
interleaving, and a venue that answers with a rejection rather than a
cancellation. No unit test would have been written for it, and the fault matrix
cannot produce it because its faults do not compose. Only enumeration finds a hole
of that shape.

### 10.9 A complete answer is not a comparable one (Decision B, approved 2026-09-13)

`VenueQueryView` carried one boolean, `authoritative`, and the gateway treated it
as licence to hand an answer to the reducer. The name claimed more than the field
could deliver, and the gap between the claim and the delivery is an economic
hazard rather than a naming quibble.

WHAT THE FIELD ACTUALLY SAYS. A venue answering a query states that its answer
covers the window it was asked about - that its execution history is not paged
short. That orders the answer against OTHER ANSWERS from the same venue. It says
nothing whatever about ordering against FILLS arriving concurrently, because the
fill path and the query path are different streams that the venue never claimed to
sequence against each other. The field is now called `window_complete`, which is
what it means.

WHY THAT GAP IS EXPENSIVE. Every step of the following is individually true:

1. the cluster holds an order whose cancel went silent, with nothing yet booked;
2. the gateway asks the venue about it, and the venue snapshots it as working with
   cumulative zero;
3. while the answer is in flight, the order trades and the cluster books a lot;
4. the answer lands, reading cumulative zero against a cluster that has booked one.

By 10.3.4 a venue reporting BELOW what the cluster booked is the unclosable
direction: no later evidence reduces a booked fill, so the order is a permanent
`conflict` and its slot never comes back. Nothing in that sequence was wrong. The
loss comes entirely from comparing two moments as if they were one.

The shape is reachable only through the cancel path, and that is not incidental. A
fill RESOLVES a submit whose outcome was unknown, so a submit in
`submission_unknown` can never accumulate booked quantity while it waits. A cancel
is different: its outcome stays unknown while the order underneath it goes on
trading. That is the window a query can be issued in and answered after, and it is
the only one.

THE BARRIER. The gateway records the order's ACCEPTED-EVIDENCE WATERMARK when it
issues a query - the count of venue observations the cluster has accepted for that
order - and compares it against the watermark when the answer arrives. If it
moved, the answer predates evidence the cluster already holds, and
`classify_query` returns `incomplete`: ask again. The verdict is deliberately weak.
It does not say the answer is wrong. It says the answer is not comparable, which is
all the gateway can honestly know and all the reducer needs it to withhold.

Three properties keep this from becoming a stall dressed up as safety. A first
query has no barrier to compare against and is never barred by one. An unmoved
watermark submits normally. And the existing attempt budget still applies, so a
barrier that keeps moving ends in `exhausted` - the order stays visibly
unresolved, which is the G-074a operator situation rather than something to heal
by guessing.

NO NEW WIRE FIELD, DELIBERATELY. A real venue's freshness proof may be a monotonic
sequence, a session barrier, an executions cursor paired with an order watermark,
or a documented finality guarantee. Inventing a universal field in
`VenueObservation` before a venue supplies the semantics it would carry is
guessing at a byte contract. The gateway can establish the barrier from what it
already knows, and an adapter can strengthen it later with whatever its venue
actually offers. This is the same reasoning 10.1.1 used to refuse settlement
fields nothing produces.

WHERE THE BARRIER LIVES. Gateway-local, like delivery and reconciliation attempt
bookkeeping, and for the same reason: a restart discards it and rebuilds it by
querying again. It is not consensus state and must never become any, because it
is a fact about one gateway's network rather than about the position.

WHAT THE SIMULATOR HAD TO CHANGE. Two things, both of which were hiding the
hazard rather than merely failing to test it.

The harness asked and answered a query in the same statement, so the interval a
fill could commit in was exactly zero and the barrier was unreachable by
construction. Reconciliation is now a pipeline: a query is issued on one tick, the
venue snapshots at that moment, and the answer is classified on a later tick after
the drain. The final drain pass stops issuing and only collects, so no query is
stranded unanswered - an unanswered query leaves an order unresolved with nothing
on record saying why.

And `window_complete` was set unconditionally true, so the branch that refuses an
answer for not covering its window was never taken. `partial_window` is a tenth
simulator class: a venue that pages its execution history and says so. It is NOT a
malfunction and is kept distinct from `inconsistent_query` on purpose. Both read
below the truth; only one is the venue contradicting itself, and a gateway that
cannot tell them apart either treats ordinary paging as a permanent conflict or
treats a contradiction as something to retry away.

The matrix now asserts both halves are actually reached - an answer refused as
incomplete, and an answer that arrived after the cluster's evidence had moved.
Without those the barrier could be deleted and the suite would still pass.

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
