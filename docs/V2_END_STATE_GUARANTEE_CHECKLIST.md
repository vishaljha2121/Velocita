# V2 end-state architecture guarantee checklist

Status: north-star acceptance checklist

Authority:

- [`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)
- [`V2 TDD implementation and validation plan`](V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md)
- [`V2 architecture contracts`](V2_ARCHITECTURE_CONTRACTS.md)

This document converts the V2 design into stable, reviewable guarantees. It is
the checklist for detecting scope drift while completing Phases 4 through 11.
It does not replace the detailed contracts or phase gates.

## 1. How to use this checklist

- `[x]` means an accepted phase gate currently provides evidence for the item.
- `[ ]` means the end-state architecture requires it, but its owning phase has
  not yet been accepted.
- A guarantee becomes accepted only when its tests, target gate, source root,
  evidence, and rollback point are recorded.
- Passing unit tests alone does not promote an item to `[x]` when its claim
  includes Raft, storage, process failure, venue behavior, or physical hardware.
- Any proposed implementation that weakens an item requires an explicit ADR
  amendment. It must not be weakened silently to make a test pass.

The remaining critical path is:

```text
Phase 4  risk + orders + transactional outbox
Phase 5  simulator + execution gateway
Phase 6  Raft integration
Phase 7  durable storage
Phase 8  execution fencing and emergency controls
Phase 9  TrueMarkets adapter
Phase 10 operations, observability, and release proof
Phase 11 strategy/model validation and controlled canary
```

## 2. Decision authority and determinism

- [x] **G-001 — One ordered decision authority.** The deterministic Decision/OMS
  kernel, not the model, edge prefilter, dashboard, or executor, authorizes an
  economic action. Raft replication of that authority remains Phase 6 work.
- [x] **G-002 — Canonical causal input.** Every input that can change an
  authoritative result is committed in a bounded command or referenced by an
  immutable digest and version.
- [x] **G-003 — Deterministic evaluation.** Identical valid state and command
  bytes produce identical result bytes and next-state digest across machines,
  restarts, and replay.
- [x] **G-004 — Explicit policy identity.** Decisions name the exact feature
  schema, policy, template, model, and source identity required by the contract.
- [x] **G-005 — Deterministic rule precedence.** Bounded rules execute in the
  approved array order, with typed no-action and rejection outcomes.
- [x] **G-006 — Strict proposal resolution.** A rule selects a bounded action
  template; deterministic policy and safety caps resolve it into an economic
  proposal. The proposer cannot bypass those caps.
- [x] **G-007 — Pure apply boundary.** The authoritative reducer performs no
  venue call, network I/O, filesystem discovery, wall-clock read, random draw,
  or other nondeterministic external operation.
- [x] **G-008 — Bounded deterministic data structures.** Authoritative command,
  policy, rule, decision, receipt, and snapshot structures have explicit
  capacities, canonical encodings, size ceilings, and typed full policies.
- [x] **G-009 — Replay equivalence foundation.** Canonical replay uses the same
  reducer as live apply and can reproduce the accepted Phase 3 state and digest.

## 3. Risk, accounting, orders, and transactional outbox — Phase 4

- [x] **G-010 — Atomic authorization transaction.** An accepted economic
  decision creates its risk reservation, deduplication receipt, order entity,
  and outbound effect in one committed reducer transition—or creates none.
- [x] **G-011 — No unreserved effect.** No risk-increasing effect can exist
  without sufficient committed reservation and a corresponding order intent.
- [x] **G-012 — Layered safety caps.** Strategy/model intent is constrained by
  immutable executable-policy caps and authoritative dynamic risk/account state;
  the stricter effective constraint wins.
- [x] **G-013 — Checked economic arithmetic.** Price, quantity, notional, fees,
  position, and reservation arithmetic uses explicit units, scales, rounding,
  and checked overflow behavior.
- [x] **G-014 — Conservation.** Reserved, released, filled, cancelled, and open
  quantities cannot become negative, release twice, or exceed reconciled order
  quantity without a typed conflict.
- [x] **G-015 — Fill authority.** Only accepted fill evidence changes position;
  an acknowledgement or local send does not count as a fill.
- [x] **G-016 — Cancel remains risk-bearing.** Cancel-pending quantity retains
  risk until accepted cancel/fill evidence or reconciliation closes it.
  *Known operational gap (see G-074a).* Because a cancelled order enters
  `reconciling` and holds its reservation until `cancel_reconciled` final
  evidence arrives, an order whose final evidence never arrives holds its logical
  order slot indefinitely and that slot silently stops quoting. This is correct
  fail-closed behavior — unresolved risk must not be added to — but Phase 4
  provides no timeout, no escalation, and no operator override.
- [x] **G-017 — Unknown retains risk.** An ambiguously submitted order retains
  its reservation and blocks conflicting risk until authoritative reconciliation.
- [x] **G-018 — Safe serial replacement.** Phase 4 must prove cancel → final
  reconciliation → fresh authoritative decision → fresh authorization, not
  merely refusal of direct replacement. Native amend and overlapping replacement
  remain disabled (`RequestReplace` returns `replacement_not_supported`).
  At most one unresolved cluster order may exist per
  `(account, instrument, venue, logical order slot)`. Cancellation retains risk;
  late fills change position exactly once; accepted final reconciliation must
  resolve the old exposure before the slot can be reused. The new decision must
  pass current policy, control, risk, and position checks; a stale queued proposal
  must not be automatically submitted. Duplicate/reordered evidence, compaction,
  snapshot restore, and replay must preserve these rules and exact effect bytes.
  Missing final evidence keeps the slot blocked (G-074a); no timeout release.
  Reservation must neither under-cover executable exposure nor count the same
  exposure twice. If two orders could execute, covering both would be legitimate
  risk coverage, not erroneous double-counting; this MVP disables that overlap.
  A `max(old, new)` hold must not be assumed safe without proven mutual exclusion.
- [x] **G-019 — Capacity fails closed.** Full order, reservation, receipt, fill,
  or outbox capacity produces a typed refusal and no partial economic mutation.
- [x] **G-020 — Snapshot/replay completeness.** Orders, risk, fills, dedup
  receipts, outbox state, and their exact identities survive snapshot and replay.

## 4. Identity, idempotency, and outcome semantics — Phases 4–6

- [x] **G-021 — Stable identity chain.** Request, decision, order, effect, fill,
  and venue-event IDs are explicit strong types and remain traceable end to end.
- [x] **G-022 — At-most-once economic mutation.** Replaying the same request,
  effect result, fill, or venue event changes authoritative economics at most once.
- [x] **G-023 — Stable duplicate response.** A duplicate accepted request returns
  its retained result and EffectId without creating a second order or effect,
  *within the retention window*. Amended 2026-09-09 (architecture §8.6): request
  receipts are finite, so outside the window a duplicate returns the typed
  refusal `request_expired` and the caller reconciles against state instead.

  The amendment weakens the ANSWER, never the SAFETY. G-022 is preserved because
  an expired request is refused rather than re-executed, and because almost every
  command carries a second, state-based deduplication that survives receipt loss -
  an existing order for a `DecisionId`, a `(VenueId, ExecutionId)` fill identity,
  an `expected_risk_version`, or an order's own lifecycle state. Retaining every
  receipt forever was not a stronger guarantee; it was an unbounded arena that
  stopped the kernel permanently once full.
- [x] **G-024 — One terminal command outcome.** Every admitted logical command
  eventually has exactly one retained typed terminal outcome.
- [x] **G-025 — Delivery is not falsely called exactly-once.** External delivery
  is at least once by stable EffectId; safety comes from deduplication and
  reconciliation, not from assuming the network is transactional.
- [x] **G-026 — Timeout means unknown.** A post-send timeout is never treated as
  proof of rejection and is never followed by a blind risk-increasing retry.

  Phase 4 acceptance closes G-010 through G-024 for the deterministic reducer.
  G-024 retention is within the declared bounded request horizon; below its
  conservative caller prefix the only outcome is the non-mutating
  `request_expired` refusal. Phase 4 supplied G-025 and G-026 their stable effect
  identity and explicit unknown state but could not claim transport behaviour it
  had no gateway to execute; Phase 5's corrected acceptance closes both. See the
  note under G-034.

## 5. Execution gateway and venue simulator — Phase 5

- [x] **G-027 — Committed effects only.** The gateway can deliver only an effect
  read from committed and applied outbox state.
- [x] **G-028 — Executor cannot invent economics.** The gateway may validate,
  lower, encode, defer, reject, send, query, or reconcile; it may not originate
  or economically modify an order.
- [x] **G-029 — Final pre-send guard.** Immediately before external send, the
  gateway checks effect shape, policy constraints, expiry, venue capability,
  quota, executor generation, kill epoch, and current authority.
- [x] **G-030 — Typed transport outcome.** Venue success, venue duplicate,
  business rejection, pre-send failure, and ambiguous post-send failure remain
  distinct states.
- [x] **G-031 — Crash-cut safety.** Crashes before send leave a deliverable
  pending effect; crashes after a possible send produce unknown/reconcile state,
  not an assumed retry.
- [x] **G-032 — Reconciliation before risk.** Gateway restart or reconnect
  rebuilds open-order and relevant venue state before new risk is enabled.
- [x] **G-033 — Adversarial simulator proof.** The gateway is tested against
  delay, loss, duplication, reorder, partial fill, fill-after-cancel,
  disconnect, rate-limit, inconsistent-query, and partial-query-window
  scenarios. TEN classes, not the nine originally listed, and they COMPOSE: the
  selector used to choose one per operation, which made a documented coverage gap
  a property of the selector rather than of the system. Each class is asserted by
  the effect it produced, not by appearing in the selector's log - which is how
  `loss` on a cancel was found to log its label while losing nothing.
- [x] **G-034 — Bounded executor resources.** Normal and emergency queues,
  attempts, timeouts, and rate-limit accounting are explicitly bounded.

  Phase 5's CORRECTED acceptance closes G-025 through G-034. See
  `docs/evidence/V2_PHASE5_CORRECTED_ACCEPTANCE.md`, which supersedes the
  2026-09-11 record after a review found reducer and pre-send defects, a
  gateway-restart hole, and two test oracles that could report green while losing
  venue evidence. The earlier record and its tag are preserved as history.

  CLOSED WITH ONE CARRIED OBLIGATION: the physical Raspberry Pi 5 gate has not
  run against this source root, because the machine was offline. The ARM64
  container gate proves the logic and the byte layout on a real aarch64 kernel,
  so what the Pi adds is physical-hardware and clean-power evidence rather than
  unverified behaviour. If that run later fails, these ten guarantees reopen.

  G-074a stays open by design: an order holding unresolvable disagreement is
  retained as `conflict` and requires operator escalation. The corrected evidence
  measures the concurrent bound rather than asserting it.

## 6. Replication and availability semantics — Phase 6

- [x] **G-035 — Apply after commit only.** A command invokes the authoritative
  reducer only after the configured Raft commit condition is satisfied.
- [x] **G-036 — Stable retry across routing and leadership.** Follower routing,
  lost responses, and leader changes preserve the original request identity.
- [x] **G-037 — Leader-crash cut-point safety.** A crash before commit creates no
  effect; a crash after commit preserves and redelivers the same effect identity.
- [x] **G-038 — Quorum loss fails closed.** Loss of quorum prevents new
  authoritative risk decisions and risk-increasing outbox creation.
- [x] **G-039 — Replica convergence.** Healthy voters converge on commit index,
  applied index, state digest, policy identity, and outbox watermark.
- [x] **G-040 — Snapshot install preserves authority.** A restarted or replaced
  voter recovers all safety and dedup state before participating normally.
- [x] **G-041 — Honest failure domain.** Three voters on one Pi are process-fault
  exercises, not host fault tolerance; host-failure continuity requires voters
  on independent failure domains.

  Phase 6 closes G-035 through G-040 by construction and by test; G-041 is closed
  as a STATEMENT rather than a mechanism, which is what it always was. The
  replication suite runs three voters in one process, so it proves process-fault
  and partition behaviour and nothing about host failure. ADR 0002's two-machine
  topology is where host-independent failure domains come from.

  `ReplicatedKernel` has exactly one call to `apply()`, in the Raft apply
  callback, so G-035 is the shape of the code rather than a rule it follows. The
  reducer is unchanged and does not know it is replicated, which is why the same
  deterministic suite passes on one voter and on three.

  Evidence: `tests/v2_replication_tests.cpp`, nine properties including both
  leader-crash cut points, quorum-loss fail-closed with a non-vacuity retry after
  healing, restart recovery by log replay, and cross-voter convergence on the
  canonical snapshot digest. G-040's snapshot install is covered as RESTORE BY
  REPLAY of the committed prefix; compaction-based install arrives with Phase 7,
  which owns snapshot durability.

## 7. Durability, bounded memory, snapshot, and compaction — Phase 7

- [ ] **G-042 — Quorum-durable success.** A successful decision means durable
  under the declared quorum policy, never merely present in process RAM.
- [ ] **G-043 — Owner loop does not synchronously wait on routine disk I/O.**
  Fixed-capacity staging and a persistence owner move batched writes/flushes off
  the consensus owner loop while durability acknowledgement remains explicit.
- [ ] **G-044 — Safe block ownership.** WAL arena blocks have single-writer
  ownership, generation-safe reuse, release/acquire publication, and no record
  straddling an invalid boundary.
- [ ] **G-045 — Backpressure before corruption.** Writer stall, disk full, or a
  full arena causes bounded refusal/backpressure; live data is not overwritten.
- [ ] **G-046 — Torn/corrupt data detection.** Framing, length, generation, and
  checksums identify torn or corrupt suffixes and recover only according to an
  explicit valid-prefix policy.
- [ ] **G-047 — Hard-state recovery.** Term/vote and other required hard state
  survive a cut through a tested atomic/two-slot recovery protocol.
- [ ] **G-048 — Snapshot-before-reclaim.** Snapshot bytes and metadata become
  durable before WAL segments or state needed for recovery are reclaimed.
- [ ] **G-049 — Complete compacted state.** Snapshot/compaction retains policy,
  risk, reservation, order, fill, receipt, outbox, unknown, fencing, source, and
  reconciliation state required for equivalent continuation.
- [ ] **G-050 — No acknowledged effect disappears.** Process crash or power cut
  cannot recover behind the last response acknowledged under the declared
  durability contract.
- [ ] **G-051 — V1/V2 storage isolation.** V2 storage cannot be mistaken for or
  destructively migrated from a V1 format without an explicit migration tool.

## 8. Execution fencing, kill, and emergency risk reduction — Phase 8

- [ ] **G-052 — Logically single active sender.** Only the externally fenced
  executor generation for a venue/account can send normal risk-increasing effects.
- [ ] **G-053 — No automatic unsafe promotion.** A standby cannot promote merely
  because it won Raft leadership or lost contact with the previous gateway.
- [ ] **G-054 — Stale authority cannot send.** A stale executor generation or
  kill epoch is rejected at the last possible pre-send boundary.
- [ ] **G-055 — Bounded authority-loss response.** Loss or uncertainty of the
  external lease/fence stops new sends within the accepted lease bound.
- [ ] **G-056 — Independent kill path.** Authenticated kill and stop-new-risk do
  not depend on normal strategy traffic, dashboards, or normal-lane queue space.
- [ ] **G-057a — Late effects against closed orders are fenced.** A reclaimed
  `incomplete` effect - one orphaned when its order resolved before the effect
  ever received evidence - can no longer reproduce its canonical bytes. A gateway
  that later sends it must therefore be stopped by executor-generation and
  kill-epoch fencing plus stable `EffectId` idempotency, never by the cluster
  retaining those bytes indefinitely.

  Phase 4 deliberately relies on this. Orphan settlement (architecture §8.2) is
  what stops one lost effect outcome from wedging the bounded arenas, and it
  trades that availability against this fencing requirement. Acceptance must
  prove that a stale-generation or stale-kill-epoch send for a closed order is
  refused at the last pre-send boundary, and that a duplicate arriving by stable
  `EffectId` is a deterministic no-op rather than a second external effect.

- [ ] **G-057 — Emergency actions only reduce risk.** The emergency lane may
  stop, cancel, mass-cancel, or reconcile; it cannot open, enlarge, or improve
  an order.
- [ ] **G-058 — Reserved emergency capacity.** Cancel/mass-cancel remains
  available when normal rate or queue capacity is exhausted.
- [ ] **G-059 — Re-enable is deliberate.** Clearing kill or promoting authority
  requires attributable control, fencing proof, and reconciliation.
- [ ] **G-060 — Emergency evidence is retained.** Every emergency external
  action is recorded and reconciled into authoritative cluster state.

## 9. TrueMarkets adapter boundary — Phase 9

- [ ] **G-061 — Official-contract implementation.** Authentication, clocks,
  identifiers, idempotency, order lifecycle, status semantics, limits,
  pagination, and reconnect behavior are derived from verified official APIs.
- [ ] **G-062 — Exact unit conversion.** Price, quantity, tick, lot, minimum,
  notional, side, time-in-force, and rounding conversions are explicit and
  tested at adapter boundaries.
- [ ] **G-063 — Capability honesty.** Unsupported or ambiguous venue behavior is
  represented as unsupported/unknown; it is not guessed into success.
- [ ] **G-064 — Canonical typed evidence.** Venue responses and errors are
  converted into bounded canonical observations before changing OMS knowledge.
- [ ] **G-065 — Identifier continuity.** Effect/order identity maps stably to
  venue client IDs so retry, query, duplicate detection, and reconciliation agree.
- [ ] **G-066 — Secret isolation.** Credentials and signing material remain in
  the gateway security boundary and never enter replicated policy, commands,
  snapshots, logs, test fixtures, or evidence bundles.
- [ ] **G-067 — Ambiguous submit reconciliation.** The adapter queries by the
  strongest supported identity before any resend after an ambiguous submission.
- [ ] **G-068 — Adapter does not enable strategy.** Completing the venue adapter
  proves controlled order lifecycle mechanics, not permission for live strategy.

## 10. Operations, observability, evidence, and rollback — Phase 10

- [ ] **G-069 — Observability is non-authoritative.** Prometheus, Grafana, query
  indexes, and reports are disposable projections that may lag or rebuild.
- [ ] **G-070 — Observability outage isolation.** Metrics/dashboard failure does
  not block consensus, committed-effect delivery, reconciliation, kill, or cancel.
- [ ] **G-071 — Bounded hot-path telemetry.** Hot paths update fixed-size bounded
  counters/pages and do not perform unbounded logging or cardinality growth.
- [ ] **G-072 — Explainable lifecycle.** Operators can trace policy -> frame ->
  decision -> reservation -> order -> effect -> attempt -> venue evidence -> fill
  -> position using stable identities.
- [ ] **G-073 — Uncertainty is visible.** Unknown submissions, reconciliation
  lag/conflicts, stale sources, queue saturation, fencing state, durability lag,
  and risk refusal cannot be presented as healthy.
- [ ] **G-074 — Actionable operating controls.** An operator can identify
  authority, stop new risk, kill/cancel, reconcile, replay, restore, and roll back
  using documented and tested procedures.
- [ ] **G-074a — Stuck reconciliation is escalatable.** A logical order slot held
  by an order stuck in `reconciling` — venue silence, a dropped terminal event, or
  an on-chain cancellation that never settles — must be visible, alertable, and
  resolvable by a documented operator procedure.

  Required; Phase 4 supplies visibility, while the operational controls remain:

  - [x] metrics for `oldest_reconciling_order_age_since_authorization` and
    unresolved-slot age, so a strategy that has quietly stopped quoting is
    distinguishable from a quiet market (Phase 4 metrics, item C4);
  - [ ] an alert threshold and a runbook entry describing diagnosis and the venue
    queries that establish final cumulative filled quantity;
  - [ ] an authenticated, replicated operator command that resolves a stuck slot by
    supplying or overriding terminal evidence, retained as attributable evidence
    and never as history mutation; and
  - [ ] an explicit statement of what the operator must verify at the venue before
    invoking it, because releasing a hold without proof of final filled quantity
    is exactly the G-011 violation the reconciliation contract exists to prevent.

  The same escalation path is required for a cluster order in `conflict`, and
  for the same reason. `conflict` is terminal but NOT resolved: it means internal
  state and venue evidence disagree and the reducer cannot determine which is
  true, so the order's risk stays reserved and its slot stays occupied. Unlike
  `filled`, `cancelled`, and `rejected` - where terminality and resolution
  coincide - a conflicted order holds exposure of unknown size indefinitely.

  Conflicted records must never be reclaimed by compaction. Reclaiming one frees
  reserved capacity against exposure that may still exist (a G-011 violation by
  a second route), discards the retained proposal that records what was actually
  authorized, and destroys the inputs `materialize_order_effect` needs to
  reproduce the exact bytes sent to the venue. Those three artifacts - what we
  intended, what we sent, and how much risk we still carry - are precisely what
  an operator needs to resolve the disagreement, and with no durable log before
  Phase 7 there is nothing to recover them from.

  The general rule, preferred over enumerating states: a record may be reclaimed
  only when its risk is RESOLVED, not merely when its state machine has stopped.
  Any future terminal-but-unresolved state then inherits the correct behavior
  without being added to a list.

  Escalation after bounded retries is permitted and is NOT a clock. A saturating
  replicated attempt counter on the order is deterministic and replays
  identically, unlike a wall-clock deadline. After a configured number of failed
  reconciliation attempts the order must be marked as requiring operator
  attention and surfaced through metrics and alerting. That escalation changes
  visibility only: it must not release a hold, infer a position, or take any
  risk-reducing action automatically, because the missing input is venue truth
  and no amount of retrying manufactures it.

  This is deliberately NOT a Phase 4 timeout. A wall-clock eviction in the
  reducer would break determinism, and an automatic release without venue
  evidence would fabricate the very fact reconciliation demands. The resolution
  path is an operator-driven replicated transition, and it belongs with the
  kill/emergency work in Phase 8 and the runbook work in Phase 10.
- [ ] **G-075 — Tested alerts.** Critical alerts have drills proving both firing
  and recovery behavior, with bounded detection expectations.
- [ ] **G-076 — Immutable acceptance evidence.** Release evidence identifies
  source root, binaries/images, config, commands, raw results, hardware state,
  limits, and rollback point.
- [ ] **G-077 — Reproducible local version control.** V1/V1.1/V2 checkpoints and
  storage boundaries remain recoverable through retained local Git tags/branches
  and named container images without requiring a public repository.

## 11. Strategy/model safety and controlled activation — Phase 11

- [ ] **G-078 — Strategy stays inside the authority boundary.** Model output is
  an immutable artifact or bounded proposal; it cannot directly send an order or
  mutate authoritative OMS state.
- [ ] **G-079 — No-lookahead research.** Historical evaluation proves timestamp,
  feature, label, and market-data ordering do not use future information.
- [ ] **G-080 — Deterministic policy corpus.** Boundary, property, golden, and
  replay tests cover every supported rule/operator/template and rejection reason.
- [ ] **G-081 — Risk stress proof.** Inventory, adverse selection, stale quotes,
  fees, spread, post-only rejection, venue limits, and message-rate stress are
  tested before live risk.
- [ ] **G-082 — Automatic safety breakers.** Accepted loss, inventory, stale-data,
  reconciliation, and operational limits stop new risk through authoritative
  policy/control paths.
- [ ] **G-083 — Lifecycle resilience.** Shadow/paper execution covers gateway
  restart, voter failure, leader change, reconnect, unknown submit, and cancel/fill
  races before a real-money canary.
- [ ] **G-084 — Deliberate activation ladder.** Development -> replay -> simulator
  -> shadow -> paper/test venue -> tightly bounded canary -> reviewed expansion;
  no stage is silently skipped.
- [ ] **G-085 — Strategy economics are measured.** Volume, fees, spread capture,
  markouts, adverse selection, inventory, fill/reject/cancel rates, realized and
  unrealized P&L, risk interventions, and unexplained residuals are retained.
- [ ] **G-086 — Multiple strategies remain isolated in V2 MVP.** Each strategy's
  policy, decisions, risk allocation, receipts, and effects remain attributable;
  cross-strategy economic netting is deferred to V2.1 and cannot happen implicitly.

## 12. Performance and capacity acceptance

These are architectural guarantees about measurement and bounded behavior, not
preselected throughput promises.

- [ ] **G-087 — Declared capacity envelope.** Pi and NUC tests establish approved
  command rate, rule count, policy size, queue capacities, latency envelope, and
  operational headroom for each deployment profile.
- [ ] **G-088 — Overload is explicit.** Beyond that envelope, admission refusal,
  queue backpressure, or safety stop occurs before unbounded memory growth,
  retry storms, consensus starvation, or silent data loss.
- [ ] **G-089 — Consensus work has scheduling priority.** Raft ticks, peer
  heartbeats, replication, durability completions, and emergency work retain
  bounded service ahead of normal client ingress.
- [ ] **G-090 — Retry amplification is controlled.** Transport attempts per
  logical command is measured and remains within the phase-approved healthy
  envelope; retry storms are a failed gate, not accepted throughput.
- [ ] **G-091 — Layout budgets remain enforced.** Cache-line metadata, hot/cold
  splits, independent arena strides, compile-time size ceilings, and layout
  reports prevent accidental payload and resident-state growth.
- [ ] **G-092 — Claims are deployment-specific.** Latency, throughput,
  durability, and availability results name the exact hardware, storage,
  topology, build, instrumentation, and configuration measured.

## 13. Explicit non-guarantees

The architecture must never be marketed or reasoned about as guaranteeing:

- profit, no loss, a leaderboard position, or a particular trading volume;
- a fixed latency or throughput number before the target-specific gate accepts it;
- exactly-once execution at an external venue;
- continuity through Pi/NUC host failure while all voting or execution authority
  remains inside that failed host/failure domain;
- safe automatic executor failover without an external fencing mechanism;
- correctness of undocumented or changed venue behavior; or
- zero downtime, zero bugs, zero packet loss, zero disk failure, or zero human error.

The system instead guarantees bounded authority, explicit uncertainty,
idempotent economic state transitions, fail-closed behavior, reconciliation,
durable evidence, and measured operating limits.

## 14. Deviation tripwires

Stop implementation and request architecture review if a change would answer
**yes** to any of these questions:

1. Can anything outside the committed reducer authorize or modify economics?
2. Does authoritative apply read a clock, random value, file, network, or mutable
   process-global fact not represented in the command/state?
3. Can an effect be sent before its reservation, order, and outbox record commit?
4. Can an ambiguous send be retried without reconciliation?
5. Can a stale executor, stale kill epoch, or unfenced standby reach the venue?
6. Can overload cause unbounded allocation, queues, retries, work per loop, or
   identifier/cardinality growth?
7. Can snapshot, compaction, or replay discard information needed for dedup,
   risk, unknown outcomes, fencing, or source continuity?
8. Can an observability component become required for correctness or emergency
   control?
9. Are same-host replicas being described as host-level high availability?
10. Is a throughput, latency, durability, availability, or strategy claim being
    made without target-specific retained evidence?

## 15. Phase ownership map

| Phase | Primary guarantees | Closure question |
|---|---|---|
| Accepted foundation | G-001–G-009 | Is authoritative policy evaluation deterministic, bounded, canonical, and replayable? |
| 4 | G-010–G-026 | Can any order/effect exist without atomic risk and idempotent identity? |
| 5 | G-027–G-034 | Can the gateway mishandle a crash, duplicate, delay, or ambiguous send? |
| 6 | G-035–G-041 | Do commit, retry, crash, quorum-loss, and convergence semantics hold under Raft? |
| 7 | G-042–G-051 | Can acknowledged state disappear or storage pressure corrupt/overwrite safety state? |
| 8 | G-052–G-060 | Can two senders or stale authority create risk, and does kill work under overload? |
| 9 | G-061–G-068 | Does the real adapter preserve units, identity, evidence, secrecy, and uncertainty? |
| 10 | G-069–G-077 | Can operators detect, stop, explain, recover, reproduce, and roll back safely? |
| 11 | G-078–G-086 | Has the strategy passed the activation ladder without bypassing architecture or risk? |
| Cross-phase | G-087–G-092 | Are resource behavior and all performance claims bounded and evidenced per target? |
