# V2 TDD implementation and validation plan

Status: approved execution baseline; Phase 1 macOS and native ARM64 Ubuntu
container candidates implemented, with Raspberry Pi ARM64 acceptance pending.

Architecture authority:
[`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)

Detailed contracts:
[`V2 architecture contracts`](V2_ARCHITECTURE_CONTRACTS.md)

Current development branch: `v2/phase-1-domain-contracts`

Preserved baselines: annotated tag `v1.0.0`, V1.1 phase tags and branches, and
all accepted/failing evidence already recorded in `CHANGELOG.md`.

## 1. Objective

V2 will evolve the counter laboratory into a deterministic trading decision
and order-management platform. A Raft cluster will own policy activation,
decision evaluation, risk reservations, order intent, accepted venue evidence,
positions, deduplication, and an external-effect outbox. A logically single
active gateway will send committed effects and reconcile the venue.

The strategy is deliberately the final phase. The earlier phases build the
correctness envelope within which strategies may operate.

## 2. Test-driven development contract

The user requested TTD; this plan interprets it as **TDD: test-driven
development**. If TTD meant another method, amend this document before Phase 1.

Every behavior increment follows:

```text
RED       write the smallest test that expresses the next contract;
          run it and prove it fails for the intended reason

GREEN     implement the smallest bounded change that makes the test pass;
          run the focused test and relevant regression suite

REFACTOR  improve naming, ownership, structure, or cost without changing
          behavior; keep every test green

EVIDENCE  record command, artifact identity, raw counters, and limitations
```

Rules:

1. A test that was never observed failing is not accepted as proof that it
   guards the new behavior.
2. Production behavior is not implemented in the test body or exposed through
   test-only branches.
3. Tests assert domain outcomes and invariants, not private implementation
   details, except for explicit allocation/syscall/ABI contracts.
4. One commit should contain one coherent contract increment and its tests.
5. A refactor commit changes no golden wire/storage corpus unless accompanied
   by a reviewed schema migration.
6. A flaky timing test is a failed test design. Timing experiments need a
   measured envelope, retained raw evidence, and a correctness oracle.
7. Fault tests name the exact cut point: before commit, after commit, before
   send, after send, before acknowledgement, during snapshot, and so on.

## 3. CTO review template for every phase

Every phase review records:

1. **Objective:** the intended capability.
2. **Business reason:** the risk, availability, latency, audit, or delivery
   outcome it controls.
3. **Mechanism:** the types, threads, processes, protocols, and state
   transitions that implement it.
4. **Invariant:** what remains true during overload, crash, retry, and replay.
5. **Failure mode:** how the design can fail and how that failure appears.
6. **Tests:** which test was red first and which suites prove regression safety.
7. **Measurements:** raw counters, latency distributions, queue bounds, state
   digests, and recovery evidence.
8. **C++ review:** ownership/lifetime, value/reference/pointer semantics,
   `const`/`noexcept`, allocation, invalidation, concurrency, atomics, cache
   locality, syscalls, error propagation, and serialization.
9. **Rollback:** compatible code/data boundary and exact Git recovery point.
10. **Decision:** accepted, rejected, or retained as experimental evidence.

## 4. Test architecture

### 4.1 Compile-time and type tests

- strong IDs cannot be implicitly interchanged;
- money and quantity constructors require explicit scale/domain;
- wire structs are not serialized through object layout;
- bounded-capacity constants are positive and internally consistent; and
- command/result alternatives are exhaustively handled.

### 4.2 Pure unit tests

- checked fixed-point arithmetic, overflow, rounding, and scale conversion;
- wire encode/decode and malformed/truncated inputs;
- policy validation and deterministic predicate evaluation;
- order, reservation, position, deduplication, outbox, and reconciliation FSMs;
- rate limit, executor generation, kill epoch, and admission policy; and
- WAL record/block framing and checksum validation.

### 4.3 Golden corpus and deterministic replay

A version-controlled corpus contains canonical command bytes, expected response
bytes, expected outbox bytes, and expected state digest. Run it:

- twice in one process;
- across fresh processes with different allocator/process seeds;
- in normal and benchmark-instrumented builds;
- after snapshot plus suffix replay; and
- on macOS development and ARM64 Linux target builds.

### 4.4 Property and model-based tests

Generate bounded command/event sequences and compare production reducers with a
small reference model. Properties include:

- duplicate command does not duplicate an effect;
- duplicate fill does not duplicate position;
- reservation never becomes negative or releases twice;
- filled quantity never exceeds the reconciled order quantity without a typed
  conflict;
- one terminal outcome exists for every admitted command;
- replay state and outbox equal live state and outbox; and
- no risk-increasing effect exists without a committed reservation.

Use deterministic seeds and print the seed plus minimized failing sequence.

### 4.5 Venue simulator tests

The simulator can accept, reject, delay, drop, duplicate, reorder, partially
fill, fill-after-cancel, disconnect, rate-limit, and return inconsistent query
results. It supports crash cut points around the external send boundary.

### 4.6 Raft integration tests

- follower redirect and stable request identity;
- leader crash before and after intent commit;
- duplicate retry across leader change;
- quorum loss and restoration;
- snapshot install with dedup/outbox state;
- stale executor/kill generation rejection; and
- digest agreement across voters.

### 4.7 Storage crash tests

- writer stall and full bounded arena;
- disk full, short write, sync error, and torn record;
- power/process cut during stage, write, flush, completion, snapshot, and
  compaction;
- valid-prefix recovery and corrupt-suffix refusal/truncation policy; and
- no successful response beyond recovered quorum-durable state.

### 4.8 End-to-end release drills

- startup with no orders;
- startup while venue has an adopted open order;
- unknown submission after response loss;
- duplicate fill and cancel/fill race;
- stale market-data automatic stop;
- operator kill and mass cancel under normal-lane exhaustion;
- executor crash/restart and reconciliation;
- voter failure and leader change; and
- observability outage without hot-path impact.

## 5. Phase 0: architecture and repository checkpoint

### Objective

Freeze the expensive-to-reverse V2 contracts before changing production code.

### Deliverables

- ADR 0001;
- this TDD plan;
- architecture contracts;
- V2 changelog entry and README navigation;
- dedicated V2 Git branch; and
- confirmation that V1/V1.1 tags, branches, images, WAL formats, and evidence
  are not deleted or rewritten.

### Tests and validation

- Markdown link and whitespace validation;
- `git diff --check`;
- repository status contains only reviewed documentation;
- history graph shows V2 branching from the retained V1.1 checkpoint; and
- no source, test, deployment, or existing evidence file is removed.

### Gate

Architecture documents agree on authority, outbox, executor, identity,
durability, and implementation order. No document calls a proposal implemented.

## 6. Phase 1: domain types and versioned contracts

Implementation state: macOS ARM64 and native ARM64 Ubuntu container candidates
complete; Raspberry Pi target-hardware gate pending. See
[`V2 Phase 1 macOS candidate evidence`](evidence/V2_PHASE1_MACOS_CANDIDATE.md)
and
[`V2 Phase 1 ARM64 Linux container candidate evidence`](evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md).

### Objective

Create the C++ vocabulary and byte contracts that every later phase shares.

### Tests first

- strong ID cross-assignment fails to compile in a compile-fail fixture or is
  rejected by static assertions;
- fixed-point construction, comparison, checked add/subtract/multiply, scale
  mismatch, overflow, and boundary rounding;
- V2 envelope round trip for every initial command/result kind;
- malformed magic, version, flags, lengths, enum values, scale, and trailing
  bytes fail closed;
- golden byte order and exact canonical corpus; and
- old V1/V1.1 decoder/tests continue passing where compatibility is promised.

### Implementation scope

- `StrongId<Tag, Rep>` or explicit wrappers with no implicit cross-domain
  conversion;
- checked fixed-point `PriceTicks`, `QuantityLots`, and money/fee types;
- schema/version/error enums;
- bounded common envelope and explicit endian codec;
- policy, decision-frame, order-effect, and venue-observation value types; and
- digest/checksum interfaces with no wire dependence on C++ layout.

### Metrics/evidence

- encode/decode duration distribution in a benchmark harness;
- maximum encoded sizes and allocation counts; and
- golden corpus hash.

### Gate

All invalid input classes fail without state mutation; canonical bytes and
fixed-point results match on macOS and ARM64.

### Rollback

No V1 wire or storage writer changes. Revert the Phase 1 commit or switch to a
preserved V1/V1.1 branch.

## 7. Phase 2: pure deterministic Decision/OMS kernel

Implementation state: policy-lifecycle/request-dedup and monotonic
control-authority increments are green on macOS ARM64 and native ARM64
Ubuntu/GCC. Strict normalized source admission is host-green and awaiting its
committed ARM64 gate. Behavior commits are `7bc9e42` and `6de06ec`; the latest
completed ARM64 gate commit is `2849a52`. Phase 2 remains incomplete and stays
on `experiment/v2-phase2-kernel` until Phase 1 Raspberry Pi acceptance. See
[`V2 Phase 2 policy-kernel experiment evidence`](evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md)
and
[`V2 Phase 2 source-admission experiment evidence`](evidence/V2_PHASE2_SOURCE_ADMISSION_EXPERIMENT.md).

### Objective

Build the authority as a transport-free, storage-free reducer library.

### Tests first

- publish/validate/activate/supersede policy lifecycle;
- strategy modes and monotonic kill/executor generations;
- request dedup returns the byte-equivalent retained result;
- accepted and conflicting duplicate source events;
- order lifecycle including partial fill, cancel/fill race, replace, unknown,
  and reconciliation;
- reservation conservation and no double release;
- position changes only for accepted unique fill/correction evidence;
- snapshot encode/decode returns the same state digest; and
- random bounded command sequences match a simple reference model.

### Implementation scope

- `TradingState` partitioned into bounded policy, strategy, order, reservation,
  position, receipt, outbox, and reconciliation regions;
- immutable `Command` alternatives and typed `ApplyResult`;
- deterministic state digest;
- snapshot schema independent of object memory;
- preallocated apply scratch; and
- no external side effects.

### C++ design gate

- one mutable owner and no shared mutable domain state;
- no borrowed pointer retained after apply;
- documented iterator/reference invalidation for every bounded container;
- normal business rejection uses result types rather than exceptions;
- any remaining allocation has a proven startup/reserve bound; and
- `noexcept` reflects real reachable behavior, not an aspiration.

### Metrics/evidence

- apply duration by bounded command kind;
- state/outbox sizes and capacity high-watermarks;
- allocation count after startup; and
- corpus replay digest.

### Gate

Live and replay reducers produce byte-equivalent result, state digest, and
outbox for the full corpus and generated sequences.

## 8. Phase 3: policy artifacts and authoritative decision evaluation

### Objective

Turn bounded market/model evidence into deterministic no-action, rejection,
cancel, replace, or submit directives.

### Tests first

- inactive/wrong policy and feature schema rejection;
- stale, recovering, sequence-regressed, malformed, and overflow frame
  rejection;
- identical state/frame/artifact produces identical directive;
- boundary values on every predicate and fixed-point comparison;
- prefilter proposal cannot bypass core evaluation;
- policy activation at an explicit sequence boundary;
- changed artifact digest cannot reuse an existing identity; and
- no-new-risk/cancel-only/kill modes dominate economic policy.

### Implementation scope

- immutable bounded `DecisionPolicyArtifact`;
- `DecisionFrame` validator;
- deterministic matrix/rule evaluator;
- reason-coded `DecisionDirective`;
- provenance linkage: policy, model, feature, risk, source, and state versions;
- deadband and action-suppression semantics; and
- shadow mode that records decisions without emitting risk-increasing effects.

### Metrics/evidence

- frames proposed/accepted/rejected/no-action by bounded reason;
- evaluation duration and rules visited;
- policy activation and stale-frame counts; and
- deterministic decision corpus hash.

### Gate

Every directive can be reproduced from retained command/artifact/state evidence,
and shadow mode cannot create an outbox effect.

## 9. Phase 4: risk reservation, orders, and transactional outbox

### Objective

Make an accepted decision atomically reserve risk, create order intent, retain
deduplication, and create a committed external effect.

### Tests first

- no effect without reservation and order intent;
- duplicate decision/request produces the retained effect ID, not another
  effect;
- insufficient/stale risk creates no order or effect;
- partial fill conserves reserved/filled/remaining quantities;
- unknown submission retains reservation;
- cancel-pending remains risk-bearing;
- replacement cannot double reserve old and new exposure incorrectly;
- replay reconstructs byte-identical pending/unknown effects; and
- snapshot retains every receipt and effect needed after retry horizon.

### Implementation scope

- risk limit and reservation ledgers;
- cluster order identity and lifecycle reducer;
- fill/event deduplication;
- transactional outbox stored inside `TradingState`;
- deterministic effect identity and payload digest; and
- bounded capacity/full policies for orders, receipts, and effects.

### Metrics/evidence

- reservation balances and conservation checks;
- orders/effects by state;
- oldest pending/unknown effect age;
- dedup hits/conflicts; and
- decision-to-effect apply latency.

### Gate

Fault/property tests cannot produce an unreserved effect, duplicate economic
mutation, negative reservation, or replay mismatch.

## 10. Phase 5: venue simulator and execution gateway

### Objective

Implement external-effect delivery and reconciliation without any live venue.

### Tests first

- uncommitted, stale-generation, stale-kill, malformed, expired, unsupported,
  and over-quota effects are not sent;
- success, venue duplicate, reject, pre-send failure, and post-send timeout are
  classified distinctly;
- crash before send leaves pending work;
- crash after acceptance and before response produces unknown and reconciles;
- unknown is never blindly retried;
- duplicate fill is one cluster mutation;
- cancel/fill race preserves residual accounting;
- emergency lane remains available when normal quota is exhausted; and
- gateway restart reconciles before new risk.

### Implementation scope

- venue-neutral gateway interface;
- single event-loop ownership of sessions and ID maps;
- bounded ingress/egress and emergency lanes;
- rate-limit/token-bucket policy;
- stable effect/client identity mapping;
- simulator transport and query API;
- canonical venue-observation encoder; and
- raw evidence archive hooks outside the authority path.

### Metrics/evidence

- logical effects and transport attempts;
- external retry amplification;
- effect state and age;
- gateway queue depths/high-watermarks;
- quota utilization and reserved emergency capacity;
- unknown/reconciliation count, outcome, and duration; and
- simulator fault seed and trace.

### Gate

The full simulator fault matrix completes with no duplicate venue order, no
lost accepted fill, and no unresolved state presented as healthy.

## 11. Phase 6: generic Raft Decision/OMS integration

### Objective

Place the tested reducer behind the existing leader-aware Raft command path
without coupling domain logic to the transport or Raft library.

### Tests first

- only committed commands invoke the reducer;
- follower routing preserves command identity;
- leader crash before commit creates no outbox effect;
- leader crash after commit reproduces the same effect;
- retry after lost response returns retained result;
- quorum loss creates no new risk decision;
- voter restart and snapshot install preserve state/outbox/dedup; and
- all voters converge on state digest, commit, apply, and outbox watermark.

### Implementation scope

- versioned generic V2 command/result envelope;
- reducer adapter replacing the counter-only coupling while retaining the V1
  implementation on preserved branches/tags;
- committed-outbox subscription/read contract;
- snapshot integration; and
- leader/read-index discipline for authority-sensitive reads.

### Metrics/evidence

- propose/commit/apply/result latency;
- durable/commit/apply index and state digest per voter;
- command and result dedup counts;
- outbox visibility index; and
- leader/term/retry amplification.

### Gate

All deterministic tests pass unchanged through one and three voters, including
leader/crash/snapshot recovery. Performance claims remain prohibited until the
V2 storage phase passes.

## 12. Phase 7: bounded asynchronous durable WAL and snapshots

### Objective

Remove synchronous storage blocking from the owner loop while preserving the
quorum-durable success contract.

### Tests first

- block-state and generation transitions;
- no record straddles a block; CRC/length detect torn data;
- release/acquire publication exposes fully initialized bytes;
- old completion cannot release a reused generation;
- local durable index advances only after successful flush;
- follower success never covers merely staged entries;
- commit never exceeds quorum-durable match under the declared protocol;
- writer stall causes bounded admission refusal rather than overwrite;
- hard-state two-slot recovery chooses newest valid sequence;
- snapshot durability precedes WAL-prefix reclamation; and
- power-cut matrix recovers no less than the last acknowledged committed state.

### Implementation scope

- fixed-capacity `WalBlockArena` and SPSC descriptor rings;
- preopened/preallocated segment writer using `pwrite`/`pwritev` plus
  `fdatasync` initially;
- explicit persistence completion integration in a pinned Raft fork/adapter;
- bounded space/time flush triggers;
- durable-index and storage-backpressure metrics;
- checksummed versioned snapshots and compaction; and
- new V2 storage magic/path with no in-place reinterpretation of V1 WAL files.

### Metrics/evidence

- stage, seal, queue, write, sync, and completion latency distributions;
- blocks by state and high-watermark;
- memory-appended to local-durable lag;
- quorum-durable to commit/apply lag;
- owner-loop maximum gap and Raft tick age;
- disk I/O pressure and sync error counts; and
- power-cut recovered/acknowledged index comparison.

### Gate

Correctness and power-cut gates pass before throughput. Under the claimed
healthy range, storage does not cause recurring election churn, queues remain
bounded, and no acknowledged effect disappears after tested recovery.

## 13. Phase 8: execution authority, fencing, and emergency control

### Objective

Connect committed effects to one active gateway and prove old senders and
normal overload cannot bypass stop/cancel controls.

### Tests first

- stale executor generation and kill epoch reject queued new risk;
- gateway loss of authority stops new sends before the configured lease bound;
- standby cannot promote without fencing and reconciliation evidence;
- kill propagates independently of normal strategy traffic;
- mass-cancel uses reserved quota under normal-lane exhaustion;
- emergency actions cannot increase quantity or open a position;
- reset requires a new attributable command and matching reconciliation; and
- observability failure does not block kill or delivery.

### Implementation scope

- generation/kill validation at final send boundary;
- independent authenticated operator control path;
- fail-closed authority lease;
- emergency cancel/reconcile lane;
- explicit manual-promotion workflow until external fencing exists; and
- retained fencing and reconciliation evidence.

### Metrics/evidence

- generation/kill observation lag per sender;
- stale-effect rejects;
- lease freshness;
- emergency quota and cancel latency;
- promotion/reconciliation duration; and
- unknown open-order count during every transition.

### Gate

No tested zombie sender creates new risk, and the kill/mass-cancel drill remains
reachable during normal queue and quota exhaustion.

## 14. Phase 9: TrueMarkets adapter

### Objective

Implement the venue-specific boundary behind the proven gateway contract.

### Contract discovery before code

Record official venue behavior for:

- authentication and clock tolerance;
- order/client identity and idempotency;
- create, cancel, replace, status, open-order, fill, position, and balance APIs;
- post-only and self-trade prevention semantics;
- order and market-data sequence guarantees;
- rate-limit units and retry instructions;
- cancel-on-disconnect/mass-cancel support;
- minimum price/quantity/notional and rounding; and
- query consistency/pagination watermarks used to prove absence.

Unsupported or undocumented capabilities remain `unsupported` or `unknown`;
they are not inferred from another TrueMarkets API tier.

### Tests first

- official example/captured-frame codec and authentication fixtures with
  secrets redacted;
- symbol/tick/quantity conversion boundaries;
- every documented response/error maps to a typed canonical outcome;
- ambiguous submission follows query/reconciliation policy;
- API rate limits preserve emergency capacity;
- reconnect rebuilds market-data and order state before new risk; and
- simulated and controlled UAT lifecycle matches the generic gateway contract.

### Gate

One complete controlled order lifecycle—intent, commit, send, acknowledgement,
open, cancel/fill, ledger update, reconciliation, restart, and report—is
explainable by stable identities and retained evidence. No strategy is enabled.

## 15. Phase 10: observability, replay, operations, and release proof

### Objective

Make correctness, degradation, kill, recovery, and rollback operable without
placing metrics on the hot path.

### Tests first

- metric schema/golden exposition and bounded labels;
- exporter/Prometheus/Grafana outage isolation;
- state/outbox/reconciliation dashboard queries;
- alert rule unit tests;
- delete-and-rebuild read models;
- operator commands against healthy, degraded, and unknown states; and
- automated end-to-end failure suite with immutable evidence directory.

### Required dashboards/alerts

- decision health and policy version;
- order lifecycle and unknowns;
- risk reservation versus venue exposure;
- outbox age and external retry amplification;
- reconciliation status and break age;
- executor generation, kill epoch, lease, and emergency capacity;
- Raft/storage durability and state-digest agreement; and
- host power, temperature, CPU, memory, and storage pressure.

### Gate

An operator can identify authority, stop risk, reconcile, replay, restore, and
roll back using documented commands. Observability outages do not change
decision or execution behavior.

## 16. Phase 11: strategy/model implementation

### Objective

Implement and evaluate market-making or other execution policies only after the
platform can explain, stop, reconcile, and recover.

### Tests first

- deterministic policy boundary and property tests;
- historical replay with no look-ahead;
- shadow decisions against live-like data;
- inventory, adverse-selection, stale-quote, fee, spread, and rate-limit stress;
- post-only/rejection behavior;
- loss and inventory breakers;
- restart/failover during an active quote lifecycle; and
- controlled-size live canary gates.

### Metrics/evidence

- filled maker/taker volume and fees;
- spread capture and short-horizon mark-outs;
- adverse-selection cost;
- inventory distribution and time to neutral;
- quote lifetime, fill probability, reject/cancel ratio;
- realized/unrealized PnL with reconciled fee and balance evidence; and
- risk/kill interventions and unexplained residuals.

### Gate

No strategy is promoted because it creates volume alone. It must stay inside
declared risk and message budgets, reconcile every order/fill/balance effect,
and pass controlled canary review. Profit or leaderboard placement is never a
software correctness guarantee.

## 17. V2 release-wide acceptance gates

V2 cannot be called accepted until:

- every risk-increasing venue effect references a committed directive index;
- replay reproduces the same state digest and pending outbox;
- duplicate requests/effects/fills do not duplicate economic state;
- unknown external effects reconcile before retry or remain visibly blocked;
- reservations, positions, orders, and venue evidence close without unexplained
  residuals in the complete drill suite;
- stale data, quorum loss, storage stall, executor loss, and kill all fail
  closed within their declared bounds;
- storage power-cut tests recover at least every acknowledged committed effect;
- no hot-path metrics rendering, network export, or unbounded allocation exists;
- three independent voters on independent machines pass one-machine-failure
  testing before host-fault-tolerance is claimed;
- one target-hardware benchmark matrix is repeatable across at least three
  independent repetitions per claimed operating point; and
- the changelog identifies exact commits, image IDs, configs, evidence paths,
  limitations, and rollback points.

## 18. Git, compatibility, and rollback policy

- Never delete or force-move `v1.0.0`, V1.1 phase tags, accepted branches, or
  evidence summaries.
- V2 work uses `v2/<phase-name>` branches and small test-plus-code commits.
- V2 wire and storage formats receive new version/magic identities.
- Never point a V2 binary at a V1/V1.1 WAL without a separately tested offline
  migration tool and immutable source backup.
- Production image tags are immutable milestone labels plus a movable local
  candidate alias only when the operator records both image IDs.
- A phase branch merges only after its acceptance evidence is committed.
- Tag `v2.0.0` only after the release-wide gates close; until then V2 remains
  proposed/candidate regardless of how much code exists.
- Prefer `git revert` for a bad accepted commit. Use a new recovery branch from
  an old tag for investigation; do not use destructive history rewriting.

Recommended branch progression:

```text
v2/architecture-contracts
v2/phase-1-domain-contracts
v2/phase-2-deterministic-kernel
v2/phase-3-policy-evaluator
v2/phase-4-risk-orders-outbox
v2/phase-5-venue-simulator-gateway
v2/phase-6-raft-integration
v2/phase-7-async-durable-storage
v2/phase-8-execution-fencing
v2/phase-9-truemarkets-adapter
v2/phase-10-operations-release-proof
v2/phase-11-strategy
```

Each phase starts from the last accepted V2 integration branch, not from an
unreviewed experiment. Experiments use `experiment/v2-*` branches and are not
silently merged into the release path.
