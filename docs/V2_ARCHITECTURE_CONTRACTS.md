# V2 architecture contracts

Status: proposed implementation contract under accepted ADR 0001. A contract
is not an implementation claim. Field sizes and limits become wire/storage
compatibility promises only after their Phase 1 golden-corpus gate is accepted.

Architecture decision:
[`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)

Execution plan:
[`V2 TDD implementation and validation plan`](V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md)

## 1. Purpose

Review-status override — 2026-09-21: historical Phase 6/7 completion prose does
not establish current acceptance. The full audit at
`evidence/V2_PHASE6_9_FULL_AUDIT_2026-09-21.md` records reproduced stale-role
authority renewal, lost emergency quota, unsafe wire lowering, missing persisted
membership and peer snapshot install, unbounded replication retention, and
hard-state evidence/ownership gaps. Local leadership is insufficient for sender
authority even in-process. Snapshot restore from one's own disk is not replacement
catch-up; asynchronous WAL bodies do not make metadata I/O asynchronous.
The original requirements below remain requirements. Proposed remedies and
two-host topology are in `V2_NUC_PI_TRADING_ROADMAP.md`, not newly approved bytes.

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
`ConfigureRiskLimits` replaces all eight settings for one `(AccountId, InstrumentId)`
in one reducer transition. It does not configure a venue account, authenticate an
operator, import external positions, or send an external effect.

Operator-only (P2.1, 2026-09-29). `ConfigureRiskLimits` joins the control set
gated by `is_operator_caller` (CallerId 1..15, `kMaxOperatorCallerId`). A command from any
other caller returns `unauthorized_control` before receipt lookup and leaves no
trace: no receipt, no StateVersion advance, state byte-identical. A strategy that
could raise its own cap would make every limit advisory. It is not a control-lane
command (`control_eligible`): it may not spend the kill reserve.

- `max_order_quantity`: per-order quantity ceiling, positive and no larger than
  `max_reserved_quantity`, the positive aggregate reserved-quantity ceiling.
- `max_order_gross_notional`: positive per-order gross notional ceiling.
- `max_reserved_exposure`: positive aggregate `reserved_margin + reserved_fee`
  hold budget, NOT aggregate gross order notional. There is deliberately no
  comparison requiring per-order gross notional to be below this hold budget.
- `initial_margin_ppm`: 1 through 1,000,000; `maximum_fee_ppm`: 0 through 1,000,000.
- `max_position_quantity`: zero or positive bound on |position| after every open
  order fills. Zero denies new exposure; it never means unlimited.
- `allow_short`: false is long-only (a sell may only reduce a long position).
- `reason_code`: non-zero reference into the operator's audit record of why the
  limits changed. Carried in the replicated log only; the kernel neither stores
  nor interprets it.

All arithmetic is checked; invalid settings are refused, never clamped.

Position rule, checked at `AuthorizeDecision` after the per-order and aggregate
caps and before any mutation. With P = signed position, R = `reserved_quantity`
(ALL open orders, both sides) and q = proposal quantity: a buy is allowed iff
P + R + q <= `max_position_quantity`; a sell iff P - R - q >= (`allow_short` ?
-`max_position_quantity` : 0). Otherwise `position_limit_exceeded` (ApplyCode 67);
checked-arithmetic failure is `risk_arithmetic_overflow`. Counting R on both sides
is deliberately conservative: either resting order may fill first, so netting them
would admit a position past the cap. A cap below the current position is accepted
and makes the account reduce-only (adding refused, reducing allowed); it is never
a reason to refuse the configuration.

Strategy re-sync. An `AuthorizeDecision` refused with `stale_risk_state` carries
result detail tag 7 (`risk_state`): words {current RiskVersion, current
PositionVersion, position quantity atoms as u64}. The strategy binds these on its
next frame instead of guessing. The detail is admitted only on `stale_risk_state`
with non-zero versions and a decision ID, by both the result codec and the
snapshot's retained-receipt validation. An exact retry of the same request
returns the retained result, so its versions are those current at the first
refusal; a strategy wanting fresh versions sends a new request.

Creation requires an absent key and `expected_risk_version = 0`; it initializes
RiskVersion and PositionVersion to 1 and all economic balances to zero. This is
an empty kernel ledger, not evidence that a real trading account has no position.
Update requires exact expected/current RiskVersion equality; the kernel advances
by exactly one, including fresh-request no-op updates. Overflow refuses rather
than wrapping. There is no delete/reset command and no caller-chosen next version.
Expected kill epoch and executor generation must match current state; configuration
is allowed while halted. Matching generations do not replace operator authentication;
the operator caller band above is the reducer's half of it.

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
not a 101-byte CPU atomic store or a durability claim. A mask/patch could also be
atomic, but full replacement avoids partial-update semantics on an infrequent
control path. The configuration cannot carry balance fields at all.

Exact request retries return the original result without another mutation.
Same-request/different-digest retries conflict; fresh stale updates refuse.
Admitted refusals can retain receipts and advance global StateVersion without
changing risk configuration or economics. Account/receipt capacity exhaustion
fails closed. New typed outcomes append `risk_account_capacity_exceeded`,
`risk_version_exhausted`, `risk_reconfiguration_blocked` and (P2.1)
`position_limit_exceeded`; existing outcomes cover invalid settings, stale
control/risk state, arithmetic overflow and insufficient capacity. Metrics count
`position_limit_exceeded` as a risk refusal.

Canonical kernel schema V9 command kind 17 has a 101-byte little-endian body:
account ID, instrument ID, risk-policy version, expected risk version, expected
kill epoch, expected executor generation (six u64); two quantities and two money
limits (four i64, in the order listed above); margin ppm and fee ppm (two u32);
then `max_position_quantity` (i64), `allow_short` (u8, only 0 or 1; anything else
decodes as `invalid_value`) and `reason_code` (u32). With existing
identity/framing/digest this is 193 bytes; the 1,789-byte command maximum is
unchanged. Result detail tag 5 uses the existing three u64 words for account ID,
instrument ID and resulting risk version. OrderEffect bytes and schema remain
unchanged.

Snapshot schema V9 appends `max_position_quantity` (i64, validated >= 0) and
`allow_short` (u8, validated 0/1) to each risk-account record: 112 -> 121 bytes.
The resident `RiskAccount` grows 112 -> 128 bytes (its 128-byte budget), so
TradingState grows 64 bytes; see the budget figures in 10.18.14.

No dual-version reader (recorded deviation). Deterministic Speed recommends that
a reader accept one previous version during a rolling upgrade. V9 does not: V8
commands, results and snapshots are refused as `unsupported_schema`, and the
static cluster identity binds both schemas, so a V9 binary refuses a V8 manifest
at startup and a mixed V8/V9 cluster cannot form. A V8 replica would otherwise
decode an 88-byte ConfigureRiskLimits body and diverge on the first one. The
deployment is a fresh cluster; there is no upgrade path.

Book alignment (Deterministic Speed ch. 49 limit records: scope, units, effective
log index, authorizer, reason, expiry, previous hash). Scope is (account,
instrument); units are typed fixed point (`QuantityLots`, `MoneyMicros`, ppm);
the effective index is the committing log entry; the authorizer is the operator
`CallerId` in the retained receipt identity; the reason is `reason_code`, whose
text lives in the operator's audit record; "previous" is the
`expected_risk_version` compare-and-set. Deviations: NO expiry (routine limits
only, no temporary override path, and no snapshot budget for one); four-eyes
approval lives in operator tooling before submission (ch. 18), not in the
reducer; pending orders are counted on both sides (conservative).

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
FILL and VENUE-EVENT receipts were originally pinned while their owning order exists, because
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

**SUPERSEDED BY 10.13.** Fill identities still follow this rule. Control-event
receipts now follow the stricter effect-reference plus producer-acknowledgement
proof because one unresolved order can legally supersede several serial cancel
effects.

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

### 9.4 The startup recovery barrier (P1-2, 2026-09-13)

`GatewayDeliveryState` is local and disposable by design (9.1.2), and 9.1.2 says
a restart "discards it and rebuilds by reconciliation (G-032)". Nothing
implemented that sentence. After a crash the planner saw a replicated `pending`
submit with no attempt record and scheduled it as a FIRST SEND, which is the
blind risk-increasing resend G-026 forbids.

The crash counterexample is three lines long: the cluster authorizes submit E and
retains it pending; the gateway sends E and the venue accepts it; the gateway
dies before the acknowledgement is applied. The restarted gateway now holds empty
bookkeeping against a cluster that still says pending, and sends E again.

A stable `EffectId` reduces the damage only if the venue deduplicates that
identity across the exact reconnect being tested. Relying on that is not the same
guarantee as reconciling before resending, and ADR 0001 chose the latter.

WHAT SEPARATES THE TWO CASES. A fresh authorization and one a previous
incarnation already sent differ in exactly one fact: WHEN this gateway learned of
the effect. Replicated state does not record that and should not - it is a fact
about a process, not about a position. So the gateway marks its own startup.

`begin_gateway_recovery` scans replicated state once and marks every unsettled
effect SUSPECT: an attempt record with `awaiting_response` set and
`resend_permitted` clear, which is exactly the pair a send that left the process
and has not been answered would carry. Nothing new is invented to handle it. A
suspect effect then travels the path an ambiguous send already travels - its
deadline passes, the shell reports `effect_unknown`, the reducer moves the order
to `submission_unknown`, and reconciliation queries the venue. "Reconcile before
resend" is not a new rule here; it is the existing rule applied to effects whose
fate the restart erased.

FAILING CLOSED IS STRUCTURAL, NOT ADVISORY. `plan_delivery` schedules nothing at
all until that scan has run, so forgetting the call sends NOTHING rather than
sending blindly. The unsafe behaviour is unreachable without an explicit call
rather than merely discouraged by a comment.

THE CONSERVATIVE DIRECTION IS FORCED. Phase 5 has no durable local proof that a
send never happened, so every unsettled submit is treated as possibly attempted -
including the ones that genuinely never left. That costs a query round trip on a
cold start. Durability, when it arrives in Phase 7, can only ever narrow the
suspect set; it can never widen it.

WHAT THE BARRIER BLOCKS, AND WHAT IT DOES NOT. New risk waits. A cancel does not:
it cannot create exposure, and a barrier that also stopped an operator getting
flat would be protecting the wrong thing at the worst possible moment. A suspect
cancel still waits out its synthetic deadline first, so its redelivery is legible
as redelivery rather than as a fresh send.

WHEN THE BARRIER LIFTS - and this is the part that needed a decision rather than
a rule. It lifts once every suspect has been HANDED TO RECONCILIATION, not once
every suspect order is RESOLVED. The stricter reading is tempting and wrong: a
suspect order whose query attempts are spent would block new risk on every other
slot forever, trading a startup hazard for a permanent outage. Its own slot stays
blocked by ordinary slot exclusivity, which is the mechanism that already exists
for exactly that situation, and the suspect submit stays unresendable regardless
of the barrier because its attempt record carries `resend_permitted = false` for
the life of the effect. The safety property does not depend on the barrier; the
barrier only bounds how long the gateway trades while its picture is incomplete.

Tested at three crash cuts - before the send, after venue acceptance but before
local recording, and after local recording but before the observation applied -
against the real simulator, with "how many orders exist at the venue" as the
oracle rather than any gateway-reported number.

`OutboxState::incomplete` is excluded from the startup suspect set. It is the
replicated terminal statement that an orphaned effect's outcome was never
established after the order's risk resolved; the pre-send guard already refuses
it and reclamation may remove it. Treating it as in flight manufactured an
awaiting-response attempt that no legitimate transport action could complete and
could leave the startup barrier raised forever. Exclusion does not conceal the
loss: `settled_incomplete_effect_count` remains monotonic and operator-visible.

### 9.5 A venue answer is not a commit (P1-3, 2026-09-13)

The delivery shell called `record_response` before applying a venue observation,
and ignored the apply result before clearing the local awaiting-response flag.
That is harmless only while `apply` is a synchronous, infallible function call.
Phase 6 makes proposal, leadership, commit and apply four separate outcomes, and
then the trace is:

1. the venue answers E, so the gateway correctly stops its transport timer;
2. the gateway proposes the canonical observation to the current leader;
3. the proposal response is lost, or leadership changes, before the gateway
   learns the committed result;
4. local `awaiting_response` is already false while replicated E is still pending;
5. submit redelivery is forbidden, ambiguity is no longer scheduled, and
   reconciliation does not target that shape;
6. the effect is stranded with nobody owning the retry of its evidence.

TWO ACKNOWLEDGEMENTS, NOT ONE. The venue answer completes TRANSPORT. Only the
cluster commit completes delivery of evidence to the authoritative state machine.
`record_response` now closes the first; `record_evidence_obligation` opens the
second, and only an observed committed result closes it.

`GatewayEvidenceState` is a bounded gateway-local work list of canonical
observation commands. It holds the OBSERVATION, not a rebuilt command, so a retry
reconstructs identical bytes rather than rebuilding them from state that has moved
on. It holds the identity unchanged, because a retry that minted a fresh one would
be a second economic command rather than the same one again.

WHAT MAKES THE RETRY SAFE IS ALREADY IN THE REDUCER, and it is stronger than
expected. `apply` answers an identical command from its retained request receipt
by returning the ORIGINAL RESULT verbatim - not a bare "already applied". That
distinction is what makes this seam work at all: a gateway that lost a result
needs to learn what the result WAS, not merely that there had been one. So a
replayed result discharges the obligation, and the state machine does not advance.

The two commit failures are deliberately modelled as different things, because a
gateway cannot tell them apart and must be correct under both. A LOST RESULT means
the command was applied and the gateway never learned; the retry meets the
receipt. LOST LEADERSHIP means the command never reached the state machine; the
retry is the first and only application. Only an unchanged identity makes one
mechanism cover both.

BACKPRESSURE. Both halves of `plan_delivery` produce evidence - a send produces a
venue answer, an ambiguity declaration produces an `effect_unknown` command - so a
full queue has to stop both. Evidence that cannot be queued is evidence that gets
dropped, and no redelivery reproduces it and no query targets it. Stopping is the
only option that loses nothing, and it self-clears as commits are observed. The
queue is sized at four obligations per outbox effect rather than one, because an
effect produces several observations over its life; sizing it to the outbox would
make backpressure a throughput limit rather than a commit-problem signal.

Phase 6 owns the transport. The obligation is Phase 5's, proved against an
injected commit result, so Phase 6 does not inherit a seam that was never designed.

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

CLOSED IN 10.11. Faults now compose and the boundary above no longer exists; the
matrix produces reconciliation conflicts on its own. The paragraph is kept as
written because the reasoning it records - that a coverage claim was a property of
the SELECTOR rather than of the system - is the part worth remembering.

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
ONE SLOT IN THE DECISION, ORDER, RESERVATION AND OUTBOX ARENAS AT ONCE.

CORRECTED IN 10.12. The sentence that stood here said sixteen simultaneous
conflicts saturate the cluster, on the strength of a harness that used one
logical order slot. A later correction attributed the measured limit of eight
to the templates in one active policy. That proof was also wrong: policy rotation
can expose different logical slot values while old conflicts remain. The
rotation-aware measurement still stops at eight, but the enforcing mechanism is
the outbox's reserved cancel budget, not the active policy's template count.

That is correct behaviour - refusing is the only alternative to freeing capacity
against exposure that may still exist - but it makes the G-074a operator
escalation path LOAD-BEARING rather than a convenience, and it does so at half
the number this section used to claim.

Raising only the decision/order/reservation arenas would not soften it. Every
unresolved order owns a submit effect and must retain capacity for one future
risk-reducing cancel. An unresolved order with no cancel therefore costs one
resident outbox record plus one reserved record; once a cancel exists it costs
two resident records. With sixteen outbox records, eight is the best-case
concurrent unresolved-order ceiling. Repeated historical effects can bind
earlier, never later.

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

THE BARRIER. The gateway records the order's ACCEPTED-EVIDENCE REVISION when it
issues a query - the last `StateVersion` at which evidence changed that order -
and compares it against the revision when the answer arrives. If it moved, the
answer predates evidence the cluster already holds, and
`classify_query` returns `incomplete`: ask again. The verdict is deliberately weak.
It does not say the answer is wrong. It says the answer is not comparable, which is
all the gateway can honestly know and all the reducer needs it to withhold.

Three properties keep this from becoming a stall dressed up as safety. A first
query has no barrier to compare against and is never barred by one. An unmoved
watermark submits normally. And the existing attempt budget still applies, so a
barrier that keeps moving ends in `exhausted` - the order stays visibly
unresolved, which is the G-074a operator situation rather than something to heal
by guessing.

The first implementation used a retained-receipt count. Section 10.13 supersedes
that choice: reclamation can decrease a count and later evidence can return it to
the same value, so equality can recur through ABA. The per-order revision is
monotonic while the order exists and survives snapshots.

VENUE FINALITY IS SEPARATE. `window_complete` says the answer is not paged short;
it does not prove the snapshot was ordered after the sends it adjudicates. The
gateway must separately establish `ordered_after_relevant_sends` from a venue
barrier, ordered session, or documented API guarantee. Both facts must be true.
This stays gateway-local rather than adding an invented universal field to
`VenueObservation`.

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

### 10.10 Two harness oracles that could pass while losing money

Both are test-integrity defects in the same class as 10.4, found by review rather
than by failure - which is the point: a wrong oracle never fails.

THE FILL ORACLE CHECKED A SUM. It asserted `booked <= reported` per order, which
prevents the cluster inventing quantity and permits every reported fill to remain
unbooked. Underneath it, a global allow-list treated
`venue_observation_not_applicable`, `order_not_found` and the identity conflicts
as "lossless" without proving that the particular execution was already booked or
belonged to a safely reclaimed order. And one conflicted order anywhere in the run
globally excused every `fill_quantity_conflict` refusal, including refusals on
entirely different orders.

The flaw is structural rather than a missing case. A REFUSAL CODE IS A FACT ABOUT
A COMMAND; what must not be lost is an EXECUTION. The same code is safe for one
execution and evidence loss for another -
`venue_observation_not_applicable` on a swept order owes nothing, and on a live
one it is a fill that vanished. No allow-list can tell those apart.

So the gate now follows every distinct `(venue, execution)` from the venue's
report to the cluster's answer. A booked execution must move position once for
the reported quantity. An unbooked execution must leave its owning order in
`conflict` with the exact `(VenueId, ExecutionId)` receipt retained. A
resolved-and-swept order proves only that risk was closed before reclamation; it
cannot prove that a later reported execution was booked. The per-order sums
survive as a cross-check against the ledger, so a disagreement between the two
stops the run rather than being silently resolved in favour of whichever is
consulted first.

THE RECONCILIATION ORACLE READ THE ANSWER OFF THE CODE UNDER TEST. It labelled
every `venue_observation_not_applicable` from a reconciliation answer as
`answered_ahead` - permitted missing evidence - without checking whether the
answer's cumulative quantity was actually above what the cluster had booked. A
reducer applicability defect of exactly the P1-4 shape would therefore have been
recorded as correct behaviour.

The harness now computes the relation itself, from the reservation read BEFORE the
apply and from the answer's own bytes, and checks the reducer against it: an
applied answer below booked must leave the order conflicted; an applied answer
equal to booked must not change whether it is conflicted; and a refusal only
counts as `answered_ahead` when the answer was independently shown to be above.
Anything else is recorded as an applicability refusal rather than excused.

The rule both repairs follow: A TEST RESULT MAY NEVER SERVE AS ITS OWN EXPECTED
ANSWER. Both oracles now have non-vacuity assertions of their own, because an
oracle that never evaluates is indistinguishable from one that always agrees.

### 10.11 Faults compose, and a label is not a behaviour (2026-09-13)

Two defects in the same organ, both of which made the fault matrix claim more
than it tested.

THE SELECTOR CHOSE ONE CLASS PER OPERATION. `choose_fault` returned on the first
match, so cross-category combinations were unreachable by construction. The
acceptance record then reported "the matrix cannot produce a reconciliation
conflict" as a property of the system. It was a property of the selector.

Worse, it hid a second reason. Every submit set the venue order's filled quantity
to its full original immediately, so by the time any cancel arrived there was
nothing left to cancel. `fill_after_cancel` could therefore only fire on an order
whose submit response had been LOST - the one case that leaves the venue holding
an unfilled order - and a partially filled order with a silent cancel, which is
the only shape a reconciliation conflict arises from, could not occur at all.

Both are fixed together, because fixing either alone leaves the claim false.
Every class now rolls independently in a fixed order, and a partial fill delivers
half the order and leaves the rest resting. `rate_limit` and `disconnect` still
preempt the rest - not by precedence, but by consequence: both mean the effect
never reached the venue's book, so there is nothing left for another class to act
on.

The matrix now produces reconciliation conflicts on its own, and asserts that it
does. The old note recording the gap has been kept rather than deleted: the
lesson in it - a coverage claim that is really a statement about the test harness
- outlives the gap itself.

A LABEL IS NOT A BEHAVIOUR. `test_every_fault_class_is_reachable` swept seeds
until each enum value had appeared in the fault log and called that coverage. It
was wrong about at least one class and could not have detected it: selecting
`loss` for a CANCEL lost nothing, because the cancel branch scheduled its
cancellation and returned before the submit branch's loss check ever ran. The
label went into the log and the behaviour it names never happened.

`loss` now means the response stream for ONE OPERATION dies, which covers a
cancel as well as a submit, and the venue is left holding the order exactly as it
was. It deliberately does NOT mean the venue silently traded: an order that filled
with no execution ever reported could not be closed by any later evidence, because
`order_reconciled` carries a cumulative total and no execution identity - so the
cluster would hold a permanently unresolvable order by construction rather than by
fault, which is a modelling error rather than an adversary.

Every class is now asserted by what it DID. Rate limiting produces no venue state;
a lost submit leaves an order the venue still answers about; a lost cancel leaves
an order the venue reports as cancelled with no cancellation ever delivered;
duplication delivers identical bytes twice; a partial fill delivers more than one
execution; `fill_after_cancel` delivers one after the cancellation; a delayed
response misses its own latency band. Reorder is asserted as an A/B - the same
seed with the class on and off must deliver a different order - because predicting
the permutation would mean encoding the scheduler's tie-breaking into the test,
and the test would then pass by agreeing with the code rather than by observing
it.

G-033 consequence: the guarantee reads "nine adversarial classes" in older
evidence. It is ten, they compose, and each is asserted by effect. The acceptance
record is superseded accordingly rather than amended.

### 10.12 The concurrent-conflict bound is the reserved outbox budget

Two earlier explanations were wrong. The Phase 5 acceptance harness used one
logical slot and could not prove sixteen concurrent conflicts. The first review
then treated eight templates in one active policy as a cluster-lifetime slot
namespace. Policy rotation disproves that: a later policy can name slot 9 while
conflicts on slots 1 through 8 remain retained.

The measured bound is nevertheless eight for a different reason. Authorization
enforces `outbox_effect_count + orders_needing_cancel_slot + 2 <=
kMaxOutboxEffects`. The two added units are the new submit and its reserved future
cancel. Thus every unresolved order consumes two units of the outbox budget:
either one submit plus one reserved cancel, or two resident submit/cancel effects.
With `kMaxOutboxEffects == 16`, at most eight such orders can coexist in the best
case. Extra historical effects reduce this availability bound.

`test_policy_rotation_reaches_the_reserved_outbox_conflict_bound` drives eight
distinct slots to conflict through public commands, then publishes, validates,
stages, and activates policy version 2 with logical slots 9 through 16. A valid
proposal on slot 9 reaches authorization, proving slot identity is not the
barrier, and is refused with `outbox_capacity_exceeded`. At that point eight
orders, eight reservations, and sixteen effects remain retained; no unresolved
risk was reclaimed.

The rejected alternatives remain useful. Calling sixteen a runtime bound ignored
the reserved cancel lane. Calling eight a template bound ignored policy rotation.
Increasing only decision/order/reservation capacity changes neither result;
increasing outbox capacity changes it but spends replicated-state and snapshot
budget and still requires the G-074a operator path because conflict is
intentionally unreclaimable without authoritative evidence.

### 10.13 Proof-carrying evidence reclamation (approved 2026-09-14)

The venue-event watermark introduced for repeated serial cancels was unsafe in
two independent ways. It compared venue-native sequences with gateway-authored
control sequences inside one numeric domain, and it inferred a contiguous prefix
from the midpoint of sparse observations. A delayed native response could
therefore be classified expired even though it was new evidence. Increasing the
arena or separating number ranges only delays or disguises that error; neither
proves that a forgotten observation cannot still arrive.

Reclamation now requires proofs rather than age:

```text
the exact owning effect is settled, superseded, or gone
AND the stable gateway has durably consumed the committed command result
AND no live fill identity or current unresolved-effect provenance depends on it
=> the venue-event receipt may be compacted
```

Each term closes a different failure. Effect settlement prevents compaction
while final evidence can still revise the effect. The gateway acknowledgement
separates cluster apply from the producer learning the result; without it, a
lost Raft client response can race receipt deletion. Economic/provenance pinning
keeps all fill receipts while their order exists and keeps the current unresolved
effect, so capacity relief never comes from forgetting executable risk.

The ownership edge is explicit. An applied venue-event `ApplyResult` stores the
observation's `EffectId` in metadata word 0; `ApplyResult` remains 112 bytes and
its frame remains 128 bytes. Policy accessors return no policy identity for this
tag. A zero effect identity in an accepted result is invalid and snapshot/result
decoders fail closed.

The consumer proof is command kind 18,
`AcknowledgeGatewayResults{acknowledged_through}`. The caller is the command's
stable `CallerId`; the body is one little-endian u64 and covers an inclusive
request prefix strictly below the acknowledgement command's own sequence. The
replicated cursor advances monotonically and a repeated or lower prefix is a
no-op. The meaning is stronger than socket receipt: the gateway has durably
incorporated every committed result in the prefix and will not blindly recreate
those commands after restart. A producer that cannot make that promise must not
advance the coordinate; bounded backpressure is the correct failure.

The withdrawn `VenueCursor` arena and all midpoint logic are removed. Apply code
59, `venue_event_expired`, remains allocated only so historical result values are
not renumbered; schema V6 reducers never emit it. Re-presenting a compacted
request identity is refused through the existing caller prefix and cannot
re-execute. A redelivery under a new request identity still meets effect and
order lifecycle checks; fill identity remains retained and independently keyed
by `(VenueId, ExecutionId)`.

Query ordering changes with reclamation. A receipt count is not a revision:
compaction can move it down and later evidence can return it to the old value.
`ClusterOrder::last_evidence_state_version` is therefore the cluster-side
barrier. It advances on accepted evidence and on fill refusals that deliberately
move the order to conflict, is encoded in snapshot V6, and never regresses while
the order exists. It detects concurrent cluster evidence but does not establish
venue finality, so `VenueQueryView` separately requires both `window_complete`
and `ordered_after_relevant_sends`. A complete but pre-send snapshot is retried,
never used to release risk or manufacture conflict.

The simulator models the matching input-ownership rule. `poll` exposes due
observations once per gateway incarnation but retains their canonical bytes.
`restart_delivery` re-exposes everything not acknowledged, and
`acknowledge_observation` removes an identity only after a committed result is
observed. This is the replayable-input option; a future adapter for a venue that
cannot replay must durably journal the observation before acknowledging it.
That adapter choice depends on real venue capability and remains outside Phase 5.

The byte migration is explicit: kernel command/result and state snapshot schemas
advance to V6 with no silent V5 reinterpretation. Measured resident state is
81,360 bytes against the 81,920-byte ceiling; maximum canonical snapshot is
65,451 bytes; headroom is 560 and 85 bytes respectively. The small snapshot
margin is now an engineering constraint: further replicated fields require a
reviewed rebalance or schema redesign rather than optimistic packing.

### 10.14 Independent evidence and recovery proofs

Acceptance machinery must not obtain its expected and actual values from the
same returned object. The original execution matrix incremented both its
per-order booked total and its per-execution booked count from
`ApplyResult::filled_quantity`. A reducer that emitted the expected receipt but
failed to move position would satisfy both comparisons. Result loss made the
inverse mistake: it skipped the bookkeeping even though cluster state had already
mutated.

The corrected oracle samples authoritative position immediately before and after
every fill apply, before any simulated result loss. The execution's side converts
that signed delta back to executed quantity; the ledger permits one nonzero
mutation per `(VenueId, ExecutionId)` and the signed sum must equal final
risk-account position movement. Returned receipts still prove identity and
refusal reason, but no longer prove their own economic effect.

Polling cannot independently prove that every venue execution was delivered,
because queue deletion removes both the input and the oracle's observation of
the input. The simulator therefore records execution truth at venue-book mutation
and records delivery separately. Every actual execution must later enter the
replayable delivery ledger with the same order, side, and quantity. Disconnect
changes only the session assigned to future observations; it neither erases nor
acknowledges already scheduled evidence. This is the same ownership rule as a
durable input log: producer acknowledgement, not connection lifetime, releases
the record.

Recovery uses the same authority ordering. `GatewayDeliveryState` is a
reconstructible process cache; `TradingState` is replicated authority. An effect
may become `incomplete` after startup scanning when definitive sibling evidence
resolves the order and settles orphaned effects. Recovery completion must inspect
the current outbox state before any cached `awaiting_response` flag. Pending or
otherwise unresolved replicated effects still block; acknowledged, rejected,
unknown, reconciling, conflict, incomplete, and safely swept effects do not.

Snapshot validation follows cross-record ownership rather than validating new V6
fields in isolation. When a retained venue-event result names a retained effect,
both must name the same decision. When an applied fill or venue event names a
retained order's decision, the order's monotonic evidence revision must cover the
result state version. These checks reject checksum-valid graphs that the reducer
could never produce and that reclamation or stale-query barriers would otherwise
trust.

One identity issue remains pending explicit byte-contract approval. Native venue
control evidence and gateway-synthesized reconciliation evidence currently share
the three-number venue/session/sequence key even though they have different
sequence producers. The recommended representation derives a two-valued origin
domain from `VenueObservationKind` and retains it in the unused venue-event result
metadata word. This costs no resident or wire bytes. A domain per event kind is
rejected because it weakens global native sequence uniqueness; numeric ranges are
rejected because convention is not proof; an explicit observation field is
rejected for now because snapshot headroom is only 85 bytes.

Proof-carrying retention needs a control escape from its own bound. If every
ordinary receipt slot contains an unacknowledged gateway result, making the
acknowledgement compete for an ordinary free slot deadlocks reclamation. The
acknowledgement therefore has a narrow admission rule: only a syntactically
valid, forward-moving prefix from an existing caller may evaluate compaction
against its proposed value. Covered receipts are removed first, after which the
command advances the cursor and occupies a normal receipt slot. No permanent
slot is reserved, and a command that cannot prove it will free capacity remains
refused.

Producer acknowledgement does not make every fill result permanently valuable.
The original applied `(VenueId, ExecutionId)` receipt remains the economic
identity tombstone while its order exists. An `already_applied` or identity-
conflict response is derivable from that tombstone and may compact once consumed.
For state-changing capacity or quantity conflict, the first exact execution
receipt remains and repeated copies compact. This bounds duplicate delivery
without erasing either the booked identity or the evidence that made an order
conflicted.

`fill_quantity_conflict` now has one lifecycle meaning. Even the checked-
arithmetic failure path marks the order conflicted and advances its evidence
revision; returning the same code without recording the disagreement made
snapshot validation dependent on hidden control flow. Snapshot admission can
therefore require the order revision to cover retained fill-capacity and
fill-quantity conflict results as well as applied observations.

### 10.15 Native and synthetic observations are different identity domains (approved 2026-09-15)

Every `VenueObservation` carried a `(VenueId, VenueSessionId, VenueSequence)`
triple and the reducer deduplicated all of them on it. Only some of them own one.

NATIVE observations - `order_acknowledged`, `order_rejected`, `fill`, `cancelled`
- are the venue's own events. The sequence is the venue's, it is monotonic
because the venue says so, and the triple is a real identity the cluster may
refuse a repeat on.

SYNTHETIC observations - `effect_unknown`, `order_reconciled`,
`cancel_reconciled` - are authored by the GATEWAY about an order. A declared
timeout is not a venue event. A query answer is a snapshot, not an event, and
10.1.6 already records that it has no venue sequence of its own. Whatever number
reached the wire was chosen by the shell, and nothing coordinates that choice
with the venue's counter.

WHAT THE SHARED DOMAIN COSTS, in both directions and both reachable:

- a gateway-authored `effect_unknown` takes sequence N; the venue later answers
  the same cancel for real at its own sequence N; the native cancellation is
  refused `venue_event_identity_conflict`. Real venue evidence discarded because
  a gateway-authored record squatted on a number it did not own.
- the venue used sequence N earlier; a reconciliation answer carrying N is
  refused the same way. That answer is the only thing that can resolve an
  unknown effect, so the order is stranded with nothing else able to reach it.

Reserving a numeric band for the gateway was considered and rejected. It prevents
a collision only for as long as the assumption about the band holds, and a real
venue's sequence is whatever that venue emits - the gateway cannot know it in
advance. An assumption that fails once and fails silently is worse than no
assumption.

THE CONTRACT. `ObservationOrigin` is derived from the observation kind by a total
switch, so the two can never disagree and no byte is spent saying twice what the
kind already says once. `find_applied_native_event` searches the native domain
only, and the venue-event path consults it only for a native observation.

The origin is RETAINED in venue-event result metadata word 1 - the word left free
when 10.13 put the owning `EffectId` in word 0. A reclamation or deduplication
pass must know whether a receipt's sequence means anything without going to find
the observation it came from. Zero bytes added: `ApplyResult` stays 112 bytes,
`TradingState` stays 81,360 with 560 bytes of headroom, and the maximum canonical
snapshot stays 65,451. Retained result VALUES change, so the codec and snapshot
schemas move to V7 and the golden corpora were regenerated as a reviewed
migration.

WHAT PROTECTS A SYNTHETIC RETRY INSTEAD:

1. COMMAND IDENTITY. The retained request receipt answers an identical command
   with the original result and does not re-execute. The gateway's evidence
   obligation (9.5) retries under the identity it first used, byte for byte,
   which is what makes this work - and that makes the seam's identity discipline
   load-bearing for exact result recovery and not only for delivery. Pinned by
   `test_a_replayed_synthetic_answer_is_caught_by_command_identity` rather than
   left as an inference.
2. STATE-BASED APPLICABILITY. Reconciliation carries a FINAL CUMULATIVE total,
   so while an effect remains unresolved its verdict is a function of the total
   and the evidence already booked, not of message history. Once an answer
   resolves the effect, the same observation under a fresh command identity is
   refused `venue_observation_not_applicable`. It does not re-enter an economic
   delta path, and `order_reconciled` never invents missing fills in the first
   place.

A consequence worth stating because a test had pinned the old behaviour: a
synthetic answer re-presented under a fresh identity after its effect has
resolved is now refused `venue_observation_not_applicable` rather than
`already_applied`. The old refusal claimed "this identity is taken", which was a
statement about a number nobody owned. The new one is a statement about the
ORDER, and it is the same statement the reducer makes about any late evidence for
a settled effect.

RESTART IS NOT A RETRY UNDER A REBUILT IDENTITY. `GatewayEvidenceState` is
process-local. If the process dies after the synthetic command commits but before
it sees the result, the replacement gateway first reads replicated state. A
resolved effect needs neither the lost obligation nor another query. If the
effect is still unresolved, the replacement gateway issues a new query and its
answer is a new snapshot, not a reconstruction of the lost command. This boundary
is covered by the startup recovery and reconciliation suites as separate
properties. A single-trace characterization remains useful, but is deferred to
the next gate-moving source increment because it would not change production
behavior.

FILLS ARE UNCHANGED and deliberately so. A fill is native, and its authoritative
economic identity is `(VenueId, ExecutionId)` rather than the sequence triple -
that is what makes a duplicate fill impossible to book twice, and it is
independent of everything above.

THE SIMULATOR ACKNOWLEDGES BY CANONICAL FINGERPRINT, not by the triple. Matching
on `(venue, session, sequence)` let a synthetic observation retire a native one
the cluster had not yet committed - evidence loss dressed up as bookkeeping, and
invisible because the queue merely got shorter.

The decoder validates word 1 as an exact enum value. `event_origin()` resolves
anything it does not recognise to `native`, which is the correct fail-closed
reading at runtime and precisely the wrong thing to accept from a decoder: a
forged word would move a gateway-authored receipt into the venue's identity
domain, where it can refuse real venue evidence.

### 10.16 MVP V1 excludes perpetual execution (approved 2026-09-15)

MVP V1 does not trade perpetuals. The verified True Markets DeFi surface is
therefore not a V1 execution target. The existing reducer may continue to model
spot-style order effects, but no adapter may present a perpetual liquidation,
ADL, or funding movement as though it descended from one of those effects.

This avoids adding speculative economic state while the venue's exposed event
identities, funding ledger, leverage, margin mode, fees, and finality fields are
unverified. It also preserves the rule that an unsupported product fails closed:
normally-flat or taker-only operation is not a substitute, because opening and
closing are distinct external actions and a failure can leave exposure between
them.

Before any perpetual connection, the trigger in
`V2_DEFERRED_FUNCTIONAL_SCOPE.md` §6 requires the reviewed ledger-first design:
typed external executions, typed funding settlements, and account snapshots used
for reconciliation rather than blind overwrite. A single overloaded
`VenueObservationKind` is rejected because it would conflate position and cash
mutations and require order/effect identities that unsolicited venue economics
do not own. Exact bytes remain unfrozen pending venue evidence.

### 10.16 Phase 6: the reducer behind consensus (2026-09-15)

ADR 0001 puts the deterministic reducer at the centre and everything untestable
outside it. Phase 6 is the seam that keeps that true under replication, and the
measure of success is that THE REDUCER DID NOT CHANGE. It gains nothing, loses
nothing, and does not know it is replicated - which is precisely why the same
deterministic suite passes on one voter and on three.

G-035 IS THE SHAPE OF THE CODE, NOT A RULE IT FOLLOWS. `ReplicatedKernel` has
exactly one call to `apply()`, inside the Raft apply callback. There is no other
path from a command to the reducer, so a command that is proposed and never
commits cannot have an economic effect - not because something checks, but
because nothing connects them.

A SEPARATE CLASS FROM V1's `RaftNode`, deliberately. V1 is accepted and gated,
and its storage, peer-wire and checksum layers are already payload-generic
(`WalRecord::payload` and `PeerMessage::payload` are byte vectors), so they are
reused unchanged. What is duplicated is the Raft callback table and nothing
economic. Generalising V1 instead would have put an accepted artifact at risk to
save about three hundred lines of boilerplate.

RESTORE IS BY REPLAY, NOT BY TRUSTING A STORED BLOB. A restarting voter appends
its WAL, sets the commit index, and re-applies the committed prefix through the
same reducer. The reducer is deterministic, so this reconstructs exactly the
state that prefix produced - and if it ever did not, this path surfaces the
divergence instead of hiding it behind a state file. Snapshot install for
COMPACTED prefixes arrives with Phase 7, which owns snapshot durability.

A RETRY IS NOT SHORT-CIRCUITED FROM THE RETAINED RECEIPT. Answering an
already-applied command from local state would be a read whose correctness
depends on still being leader, and a deposed leader would serve a stale answer
with no way to know. The retry is proposed like anything else, and the reducer
answers it with the original result when it commits. The cost is a duplicate log
entry, bounded by the caller's own retry policy; the alternative costs a
read-index discipline that Phase 6 does not need.

THE CONVERGENCE ORACLE IS THE CANONICAL SNAPSHOT DIGEST. Comparing state
versions, order counts or outbox watermarks across voters can agree while the
states differ. The digest covers all of it at once, so `state_digest_of` is used
by both the convergence check and any future snapshot install, and the two cannot
drift apart about what "the same state" means.

#### 10.16.1 A lifetime bug that consensus made reachable

`log_append_entry` in willemt/raft does
`memcpy(&entries[back], ety, sizeof(raft_entry_t))`. That copies the entry STRUCT
and therefore only the `data.buf` POINTER. The caller's buffer would have to
outlive the log entry, and a proposal's buffer cannot: commit happens after
`propose` returns, and a slow follower can be re-sent an entry arbitrarily later.

It surfaced immediately and deterministically here as
`committed entry did not decode`, because a V2 command is a canonical frame with
a length and a digest - corrupt bytes fail loudly rather than producing a
plausible wrong value. A less structured payload would have produced a wrong
answer instead of an error.

`ReplicatedKernel` now owns every payload the log can still reference, keyed by
log index, and `log_offer` repoints the STORED entry at the owned copy. The map
is bounded by the log: entries leave on poll, pop and clear.

Recorded because the ownership question is not obvious from the callback
signature, and because the same pattern appears in V1's `RaftNode`. A probe that
scribbled the buffer immediately after `raft_recv_entry` did NOT reproduce a
failure there, and V1's suites pass under AddressSanitizer, so this is a thing to
look at rather than a confirmed defect in V1 - stated at that strength
deliberately.

### 10.17 Phase 7a: durable snapshots, and two library truths worth writing down

The safety half of Phase 7, which also closes Phase 6's snapshot-install
leftover. The performance half - asynchronous WAL, block arena, SPSC rings - is
NOT done, and until it is, Phase 7's objective sentence ("remove synchronous
storage blocking from the owner loop") is not met and no performance claim may
be made.

THE SNAPSHOT INVENTS NO ECONOMICS. Its payload is the canonical state snapshot
the reducer already defines, so `SnapshotStore` adds framing and durability and
nothing else. That matters twice: the canonical encoder already refuses an
impossible state, and it already carries its own digest, so a snapshot that
decodes is one the reducer would have accepted. A new magic (`VSN2`) and a new
filename keep a V1 directory from ever being misread as a V2 snapshot.

DURABLE BEFORE RECLAIMED, enforced by the order of statements rather than by a
comment. `save` returns only after fsync, rename, and a fsync of the DIRECTORY -
the last is easy to omit and would let a crash reveal the old snapshot after
`save` returned, with the prefix already reclaimed on the strength of it. Only
then does `raft_end_snapshot` poll entries away, and only then is the WAL prefix
discarded. A crash anywhere in that sequence leaves a durable snapshot plus a log
that still holds the prefix: an overlap, which restart skips by index, and which
costs a little disk. The reverse order costs committed entries.

A CORRUPT SNAPSHOT DEGRADES TO REPLAY. `load` returns absent for anything it
cannot fully verify rather than throwing, because replay from the log is always
correct and refusing to start turns a recoverable disk fault into an outage. A
snapshot is an optimisation over replay and has to fail like one.

#### 10.17.1 willemt/raft promotes a lone voter without campaigning

> "Only one voting node means it's safe for us to become the leader"

`raft_periodic` makes a single voting node leader directly. That is correct for
safety and it leaves `current_term` at zero, which Raft's own model does not have
a leader for: term zero means no leader has ever existed.

Left alone it is not cosmetic. `raft_recv_entry` assigns `ety->term =
me->current_term`, so entries are written with term zero; `raft_begin_load_snapshot`
rejects a zero `last_included_term` outright. A single-voter cluster could
therefore take a snapshot, reclaim the prefix it covered, and then silently fail
to install that snapshot on restart - recovering from a log whose prefix was
already gone. Two library behaviours that are individually defensible compose
into data loss.

`ReplicatedKernel::tick` restores the invariant: a leader at term zero is raised
to term one. On a multi-voter cluster the branch never fires, because an election
has already raised the term.

#### 10.17.2 A snapshot's term is the entry's term, not the server's

The first attempt recorded `raft_get_current_term` as `last_included_term`. Those
are different quantities - a snapshot describes a LOG POSITION, and the position's
term is what a peer compares against its own log - and the difference is invisible
until a peer needs it. It also reads zero on a node that has not campaigned, which
is how the previous defect surfaced. The term now comes from
`raft_get_entry_from_idx(applied)`.

#### 10.17.3 The committed-outbox read carries its own authority

`CommittedOutboxView` pairs the bytes with the authority under which they were
read. `TradingState` alone cannot say whether it is current, so handing out a
bare reference invites a caller to act on a deposed leader's copy - which looks
identical to a correct one.

It proves only that THIS node believed itself leader when the bytes were taken. A
read-index round trip proves leadership at the read's linearization point and is
the stricter form; that is deferred, and the distinction is recorded here rather
than left for a reader to assume the stronger guarantee.

### 10.18 Phase 7b: bounded block staging, and the durability protocol it enables

`WriteAheadLog::append` opens, writes, fsyncs and closes PER RECORD, from inside
a Raft log callback and therefore from inside the owner loop. Every proposal pays
a full sync before anything else can happen. Removing that is Phase 7's objective.

THE CONTRACT THAT MUST NOT BREAK, stated before any mechanism: a voter may
contribute to quorum only for entries it has made DURABLE. Synchronous appends
satisfy it by accident, because the log index and the durable index are the same
number. Splitting them is the entire point, and every remaining hazard in this
section lives in the gap between the two.

BLOCKS, AND WHY A RECORD NEVER STRADDLES ONE. A torn write at a block boundary
can split a record, and half a record whose length prefix survived is
indistinguishable from a whole short one. `WalBlockArena` seals a block short
rather than straddle, so a torn write is always a bad BLOCK - detectable by
checksum - rather than a plausible record. A record larger than one block's
payload is refused rather than split.

GENERATIONS, AND THE STALE COMPLETION. A slot is reused once its bytes are
durable. A completion that arrives after the slot has been recycled would
otherwise release work that a different record now occupies, marking bytes
durable that were never written. Every reuse mints a new generation and
`complete` ignores a descriptor whose generation has moved on. Removing that
guard fails the suite, which was checked by mutation rather than assumed.

THE DURABLE INDEX ADVANCES IN ORDER ONLY. A later block completing before an
earlier one would let the index cover a gap it never crossed. Out-of-order
completion is refused.

BOUNDED REFUSAL, NEVER OVERWRITE. A stalled writer fills the arena and admission
is then refused. Overwriting would discard bytes that are not yet durable, and
the entry they belong to may already have been counted toward a quorum. The
refusal lifts as soon as one block drains, because backpressure that never lifted
would be a deadlock with a safety argument attached.

NO HIDDEN THREAD. The writer is driven by an explicit call, so tests drive it
deterministically and a production loop chooses its own thread. This is the
discipline the venue simulator uses for time, applied to I/O: nothing that
decides behaviour may be implicit.

#### 10.18.1 Why the leader must be durable BEFORE Raft sees an entry

willemt computes commit like this:

```c
int i, votes = 1;                       /* the leader counts itself */
for (...) if (point <= raft_node_get_match_idx(node)) votes++;
if (raft_get_num_voting_nodes(me_) / 2 < votes) raft_set_commit_idx(me_, point);
```

The leader's self-vote is unconditional and there is no setter for one's own
match index, so a leader that has merely STAGED an entry still counts itself
toward quorum. Asynchronous durability therefore breaks the contract above
unless something else restores it.

The plan anticipated this and called for "a pinned Raft fork/adapter". A fork is
avoidable, and the alternative is simpler to reason about:

> DECLARED PROTOCOL. A voter admits an entry to the Raft log only after that
> entry's bytes are durable. The leader satisfies this by staging to the WAL
> first and calling `raft_recv_entry` only once the durable index covers it. A
> follower satisfies it by withholding its AppendEntries success response until
> its own durable index covers the entries that response acknowledges.

Under that protocol willemt's unconditional self-vote is honest, because
everything in the leader's log is durable by construction. The owner loop still
never blocks: proposing stages and returns, and the sync happens on the writer's
schedule.

What it costs is that the leader's log index lags its accepted proposals, so
`ProposeStatus::admitted` comes to mean STAGED rather than LOGGED. That is a
weaker statement than it was, and the distinction is recorded here rather than
left for a caller to assume the stronger one.

#### 10.18.2 What the integration actually enforces, and what it does not

`log_offer` now STAGES and returns. The sync happens in `flush_durability`,
driven from `tick`, `propose` and `receive_peer` - so the owner loop no longer
pays an open/write/fsync/close per record, which is Phase 7's objective for the
write path.

#### 10.18.3 The pinned Raft fork, and a vacuous test that hid behind it

The property left open above is now enforced, by a pinned patch in
`deploy/patches/0001-raft-durable-quorum.patch`.

WHY IT COULD NOT BE DONE FROM OUTSIDE. Two places in willemt count the LOCAL
node's contribution without consulting durability, and neither is reachable
through the public API: `raft_recv_appendentries_response` starts its quorum
count at `votes = 1`, and there is no setter for one's own match index; and
`raft_recv_entry` commits immediately when there is a single voting node. A
peer's durability was never the problem - an append-entries response already
reports it.

The patch adds one OPTIONAL callback, `durable_idx`. When it is NULL the
behaviour is byte-for-byte as before, which is what keeps V1's node unaffected
and is why the patch is safe to apply to a shared target. It also adds the
single-voter re-check to `raft_periodic`, because at `raft_recv_entry` time an
asynchronously staged entry is never durable yet and nothing else would revisit
it.

Applied at configure time rather than vendored, so the upstream pin stays the
source of truth and the delta stays reviewable as a diff. A failure to apply is
FATAL: silently building unpatched Raft would silently restore the defect.

FLUSHING IS THE CALLER'S DECISION, and this changed because of a test failure of
a particular kind. `tick` no longer touches the disk; `flush_durability` is
separate and public.

The first version of this work had `tick` flush, and asserted the invariant
`commit <= durable` after every tick across every scenario. That test passed. It
also passed with the durability callback unwired entirely, and with recovered
durability left unseeded. It was VACUOUS: the harness closed the gap on every
tick, so the window the invariant describes never existed at a moment anything
could observe it.

Separating the two makes the gap observable, and the real test withholds the disk
- ticking fifty times without flushing and asserting that nothing commits, that
no result appears, and that the entry is genuinely in the log so something is
actually being withheld - then flushes and requires that the same entry commits,
so the gate is a gate and not a deadlock. Both mutations now fail it.

The general lesson is the one this project keeps relearning: an invariant that
holds because the harness maintains it is not evidence about the code. The fix
was not a better assertion but an API that stops deciding things implicitly -
the same reason the arena's writer and the simulator's clock are explicit.

ENFORCED: THE PERSISTED COMMIT INDEX NEVER EXCEEDS THE DURABLE INDEX. willemt can
advance its in-memory commit index past what this node has synced, because it
counts the leader's own vote unconditionally. Recording that would be the one
unrecoverable version of the mistake - a restart reading a commit index that
points past the end of its own log, which the constructor then refuses, turning a
survivable crash into a node that will not start. `persist_commit_index` clamps,
so the hard state stays a statement about bytes on disk.

ALSO ENFORCED NOW, via the patch: the in-memory commit index never exceeds
quorum-durable either. A voter counts itself only for entries whose bytes are on
disk, so a leader can no longer form a quorum around a copy a power cut would
erase. `seed_durable_index` closes the restart half - records read back from disk
ARE durable, and without saying so a restarted node would refuse to count itself
for its own recovered log.

THE SEGMENT IS RECLAIMED ON BOTH PATHS, and the suffix one is correctness rather
than housekeeping. `log_pop` truncates the segment as well as the legacy file,
because an entry Raft has popped must not come back on restart - a segment that
kept it would resurrect a log the cluster has already rejected. `compact`
discards the prefix a durable snapshot covers.

RECOVERY PREFERS THE SEGMENT and falls back to the legacy per-record file, so a
directory written by an older build still starts.

#### 10.18.4 Moving the sync off the owner's thread

`flush_durability` still performed the `pwrite` and the `fsync` inline. That is
one sync per drain rather than one per record, which is most of the win, but the
owner loop still stopped for the disk. `WalWriter` removes the rest.

WHO OWNS WHAT, because that is the whole safety argument. The OWNER owns the
arena: staging, sealing, and applying completions all happen on its thread, so
every mutation of arena state is single-threaded and needs no lock. The WRITER
only ever READS the bytes of a SEALED block. A sealed slot is not reused until
its completion is applied by the owner, so those bytes are stable for exactly as
long as the writer holds the descriptor - the generation guard is what makes that
true rather than merely likely.

Descriptors cross in an `SpscRing` in each direction. Single producer and single
consumer is a precondition rather than a hint: two producers racing on the head
would both believe they held the same slot and nothing would detect it. Full
means full - `push` refuses rather than overwriting, because an overwritten
descriptor is a block whose bytes never reach the disk while the arena believes
they have.

ONE SYNC PER PASS, PUBLISHED AFTER. The writer drains submissions, writes them
all, syncs ONCE, and only then pushes completions. Publishing before the sync
would make "completed" mean "written", and written is not durable.

THE THREAD IS OPT-IN. `start_writer` moves the I/O; without it `flush_durability`
polls inline. That keeps every existing test deterministic and makes the threaded
path a thing to be demonstrated rather than assumed - which is the same reason
the arena's writer and the simulator's clock are explicit.

Verified under ThreadSanitizer, both the ring on its own across a real
two-hundred-thousand-entry producer/consumer handoff, and the replication suite
with the writer thread running. The ordering claims in `SpscRing` are about two
threads, so asserting them on one would have been asserting nothing.

#### 10.18.5 What the storage layer reports

`WalMetrics` is deliberately separate from the trading metrics: none of it is
economic, and a full arena is a disk problem rather than a risk problem. It
carries blocks submitted and blocks durable - a difference that stops shrinking
is a stalled writer - backpressure refusals, and the highest staged index against
the durable one. That last pair is the exposure asynchronous durability buys,
expressed as a number rather than as a paragraph.

#### 10.18.6 Preallocation, and the assumption it broke

Extending a file is a metadata operation the filesystem may itself have to
journal, so doing it on the write path puts an unpredictable cost exactly where
this phase is trying to remove one. The segment now reserves blocks ahead of the
frontier, turning almost every write into an overwrite of space that already
exists. Reserved space is zeros, and `decode_block` rejects a zero magic, so it
reads as the end of the log exactly as an unextended file would.

IT BROKE THE RESUME LOGIC, and the way it broke is worth keeping. The segment
used to resume at `(file_size / block) * block`, which is correct only while file
size means "end of data". Reserving ahead ends that: a segment with a 2 MiB
reservation and one block written would resume 2 MiB in, leaving a hole of zeros
that recovery reads as the end of the log - silently losing everything appended
after the reopen.

The frontier is now found by WALKING blocks until one fails to verify, which is
the same walk recovery does. That also handles a torn tail for free: the first
block that does not verify is where writing resumes, so a block that never
completed is overwritten rather than left in the middle of the file.

The test that caught it appends after a reopen and requires both records back.
An earlier version of that test staged arbitrary bytes rather than encoded
records and so proved only that the BLOCK layer round-tripped - the record
decoder had nothing to find and reported zero either way. Staging real records
was the difference between a test that passes and a test that looks.

#### 10.18.7 A follower's ack is the other half of the same defect

The pinned patch gates a LEADER's own vote. A follower's acknowledgement carries
the same hazard from the other direction and the patch cannot touch it, because
the number is produced on this side of the wire: `raft_recv_appendentries` returns
a `current_idx`, the leader turns it straight into that follower's match index,
and counts it toward quorum. A follower that acks entries it has only staged is
a leader counting an undurable copy, one hop away.

`receive_peer` clamps the reported index to the follower's own durable index. It
costs a round trip - the leader sees a lower match, resends, and the follower
reports the higher index once its writer catches up - and the alternative is a
quorum formed around bytes a power cut would erase.

Pinned by a three-voter test in which ONLY THE LEADER FLUSHES: followers receive,
append and reply, and the leader must not commit. Removing the clamp fails it.

#### 10.18.8 The power-cut sweep, and the two defects it found on its first run

The earlier "power-cut matrix" was three durability points. It is now a sweep:
the segment is truncated at every structurally distinct offset - nothing, inside
the magic, inside the header, one byte short of a block, exactly a block, a byte
past one, and every block boundary through the written frontier - and after each
cut the node must start, must not invent state, and must be able to continue.

It found two things immediately, which is the argument for sweeps over points.

A RECOVERED COMMIT INDEX PAST THE END OF THE LOG. A cut that loses the tail
leaves exactly that shape: hard state that was durable naming entries that were
not.

This was first "fixed" by clamping and reporting, on the grounds that refusing
turns a survivable crash into a permanent outage. That was wrong, and it is worth
keeping why. The sweep truncates the segment EXTERNALLY, which cannot happen
while the durability contract holds - with the gate enforced, commit never
exceeds durable, so this shape means the storage lost data it had acknowledged.
The sweep was simulating a broken disk, and clamping taught the node to tolerate
a broken disk silently, on the single-voter configuration that has no peer to
re-supply what was lost.

RECOVERY NOW REFUSES, names the condition, and leaves the files untouched for
diagnosis. A node in this state does not start without a human, which is the
correct cost: the alternative is a cluster continuing with a hole in its
committed history and nobody aware of it. ADR 0006 carries the amendment and the
condition under which it should be revisited - peer snapshot transport, which
would let a FOLLOWER rejoin by re-fetching rather than requiring an operator.

ONE 64 KiB BLOCK PER RECORD. `flush_durability` seals whatever is open on every
call - correctly, since leaving a record staged to save a write is the latency
the phase exists to remove - so a quiet system writes one block per record. At
64 KiB a two-hundred-byte command cost 64 KiB of disk. The block is now 4 KiB,
page-sized, with a static assertion tying it to the largest canonical command
frame so growing the command budget fails the BUILD rather than silently
refusing records at runtime, which would look like backpressure and would not be.

#### 10.18.9 What the measurement actually says

Phase 7's gate puts correctness and power-cut evidence before throughput. Both
now pass, so `v2-storage-benchmark` measures rather than claims. It reports the
OWNER LOOP's time, separated from the disk's, because that is what the phase set
out to change - and it reports the synchronous and off-thread figures together,
since the difference between them is the only part that is about the design
rather than about the machine it ran on.

On the development host, two thousand command-sized records:

| | synchronous | off-thread |
|---|---:|---:|
| median | 21.6 us | 0.042 us |
| p99 | 109 us | 208 us |
| max | 7136 us | 358 us |

THE MEDIAN IS THE OBJECTIVE AND THE P99 IS THE HONEST PART. Off-thread is about
five hundred times faster at the median and twenty times better at the worst
case, because the owner no longer waits for an fsync. At p99 it is WORSE, and
that is not a defect: under sustained load faster than the disk the owner waits
for a block instead, which is bounded admission refusal doing exactly what it
promises. The design converts an unbounded per-record stall into a bounded
queue-full one; it does not make the disk faster.

The first run of that benchmark reported four fifths of records refused, because
shrinking the block from 64 KiB to 4 KiB shrank the total staging budget
sixteen-fold along with it. The arena is now sixty-four blocks - a quarter of a
megabyte, which is the budget that was actually intended. A benchmark that found
a sizing error before it found a number was worth writing.

These figures are a development laptop's. The deployment targets are a Pi and a
NUC, and no claim is made about either until the gates run there.

STATUS: Phase 7 is complete in implementation scope and in its test-first list -
block and generation transitions, no record straddling a block, torn-data
detection, release/acquire publication proven under ThreadSanitizer, stale
completions refused, durable-index ordering, a follower ack that never covers
staged entries, commit never exceeding quorum-durable, bounded refusal under
writer stall, hard-state torn-write refusal, snapshot durability before
reclamation, and a power-cut sweep. Guards are tested by mutation as well as by
assertion.

#### 10.18.10 F2 admission and synchronous batch corrections

Canonical decoding precedes admission on both proposal lanes. Malformed normal
input returns existing `refused`; malformed control input retains its lane-specific
refusal. No reducer work is performed speculatively. Committed/recovered bytes
still decode strictly; this is neither corrupt-history repair nor Byzantine peer
validation. The extra bounded decode is the cost of not journaling poison.

The synchronous segment helper owns a snapshot of each sealed descriptor exactly
once, without advancing the async submission cursor. It refuses already-submitted
writer work, writes all blocks, syncs once and only then completes them in order.
Write/sync failure preserves the sealed blocks and restores the append frontier.
This retains retry semantics and prevents reusing memory before durability.
It does not alter the async protocol or block bytes. Tests and remaining limits
are recorded in `evidence/V2_REMEDIATION_CHECKPOINTS.md`, CP-002.

#### 10.18.11 F3 unbound request-body APIs are quarantined

Neither a raw `OrderEffect` plus gateway-supplied metadata nor a local `verified`
flag proves economic binding. The market helper now always refuses; the reducer
has no authorized market/swap operation. The unbound limit helper also refuses
submits until a reviewed binding interface exists. Refusals leave output intact.
The exact decimal utilities remain usable arithmetic, not permission to send.
Matching tick-size times lot-size does not prove either factor, asset routing or
venue capability. The owner subsequently approved the single immutable committed
catalog direction and subsequently the installation/recovery contract in
`docs/F3_EXECUTION_METADATA_REVIEW.md` (ADR 0012), then the canonical catalog-v1
contract in `docs/F3_CATALOG_V1_BYTE_REVIEW.md`. The separate-process binding
envelope remains unfrozen; no TradingState/snapshot budget increase is approved.
No command/effect/snapshot bytes have changed in the refusal/preflight increments.
In particular,
artifact absence must be a node dependency failure, never a replica-dependent
economic answer; compatibility must not reinterpret historical authorizations.

#### 10.18.12 F3 recovery compatibility precedes writable startup

Before creating its writable segment/writer, ReplicatedKernel loads the snapshot
and decodes complete retained commands in both WAL stores. This includes the
uncommitted suffix and snapshot-covered overlap. The former can commit later;
the latter must not hide a mixed-schema directory. This is structural validation,
not speculative reducer application. Unsupported snapshot frame/state versions
throw rather than disappear into full-log replay. State-schema recognition precedes
the current schema's size ceiling, since a future schema may legitimately be larger.
Supported-schema corruption may still recover from a complete valid WAL; absent
committed history continues to refuse. An incompatible directory retains its
diagnostic files and bytes. Startup assumes exclusive directory ownership.

The approved version-8 command/result/snapshot boundary must land together with
catalog-root semantics. Constants remain 7 until then: stamping the partial
implementation as version 8 would produce a second kind of incompatible history
inside that version. ADR 0012 records the fresh-lineage rollout; CP-005 records
the preflight RED/GREEN evidence. This does not establish artifact availability,
bounded replication history, replacement-voter transfer or live gateway authority.

#### 10.18.13 F3 catalog-v1 canonical ownership boundary

The approved standalone codec admits 1..16 rows sorted by exact five-field key
(account, venue, instrument, instrument-spec version, capability version). It
rejects duplicate/unsorted keys rather than normalizing them, because a committed
root must identify one canonical representation. The independent byte contract
and digest domain are in `F3_CATALOG_V1_BYTE_REVIEW.md`. Hashing is over explicit
little-endian fields and length-prefixed text, never C++ padding.

Validation checks account quote/network consistency, repeated-version facts,
exact positive precision factors and existing profile/capability constraints.
The factor product uses signed 128-bit arithmetic: two positive signed-64 factors
fit; both zero remainder and the expected quotient are required. Truncation must
not hide fractional economics. A valid hash cannot excuse invalid structure.

Encoding stages in bounded scratch before publishing bytes, so refusal cannot
partially overwrite output and output may alias the input rows. Decoding checks
into a candidate and assigns only after successful validation/root comparison.
The verified context OWNS its fixed-size rows; destroying the artifact buffer
does not invalidate lookups. A returned row pointer borrows from that context:
the future process owner must publish it as const and keep it alive for the
lineage. Replacing/destroying the context invalidates those views; fixed storage
alone does not preserve semantic identity.

Host layout at cache-line settings 64 and 128: context 8,488 bytes, alignment 8,
below its separate 16 KiB ceiling. Maximum artifact 8,076 bytes fits 8 KiB.
TradingState and snapshot are unchanged. Validation has at most 120 cross-row
pairs (O(n^2), n<=16); lookup scans at most 16 rows. Codec/context use no heap,
files or network. These fixed bounds do not measure deployment latency and do
not include temporary codec stack scratch in the resident-context number.

CP-007 proves the standalone codec, not committed metadata binding. Kernel and
snapshot schemas remain 7 until the approved coherent schema-8 integration.
Catalog integrity is not venue truth or authority to send. Unbound request-body
APIs remain quarantined; real encoder/venue admission and F4 authority remain open.

#### 10.18.14 F3 immutable installation and dependency-before-apply boundary

CP-008 implements the approved schema-8 boundary (command, result and snapshot
together). Install kind 20 carries root32 + expected kill epoch u64 + expected
executor generation u64. It is operator-only, first installs while halted and
before economic configuration, and never replaces an installed root. Same-root
retry obeys ordinary identity/control rules. Codes 62..66 are appended; previous
kind numbers and OrderEffect bytes are unchanged. Old schema-7 storage is retained
as evidence, not migrated or replayed as this lineage.

The replica owns a const 8,488-byte verified catalog; a borrowed row pointer is
valid only during that owner's lifetime. Reducer calls receive explicit context.
Installed-root commands require the matching artifact before any reducer work;
first install requires it only when deterministic admission permits installation.
Local absence throws a node dependency error, never a replicated refusal code.
The optional pinned Raft before_apply hook checks before the library increments
its applied index. Wrapper-only or applylog-only checks miss indirect apply paths
or run too late. Failure latches authority/proposal/peer/apply service closed;
diagnostic reads and draining the writer are not permission to execute.

Staging compares complete profile/capability values with the exact five-field
row; authorization rechecks that binding after sweep-before-borrow. Historical
materialization uses the retained proposal, not latest policy, and copies the
root and versions beside OrderEffect. It checks notional, bounds and supported
proposal semantics. Gateway admission requires that binding, the historical
runtime capability version and the effect's operation bit. It cannot use the
legacy bare materializer to bypass metadata checks. Cancel price/quantity remain
canonical zero; the original proposal supplies historical instrument identity.

Snapshot prepublication validates both canonical structure and catalog semantics.
Checks cover staged/active bundles, proposed decisions and retained effects;
root equality alone does not prove metadata membership. The bounded startup walk
is O(decisions*rows + effects*(receipts+decisions+rows) + policies), with fixed
arena bounds. It is not added to every apply. Ordinary row lookup scans <=16 rows;
no filesystem, network or heap-dependent decision is introduced into apply.

Measured host TradingState 81,392 bytes left 528 under 80 KiB and maximum
encoded snapshot 65,483 left 53 under 64 KiB at CP-050. After P2.1 (B1 position
limits, 2026-09-29): TradingState 81,456 bytes leaves 464 under 80 KiB (layout
tripwire lowered 512 -> 448), and the maximum encoded snapshot 65,519 leaves 17
under 64 KiB, measured identically at cache lines 64 and 128. The snapshot budget
now has room for almost nothing: any further per-record growth must be paid for. Context is separate, and temporary codec
scratch is not included in its resident size. No budget expansion or latency
claim is made. Real encoder/venue admission, authority protocol F4 and target
gates remain open. Evidence: `evidence/f3_binding_2026-09-22/README.md`.

#### 10.18.15 Snapshot-covered history debt is not live log capacity (CP-046)

Owner-approved amendment: `F5_INSTALL_HISTORY_BRIDGE_REVIEW.md` and ADR0014.
The root archive catalog and active generation selector are independent durable
heads. Keep missing audit history in the candidate's existing identity-bound WAL
so the selected generation is self-contained before publishing archive coverage.
No new durable encoding or weakened missing-history refusal is authorized.

The implemented cold-recovery side distinguishes historical `(A,S]` from live
suffix `(S,D]`: archived cut A, snapshot S, durable end D. The entire retained WAL
must fit the configured positive cap (maximum1GiB); only the suffix uses the
existing8192-entry live capacity. Archive publication preflights exact missing
history objects/bytes plus conservative suffix reservation. Already archived
overlap is validated but never charged twice. Failure preserves WAL and refuses
service, not historical evidence. Independent-copy rules still govern deletion.

During recovery the node does not advance consensus, admit requests, or release
output/authority. One serial worker owns publication; the owner acquires and
checks its generation/sequence/cut and unchanged durable coordinates before
clearing quarantine. Cold validation remains synchronous, and no online activation
or whole-process memory/latency bound follows. Reverification per archive segment
can reread growing prefixes: bounded resident batches do not imply linear I/O.
For two voters this quarantine removes quorum until recovery finishes.

Actual peer catch-up, candidate selection, same live result-ledger preservation
and draining old-generation writes across activation remain unimplemented here.
Host evidence and explicit limitations:
`evidence/f5_history_recovery_2026-09-24/README.md`. F5/F6 and E1/E2/E3 remain open.

#### 10.18.16 Fresh independent evidence before live reclamation (CP-048)

Owner approved fresh authenticated destination verification instead of persistent
backup receipts for MVP. Native two-host compaction can prepare local archive and
snapshot, but retains WAL/log ownership until exact destination evidence is
consumed with current local coverage. Cold recovery relying on reclaimed history
must reacquire that evidence before serving. No pin or missing/corrupt backup
cannot silently downgrade the deployment to local-only evidence.

The destination verifies the source-published immutable catalog/object/dependency
set, syncs files and namespace, and binds evidence to pinned destination identity,
publisher/manifest, exact catalog generation/digest/range, publication/current
storage generations, attempt and fresh challenge. Values are move-only and
one-use; no caller boolean mints authority. File integrity and a destination
identity digest do not authenticate a remote host: that is the trusted transport
owner's obligation, not established by a local-directory test.

Nonwrapping per-process counters still repeat across restart. A fresh32-byte
cold getentropy nonce, failing closed on error, derives domain-separated
per-attempt256-bit challenges. Replay of an old response is rejected even if
catalog and counters match. This local handshake state never enters the reducer,
commands or persisted catalog. No wire bytes are frozen here.

Trade-off: backup outage/reverification cost can delay compaction and relevant
startup. Owner APIs copy bounded native values and validate acquired evidence;
destination sync/read/network work stays off-owner. Current verifier repeats a
complete archive check twice; no latency or whole-process memory claim. Preserve
the conservative checks for MVP. Evidence/limits:
`evidence/f5_destination_verification_2026-09-24/README.md`. E1/E3 activation,
authenticated process integration and target acceptance remain open.

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
The delivery planner must carry both remaining counts to the pre-send guard,
not stop scanning when normal quota alone reaches zero. It spends normal quota
first and emergency quota only for a permitted cancel after normal quota is
exhausted, preserving authorization order. This is local bounded scheduling,
not a new replicated field or permission to bypass evidence backpressure.
New submit authorization also requires nonzero kill epoch and executor generation:
zero denotes bootstrap, whereas canonical effect bytes require initialized
authority. Refusing before creating a reservation/order/effect prevents stranded
risk. Ordinary command receipt/version bookkeeping still occurs on refusal.
Failure to contact the Decision/OMS authority causes the executor to stop new
risk before its authority lease expires.

### 11.1 F4 — approved protocol implemented at the host-library boundary

CP-009 reproduces stale-leader renewal and arrival-time freshness on the current
catalog-bound tree. See `F4_AUTHORITY_PROTOCOL_REVIEW.md` and ADR 0013.
The owner approved a one-shot, current-term committed replication-only barrier
per new-risk attempt, with expiry anchored at gateway request start and final
transport-owner validation. It reuses commit machinery but adds WAL/latency cost.
The durable type/body is separately approved below; no IPC layout or production
freshness bound is approved here.
Local leader status is diagnostic, not renewal evidence. Quorum proof is not
external fencing or revocation of in-flight requests. Existing cancel exceptions,
timing limitations and unchanged no-automatic-promotion policy are part of that
approval; G-052..G-060 remain open for deployment. CP-011 implements the marker
prerequisite and CP-012 composes owned proof issuance/consumption with final
transport admission. `F4_AUTHORITY_BARRIER_BYTE_REVIEW.md` records approved type
101 and a 186-byte frame with exact intent/control/catalog/domain expectations and
retained-marker drain admission: lagging followers may retain multiple markers,
but local issuance waits for the highest unapplied marker to drain or be removed.
Old-term markers may require an ordinary current-term command to unblock because
the library has no automatic leader no-op (the one-voter lab path differs).
The owner approved these bytes/domain/drain details. CP-011 tests both normal
and control commands unblocking a multi-voter old-term drain without producing
an economic result for the marker. The current implementation derives the drain
by scanning retained typed entries at owner boundaries, outside pre-removal Raft
callbacks. This does not bound total log history: F5 retains identity/retention
responsibility. Marker admission alone is not fresh authority for a live send.

CP-012's producer result is fixed-size, owned and constructible only by the
replication owner; replay never creates it. Dequeue revalidates term, controls,
deliverability and the entire historical BoundOrderEffect. GatewayAuthority is
noncopyable, defaults closed with a zero freshness bound, and matches exact
incarnation/sequence/request/binding. Its original request tick is never replaced
by arrival time; subtraction-based age checks avoid deadline-addition overflow.
Observing changed controls/term or regressed time invalidates outstanding work.

Final `dispatch_gateway_send` checks current delivery/retry/evidence/quota
eligibility, complete prepared binding and known fences. A second clock sample
checks time spent preparing/validating, then one-shot consumption and uncertainty
recording precede the transport callback. Owner inputs are stable and callbacks
non-reentrant; arbitrary-pause wall-clock or venue-acceptance bounds are NOT
proven. A copied proof cannot recreate a consumed private permission. A transport
throw remains possibly sent, never automatic proof-unsent.

The old `renew_authority` compatibility function always returns false and cannot
alter control freshness. `evaluate_delivery_candidate` replaces the misleading
`evaluate_pre_send` name: its boolean result is not live authorization. Committed
cancels need no fresh proof but retain exact binding, known controls, stop-all,
retry/evidence limits and actual quota consumption. Stop-new-risk does not kill
the emergency lane. No automatic activation is added.

Host F4 evidence: `evidence/f4_proof_2026-09-22/README.md`. The explicit test of an
unseen remote kill after a prior proof allows one still-fresh attempt; observing
the kill later stops new risk but cannot undo bytes already handed off. Persisted
domain provenance (F5), authenticated IPC/operator re-enable, production clock,
real adapter/fencing, timing measurement and target gates remain open.

### 11.2 Phase 2 — node<->gateway protocol and the gateway process

Owner decisions (2026-09-29): the gateway is a separate process (one per venue,
on nuc1); new node-protocol bytes are approved outright; halt on unknown. The
gateway never changes a position: it submits what the venue reports as
`ObserveFill` / `ObserveOrderEvent` commands under its own non-operator caller.

Node side. `gateway.caller = <CallerId>` names the one caller whose gateway
requests a voter serves; it must be a configured caller outside the operator
band. Two requests ride the existing authenticated ingress envelope, with a
first body byte outside the lane range (so `decode_ingress` refuses them):
state read (0x10, 26 bytes) and authority (0x11, 212 bytes: the header plus the
canonical 186-byte barrier frame). Both are served only by the current leader;
a follower answers `not_leader` with its leader hint, a busy kernel
`persistence_busy`. A state read returns the leader's APPLIED state as
`encode_state_snapshot` bytes in at most three 24 KiB chunks, each carrying the
responder, leader, term, applied index, authority domain, total length and the
SHA-256 of the whole snapshot; the gateway reassembles only chunks with an
identical header from one responder, checks the digest, then decodes. An
authority request is proposed with `propose_authority_barrier`; the node holds
one pending slot, answers the proof from `take_authority_proof()` only if its
request equals the pending barrier, and abandons the barrier (answering
`authority_lost` or `not_leader`) on leadership loss or after 5 s. Exact
layouts are in `node_protocol.hpp`.

Every gateway response echoes a correlation: the request id and SHA-256 over
`"velocita.v2.gateway-request.1"` and the exact request body. The body carries a
fresh 128-bit per-process incarnation (entropy from `getentropy`), so no
response can correlate with another process's request.

WHAT PROVES A PROOF IS GENUINE. `GatewayLink` constructs an `AuthorityProof`
only from an `authority_granted` frame whose envelope MAC verified under the
response key derived from this gateway caller's secret for route (response,
issuing voter -> gateway key, cluster incarnation), that passed that voter's
session fence, whose correlation equals the link's single pending authority
request, and whose decoded barrier equals that request's barrier (same process
incarnation, sequence, effect, digest, controls, catalog root and authority
domain) with issuer id equal to the authenticated sender; the voter emits such
a frame only from `take_authority_proof()` for that barrier. The MAC key is
symmetric: this defends against the network and every other caller, not
against a compromised gateway host, which already holds send capability.
`GatewayAuthority::accept` then applies its own freshness, highwater and
binding checks, and `dispatch_gateway_send` consumes the permission once.

Gateway side. `GatewayLoop` (one non-blocking step, injected clock, bounded
arenas) reads state, plans with `plan_delivery`, requests one proof per
risk-increasing send, dispatches through `dispatch_gateway_send`, and turns
venue reports into evidence obligations whose command digest is the
observation fingerprint. Committed cancels use the library's committed-cancel
exception. Startup is the only activation: if the first read finds unsettled
effects (possibly sent by a previous process), or `--allow-new-risk` is absent,
or the freshness bound is zero (the ADR 0013 default), new risk stays stopped.
A term, kill-epoch, generation or mode change, an unknown outcome, a refused
evidence command, an inconsistent or exhausted reconciliation, or an order in
`conflict` stops new risk, and nothing in the process re-enables it: an
operator restarts the gateway. Venue reports are acknowledged to the venue only
after their command committed.

Not closed by this section: G-052 external single-sender fencing (two gateway
processes for one venue are not prevented), a measured live freshness bound,
and automatic re-activation after a leader change (deliberately absent).

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

CP-049 implementation note (approved CP-036/045 direction, not a new byte
contract): the serial writer now supports a process-local target handoff. The
owner seals/submits ALL old staging before release-publishing one request and pauses new
blocks/maintenance. The worker must acquire that request and THEN recheck the
submission queue: an empty pop before acquiring it is not a drain proof. Every
older write must be synced and its completion dequeued before targets change.
The owner must additionally apply those dequeued completions before retiring the
old arena. Both borrowed target objects remain alive through acquired switch
completion; the new target remains alive for subsequent writes. No pointer in
this protocol owns storage by itself.

The response binds previous/next storage generation, operation sequence and
success. Generation and successful-operation sequence advance without wrap;
wrong expected generation returns failure without rebinding. Admission and
completion polling perform bounded native operations, not filesystem calls or
thread joins. The worker can wait for disk or owner completion drain indefinitely;
this is responsiveness, not a completion-time/availability guarantee. Shutdown
may join, but online switching may not use shutdown as its fence. Existing
maintenance must finish and have its completion acquired before a switch can be
admitted. This is only a writer-lifetime boundary: selector publication, final
Raft/ledger revalidation and prepared-generation authority still belong to the
unimplemented online installer. No E1/E3 acceptance follows from this component.

CP-013 remediation note (2026-09-22): SnapshotStore input is bounded by the
existing canonical maximum plus framing/lookahead before allocation proportional
to input. This closes one cold-recovery allocation surface, not whole-node
boundedness. Flush and metadata failures now latch the V2 owner closed; cache
publication follows successful persistence, and tick checks the latch even if
the pinned Raft self-vote path swallows a callback error. No new bytes/schema.
F5 identity/queues/archive/snapshot-install and F6 async hard-state/durability
model remain open. The owner approved `F5_F6_RECOVERY_DURABILITY_REVIEW.md` /
ADR0014 direction after CP-013, not unspecified new bytes or capacities. The exact
`F5_STATIC_IDENTITY_BYTE_REVIEW.md` contract was approved after CP-014; it addresses
startup drift binding, not yet identity-bound history/peer traffic. These increments do not replace the
guarantees below.

CP-014 adds a restricted persistence-ordering model of actual metadata save/load;
removing either file sync or directory sync fails independent recovered-value
checks. It is not physical power-loss evidence, exhaustive writeback/tear coverage
or asynchronous hard-state implementation. See `evidence/f6_persistence_model_2026-09-22/README.md`.

CP-015 implements that identity codec and a move-only directory owner. Matching
recovery validates then re-establishes publication durability before issuing the
token. CP-016 removes the raw ReplicatedKernel constructor: a consumed token is
the first member, checked before stores and destroyed after every store/writer.
Static ID/voters/catalog/domain derive only from it. The catalog reference points
into the owned token, avoiding both caller-lifetime dependence and a second copy.
A04 startup drift is host GREEN; exact refusal preserves history and a healthy
same-identity restart restores it. Advisory locks and pre-open inode checks do not
prevent later hostile pathname replacement; trusted stable paths remain required.
See `evidence/f5_kernel_startup_2026-09-22/README.md`. Token ownership is not quorum
authority; foreign-history binding, full F5 and device proof remain open.

CP-018: background WAL errors are captured and release/acquire-published to owner
service guards. Worker stop always joins; failure is permanent and neither restart
nor direct polling can resume publication. This closes an exception/ownership
boundary, not asynchronous Raft hard-state fencing. Observation is at owner API
boundaries, not instantaneous cancellation of previously emitted traffic. Focused
thread tests/mutants and full host regression are recorded in
`evidence/f5_f6_parallel_2026-09-22/README.md`. Exact history/peer framing and new
genesis publication order remain PROPOSED in `F5_BOUND_FRAME_BYTE_REVIEW.md`.

CP-019 supersedes that proposal status: the owner approved the complete byte
sheet. Common framing and the ordinary-peer boundary are implemented; local
storage and genesis integration are not yet implemented. The only public peer
receive API accepts bound bytes, never a raw PeerMessage. Framing checks lengths
before hashing, then integrity/manifest/owner before publishing an input-borrowed
view. Typed peer preflight walks the ENTIRE bounded entry layout before the
shared V1 decoder may allocate entry vectors. All four message types, including
replies, then pass route/member/candidate and canonical payload/domain checks
before any Raft callback. This prevents foreign traffic changing term/vote/log;
it does not authenticate a malicious sender who knows the public manifest.

The sender copies at most sixteen entries into one batch; the whole encoded frame
is at most 32 KiB. In the pinned Raft implementation an acknowledgement advances
next_idx only through the received prefix and triggers another send for a retained
suffix. A three-node forty-entry backlog test pins complete convergence through
this boundary. These are per-message limits, NOT queue/log/order count limits.
The kernel owns one cold-allocated 65,613-byte framing scratch (largest approved
local frame plus digest domain); TradingState is unchanged. Successful peer decode
still allocates bounded entry vectors. Whole-node queues, log retention and owner
work budgets remain open; this is not an allocation-free gateway claim.

Generic bound-frame decoding checks framing and scope only. Metadata tuples,
record coordinates, snapshot state/catalog and crash-publication transitions need
their typed storage consumers. No generic codec result may be treated as verified
recoverable history. See `evidence/f5_bound_peer_2026-09-22/README.md` for the
independent golden, preflight allocation probe, admission/batch mutants and limits.

CP-020 integrates the approved local consumers: bound metadata is mandatory,
WAL block version2 contains only bound canonical records, and local snapshots
carry the same verified manifest plus local owner. Legacy flat-WAL presence is
incompatible even when empty; V1 codecs remain unchanged. Recovery validates ALL
complete selected records before writable open, not merely the suffix it applies.
Metadata term covers every log/snapshot term; retained indices are contiguous,
terms nondecreasing and the snapshot overlap consistent. Snapshot index can exceed
the older metadata commit checkpoint because a durable snapshot itself establishes
that committed prefix. This does not permit term/vote rollback or prove that an
entire same-node directory has not been rolled back externally.

Genesis publishes zero metadata first and immutable identity last. Complete exact
genesis temps follow the reviewed retry table; partial/foreign/ambiguous artifacts
refuse unchanged. Recovery never resumes genesis or promotes a metadata temp.
Complete matching replacement temps may be overwritten by a later newly requested
save only after validation. Full nonzero corrupt WAL blocks refuse, preserving
evidence; physical short tails yield only the complete prefix and cannot excuse
missing committed history. Supported snapshot corruption may replay only when
the remaining log proves coverage. Unsupported/foreign snapshots never fallback.

Independent review caught an oversized-future-snapshot regression and a complete-
corrupt-block overwrite hazard; both have permanent tests. Six isolated mutations
verify intended refusal guards. Host51/51 Release/sanitized is not target, latency
or power-loss proof. At CP-020 the CP-014 syscall model still exercised generic
MetadataStore, not the new bound store. CP-027 supplies a separate actual bound
save/load model with independent tuple expectations and intended sync-only mutation
failures. This closes that model-port gap, not asynchronous hard-state or physical
power-loss acceptance. Evidence: `evidence/f5_local_history_2026-09-22/README.md`
and `evidence/f6_bound_metadata_model_2026-09-23/README.md`.

CP-021 additionally enforces the receiver's verified-prefix commitment boundary:
V2 passes min(leader_commit, previous_log_index+entry_count) to pinned Raft,
whose own cap incorrectly relies on its full local log for this boundary. Empty
heartbeats cover previous_log_index only; failed prefix verification still cannot
advance commit, and a lower leader commit remains lower. Prefix arithmetic is
validated before mutation; canonical/V1/fork bytes are unchanged. Synthetic
ingress tests expose unchecked-tail apply without the guard; independent review
did NOT establish an honest current-sender execution reaching that divergent-tail
shape, because backtracking truncates. Retain this qualification rather than
turning boundary hardening into an unsupported production incident claim.
Evidence: `evidence/f5_verified_prefix_2026-09-23/README.md`.

Bounded result ownership and1024/64 capacity are approved in
`F5_ADMISSION_RESULT_REVIEW.md` §7. After separate §8 approval, CP-023 replaces the
kernel deque with per-entry reservations and retained-ready delivery. Full followers
process term/prev without new entries and cap commitment at min(advertised,prev);
this can mutate state and is NOT malformed-input/no-mutation refusal. Read-only
whole-batch capacity/generation simulation relies on synchronous single-owner
callbacks; successful durable deletion releases actual suffix slot ownership before
replacement offers reserve. Recovery reserves retained uncommitted entries before
writable storage opens. Evidence: `evidence/f5_result_integration_2026-09-23/README.md`.
Every result-producing command needs a reservation; authority markers do not.
Ready notifications survive elections/compaction until taken. Replacing a full
uncommitted suffix requires a prepared ownership transfer AFTER successful durable
truncation, not premature release or an impossible demand for spare slots. This
was implemented for result ownership in CP-023, without closing node-wide bounds.

CP-025 implements owner-approved outbound/log contracts in
`F5_OUTBOUND_LOG_CAPACITY_REVIEW.md`. Four same-kind coalescing owned frame slots
per peer replace retry accumulation; persistent fair draining and lower-term
watermarks preserve bounded transport ownership without synthesizing replies.
Kernel egress copies into a caller-owned bounded span; dynamic decoding is a
test adapter. Borrowed append payloads are encoded synchronously into owned bytes,
including bounded staging for input/output alias safety. Max16-entry prefix sends
continue to advance a longer Raft suffix; coalescing is packet loss/retry, never
trading-effect cancellation or log deletion.

Retained log owns8192 stable2048-byte cells,8128 ordinary/64 protected actual-pool
tokens. Existing operator classifier only; authority markers ordinary. Whole
follower admission plans log/result ownership before mutating Raft; reusable
suffix credit becomes actual only after successful durable removal. Insufficient
space uses the approved term/prev-only response path. Committed retained history
also counts at startup. Full-history vector materialization is removed from
production recovery/rewrites; bounded scanning preserves bytes and fd/durability
ordering. Ready results remain separately owned until consumed.

Actual mailbox66680 bytes/node includes BOTH remote-peer sets even on two voters;
log payload16777216 + descriptors327680 + owner64 bytes; kernel inline91424 is not
total footprint. Existing library ring has a separately budgeted resize peak.
No allocation-free admission, emergency deadline, continuous fill ingestion,
target performance or process-RSS guarantee is implied. Archive/install/replacement
and safe prefix reclamation remain open; NEVER delete history to escape fullness.
Evidence/limitations: `evidence/f5_capacity_integration_2026-09-23/README.md`.

CP-026 completion review: `F5_COMPLETION_AND_NUC_PLAN.md` (recommendations, not
new accepted bytes). Current-source peer catch-up and archive-loss REDs remain.
Install must preserve READY result values and settle covered RESERVED obligations;
snapshot receipts alone do not prove historical per-entry outcomes. Staging files
is not selecting a generation: after durable active selection, old-owner rollback
is forbidden. The proposed offline replacement mode does not authorize genesis
bootstrap after lost hard state. No acceptance scope is reduced.

After CP-026 the owner approved offline fenced repair and the immutable/no-auto-
prune, fail-closed archive policy. The exact standalone portable codec in
`F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md` was approved after CP-027 and implemented at
CP-028, not snapshot/install/coverage bytes.
Archive parsing must not retrospectively filter canonical commands that refused
or authority requests that yielded no proof. Gateway-process incarnation is not
cluster incarnation; requested catalog/effect/epochs remain historical evidence.

CP-028 uses a 96-byte segment header and 32-byte record headers with two levels
of domain-separated SHA-256, capped at 4 MiB and 8,192 records per object. The
encoder pulls one immutable record at a time; it does not require a descriptor
array proportional to record count. The decoder validates the entire immutable
span before invoking any visitor. Both run in linear bytes/records with fixed
codec scratch, but the caller currently supplies the entire contiguous buffer;
file-backed streaming and durable publication are separate unfinished work.
Preflight refusal leaves encoder output unchanged. Second-pass source failure can
leave partial unpublishable bytes; visitor refusal can leave a consumed prefix,
never success metadata. Input stability and nonaliasing are explicit caller
preconditions. This is neither a transaction over applied commands nor a
commit/durability certificate. Component GREEN cannot release a retained-log
cell; the archive-loss and peer-catch-up integration REDs remain open.
Proof and API details: `evidence/f5_archive_codec_2026-09-23/README.md`.

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
