# Velocita Raft changelog

This changelog records release intent, validated behavior, measured failures,
and the evidence required to promote the next release. It is deliberately more
than a commit log: implementation claims and experimental conclusions are kept
separate, and a throughput number is not published until a repeatable benchmark
meets its acceptance gates.

The project currently uses milestone names (`V1`, `V1.1`, `V2`) and the corresponding
container tag `localhost/velocita-raft:v1`. `CMakeLists.txt` still declares the
project version as `0.1.0`; artifact and semantic-version unification remains
release-engineering work.

## [V2] - In development

### Phase 5 in development - venue simulator and execution gateway

Phase 5 opened with a contract review rather than code, because sections 9
through 15 - the primary gateway contract - had never passed an approval gate
while sections 8.1 through 8.7 had. The review found thirteen contradictions or
gaps; five blocked implementation and are resolved in architecture section 9.1.

Resolved: "committed" collapses to "applied" until Raft arrives in Phase 6;
`claimed` and `attempted` are gateway-local rather than consensus state,
resolving a direct contradiction between sections 9 and 8.2; expiry is defined as
control-generation staleness, deleting a phantom field the contract required the
guard to check but `OrderEffect` never had; lowering is representation only and
never economics, closing the largest under-specified permission in the contract;
and the gateway is a separate process in deployment, tested in-process with the
transport interface as the seam, because credential isolation under G-066 is not
achievable inside the reducer's process.

Also recorded: an open obligation inherited from Phase 4, where section 3 demands
retention exceed horizons that section 8.4 says are unknown until Phase 5, so
Phase 4's retention policy cannot yet be shown to satisfy it; and a trap, where
`wire.hpp` carries an unwired Phase 1 message vocabulary that does not correspond
to `KernelCommandKind` and is not a transport.

First increment delivers the final pre-send guard of G-029 as a pure function
that reads no clock, mutates nothing, and returns a decision carrying the exact
canonical bytes when permitted and nothing at all when refused, so a refused
effect cannot be sent by construction. Release 26/26 and full host Debug
ASan+UBSan 26/26.

Status: Phases 1-4 are accepted on the required host, ARM64 Ubuntu, and physical
Raspberry Pi 5 gates. Deterministic policy evaluation, risk reservation, cluster
orders, accepted venue-evidence accounting, and the transactional outbox are
implemented. The execution gateway, generic V2 Raft integration, durable V2
storage, and live trading behavior are not implemented. No V2 release has been
tagged, and no V1/V1.1 wire, storage, deployment, or evidence artifact has been
deleted or superseded in place.

