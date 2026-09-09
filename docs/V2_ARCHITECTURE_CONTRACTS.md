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