The accepted architecture direction is recorded in
[`ADR 0001`](docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md).
The authoritative domain, identity, message, order, risk, outbox, execution,
reconciliation, persistence, threading, failure, and metric contracts are in
[`docs/V2_ARCHITECTURE_CONTRACTS.md`](docs/V2_ARCHITECTURE_CONTRACTS.md).
The test-first build order and phase acceptance gates are in
[`docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md).

### Objective

Evolve the counter laboratory into a deterministic trading Decision/OMS
platform. A Raft cluster will own authoritative policy activation, decision
evaluation, risk reservations, order intent, accepted venue evidence,
positions, deduplication, and a transactional outbox. A logically single
active execution gateway will deliver committed effects to a venue and return
acknowledgement, fill, cancel, unknown-outcome, and reconciliation evidence
through the cluster.

### Accepted design decisions

- A model or calibration service produces an immutable, versioned decision
  policy artifact; publication does not silently activate it.
- A market-data/feature edge proposes bounded canonical decision frames. The
  Raft state machine performs the final authoritative evaluation.
- An accepted decision atomically records the order, risk reservation,
  deduplication receipt, and committed outbox effect.
- Raft apply performs no external I/O. The executor delivers committed effects
  at least once by stable identity and reconciles unknown outcomes before
  retry.
- The executor may refuse or defer an unsafe effect but may not originate or
  economically modify an order.
- One executor generation is active per venue/account. Automatic standby
  promotion remains prohibited until the old sender can be externally fenced.
- Normal new-risk traffic and emergency stop/cancel traffic use separate
  bounded lanes and authority rules.
- V2 retains the bounded asynchronous WAL direction: fixed-capacity staging
  may move and amortize persistence work, but protocol success stays behind an
  explicit quorum-durability fence.
- Strategy economics are the final implementation phase, after identity,
  deterministic replay, OMS/risk, outbox, simulation, storage, execution,
  reconciliation, kill, and operational recovery are proven.

### Development method

V2 uses test-driven development: observe the contract test fail, implement the
smallest bounded behavior that makes it pass, refactor with all tests green,
and retain evidence. Every phase defines tests, metrics, failure drills,
acceptance gates, and rollback boundaries before production code is added.

### Phase 1 candidate - domain types and versioned contracts

Commit `a6ee8df5870a` implements the macOS candidate:

- zero-cost strong identity wrappers prevent implicit cross-domain ID use;
- checked fixed-point price, quantity, money, fee, and feature types make scale,
  overflow, and rounding policy explicit;
- fixed-capacity policy, decision-frame, order-effect, and venue-observation
  values have typed structural validation;
- the V2 envelope uses explicit little-endian fields, a versioned initial
  message-kind registry, bounded lengths, fail-closed flags, and CRC-32;
- canonical payload codecs reject truncation, trailing bytes, excessive counts,
  unknown schema/enums, missing identity/digests, and invalid domain values;
- SHA-256 provides immutable artifact and corpus identity; and
- a Release microbenchmark reports codec timing, encoded-size ceilings, and
  code-audited allocation upper bounds without claiming system throughput.

The exact 144-byte policy corpus has SHA-256
`7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30`.
Debug, Release, legacy regression, AddressSanitizer, and UndefinedBehaviorSanitizer
tests pass on macOS ARM64. The exact Phase 1 content root also passes Release,
benchmark-instrumented, AddressSanitizer, and UndefinedBehaviorSanitizer tests
under native ARM64 Ubuntu 26.04 with GCC 15.2. Raw commands, measurements,
limitations, and the remaining target gate are recorded in
[`docs/evidence/V2_PHASE1_MACOS_CANDIDATE.md`](docs/evidence/V2_PHASE1_MACOS_CANDIDATE.md)
and
[`docs/evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md`](docs/evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md).

Phase 1 passed its physical Raspberry Pi ARM64 gate on 2026-09-03 as the
unchanged contract subset of source root
`0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8`.
The combined gate ran the unchanged Phase 1 suites in Release,
benchmark-instrumented Release, and ASan/UBSan Debug configurations. Detailed
target evidence is recorded in
[`docs/evidence/V2_PHASE1_RASPBERRY_PI_ACCEPTANCE.md`](docs/evidence/V2_PHASE1_RASPBERRY_PI_ACCEPTANCE.md).

### Phase 2 experimental increments - policy lifecycle and control authority

Experimental commits `7bc9e42` and `c783ab7` introduce the first pure reducer
increment and its native ARM64 Ubuntu development gate. The increment provides
a fixed-capacity policy catalog, retained request outcomes, exact/conflicting
duplicate handling, immutable and monotonic policy identity, and bounded
proposed/validated/active/superseded transitions. It performs no I/O, clock or
random read, locking, heap growth, order creation, risk change, or external
effect.

Seven Release suites, four benchmark-instrumented V2 suites, and four
ASan/UBSan V2 suites pass under ARM64 Ubuntu 26.04 with GCC 15.2 for source root
`627b38f488aad60d3f0a65874a4bb132e196400c31c2e406f4b615e1095221d3`.
Detailed RED/GREEN evidence, invariants, C++ design notes, and limitations are
recorded in
[`docs/evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md`](docs/evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md).

Commits `6de06ec` and `2849a52` add the next TDD increment: fail-safe strategy
mode, monotonic kill epochs, monotonic executor generations, generation-fenced
mode transitions, and the extended ARM64 gate. The latest committed source
root is
`8e4cba2743760eb71bb3ee7277d63183c2e706cc3bec5a6d449f0eec21291a72`;
8/8 Release suites, 5/5 benchmark-instrumented V2 suites, and 5/5 ASan/UBSan
V2 suites pass under ARM64 Ubuntu 26.04 with GCC 15.2.

This is not Phase 2 completion or acceptance. It remains on
`experiment/v2-phase2-kernel` while the remaining reducer increments are built. State
digest/snapshot, reconciliation-gated authority restoration, orders,
reservations, positions, outbox,
reconciliation, model-based replay, reducer measurement, Raft/storage
integration remain pending.

Experimental commit `33d27f6` adds strict normalized source admission. A
source is identified by source ID, generation, and sequence; the
accepted event bytes are represented by a canonical digest. Exact redelivery
is a retained no-op, conflicting redelivery fails closed, and unseen gaps or
regressions cannot advance authoritative source state. Fixed source and event
receipt capacities are checked before mutation. Venue-specific reconnect,
buffering, and snapshot recovery remain adapter responsibilities.

The committed source passed 9/9 Release suites, 6/6 benchmark-instrumented V2
suites, and 6/6 ASan/UBSan V2 suites under ARM64 Ubuntu 26.04 with GCC 15.2.
Its canonical source root is
`0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8`.

This increment is not an indefinite-stream implementation: the experimental
128-event receipt region has no replicated expiry yet and deliberately rejects
when full. Reconciliation commands, snapshot/state-digest coverage, and
randomized replay remain pending. The increment passed its physical Raspberry
Pi checkpoint on 2026-09-03 with clean power state before and after the run.
Detailed rationale, invariants, C++ mechanics, and limitations are recorded in
[`docs/evidence/V2_PHASE2_SOURCE_ADMISSION_EXPERIMENT.md`](docs/evidence/V2_PHASE2_SOURCE_ADMISSION_EXPERIMENT.md).

Experimental commit `865ec08` adds a bounded canonical `TradingState`
snapshot codec and SHA-256 state digest. The format uses explicit little-endian
logical fields and excludes C++ padding, inactive fixed-capacity slots,
metrics, and scratch state. Decode is transactional: it validates into
caller-owned candidate scratch and publishes to the destination only after the
frame, schema, digest, capacities, enums, contracts, and state invariants pass.

The exact commit passed 10/10 Release suites, 7/7 benchmark-instrumented V2
suites, and 7/7 ASan/UBSan V2 suites under ARM64 Ubuntu 26.04 with GCC 15.2.
Its canonical source root is
`17bf519d09f4b4e3f87fcf399d0fe18999b5358112736c38c2e7262be6afba75`.
The empty-state golden digest is
`0e737df191c265e7d2a4825c229ffe5220a4fc0e9a9a679731ac475ea5e44eb8`,
and the currently declared maximum snapshot is 45,595 bytes.

This is not durable snapshot persistence or complete replay: file publication,
durability fences, crash selection, Raft snapshot installation, randomized
suffix replay, retention/expiry, and performance measurement remain pending.
The exact source root passed its physical Raspberry Pi checkpoint on
2026-09-03: both wrapper and container gates passed, all three configurations
passed, firmware reported `throttled=0x0` before and after, and temperature
moved from 43.55 C to 48.50 C. Detailed evidence is recorded in
[`docs/evidence/V2_PHASE2_STATE_SNAPSHOT_EXPERIMENT.md`](docs/evidence/V2_PHASE2_STATE_SNAPSHOT_EXPERIMENT.md).

Experimental commits `a04094e` and `0a20e4f` add canonical versioned kernel
command/result frames and a byte-level replay boundary. All eight current
command alternatives use explicit stable kind values rather than
`std::variant` indexes. Frames use explicit little-endian fields and SHA-256
integrity; the command maximum is 608 bytes and the result frame is 110 bytes.
Malformed input and insufficient result capacity fail before reducer mutation.

A ten-record corpus now proves that continuous replay and midpoint snapshot
restore plus suffix replay return byte-identical suffix results and converge to
the same canonical snapshot. The reviewed final state digest is
`596ec54fb24f55b91579330e08cefa63b3af8d39c1e277cd835c5af4e1b566c8`.
The combined increment passed 12/12 Release suites, 9/9
benchmark-instrumented V2 suites, and 9/9 ASan/UBSan V2 suites under ARM64
Ubuntu 26.04/GCC 15.2. Its canonical source root is
`810f8c4db00d6ae37061694ab3be6f775b3312f0b837c09b00c589903d5e04aa`.

The exact source root passed its physical Raspberry Pi checkpoint on
2026-09-03. Both wrapper and inner ARM64 gates passed; firmware reported
`throttled=0x0` before and after, and temperature moved from 43.55 C to 48.50 C.
Generated/model-based replay, durable snapshot publication, Raft/WAL
integration, receipt expiry, and compaction remain pending. Detailed evidence
and C++ rationale are in
[`docs/evidence/V2_PHASE2_CANONICAL_REPLAY_EXPERIMENT.md`](docs/evidence/V2_PHASE2_CANONICAL_REPLAY_EXPERIMENT.md)
and
[`docs/V2_CANONICAL_CODEC_REPLAY_GUIDE.md`](docs/V2_CANONICAL_CODEC_REPLAY_GUIDE.md).

Experimental commit `26f0853` adds deterministic differential replay against
a structurally independent reference model. Thirty-two fixed seeds generate 96
commands each. The production byte path and model agree after every command on
typed results, canonical result bytes, and canonical state. Six snapshot cuts
per history reproduce every suffix result and final snapshot. Required coverage
includes policy lifecycle conflicts, control-fence rejection, source ordering,
exact retries, and request-identity conflicts. The aggregate corpus digest is
`1eadcbdef0f9a244b935afdb3dd3be9c6e6f42fffe913509cc3baaea303357a0`.

The increment passed 13/13 Release suites and 10/10 instrumented and
ASan/UBSan V2 suites under ARM64 Ubuntu 26.04/GCC 15.2. Its canonical source
root is
`d6eb59adecac3a486c2eee83151df02d66e7425865dd5076ad34ac0beeb96668`.
The exact root passed its physical Raspberry Pi checkpoint on 2026-09-04. The
wrapper and inner ARM64 gates passed all 10 sanitized V2 suites; firmware
reported `throttled=0x0` before and after, and temperature moved from 43.55 C
to 48.50 C. This expands semantic/replay evidence; it does not establish
real-Raft integration, durability, capacity, or trading authority. Detailed
evidence is in
[`docs/evidence/V2_PHASE2_MODEL_REPLAY_EXPERIMENT.md`](docs/evidence/V2_PHASE2_MODEL_REPLAY_EXPERIMENT.md).

### Phase 2 accepted - deterministic kernel foundation

Phase 2 is accepted at its corrected architectural boundary: bounded policy,
control, source-admission, receipt, canonical-codec, snapshot, digest, and
model-replay mechanics. Decision evaluation begins in Phase 3; risk, orders,
and the transactional outbox remain exclusively Phase 4 work. This removes the
earlier checklist overlap without pulling later behavior forward.

The closure record is
[`docs/evidence/V2_PHASE2_DETERMINISTIC_KERNEL_ACCEPTANCE.md`](docs/evidence/V2_PHASE2_DETERMINISTIC_KERNEL_ACCEPTANCE.md).

### Phase 3 planned - authoritative decision evaluation

Phase 3 has a staged design checklist but no accepted behavior. The plan first
freezes pure fixed-point predicate and ordered-rule semantics, then separately
reviews causal source/event-time schema, authoritative retained decisions, and
economic action templates. This keeps accepted schema changes and risk-bearing
order semantics out of an oversized commit. See
[`docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md`](docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md).

Experimental commit `6ee47b7` implements the Phase 3A pure ordered-rule
evaluator after an observed missing-interface RED build. It validates the full
bounded rule-to-frame feature-reference set before economic evaluation, then
uses exact fixed-point comparisons and first-match array order. The result owns
its directive, reason, matched index, and rules-visited count. The complete
Release suite passed 14/14 and the V2 ASan/UBSan suite passed 11/11 on the host.
It does not yet provide authoritative decisions, revised decision-frame bytes,
retained decision identity, risk, orders, outbox, ARM64, Pi, or trading proof.
Detailed evidence is in
[`docs/evidence/V2_PHASE3_RULE_EVALUATOR_EXPERIMENT.md`](docs/evidence/V2_PHASE3_RULE_EVALUATOR_EXPERIMENT.md).

Experimental commit `3f65187` implements the Phase 3B decision-frame schema
revision. Payload families now carry independent schema versions; only the
decision frame advances to V2. Its canonical input adds source ID, source
generation, source sequence, event time, and receive time. The authoritative
decoder rejects the frozen V1 corpus with typed `unsupported_schema` rather
than reinterpreting absent provenance or misclassifying the shorter record as
truncated V2.

The two-feature V2 golden frame is 181 bytes with SHA-256
`9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3`;
the bounded maximum decision frame is now 421 bytes. The complete Release suite
passed 14/14 and the V2 ASan/UBSan suite passed 11/11 on the host. Source-state
comparison, validity evaluation, retained decision identity, ARM64/Pi evidence,
risk, orders, and outbox remain pending. Detailed evidence is in
[`docs/evidence/V2_PHASE3_DECISION_FRAME_V2_EXPERIMENT.md`](docs/evidence/V2_PHASE3_DECISION_FRAME_V2_EXPERIMENT.md).

Experimental commit `e650f31` adds the required pre-3C memory-layout
guardrails. Canonical codec sizes, resident C++ object sizes, and cache-line
rounded arena strides now have distinct reviewed budgets. The target cache-line
size is an explicit compiled ABI parameter, and the ARM64 gate records a
machine-readable layout report in its evidence metadata. Compile-time
assertions stop payload or object growth that exceeds a reviewed ceiling.

For a declared 64-byte target line, `DecisionFrame` is currently 421 bytes
canonical, 432 bytes resident, and 448 bytes per rounded arena slot;
`TradingState` is 57,152 bytes against a 65,536-byte ceiling. The complete
Release suite passed 15/15 and the V2 ASan/UBSan suite passed 12/12 on the host.
The hot/cold retained-decision layout remains intentionally pending Phase 3C;
no cache-performance or capacity claim is made.

The exact source root
`1d9e44e20c495ee935b58b0723351cd5282642d0cdc098c4c35eba25d29bb577`
passed its physical Raspberry Pi 5 ARM64 gate on 2026-09-04. The target's
runtime L1 data-cache line matched the compiled 64-byte assumption, all 12 V2
sanitizer suites passed, firmware power remained `throttled=0x0`, and host
temperature moved from 44.10 C to 49.05 C. Detailed evidence is in
[`docs/evidence/V2_PHASE3_LAYOUT_GUARDRAILS_EXPERIMENT.md`](docs/evidence/V2_PHASE3_LAYOUT_GUARDRAILS_EXPERIMENT.md).

Experimental commit `c4283f6` defines the pre-reducer Phase 3C retained
decision layout. A 64-byte hot record holds lookup, outcome, and control fields;
a 472-byte cold record retains the command digest and complete decision frame.
Their arena strides are independently rounded to 64 and 512 bytes. Sixteen
records would consume 8,576 packed object bytes or 9,216 rounded arena bytes.
The records are deliberately not yet members of `TradingState`: snapshot,
digest, replay, and atomic capacity behavior must change in the same reducer
increment. Host Release passed 15/15 and V2 ASan/UBSan passed 12/12. Detailed
evidence is in
[`docs/evidence/V2_PHASE3_DECISION_RECORD_LAYOUT_EXPERIMENT.md`](docs/evidence/V2_PHASE3_DECISION_RECORD_LAYOUT_EXPERIMENT.md).

The Phase 3C host candidate integrates that layout into authoritative state.
`EvaluateDecisionFrame` has explicit wire kind 9; kernel command/result and
snapshot schemas advance to V2; and deterministic rejection, ordered
evaluation, control suppression, shadow authority, retained identity, typed
decision conflicts, and bounded capacity are covered. Hot lookup does not scan
the cold frame. Canonical replay and an independent generated reference model
retain decisions across every tested snapshot cut.

The current 64-byte-target report records a 69,832-byte `TradingState` under a
reviewed 73,728-byte object ceiling, a 57,997-byte maximum canonical snapshot,
a 608/640-byte maximum command, and a 126/128-byte result. Host functional,
Release V2, and ASan/UBSan V2 suites pass 16/16, 13/13, and 13/13 respectively.
The exact source root
`d3a45742b260562abb987c31092d04bc2abe8b162f71e0008af14f3be4942b75`
also passed all 13 V2 suites on the physical Raspberry Pi 5 on 2026-09-04.
The target cache-line assumption matched at 64 bytes, firmware power remained
`throttled=0x0`, and temperature moved from 45.75 C to 49.60 C.

Phase 3-wide measurement and Phase 3D economic action-template work remain
pending; no risk, order, effect, venue authority, snapshot-latency, or
throughput claim is made. Snapshot-validator and per-command receipt-lookup
performance concerns are explicitly deferred to the measured V2.1 scope in
[`docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md`](docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md).
Detailed Phase 3C evidence is in
[`docs/evidence/V2_PHASE3C_AUTHORITATIVE_DECISION_EXPERIMENT.md`](docs/evidence/V2_PHASE3C_AUTHORITATIVE_DECISION_EXPERIMENT.md).

Phase 3D direction review approves bounded constant-or-feature value sources,
logical order-slot targeting, explicit replace intent, immutable policy safety
caps, and versioned venue capabilities. The proposed authority boundary keeps
model output advisory, validates exact strategy intent against the deployed
policy envelope, and leaves dynamic risk checking plus atomic reservation to
Phase 4. Silent price/quantity clamping is not planned: a limit violation must
produce a typed no-effect rejection unless a future policy explicitly defines
adaptive sizing as strategy behavior.

The first Phase 3D TDD slice adds standalone typed action contracts without
changing policy, command, result, snapshot, or replay schemas. Separate
`PriceSource` and `QuantitySource` types, canonical inactive fields, strict
safety-cap validation, logical order slots, instrument/capability versions, and
exact economic proposals are host-green. New Phase 3D semantics separate
post-only from GTC/IOC/FOK and prohibit executor discretion. Full contract
tests pass 17/17; optimized Release and ASan/UBSan V2 suites pass 14/14. Pure
resolution, authoritative instrument/capability inputs, codecs, reducer
integration, ARM64, and Pi evidence remain pending. Detailed evidence is in
[`docs/evidence/V2_PHASE3D_ACTION_CONTRACT_EXPERIMENT.md`](docs/evidence/V2_PHASE3D_ACTION_CONTRACT_EXPERIMENT.md).

A follow-up contract slice adds authoritative `InstrumentExecutionProfile` and
`VenueCapabilities` inputs. They bind monetary tick-lot conversion, inclusive
instrument bounds, supported directives/TIFs, and post-only capability to
explicit versions. The 72-byte cold instrument profile uses a reviewed 80-byte
object ceiling; the venue capability record exactly meets its 32-byte ceiling.
No canonical payload or reducer state changes yet. Functional tests remain
17/17 green, with focused Release and ASan/UBSan action/layout gates passing.

The standalone Phase 3D pure resolver now converts fixed-or-feature templates
to exact economic proposals using explicit signed rounding and checked integer
arithmetic. It enforces scope/version binding, venue semantics, freshness,
instrument bounds, and immutable offset/deviation/quantity/notional caps with
typed reject-not-clamp outcomes. Cancellation remains available during stale
market data. Functional tests pass 18/18; focused Release and ASan/UBSan
resolver/layout gates pass 2/2. Policy-schema/reducer integration and target
evidence remain pending, so this is not yet an order-authoritative path.

A candidate bounded executable-policy companion now maps the existing 32
ordered rules to at most eight deduplicated templates; slot zero is canonical
no-action. A separate 32-entry feature-schema artifact owns price/quantity unit
conversions. Structural and cross-artifact tests are host-green, including
Release and sanitizers. The active-plus-staged resident projection exceeds the
old state ceiling by 24 bytes before lifecycle metadata, correctly blocking
silent integration. Canonical size and Pi/NUC evidence must determine the final
state/snapshot budget before these candidate bounds are accepted.

### Phase 4 B1 - replicated risk-limit configuration (2026-09-09)

- Added approved full-config `ConfigureRiskLimits` command kind 17 and risk-result
  detail tag 5. Justification: risk limits must be configured by deterministic
  commands, not only fixtures. No balance/reset fields or external effects.
- Added strict expected-version updates, kernel-selected next RiskVersion,
  epoch/executor checks, positive/range validation, prospective per-order caps,
  current aggregate hold coverage and quiescent rate/policy changes. Existing
  balances and PositionVersion are preserved. Added typed capacity, exhausted-
  version and blocked-reconfiguration refusals; no existing enum values removed.
- Advanced kernel command/result and snapshot schemas to approved V5; kind 17's
  body is 88 bytes and full command 180 bytes. Receipt detail reuses three words.
  No resident capacity increase or OrderEffect change. Older schemas are rejected,
  not migrated or reinterpreted.
- Added creation/update/codec RED-to-GREEN tests, command-configured authorization,
  version staleness, economic preservation, replay/restore, full canonical body,
  malformed/truncated input, capacity, and version/rate/control boundary coverage.
- Updated five schema-dependent golden digest expectations (source command,
  apply result, empty snapshot, replay state and generated corpus); retained V4
  source/empty-state payload projection checks. No tests removed. The existing
  Phase 1-3 reference model still excludes Phase 4; corrected its misleading
  claim that a complete Phase 4 model already exists.
- Added architecture contract section 8.1 as the reference for later operator
  documentation; corrected stale replacement comments to the approved serial-only
  scope. Full Phase 4 acceptance remains outstanding.
- Validation: Release 23/23 and focused host ASan+UBSan 5/5 (risk, snapshot,
  codec, replay and Phase 1-3 model). Layout unchanged at 81,024-byte state and
  65,121-byte maximum snapshot. No physical Pi or pinned ARM64 gate claimed.

### Phase 4 review corrections - reclamation identity and snapshot accounting

Working-tree increment, 2026-09-08. No Phase 4 acceptance claim.

- Approved B1 terminology refactor: renamed `RiskAccount::max_order_notional`
  to `max_order_gross_notional`, and `max_reserved_notional` to
  `max_reserved_exposure`. Added comments distinguishing one order's gross
  notional from the aggregate margin-plus-fee hold budget. Updated reducer,
  snapshot codec/validation, test references and handoff; removed misleading
  source identifiers. No behavior, member layout, canonical field order or
  schema change; no aggregate gross-notional cap or configuration command added.

- Added permanent RED regressions for authorization identity after 20 completed
  cycles and for checksummed snapshots with unbacked account aggregates. Both
  failed for the intended reason before implementation changes.
- Changed evaluation and authorization retries to consult retained receipts after
  payload reclamation. Justification: freeing arena slots must not allow reuse of
  a DecisionId, a second economic authorization, or aliased/unmaterializable effects.
  Submit receipts use the existing generation-1 EffectId derivation; cancel
  receipts cannot substitute for them. No new identity or canonical fields.
- Added snapshot ownership and checked per-account quantity/margin/fee sums.
  Justification: individually valid records are insufficient if aggregate risk
  disagrees with live reservations. Missing owners, orphan holds, mismatches and
  overflow fail closed; rejected restore preserves its destination.
- Added tests for fresh-request retries/conflicts across sweep and restore,
  distinct fresh effects, full effect/snapshot bytes, account attribution and
  accumulator overflow. Corrected the maximum-size fixture's previously
  inconsistent account totals without reducing its encoded size or capacities.
- Removed the stale claim that DecisionId conflict detection must be lost after
  sweeping, and superseded the handoff's 16-lifetime-order limitation. Retained
  receipts already provide identity tombstones; their eventual exhaustion and
  unauthorized-decision retention remain separate limitations. No tests removed.
- Validation: Release 23/23; focused Release 2/2; focused host Debug ASan+UBSan
  2/2 (`build-phase4-review-sanitized`). Full Phase 4 sanitizer acceptance,
  pinned ARM64, physical Pi and the Phase 4 evidence document remain outstanding.
- Layout unchanged: `TradingState` 81,024 bytes; maximum canonical snapshot
  65,121 bytes. Snapshot validation adds only 96 bytes of temporary totals.

### Phase 4 in development - risk reservation, orders, and transactional outbox

Commit `401c861` introduces the first Phase 4 development checkpoint. It is not
a Phase 4 acceptance claim: no sanitizer, pinned ARM64, or physical Raspberry Pi
gate has been run against it, and no Phase 4 evidence document exists.

#### Added

- `AuthorizeDecision`, `ObserveFill`, `RequestCancel`, `ObserveOrderEvent`, and
  `RequestReplace` as explicit kernel command kinds 12 through 16; kernel
  command/result and snapshot schemas advance to development V4 without
  reinterpreting accepted V3 bytes.
- An atomic authorization transition: one accepted authoritative `place_limit`
  decision creates its risk reservation, cluster order, retained receipt, and
  pending outbox effect, or creates none.
- Risk accounts, reservations, cluster orders, and normalized outbox records in
  `TradingState`, at 112, 64, 40, and 64 bytes respectively.
- Domain-separated SHA-256 derivation of `ClusterOrderId` and `EffectId`, with
  zero and collision cases failing closed.
- Partial-fill accounting that moves signed quantity into position and
  proportionally reduces margin, fee, and notional holds.
- `VenueObservation` schema V2 (166-byte `OrderEffect` with explicit
  `PostOnlyMode`; V1 rejected, not reinterpreted) and observation kind
  `cancel_reconciled`, whose quantity is the final cumulative filled quantity.
- `observation_fingerprint`: SHA-256 over an ASCII domain tag plus the canonical
  observation bytes, verified before receipt lookup or economic deduplication so
  a copied digest cannot hide changed evidence.
- Cross-kind venue event identity conflicts: a fill and an order event may not
  reuse one venue/session/sequence in either direction.
- `materialize_order_effect`, reconstructing an effect's exact historical
  outbound bytes from retained outbox, receipt, and decision state.
- `ApplyCode::replacement_not_supported` and `ApplyCode::order_slot_occupied`.
- Two tests proving the above two refusals leave economics byte-unchanged and
  hold across snapshot restore.

#### Removed

- The `max(old, new)` reservation hold in `apply_request_replace` (276 lines).
  Justification: `max(old, new)` is the correct worst-case hold only if the
  venue guarantees old and new orders can never both execute. Where they can
  coexist, true exposure is `old + new` and `max` under-reserves, violating
  G-011. No venue this system targets documents that exclusivity, and proving it
  requires the Phase 5 venue simulator, so the assumption could not be settled
  inside Phase 4.
- The replace acknowledgement/rejection branch in the venue-observation handler
  (140 lines). Justification: unreachable once no effect can carry
  `EffectOperation::replace`.
- `ClusterOrder::active_filled_baseline`, and its snapshot validation, encode,
  and decode. Justification: its only readers lived in the deleted branch. It
  existed to reconstruct an old order's residual after a rejected replacement
  without retaining a second reservation object; with replacement deferred there
  is no such reconstruction. `ClusterOrder` falls from 48 to 40 bytes and
  `kStateSnapshotClusterOrderBytes` from 43 to 35.
- Five replacement-economics tests. Justification: they asserted behaviour that
  no longer exists. The outbox materialization test was converted to a
  submit/cancel effect pair, preserving its actual subject - that every retained
  effect reconstructs exact historical bytes across lifecycle and snapshot
  restore. The replacement control-recheck assertion became an unconditional
  refusal assertion: no control state permits direct replacement. This proves a
  disabled capability, not a stronger implementation of safe serial replacement;
  the serial lifecycle still needs current-control recheck evidence.

#### Decided (revised user approval, 2026-09-08)

Native amend and overlapping replacement remain disabled. Safe SERIAL
replacement remains a Phase 4 acceptance requirement: cancel, final
reconciliation, fresh authoritative decision, fresh authorization under current
controls and risk. A typed refusal alone does not implement this capability.
Wire kind 16 remains allocated; this decision changes no canonical bytes or code.

- Added: an explicit serial-replacement acceptance matrix in CURRENT_HANDOFF and
  corrected G-018. Justification: prove late-fill accounting, risk retention,
  fresh authorization, identities and full effect bytes through evidence
  permutations, compaction, snapshot restore and replay before closing Phase 4.
- Removed: the blanket replacement deferral and the rationale that covering two
  executable orders inherently violates safe reservation. Justification: covering
  both exposures is legitimate; double-counting one exposure is the error.
  `max(old, new)` remains unsafe without proven mutual exclusion. The selected
  MVP avoids overlap, rather than asserting that all safe replacement is impossible.
- Removed: dependence on the earlier venue-endpoint availability claim for this
  scope decision. Justification: reducer safety must not assume unverified venue
  semantics. No gateway, venue adapter, or native-amend implementation is added.
- No implementation or test was added or removed in this documentation revision;
  the serial acceptance matrix remains unproven, and no new gate pass is claimed.

Deferring replacement was not sufficient on its own. `ClusterOrderId` derives
from the varying `DecisionId`, so cancel-then-authorize created a second order
and a second reservation on one logical slot, violating the selected no-overlap
policy. `AuthorizeDecision` now enforces at most one unresolved cluster order
per account, instrument, venue, and logical order slot, keyed on the reservation
not being released, because retained risk is what G-018 constrains. The RED for
`test_fresh_submit_cannot_bypass_unresolved_cancelled_slot` observed exactly
that defect before the fix.

#### Measured

```text
ClusterOrder                    48 -> 40 bytes
TradingState                81,144 -> 81,016 bytes (headroom 776 -> 904)
maximum canonical snapshot  65,241 -> 65,113 bytes (headroom 295 -> 423)
```

Host Release and all 23 CTest suites pass. The reclaimed snapshot headroom is
reserved for Phase 4 compaction metadata and must not be spent on new capacity.

#### Added - bounded arena reclamation (B2)

- `sweep_resolved_order_groups`: reclaims fully resolved order groups - order,
  reservation, its outbox effects, and its decision record - from the bounded
  16-slot arenas. Without it the kernel stops authorizing after 16 lifetime
  requote cycles on a slot, which slot exclusivity makes arrive one requote at a
  time. Deterministic: no clock, no random source, no allocation, fixed passes
  over arenas of 16.
- `TradingState::reclaimed_group_count`, a monotonic counter carried in the
  canonical snapshot after the control prefix. Justification: it gives the
  dashboard an auditable "how much has ever been swept" signal without growing
  the 128-byte canonical result or regenerating every existing golden result,
  and it turns an otherwise silent state-digest change into an observable event.
- Four tests covering continuous requoting past every arena bound, retention of
  records an unsettled effect still needs, refusal to reclaim a conflicted
  order, and preservation of order/reservation index pairing across compaction.

#### Reclamation contract

Reclaimable requires TERMINAL AND RESOLVED, which are different properties.
`filled`, `cancelled`, and `rejected` resolve their risk. `conflict` is terminal
but unresolved - internal state and venue evidence disagree, so the exposure is
of unknown size - and `reconciling` still awaits final venue evidence. Neither is
reclaimable: dropping one would free capacity against exposure that may still
exist, and would destroy the retained proposal and effect bytes an operator needs
to resolve the disagreement.

A group is also retained while any of its effects is unsettled, because
`materialize_order_effect` rebuilds an effect's exact historical bytes from the
retained decision and receipt. Reclaiming those inputs would strand a pending
external effect whose payload can never be reproduced, breaking G-020.

Reservations and orders are reclaimed as a PAIR under dense compaction with one
index-fixup pass. `ClusterOrder` addresses its reservation by array index, so
compacting reservations independently would silently repoint an order at another
order's risk with no compile error and no validation failure.

The horizon is reference-based rather than time-based: the arena being full is
the trigger and "nothing can reference this" is the safety condition, so no time
horizon is required. To revisit in Phase 5, when the gateway retry horizon and
the venue reconciliation window become measurable. One accepted consequence: once
a decision record is reclaimed, reusing its `DecisionId` from a different request
with different provenance is no longer detectable as a typed conflict. Exact
command replay is unaffected, because request-receipt deduplication runs before
decision lookup.

#### Fixed during implementation

A stale-reference defect found by the B2 REDs. `apply_authorize_decision`
borrowed `const auto& cold = state.decision_cold[decision_index]` before the
capacity check; running compaction there left the reference in bounds but naming
a different, cleared slot, so an order was reserved from a zeroed proposal with
zero quantity. The failure was silent - no crash and no sanitizer report. Both
sweep call sites now run before any index is resolved or any arena reference is
borrowed, which makes the hazard structurally impossible rather than a rule a
future author must remember.

#### Measured after B2

```text
TradingState                81,016 -> 81,024 bytes (headroom 904 -> 896)
maximum canonical snapshot  65,113 -> 65,121 bytes (headroom 423 -> 415)
```

Empty-state, replay, and generated-model golden digests were regenerated because
the canonical snapshot gained the counter. Development V4 is unaccepted, both
replay paths still converge byte-for-byte, and no accepted V3 bytes were
reinterpreted.

#### Added - serial replacement acceptance, first slice

`test_serial_replacement_across_a_risk_limit_update` proves the cancel,
reconcile, fresh-authorize lifecycle while replicated risk configuration changes
underneath it. Refusing native amend does not by itself satisfy G-018: the
guarantee constrains the amend/cancel-replace transition, and with native amend
disabled the serial path is that transition, so it must be proven rather than
assumed.

Eight legs: rate or policy change refused while the cancelled order still holds
risk; aggregate cap below the retained hold refused; prospective per-order cap
accepted mid-cancellation without rewriting balances, position version, order
state, or historical effect bytes; stale-version successor refused; venue
cancellation not releasing risk; final reconciliation releasing the residual;
fresh successor authorizing against the new configuration; and every effect
reconstructing byte-for-byte across snapshot restore and canonical replay, with
the replayed snapshot byte-identical to the direct one.

Legs 1-7 passed on first run and are recorded as acceptance coverage for
behaviour already implemented correctly, not as fixes.

#### Fixed - replay fidelity

Leg 8 failed genuinely with `canonical replay changed historical effect bytes`.
`materialize_order_effect` takes `committed_directive_index` from the retained
authorization receipt's `state_version`, and an admitted refusal also retains a
receipt and advances the global StateVersion. A replay omitting refused commands
therefore authorizes at a different StateVersion and produces different canonical
effect bytes. The defect was in the test harness rather than the kernel, but the
property is general: a faithful replay must re-drive every admitted command,
refusals included. `evaluate_new_decision` gained an optional command sink,
having previously applied two commands invisibly.

#### Fixed - orphaned effects wedged the bounded arenas

`test_reclamation_during_cancellation_preserves_the_recovery_path` found that an
order could be terminal and risk-resolved while one of its own effects had never
received evidence: order `cancelled`, reservation `released`, cancel effect
`acknowledged`, submit effect still `pending`. Once the order closed, no future
observation could be scoped to that submit effect, but reclamation required every
effect settled - so the group was permanently unreclaimable. With the decision
arena full, evaluation failed closed and could not recover even after the stuck
slot reconciled. Wedged rather than degraded.

Orphaned effects now settle to `OutboxState::incomplete`, an enum value that
already existed and was never set, and `incomplete` counts as settled for
reclamation. It is deliberately not `acknowledged`, which would claim evidence
the venue never gave. Settlement runs only on definitive evidence - final cancel
reconciliation or a rejected submit the venue never accepted - and specifically
NOT on a full fill, because a cancel in flight when the fill lands is still owed
a venue response and is awaiting evidence rather than orphaned.

Justification for auto-healing here but never for `conflict`: this group's RISK
is resolved, so there is no exposure left to under-reserve. `conflict` is
terminal but unresolved and must never be settled automatically. The distinction
is specified in architecture contracts section 8.2.

`effect_is_settled` and `order_is_risk_resolved` were factored into shared
helpers so reclamation and settlement cannot drift apart on what "resolved" means.

#### Added - orphan settlement accounting

`TradingState::settled_incomplete_effect_count`, a monotonic counter in the
snapshot header. A rising count means the gateway is losing effect outcomes even
while reclamation stays healthy. It grew the V5 empty payload by eight bytes,
which falsified the guard asserting V5's empty payload matched V4's; that guard
now pins the empty-payload size instead, which is what unreviewed header growth
actually moves. Empty-state, replay and generated-model goldens were re-pinned
for development V5.

`TradingState` is 81,032 bytes and the maximum canonical snapshot 65,129.

#### Added - serial replacement matrix completed

`test_risk_configuration_respects_every_unresolved_order_state` pins the
architecture section 8.1 rule against all four unresolved states: cancel_pending,
submission_unknown, reconciling and conflict. Rate changes and bare
policy-version bumps are refused and mutate nothing; prospective per-order caps
are accepted without revaluing the live hold; aggregate caps below the retained
hold are refused rather than clamped; and rates become configurable once the hold
is released, without disturbing the filled position. The behaviour was already
correct, but the implementing predicate keys on the reservation not being
released rather than on a lifecycle-state list, so the states section 8.1 names
were only covered transitively and a future refactor could silently drop one.

`test_two_sided_slots_requote_independently` adds the suite's first two-sided
fixture. A second ordered rule bound to a second template places a sell quote on
logical slot 2 while a buy rests on slot 1, so first-match selection steers each
side to its own slot. It proves the slots never contend, exclusivity is enforced
per slot, holds are summed rather than netted, reconciling one side leaves the
other's state, pairing and effect bytes untouched, the freed slot requotes while
the other still rests, and the interleaving survives snapshot restore. It also
separates same-identity retry, which returns the original result through receipt
deduplication, from a different request re-authorizing a consumed decision, which
returns already_applied with the retained identities.

Both are acceptance coverage; neither found a kernel defect. A fixture trap is
recorded in code: two_sided_state swaps the policy after ready_state has already
evaluated its fixture decision, so that decision targets slot 1 whatever the
two-sided bindings say, and authorizing it makes both quotes collide in a way
that mimics a slot-exclusivity defect.

#### Added - independent Phase 4 property corpus

`tests/v2_phase4_property_tests.cpp` drives 24 seeds of 220 randomized but
well-formed command steps and checks contract-derived properties after every
command: no outbox effect outliving its cluster order, reservation arithmetic
that cannot go negative or exceed the original, released reservations holding
nothing, byte-identical snapshot round trips, monotonic reclamation and orphan
counters, reclamation being economically inert, and byte-identical canonical
replay of the whole history including refusals.

The Phase 4 gate is stated as a property rather than as agreement with a second
implementation, so the corpus derives its checks from the contracts. That makes
it independent evidence in a different way from the Phase 1-3 mirror model, which
remains the reference for the mirror style; a Phase 4 mirror is still worth
adding and is not superseded.

Phase 4 state is created only through commands - the risk account via B1
creation, decisions via real source admission and evaluation - so the corpus
cannot hand-seed states the kernel would never produce. Coverage is asserted
rather than assumed: the run fails unless authorization, partial and full fills,
cancel, reconciliation, unknown submission, configuration, reclamation, orphan
settlement and refusals were all exercised, and a vacuity guard requires at least
a third of commands to be admitted.

No kernel defect was found. Three corpus-side defects were, each a hazard for any
future hand-built state: a zero source-cursor sequence is invalid; an
executable-policy bundle's staged and activated StateVersions must both be at or
below the current version; and asserting a specific replacement refusal code is
wrong once the receipt arena fills, since admission refuses on capacity before
dispatch reaches the handler.

Release 24/24 and focused host Debug ASan+UBSan 6/6.

#### Added - unauthorized-decision reclamation

A live maker evaluates continuously and most frames produce no_action, so the
decision arena filled with records no order would ever reference, and reclaiming
order groups did not help because there was no resolved group to free. Two rules
now make retained decisions reclaimable.

Structurally non-authorizable records are reclaimed with no horizon and no clock.
Every condition in the authorization predicate is an immutable property of the
retained record, so a record failing any of them can never become authorizable
however the world changes. This covers no_action, shadow, control-suppressed and
rejected evaluations, which is the dominant volume.

Superseded records are reclaimed when a newer actionable proposal exists for the
same account, instrument, venue and logical order slot. A maker only ever wants
the latest quote for a slot, and resurrecting an older proposal would rest a
price computed from older market data - the same objection that already justifies
refusing a stale RiskVersion. Ordering is by array position, never DecisionId,
because position is arrival order and is canonical under stable compaction.

Identity is preserved without new machinery: applied evaluation receipts outlive
the decision arena and retain the DecisionId, payload digest and original
outcome, so a reclaimed DecisionId re-presented with a different payload still
conflicts and an exact replay still returns its retained result. The concern
recorded in an earlier session was already resolved by the reclamation-identity
fix.

The sweep previously returned early when no order group was reclaimable, which is
precisely the case that matters here; decisions are now assessed independently.
No schema or layout change was required.

Recorded as a strategy assumption in architecture section 8.4: supersession means
a decision must be authorized promptly, before the edge produces a newer proposal
for that slot. That is correct for level-triggered market making and wrong for a
strategy that deliberately queues intents. A state-version horizon, which would
be strictly more general, is deferred to a later version because N cannot be
justified until Phase 5 supplies a venue simulator.

#### Changed - two test premises became unreachable

The decision arena can no longer be filled by unauthorized decisions, since at
most one survives per slot plus those referenced by live orders. The wedge
scenario in test_reclamation_during_cancellation_preserves_the_recovery_path is
now structurally unreachable, so that test asserts the stronger property that a
stuck order cannot wedge evaluation at all. test_conflicted_orders_are_never_swept
now takes sweep pressure from the order arena using the two-sided fixture.
observe_fill gained side and price parameters, because a fill whose side
disagrees with the resolved proposal is correctly refused as not applicable but
reads as a mysterious fixture failure on a two-sided book.

#### Added - Phase 4 metrics

`collect_metrics` is a pure function of `TradingState`, adding no field,
participating in no digest, and reading no clock. Section 15 asks for ages and
latencies, which are durations, and apply may not read a clock, so every age is a
STATE-VERSION distance instead. That is exact, deterministic, identical on every
replica, and arguably the better operational measure: it is load-relative, so a
stuck slot looks equally stuck in a fast market and a quiet one, and it cannot
drift or jump when a host's clock is corrected. It is not convertible to wall
clock from one sample; an exporter needs its own timestamped StateVersion
timeline outside the authority boundary.

Reported: arena occupancy against compile-time capacities; orders, effects and
reservations by state; counts and oldest ages for unresolved orders, unsettled
effects, reconciling orders and conflicted orders; maximum
decision-to-authorization distance; the retained reclamation and
orphan-settlement counters; control state; and per risk account the reserved
exposure, position, versions and headroom against aggregate ceilings. Venue-event
identity count/headroom and caller-cursor count/headroom now expose the two
retention pressures that can stop admission.

Unsettled-effect age now uses the authorization receipt for the exact `EffectId`,
so a later cancel is not aged from its parent order. Reconciling age is named
`oldest_reconciling_order_age_since_authorization` because Phase 4 does not retain
a transition timestamp; it is an upper bound, not duration in the state.

`unresolved` keys on retained risk rather than lifecycle state, matching the
reclamation rule: a metric counting "not terminal" would omit a conflicted order,
which is terminal and simultaneously the most important record on the dashboard.
An age for a record authorized at or after the current state version reports zero
rather than wrapping, so a corrupt input cannot masquerade as maximal staleness.

`OutcomeCounters` is separate and deliberately not in state. Duplicate and
refusal counts describe command outcomes rather than retained records - a
duplicate leaves no trace in state, which is what makes it a duplicate - so they
are accumulated by an observer. Placing them in `TradingState` would make an
observability counter part of the state digest and therefore of consensus.

G-074a now has its signal: a slot stuck in reconciling is no longer
indistinguishable from a quiet market.

#### Fixed - a stale-binary false pass

`ctest` does not build, so building only the target under test left untouched
suites running stale binaries. `v2-decision-kernel-tests` had been passing
against a binary predating unauthorized-decision reclamation; a full build
exposed it. The failure was a premise change rather than a defect: that test
filled the decision arena with sixteen same-slot proposals, which supersession
now sweeps down to one. The capacity-atomicity invariant is still worth testing,
so the fixture assigns each retained proposal a distinct logical slot, making the
arena genuinely full and unsweepable.

#### Fixed - Phase 4 acceptance review corrections (2026-09-10)

- Authorization now requires the decision to match the current active policy,
  executable bundle, instrument/venue versions and scope, with no newer same-slot
  actionable proposal. Old bytes retain provenance but no longer resurrect old
  authority after policy rotation or supersession.
- `order_acknowledged` is submit-only; a cancel mismatch is refused before it can
  resolve the effect and wedge the still-pending order. Repeated semantic
  `effect_unknown` observations return `already_applied` without another applied
  venue transition.
- Fill evidence is submit-only at the effect boundary. A fill cannot acknowledge
  a cancel effect; a partial fill resolves an unknown submit, while a fill against
  the historical submit books execution without erasing an independently unknown
  cancel. The mismatched-operation path refuses before economic mutation.
- Applied fill and order-event results populate the existing `DecisionId` field.
  This zero-byte owner link pins identity receipts while their order exists and
  lets resolved groups release them. A sustained 119-order test crosses the old
  64-event lifetime ceiling.
- Authorization reserves venue completion capacity using remaining quantity
  atoms plus bounded submit/cancel/reconciliation transitions; two events per
  order was rejected because Q one-atom fills are legal. Pressure now refuses new
  risk, not evidence for admitted exposure.
- Every unresolved order retains one cancel-effect slot in addition to its submit
  effect. A definitively rejected cancel can reuse its own slot at a strictly
  higher generation; unresolved effects are never overwritten.
- Caller cursors are registered at first admission with a zero expired prefix.
  Snapshot validation now bounds the arena and rejects zero/duplicate caller
  identities. The watermark is documented honestly as a conservative prefix,
  requiring monotonic sequences per stable caller.
- Successful observation results require complete owner/identity fields, while
  typed malformed-observation refusals may retain partial non-empty diagnostics.
  Thus every admitted refusal remains encodable and snapshotable.
- Phase 4 metrics now use exact effect-creation receipts, honest reconciliation
  naming, identity-arena headroom, and conflict classification for derived
  identity/fill-quantity collisions.
- The randomized Phase 4 corpus now generates correct sell fills, exact- and
  cross-request duplicates, conflicting observation identities, seed/step
  diagnostics, and an independent signed-position ledger.

These changes add no `TradingState` field and do not grow the canonical V5 result
or maximum snapshot. Old unaccepted development V5 snapshots with applied
observation results whose `DecisionId` is zero are intentionally unsupported.

#### Added - Phase 4 acceptance gate contract (2026-09-10)

- Added Phase 4 ARM64-container and physical-Pi wrappers pinned to source root
  `a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
- Extended the shared native ARM64 gate to build and execute the Phase 4 metrics,
  property, and risk/order/outbox suites in Release, instrumented Release, and
  Debug ASan+UBSan configurations. Without these targets, an older gate could
  report green while never executing the Phase 4 acceptance corpus.
- Added the Phase 4 evidence document with candidate identity, byte contracts,
  host results, target commands, layout, limitations, and an explicit pending
  status. ARM64 and Pi success will be recorded only after those gates run.
- The pinned ARM64 Ubuntu 26.04/GCC 15.2 gate passed on 2026-09-10 for the exact
  expected source root: Release 25/25, instrumented V2 22/22, and Debug
  ASan+UBSan V2 22/22. The subsequent physical Pi result is recorded below.

#### Accepted - Phase 4 risk, orders, and transactional outbox (2026-09-10)

The physical Raspberry Pi 5 reproduced source root
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`
and passed Release 25/25, instrumented V2 22/22, and Debug ASan+UBSan V2 22/22.
Power remained `throttled=0x0`; temperature moved from 44.10 C to 50.15 C.
Evidence is retained at
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`
and summarized in
`docs/evidence/V2_PHASE4_RISK_ORDERS_OUTBOX_ACCEPTANCE.md`.

G-010 through G-024 are closed. G-025/G-026 remain open for the Phase 5
transport implementation, and G-074a remains the documented fail-closed
operator-recovery gap. The annotated checkpoint is `v2-phase4-accepted`. This
acceptance starts no gateway and authorizes no venue or real-money connectivity.

#### Standing invariants for later phases

Two rules were established here whose violation fails silently. First, the sweep
compacts arenas, so any index or reference taken before it runs stays in bounds
while naming a different slot; both call sites sweep before any arena lookup and
new lookups must not be inserted above them. Second, reclamation tests whether a
record's risk is RESOLVED rather than whether its state machine has stopped, so
future terminal-but-unresolved states inherit the correct behaviour without being
added to a list.

#### Historical limitations at the initial Phase 4 increment

At the initial Phase 4 increment, direct replacement, canonical risk-limit
configuration commands, terminal tombstone compaction and replicated expiry,
complete reconciliation and ordered venue evidence, position/margin/fee
accounting beyond filled quantity and open holds, Phase 4 metrics, and a Phase 4
model/property/replay corpus were all unimplemented. The 16-slot arrays bounded
retained lifetime records rather than reclaimed live orders, so the kernel would
fail closed after 16 lifetime orders on a slot; slot exclusivity made that arrive
sooner because every requote cycle consumed a fresh slot.

A logical order slot held by an order stuck in `reconciling` still has no
timeout, escalation, or operator override. Phase 4 metrics now expose its age,
but alerting and recovery remain G-074a work owned by Phases 8 and 10. It must
not be "fixed" with a wall-clock timeout in the reducer: that breaks determinism,
and releasing a hold without venue evidence of final cumulative filled quantity
is the G-011 violation reconciliation exists to prevent.

### Model and market-data edge - initial contract layer

A passive market-data research and shadow-quoting package is added under
`model/`. It implements the market-data/feature edge and offline calibration
service described in ADR 0001, and it adds no C++ and changes no accepted
contract.

The plug-in point is a byte contract, not an API. `model/` reimplements the
canonical `DecisionFrame` (schema V2) and `PolicyArtifact` (schema V1) encoders
in Python and asserts byte-for-byte parity against the exact C++ golden corpora:
the 181-byte decision frame hashing to
`9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3` and the
144-byte policy corpus hashing to
`7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30`. A schema
revision on either side fails that test before anything else.

`FeatureSchemaId` is derived from the SHA-256 of the schema's canonical
description, so editing a feature's unit or index changes the identity and the
cluster rejects the scope mismatch rather than evaluating an old rule against a
new meaning.

`simulation/rule_evaluator.py` mirrors `src/v2_rule_evaluator.cpp`: ordered
first-match evaluation, the whole-set feature-availability pre-pass, visit
counting, and the section 3.4 control-mode dominance table including the
`halted` short-circuit. The parity tests pin the written contract; they do not
execute the C++ evaluator. A differential harness over one generated corpus,
equivalent to `v2_model_replay_tests.cpp`, remains the required gate before any
policy authored by this package is activated.

The package is passive structurally rather than by configuration: it implements
no `OrderEffect` encoder, its HTTP transport refuses any non-GET request and any
state-changing path before opening a socket, a static AST audit rejects
wallet/signing imports and order-management definitions across the tree, and the
config loader refuses credential-shaped keys. 109 host tests pass.

Venue verification on 2026-09-04 confirmed the True Markets API base, ES256 JWT
auth, `GET /v1/conductor/assets`, `POST /v1/conductor/quotes`, and
`POST /v1/conductor/orders` (market orders). The documented retail DeFi path is
RFQ-shaped rather than a resting-quote order book, while maker rebates and FIX
order management are advertised on the KYC'd CeFi firm path. If that reading
holds, a passive market-making strategy is not expressible on the venue surface
this account can reach, and the adapter therefore reports a venue-level NO-GO
rather than per-pair scorecards.

This is not a collection, data, strategy, or profitability claim. No data has
been collected, no pair has been studied, no policy rules have been fitted, and
live collection is blocked pending the confirmations listed in
[`docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md).
Detailed design is in
[`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`](docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md).

### Version preservation

- V1 remains recoverable at annotated tag `v1.0.0`.
- Existing V1.1 branches and phase tags remain unchanged.
- V2 begins on branch `v2/architecture-contracts` from the documented V1.1
  storage-design checkpoint.
- V2 will use new wire/storage versions and will never reinterpret V1/V1.1 WAL
  data in place.
- `v2.0.0` will be created only after the release-wide acceptance gates pass.

## [V1.1] - Unreleased

Status: in progress. Stages 1 and 2 are accepted on Raspberry Pi ARM64; Stages
3 and 4 remain pending. No planned item is an implementation claim until it is
moved under **Implemented changes** and supported by tests and target-hardware
evidence.

The authoritative execution specification for this release is
[`docs/V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](docs/V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md).
It defines the phase objectives, wire/routing design, invariants, canonical
retry-amplification metric, experiment scorecard, and pass/fail gates. This
changelog remains the concise release record; the plan is the implementation
contract.

### Objective

Eliminate the leader-routing retry storm exposed by V1 benchmarking, protect
Raft control-plane progress from client load, and establish a repeatable safe
operating envelope before assigning a throughput number.

### Why V1.1 is required

V1 clients receive `not_leader` without a leader identity, rotate blindly to
the next request channel, and retry immediately. Under unstable leadership,
this turns a small logical workload into a large internal workload. The node
owner loop also polls as many as ten client fragments before peer traffic, and
that count is not a time budget because a proposal may perform synchronous WAL
work.

Representative V1 benchmark evidence showed:

- at 20 requests/s, one run scheduled 1,400 total commands, completed 3, timed
  out 1,397, received 63,426 `not_leader` responses, and made 64,826 successful
  Aeron offers: 46.3 successful transport attempts per logical command;
- at 35 requests/s, one run scheduled 2,450 total commands, completed 1,548,
  timed out 902, received 371,289 `not_leader` responses, and made 373,739
  successful Aeron offers: 152.5 attempts per logical command; and
- every Aeron offer-failure and backpressure counter in those two runs was
  zero, so the dominant failure was routing/retry amplification rather than
  Aeron rejecting publication offers.

### Planned implementation stages

#### Stage 1 - Versioned leader-aware routing

- Add `leader_id` to `not_leader` responses, using `-1` when the node does not
  know a leader.
- Introduce a backward-compatible response wire version and tests for legacy,
  leader-aware, malformed, and truncated responses.
- Map configured Raft node IDs to Aeron request publications.
- Cache leader hints with their observed term and ignore stale or non-member
  hints.
- Retry the same request ID directly against the hinted leader rather than
  rotating blindly.

#### Stage 2 - Bounded recovery and admission control

- Add deadline-bounded exponential retry backoff with jitter when no leader is
  known.
- Add explicit `leader_known`, `leader_suspect`, and `leader_unknown` client
  states.
- Stop admitting unbounded new work during a leaderless interval; fail fast or
  use a small time-bounded queue while allowing bounded discovery probes.
- Preserve open-loop benchmark scheduling and record leaderless admission
  rejection instead of pausing the generator and hiding outages.

#### Stage 3 - Owner-loop control-plane priority

- Service overdue Raft ticks and bounded peer traffic before client ingress.
- Flush vote, heartbeat, replication, and acknowledgement work before admitting
  more proposals.
- Bound client processing by fragment count and time/tick slack rather than
  relying only on `poll(..., 10)`.
- Bound peer and response drains so priority cannot become starvation.
- Preserve the single-owner Raft execution model; do not introduce concurrent
  mutation of the Raft server.

#### Stage 4 - Measurement and release proof

- Export raw logical-command, transport-attempt, retry, `not_leader`,
  leaderless-admission, completion, and timeout counters.
- Calculate retry amplification outside the hot path:

  ```text
  retry_amplification = transport_attempts / logical_commands_started
  ```

- Add Grafana panels and alerts for amplification, leader knowledge, admission
  rejection, term/leader churn, owner-loop tick age, and completion rate.
- Repeat the same target-hardware load matrix with fresh state per repetition,
  exact sample merging, stable power, recorded temperature/governor, and no
  unrelated workload.

### Planned new metrics

The final names may change during schema review, but their meanings must not:

- `velocita_client_logical_commands_started_total`
- `velocita_client_transport_attempts_total`
- `velocita_client_retries_total`
- `velocita_client_not_leader_responses_total`
- `velocita_client_admission_rejected_total{reason="leader_unknown"}`
- `velocita_client_completed_commands_total`
- `velocita_client_timed_out_commands_total`
- `velocita_client_leader_known`
- `velocita_client_retry_amplification` as an off-path recording rule, not a
  division performed by the client owner loop

Request IDs, arbitrary error text, and suite IDs must not become metric labels.
They belong in bounded evidence files to avoid unbounded Prometheus cardinality.

### V1.1 release acceptance gates

- Stable-leader retry amplification is approximately 1.00 and no greater than
  1.05 over the declared healthy operating range.
- A request initially routed to a healthy follower normally reaches the leader
  in no more than two transport attempts.
- Leader loss produces bounded retries and bounded admission rejection, not a
  retry burst that grows with event-loop speed.
- A leaderless interval does not cause unbounded pending work or queue growth.
- No peer messages are dropped and no recurring term changes occur under the
  declared healthy load range.
- Every scheduled benchmark command is accounted for as completion, timeout,
  admission rejection, capacity rejection, or explicit protocol/application
  error.
- Same-request-ID retry still applies an increment at most once after an
  unknown outcome.
- Three voters converge after follower restart, leader failover, quorum
  restoration, and full supervised restart.
- Results are repeatable across at least three independent repetitions at each
  claimed rate before a capacity or latency statement is published.

### Implemented changes

#### Stage 1 accepted - leader-aware routing

- Added response wire version 3 with explicit extension flags, `leader_id`, and
  term-aware routing information. The decoder continues to accept V1 production
  and V2 benchmark responses and represents their absent leader as unknown.
- Added validated `RequestTarget {node_id, channel}` configuration. Duplicate
  IDs, duplicate channels, missing member mappings, and non-member mappings fail
  before Aeron publications are created.
- Added a pure C++ leader router that caches `(term, leader_id)`, ignores
  older-term hints, rejects non-member hints, invalidates knowledge on a newer
  unknown/invalid term, and redirects the same request ID to a valid leader.
- Changed `raft-client` and `raft-bench` from anonymous `--request-channel`
  lists to explicit `--members` plus repeatable `--request-target ID=URI`
  mappings.
- Changed the one-shot client from broadcasting every retry to all voters to
  offering to one target and following the returned leader hint.
- Added exact benchmark counters for admitted logical commands and all
  transport attempts. Summary schema 2 and Prometheus evidence now report
  retry amplification without adding work to the Raft node owner loop.

Phase 1 passed its Raspberry Pi ARM64 acceptance gate on 2026-08-27.

#### Stage 2 accepted - bounded recovery and admission control

- Added explicit client leader-knowledge states: `unknown`, `suspect`, and
  `known`. A valid redirect is suspect until an `ok` response confirms the
  leader for the observed term.
- Added a pure monotonic recovery policy with deterministic exponential jitter
  between a positive 5 ms floor and a 250 ms cap. A retry never runs at or
  after its command deadline.
- Added a 250 ms response-silence deadline because successful local Aeron
  publication does not prove remote receipt, quorum commit, or response. A
  silent route becomes unknown and discovery resumes with the same request ID.
- Added an unconfirmed-leadership admission limit of one command and a globally
  paced discovery probe. Further open-loop demand is recorded as explicit
  leader-unknown admission rejection rather than hidden by pausing the load
  generator.
- Extended pending-command state with last target and next retry time while
  retaining the original request ID for every transport attempt.
- Added benchmark summary schema 3 evidence for discovery, admission rejection,
  retry counts, pending high-watermark, leader state, and exactly-one terminal
  accounting. Failure rows now retain attempts, last target, and leader state.
- Added a one-command Pi recovery runner covering leader failover under demand,
  an explicit leaderless/quorum-loss window, bounded recovery, and same-ID
  exactly-once resolution.

The candidate passed production and benchmark-instrumented native tests and a
15-command three-node macOS smoke run at retry amplification `1.00`.

A local leader-removal diagnostic remained bounded—40 of 40 terminal outcomes,
pending high-watermark 2, 58 attempts for 12 admitted commands—but the two
remaining voters did not elect before the eight-second window ended. It
therefore produced 28 explicit admission rejections, 3 timeouts, amplification
`4.83`, and final leader state unknown. This is retained as failed recovery
evidence, not presented as a pass or throughput result.

Phase 2 passed its Raspberry Pi ARM64 acceptance gate on 2026-08-28. The
production image was `015581814255` with benchmark instrumentation disabled;
the diagnostic image was `6cab4df1978b` with instrumentation enabled. A
follower-first/leader-last rolling upgrade preserved quorum, moved leadership
from node 3 in term 89 to node 2 in term 90, and converged all three voters at
log, commit, and applied index 39 with counter value 20.

Recovery suite `20260828T180907Z` produced the following immutable evidence:

- leader failover under 5 commands/s scheduled 75 commands, completed 70,
  rejected 5 during unknown leadership, used 86 transport attempts, bounded
  pending depth at 2, recovered with zero retries in the stable tail, and
  measured retry amplification `1.229`;
- explicit quorum loss scheduled 100 commands, completed 67, rejected 30
  during unknown leadership, used 117 transport attempts and 43 discovery
  probes, bounded pending depth at 2, recovered with zero retries in the stable
  tail, and measured retry amplification `1.671`; and
- unknown-outcome recovery retried request ID `1787940625718131511` after
  quorum restoration and changed the counter exactly once from 20 to 21 while
  the commit index remained held at 208 during the no-quorum check.

Suite `20260828T180440Z` is retained as a test-harness incident, not a product
failure: Ubuntu AWK rejected the convergence observer's `log[...]` array name
because `log()` is built in. The observer therefore returned a false negative
before comparing voter state. Commit `b5d530d` renamed the array to
`current_index`, after which the complete target-hardware gate passed.

#### Stage 3 candidate - owner-loop control-plane priority

- Added an allocation-free `OwnerLoopScheduler` using
  `std::chrono::steady_clock` time points, a one-millisecond Raft tick interval,
  a 250-microsecond client safety margin, and named positive work/queue bounds.
- Changed Raft outbound and completion transfer from whole-deque temporary
  vectors to one-message `std::optional` moves so the owner loop, rather than
  queue size, controls work per iteration without an additional drain-vector
  allocation.
- Reordered the node loop to service overdue ticks, bounded peer ingress, Raft
  output, and peer publication before polling at most one client fragment when
  sufficient tick slack remains. A second bounded peer flush follows client
  admission before bounded response publication.
- Shared pre/post-client drain and peer-offer budgets across the complete
  iteration, preventing two service points from silently doubling the stated
  limits.
- Added a statically dispatched, template-based iteration coordinator and fake
  clock/poller tests that prove tick/peer/client ordering, tick-slack
  suppression, the one-client-fragment limit, and cumulative work budgets.
- Added fixed peer and response queue capacities and stopped client polling
  while the external response queue is full. The existing peer-drop behavior
  remains visible through its counter; committed responses are retained until
  bounded drain capacity is available.
- Advanced the fixed shared-memory metrics page to version 2 and added
  owner-loop iteration count, maximum tick gap, full client/peer poll-budget
  counts, and client-poll skip reasons. Plain owner-thread counters are copied
  every 100 ms; the external exporter performs text rendering and unit
  conversion.

Both normal and benchmark-instrumented builds pass all three native test
executables. A three-process macOS loopback smoke run elected node 1, completed
25 of 25 measured commands at 5 commands/s with 25 attempts, zero timeouts,
retry amplification `1.00`, and p99 `11,719,084 ns`. These are development
results only. A second 50 commands/s development run completed 250 of 250
measured commands with 250 attempts, zero timeouts, amplification `1.00`, p99
`11,921,000 ns`, stable term 1, and no response-backlog polling blocks. Maximum
observed tick gaps were `2,978,375 ns`, `4,744,666 ns`, and `3,598,917 ns` for
nodes 1--3. The processes were stopped immediately after measurement, so their
final one-entry follower lag is not convergence evidence. Deterministic
ordering and configured-budget tests now pass in both build modes. Phase 3
remains a candidate until sustained fairness, overload behavior, follower
catch-up, and the Raspberry Pi acceptance matrix pass.

Raspberry Pi run `20260829T120112Z` was retained as exploratory evidence only
because `vcgencmd get_throttled` was already `0x50000` before measurement. A
clean-boot repeat, `20260829T124828Z`, held `performance` governor and remained
at `throttled=0x0`, but failed the Phase 3 acceptance gate:

- at 5 commands/s, 75 measured schedules produced 50 completions and 25
  leader-unknown admission rejections, with amplification `2.68`, p50
  `395,937,951 ns`, and p99 `828,406,854 ns`;
- at 20 commands/s, 300 schedules produced 64 completions, 230 admission
  rejections, and 6 timeouts, with amplification `5.086`, p50
  `1,041,311,701 ns`, and p99 `1,973,153,543 ns`;
- the 5/s voters stayed converged in term 1, but the 20/s voters ended
  leaderless in terms 7--9, with maximum owner-loop gaps between 1.068 and
  1.471 seconds; and
- post-run Linux pressure-stall information reported CPU `some avg10=0.02`
  versus I/O `full avg10=75.14` and `full avg300=57.25`. Memory remained
  available, no swap was configured, storage integrity checks passed, and the
  kernel reported no current-boot undervoltage or filesystem I/O error.

The state machine remained fast (`commit_to_apply` p99 below 12 us); the delay
was before commit. Code inspection explains the physical amplification: every
log offer opens, appends, `fsync`s, and closes the WAL, while every commit-index
advance rewrites and `fsync`s a metadata temporary file, renames it, and
`fsync`s the parent directory. Three logical voters share one SD card, so one
logical command can impose approximately nine durability barriers. The
control-plane-first scheduler cannot preempt a blocking durability syscall.
Phase 3 therefore remains unaccepted pending an explicit ordered-persistence
design and storage-isolation experiments; increasing retry deadlines alone is
not an accepted fix.

The resulting design checkpoint evaluates a fixed-capacity per-voter block
arena based on the book's memory-mapped ring and double-buffered snapshot
patterns. The accepted direction is bounded memory staging plus asynchronous
ordered batch persistence, not memory-only acknowledgment. It requires an
explicit durable-index completion path because the pinned `willemt/raft`
callbacks require `log_offer`, term, and vote changes to be flushed before they
return. Snapshot and compaction may recycle a WAL prefix only after a
checksummed replacement snapshot is durable. The full proposal, rejected
shortcuts, experiments, and release gates are recorded in
`docs/V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md`.

### V1.1 validation results

Local development evidence on 2026-08-27:

- production build: all three native test executables passed;
- benchmark-instrumented build: all three native test executables passed;
- three-node macOS loopback: an initial request found leader 3 in two attempts;
- forced follower-first route: follower 1 redirected the same request ID to
  leader 3 and the increment completed in exactly two attempts; and
- five-second measured loopback benchmark: 25 logical commands started, 25
  transport attempts, 25 completions, zero timeouts, zero invalid hints, and
  retry amplification `1.00`. One discovery retry occurred during warm-up.

These results validate the development implementation and test harness. They do
not substitute for target-hardware validation.

Raspberry Pi ARM64 acceptance evidence on 2026-08-27:

- the production candidate image `e2b3cb3c5c10` and benchmark-instrumented
  candidate image `53913e4eeec0` each built with all three tests passing;
- a follower-first/leader-last rolling upgrade preserved quorum, caught both
  followers up before leader replacement, and restored three active services;
- the fully upgraded cluster elected node 3 in term 81 and all three voters
  ran the production candidate image;
- request ID `1787831360569930796` reached follower 1 and then leader 3 in two
  attempts, incrementing the counter from 19 to 20 at log index 35;
- repeating that exact request ID returned value 20 and index 35 in two
  attempts, demonstrating at-most-once application and no duplicate log
  admission; and
- benchmark suite `20260827T115818Z` completed 75 of 75 measured commands with
  75 transport attempts, zero timeouts, zero capacity rejections, zero invalid
  leader hints, retry amplification `1.00`, p50 `46,016,965 ns`, and p99
  `101,675,703 ns`.

This closes the Phase 1 gate. It validates routing correctness and efficiency
at the gate load; it is not a certified maximum-throughput claim.

## [V1] - 2026-08-25

Status: functionally accepted for a three-process educational cluster on one
Raspberry Pi 5. Capacity not certified.

### Implemented

#### Native Raft service

- Implemented the service, client, storage, wire protocol, dump tool,
  benchmark driver, and exporters in C++20 with no Java runtime in the final
  image.
- Integrated pinned native Aeron and `willemt/raft` revisions.
- Ran an embedded Aeron C media driver per process and used Aeron UDP for client
  requests, responses, `RequestVote`, and `AppendEntries` traffic.
- Supported one voter or three statically configured logical voters.
- Kept Raft mutation under a single owner thread.

#### Replicated state and correctness

- Added a deterministic replicated counter FSM supporting `increment` and
  replicated `get` commands.
- Replicated stable request IDs and retained FSM results for request
  deduplication.
- Preserved the same request ID across client retries so an unknown-outcome
  command could be resolved without applying it twice.
- Returned responses only after committed entries were applied to the FSM.

#### Persistence and recovery

- Added an append-only, versioned WAL with little-endian encoding, record
  lengths, and CRC32 integrity checks.
- Persisted term, vote, and commit metadata through atomic file replacement.
- Replayed committed state and deduplication results on restart.
- Added `raft-dump` for offline inspection of metadata and log records.
- Failed startup closed on corrupt or torn records rather than silently
  accepting ambiguous durable state.

#### Packaging and service management

- Built a multi-stage ARM64 Podman image that runs tests during the builder
  stage and uses an unprivileged runtime UID.
- Ran each voter in a separate rootless container with its own Aeron shared
  memory and durable named volume.
- Added rootless Quadlet units and a systemd user target for boot management.
- Enabled bounded `on-failure` restart policy and rate limiting so persistent
  faults become visible instead of producing infinite crash loops.
- Used `loginctl` linger so the rootless user service manager starts at boot and
  remains available without an interactive SSH session.

#### Observability

- Added a fixed-layout, versioned shared-memory metrics page per voter.
- Kept Prometheus formatting and HTTP serving in a separate native exporter.
- Exported Raft role, leader, term, log/commit/apply indexes, FSM value, owner
  tick age, Aeron fragments/bytes, publication outcomes, queue depths, and
  observed leadership/term changes.
- Added external host, process, filesystem, microSD, memory, CPU, and Raspberry
  Pi thermal metrics.
- Installed bounded local Prometheus and Grafana services with provisioned
  recording rules, alerts, cluster-health panels, and benchmark panels.
- Verified that stopping observability components does not stop Raft.

#### Operations and testing

- Added a Pi operator runbook, repeatable cluster lifecycle scripts, and a
  bounded all-in-one failure-drill runner with exit-trap restoration.
- Added native core, shared-metrics, and benchmark tests.
- Added an open-loop benchmark driver with intended send times to prevent
  coordinated omission.
- Recorded exact warmup and measured samples, explicit failures, fixed-bucket
  histograms, p50 through p99.99, maximum, counts, and benchmark-only ownership
  timings.
- Reset benchmark cluster state for every repetition and merged exact samples
  rather than averaging percentiles.
- Kept benchmark-only timestamp instrumentation out of the production image.

### What V1 did well

The bounded failure-drill suite on 2026-08-22 passed every automated scenario:

| Scenario | Result | Evidence |
|---|---|---|
| Baseline | PASS | Three voters converged; counter 17; node 2 leader |
| Supervised crash restart | PASS | Node 1 PID changed 1618 to 2610; restart count increased 0 to 1 |
| Leader failover | PASS | Leadership moved from node 2 to node 1 in 1,522 ms |
| Quorum loss | PASS | Commit remained 24 with one voter; same-ID retry resolved once; counter 18 to 19 |
| Observability isolation | PASS | Raft remained available while exporter, Grafana, and Prometheus were stopped independently |
| Exporter-down alert | PASS | Alert fired 18,584 ms after exporter shutdown |
| Final recovery | PASS | Three voters converged; counter 19; node 1 leader |

These results prove process-level restart, majority failover, quorum safety,
same-ID deduplication after an unknown outcome, observer isolation, and follower
catch-up. They do not prove physical high availability because all voters share
one Pi, kernel, power supply, network interface, and microSD card.

Low-rate smoke runs also proved that the complete client-to-quorum-to-apply path
could execute without timeouts. One 15-second run completed all scheduled work
at 1, 3, and 5 requests/s, with measured p99 end-to-end latency of 64.6 ms,
98.1 ms, and 147.2 ms respectively. These are experiment observations, not
release latency objectives.

For one successful 5 requests/s run, benchmark-only attribution reported:

| Stage | p99 |
|---|---:|
| Client offer to matching response | 147.2 ms |
| Local WAL append | 45.7 ms |
| Quorum append | 56.1 ms |
| Admission to commit | 143.1 ms |
| Commit to apply | 6.0 us |

The spans overlap and must not be added. They showed that FSM application was
cheap while persistence, quorum progress, and scheduling dominated the
observed latency.

### What failed or remained unproven

#### Retry amplification and unstable progress

The load driver exposed a liveness failure mode rather than a clean throughput
knee. A short rebaseline run completed all measured commands at 5, 10, 20, and
30 requests/s before timing out work at 40 requests/s, but a later three-repeat
sweep was highly non-monotonic:

| Offered rate | Measured scheduled | Measured completed | Measured timed out |
|---:|---:|---:|---:|
| 20/s | 3,600 | 0 | 3,600 |
| 25/s | 4,500 | 47 | 4,453 |
| 30/s | 5,400 | 322 | 5,078 |
| 35/s | 6,300 | 1,248 | 5,052 |
| 40/s | 7,200 | 687 | 6,513 |

Higher offered rate sometimes completed more work than a lower rate. That is
not a valid capacity curve; it is evidence that leadership state and retry
feedback dominated the experiment.

The client protocol did not include a leader hint. On `not_leader`, the driver
rotated to the next node and retried immediately with no backoff. This created a
positive feedback loop: rejection caused retry traffic, retry traffic consumed
owner-loop time, delayed consensus progress caused more rejection, and the
cycle repeated.

The node owner loop ran an overdue Raft tick first but then polled up to ten
client fragments before peer traffic. Because a proposal can include
synchronous WAL work, a fragment-count limit was not a meaningful time budget.
V1 therefore lacked protected scheduling for heartbeat, election, and
replication work.

Consequently, V1 has no certified sustainable throughput or latency SLO. The
correct conclusion is not “capacity is 5/s” or “capacity is 30/s”; the correct
conclusion is “repair routing, retry, admission, and scheduling behavior, then
repeat the experiment.”

#### Known product limits deferred beyond V1.1

- Three voters on one Pi are one physical failure domain, not high availability.
- Static membership only; no joint-consensus membership changes.
- No snapshot transfer or WAL compaction; the WAL grows indefinitely.
- Torn/corrupt final records fail safely but require manual operator recovery.
- Reads are replicated log entries rather than lease or ReadIndex operations.
- Synchronous persistence remains in the proposal path.
- No authenticated or encrypted Raft/client transport; endpoints are restricted
  to loopback for the V1 topology.
- No representative matching-engine, market-data, risk, or venue operation
  classes; the workload is still a replicated counter.

These are real limits, but they are intentionally not mixed into V1.1's retry
and liveness scope. A release should have one coherent risk-reduction objective
rather than becoming an unbounded rewrite.

### Benchmark and lab incidents separated from product findings

- An early benchmark readiness probe exhausted its small `/dev/shm` allocation;
  the harness was corrected to provide explicit Aeron shared-memory capacity.
- A benchmark client later exited with signal 135 (`SIGBUS`) when mapped Aeron
  term buffers exceeded its container shared-memory limit; the driver container
  was raised to 512 MiB.
- A sudden Pi power loss produced unclean journal warnings and a Podman overlay
  link warning. Subsequent `podman system check --quick` completed and the
  production cluster recovered, but this was an infrastructure incident, not a
  controlled Raft durability test.
- Kernel logs recorded historical undervoltage events. After changing the power
  source, `vcgencmd get_throttled` returned `0x0`; benchmark temperature rose
  from approximately 52.4 C to 60.1 C without thermal throttling.
- The harness now resets disposable benchmark volumes for each repetition,
  increases client shared memory, preserves immutable evidence, restores the
  production target through an exit trap, and stops disposable voters before
  capturing their final role/term/index status.

Keeping these incidents separate prevents infrastructure and harness defects
from being misreported as consensus-algorithm defects while still retaining
them as operational lessons.

### Recommended change for V1.1

V1.1 should implement the four staged changes documented in the unreleased
section: leader-aware routing; bounded retry and admission control;
control-plane-first owner-loop scheduling with bounded client work; and
off-path retry-amplification measurement followed by repeatable target-hardware
validation.

### Evidence references

- [`docs/V1_FAILURE_DRILL_RESULTS.md`](docs/V1_FAILURE_DRILL_RESULTS.md)
- [`docs/BENCHMARKING.md`](docs/BENCHMARKING.md)
- [`docs/PI_OPERATOR_RUNBOOK.md`](docs/PI_OPERATOR_RUNBOOK.md)
