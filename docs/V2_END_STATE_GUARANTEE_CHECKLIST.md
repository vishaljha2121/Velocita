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

- [x] **G-042 — Quorum-durable success.** A successful decision means durable
  under the declared quorum policy, never merely present in process RAM.
- [x] **G-043 — Owner loop does not synchronously wait on routine disk I/O.**
  Fixed-capacity staging and a persistence owner move batched writes/flushes off
  the consensus owner loop while durability acknowledgement remains explicit.
- [x] **G-044 — Safe block ownership.** WAL arena blocks have single-writer
  ownership, generation-safe reuse, release/acquire publication, and no record
  straddling an invalid boundary.
- [x] **G-045 — Backpressure before corruption.** Writer stall, disk full, or a
  full arena causes bounded refusal/backpressure; live data is not overwritten.
- [x] **G-046 — Torn/corrupt data detection.** Framing, length, generation, and
  checksums identify torn or corrupt suffixes and recover only according to an
  explicit valid-prefix policy.
- [x] **G-047 — Hard-state recovery.** Term/vote and other required hard state
  survive a cut through a tested atomic/two-slot recovery protocol.
- [x] **G-048 — Snapshot-before-reclaim.** Snapshot bytes and metadata become
  durable before WAL segments or state needed for recovery are reclaimed.
- [x] **G-049 — Complete compacted state.** Snapshot/compaction retains policy,
  risk, reservation, order, fill, receipt, outbox, unknown, fencing, source, and
  reconciliation state required for equivalent continuation.
- [x] **G-050 — No acknowledged effect disappears.** Process crash or power cut
  cannot recover behind the last response acknowledged under the declared
  durability contract.
- [x] **G-051 — V1/V2 storage isolation.** V2 storage cannot be mistaken for or
  destructively migrated from a V1 format without an explicit migration tool.

  Phase 7 closes G-042 through G-050. G-051 is the one that does not close, and
  the reason is worth stating precisely rather than deferring silently.

  G-042 IS THE DECLARED POLICY, NOT A HOPE. willemt counts a leader's own vote
  unconditionally, which cannot be corrected from outside the library, so the
  pinned fork in `deploy/patches` gates that vote on an optional `durable_idx`
  callback. The other half is on this side of the wire: a follower's reported
  `current_idx` becomes its match index, so `receive_peer` clamps it to that
  follower's own durable index. Both halves are pinned by mutation - removing
  either fails a named test. Evidence: `v2-durable-quorum-tests` (three cases,
  including that a NULL callback leaves historical behaviour byte-identical, so
  V1 is provably unaffected), plus `gate` and `follower-ack` in
  `v2-replication-tests`.

  G-043 IS TRUE OF THE STEADY STATE AND IS CLAIMED AS NOTHING MORE. `log_offer`
  stages and returns; the sync happens in `flush_durability`. Measured on the
  development host over two thousand command-sized records, owner-loop time went
  from 21.6 us median / 109 us p99 to 0.042 us / 208 us. But truncation and
  compaction QUIESCE THE WRITER and rewrite the segment synchronously, which is
  a rare maintenance barrier on the owner loop. That is a real exception to the
  guarantee's plain reading and is recorded here rather than in a footnote.
  Evidence: `writer` and `writer-truncation-race`.

  G-044 and G-045 are the arena's own suite: no record straddles a block, a
  block-sized record is refused rather than split, generations make a stale
  completion unable to release a recycled slot, the durable index advances only
  on completion and only in order, and a stalled writer produces bounded refusal
  rather than overwriting bytes that are not yet durable. Two of these were
  confirmed by mutation rather than assumed. Evidence: `v2-wal-arena-tests`.

  G-046'S VALID-PREFIX POLICY IS "REFUSE", and that is a reversal. Recovery
  first clamped a commit index past the end of the log; it now refuses and
  preserves the files. ADR 0006's amendment carries the argument - a sweep that
  truncates the segment externally is simulating a disk that lost acknowledged
  data, and clamping taught the node to tolerate that silently on the
  single-voter shape that has no peer to re-supply it. Evidence: the power-cut
  sweep in `corrupt-wal-sweep`, which cuts at every structurally distinct offset
  through the written frontier and requires the refusal to NAME the missing
  history rather than merely to happen.

  G-047: `torn-hard-state`. G-048: `compact` and `snapshot-directory`, which
  proves a directory-sync failure is not reported as success. G-049: the
  canonical snapshot encoder covers `kill_epoch` and `executor_generation`
  alongside policy, risk, reservations, orders, receipts, outbox and cursors, so
  the fencing state Phase 8 will USE is already carried across a compaction -
  what Phase 8 adds is enforcement, not new state to persist. G-050 is the sweep
  again, from the other side: no cut recovers behind the last acknowledged
  commit.

  G-051 WAS FOUND OPEN WHILE WRITING THIS SECTION, and is closed here.
  `ReplicatedKernel` reuses V1's `MetadataStore` and `WriteAheadLog` unchanged,
  which was a deliberate and still-correct decision - those layers are
  payload-generic. The consequence was not noticed: both wrote the SAME
  FILENAMES, `metadata.bin` and `raft.wal`, carrying the SAME `kMetadataMagic`,
  `kWalMagic` and `kStorageVersion`. A V2 node pointed at a V1 data directory
  therefore read V1's hard state SUCCESSFULLY - magic and version matched,
  because they were the same constants - and failed only later and incidentally,
  when a V1 payload reached the V2 command codec. Nothing at all stopped a V1
  node reading V2 storage.

  V2 now uses `v2-metadata.bin` and `v2-raft.wal`, so the two formats cannot
  occupy one path. Distinct names were chosen over a magic discriminator because
  they give the stronger guarantee - the file is not merely rejected, it is not
  consulted - and because they change NO PERSISTED FRAME'S BYTES, so this is not
  a byte-contract change. The file name is a defaulted constructor parameter,
  with V1's name as the default, so V1 is unchanged by construction rather than
  by review.

  Evidence: `v1-v2-isolation` in `v2-replication-tests`. It builds a populated
  V1 directory, starts a V2 node on it, and requires the node to begin with term
  0, commit 0 and no state - then lets it RUN there and requires V1's two files
  to be byte-unchanged afterwards, because isolation that works by clobbering
  the other milestone's data is the destructive migration this guarantee names.
  Mutation-confirmed: restoring the shared names fails it with `recovery
  refused: committed WAL history is missing`, which is precisely the accidental
  late failure described above.

  MIGRATION. An existing V2 data directory is not read by the new names. There
  is no V2 production data, so no tool is provided; a Pi or NUC carrying a Phase
  6/7 scratch directory should have it removed or its two files renamed.

  NOTE ON GATE COVERAGE, which this change exposed. `include/velocita/storage.hpp`
  and `src/storage.cpp` are OUTSIDE the source-root manifest - it selects
  `include/velocita/v2` plus `v2_*` and `*.hpp` under `src` and `tests` - yet V2
  links against them, as this change demonstrates. A modification there does not
  move the pinned root. That is defensible while V1 is separately accepted and
  gated, but it means the Phase 6/7 root does not cover all the code Phase 6/7
  runs, and it should be reviewed before the root is used as an acceptance
  identity again.

  Gate evidence: `V2 Phase 6/7 replication and durability acceptance target
  gate: PASS` on the x86-64 NUC, 34 / 31 / 31, source root
  `b241ad40cb1770e243a3bf06220e2a1711857c9c03ceeedae71deab613f3dcde`. The ARM64
  container gate is written and pinned but has not been run, and the physical Pi
  gate remains a carried Phase 5 obligation.

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

  PHASE 8 IS IN PROGRESS AND NOTHING HERE IS TICKED YET. The first slice landed
  the two mechanisms the rest depends on, and the honest position is that both
  are ENFORCED but neither is yet DRIVEN - the refusal exists, the thing that
  would renew or trigger it does not. Ticking a box on the refusal alone would
  claim a working kill path from a guard that nothing calls.

  THE AUTHORITY LEASE (toward G-055). Generation and kill-epoch fencing compare
  the gateway's cache against cluster state, which only helps a gateway that can
  still READ the cluster. A partitioned one keeps believing its last good answer
  indefinitely - so the fence protected against a gateway that knows it was
  superseded, and not against one that cannot find out. `GatewayControlView` now
  carries `confirmed_at`, `lease_bound` and `now`, and a risk-increasing send is
  refused with `authority_lease_expired` once the cache is older than the bound.
  Fail-closed: a zero bound is not "no limit" but "no authority", and a view
  claiming confirmation in the future earns no credit.

  A CANCEL IS EXEMPT FROM THE LEASE, deliberately. A lapsed lease means this
  gateway may no longer be the authority, and the safe response to that is to
  stop opening exposure, not to stop closing it. Refusing cancels would trap
  risk at exactly the moment an operator most needs it gone.

  THE RESERVED LANE (toward G-056, G-057, G-058). `SendQuotaView` gained
  `emergency_lane_remaining`, which only risk-REDUCING effects may draw on. The
  asymmetry is the reservation: a lane both kinds can spend is one lane with two
  names. Admission is by the OPERATION'S IDENTITY - cancel yes, submit and
  replace no - rather than by arithmetic on the effect's fields. A `replace` can
  be economically reducing, but proving it requires comparing against the
  resting order, and a lane whose safety rests on a comparison fails whenever
  the comparison is wrong. The cost is that a reducing replace waits for normal
  capacity, which is the right price.

  Evidence: four properties in `v2-gateway-tests` - a lapsed lease stops new
  risk (and the boundary tick itself does not), an unconfigured lease is expired
  rather than unlimited, a cancel survives both an exhausted normal lane and a
  lapsed lease, and a submit cannot draw on the reserved lane. Each is
  mutation-confirmed against its own assertion. Making the lease fail-closed
  also silenced every send in `v2-gateway-delivery-tests` until its fixture
  declared a lease, which is how the enforcement was confirmed to reach every
  delivery path rather than only the new tests.

  THE RENEWAL PATH (G-055, second slice). `renew_authority` is where the lease
  actually binds. A lease any read could renew would expire only when the
  process stopped, which is the one case needing no protection; this one renews
  ONLY on an authoritative read, so a gateway that cannot reach a leader gets no
  renewal and lapses by itself, with nobody having to notice or reach it. Two
  refusals carry weight beyond the obvious: a backwards tick does not renew,
  because a clock that moved backwards is the worst thing to extend authority
  on; and renewal adopts the authority it confirmed rather than only the time,
  because refreshing the clock while keeping the old generation would leave a
  gateway holding authority that looks fresh and is stale - worse than not
  renewing at all. Evidence: three properties in `v2-gateway-tests`, including
  the end-to-end one an operator cares about - after the last authoritative
  read, the gateway keeps sending for at most `lease_bound` ticks and then stops
  on its own, and no amount of further time brings it back.

  OPERATOR AUTHORIZATION (G-056, first half). The reducer CANNOT verify a
  signature: G-066 keeps key material out of replicated state and `apply`
  performs no I/O, so authentication is necessarily a property of the edge. What
  the cluster can do is refuse a control action - kill epoch, executor
  generation, strategy mode - from a caller the edge never authenticated, and
  record which operator issued the one it accepted. `kMaxOperatorCallerId`
  reserves a `CallerId` band for that, and the existing `CommandIdentity`
  already carries both the caller and a digest binding the payload, so this
  needed NO NEW BYTES. `ApplyCode::unauthorized_control` is appended at the end
  of the enum, so every existing code keeps its value.

  The refusal happens BEFORE the receipt lookup, so an unauthorized control
  command leaves no trace - retaining a receipt would make a replay of the same
  bytes return a completed action rather than the refusal, which is exactly the
  confusion an audit of a kill cannot afford.

  THE DEPLOYMENT OBLIGATION THAT MAKES THIS MEAN ANYTHING: the edge must never
  issue a reserved `CallerId` to a strategy. The reservation is one-directional
  by design - control must come from an operator caller, but an operator caller
  may also issue ordinary commands - because making it exclusive would buy
  nothing the edge does not already have to guarantee.

  THE CONTROL LANE (G-056, second half). `propose` had no admission control at
  all, so a strategy flooding the cluster filled the staging arena and the
  command that would stop the flood was the one that could not get in.
  `ProposeLane::control` may draw on `kControlLaneReservedBlocks` that nothing
  else may touch - the same asymmetry as the gateway's emergency lane, on the
  cluster side. The check sits in `propose` rather than in the arena because by
  the time `log_offer` stages, Raft has already assigned the entry an index;
  before the entry exists is the only point at which turning it away is free.

  A MARGIN, NOT AN EXEMPTION. Control commands are refused too once the reserve
  is gone: unbounded admission is precisely what the arena's backpressure exists
  to prevent (G-045), and overwriting bytes that are not yet durable in order to
  deliver a kill trades one failure for a worse one.

  Evidence: `control-lane` in `v2-replication-tests`, which floods until the
  normal lane closes and then MEASURES what the control lane can absorb. A
  one-command assertion proved nothing - whatever block was open still had room,
  so one more record fits whether or not anything was reserved, and shrinking
  the margin to zero left such an assertion green. The test now requires the
  control lane to supply more bytes than a single block could hold, which only
  real reserved blocks can do.

  THE LANE IS A LABEL UNTIL IT IS CHECKED. `ProposeLane` is chosen by the
  caller, so naming the control lane is not the same as being entitled to it.
  Two ways that would have made the reservation meaningless, both now refused
  before the entry exists: label an ordinary command "control" and spend the
  kill path's blocks on strategy traffic; or offer a real control command from
  an unauthenticated caller, which the reducer refuses only AFTER the entry has
  consumed reserved capacity, committed and replicated - a refusal that costs
  the resource it was protecting is a denial of service on the kill path. Bytes
  that do not decode are not a control command either: the lane cannot take the
  caller's word for what it is carrying. Evidence: `control-lane-admission`.

  G-053 IS ABOUT AN EVENT NOBODY WATCHES. The dangerous reading of "the leader
  is the gateway" is that whoever wins Raft has thereby become the execution
  authority - dangerous because leadership changes on its own, from a timeout, a
  slow link or a restart, while the old gateway may still be alive and sending.
  Executor generation is what the pre-send guard fences on, so the property is
  simply that no leadership event moves it.

  `no-auto-promotion` promotes once deliberately, then takes leadership away and
  returns it three times, and requires the generation to be unchanged on EVERY
  voter after each - and unchanged across a restart, since a voter coming back
  with no memory of who it was must recover the generation rather than mint one.
  It also requires leadership to have actually moved, because "leadership
  changed and nothing promoted" is otherwise a claim about an event that never
  happened. Mutation-confirmed against the realistic unsafe design - promote once
  per leadership term - not merely against an obvious one.

  G-059, THE HALF THAT IS ACTUALLY DANGEROUS. A kill is easy; UNDOING one is
  where the damage gets done. The moment after an emergency is when the
  cluster's picture of its own exposure is least trustworthy - usually that is
  why the kill happened - and resuming normal trading then can double a position
  nobody has confirmed. `apply_set_strategy_mode` now refuses a transition into
  `normal` while any order sits in `submission_unknown`, `reconciling` or
  `conflict`: the three states that mean the cluster cannot say what its
  exposure is.

  TWO BOUNDARIES MAKE THIS A SAFETY RULE RATHER THAN A DEADLOCK. It is
  ONE-WAY - reducing authority is never blocked, or an unreconciled cluster
  could not be halted, and a rule that prevents halting has become the thing it
  exists to prevent; a kill in particular must always work. And only the
  unknown states block it - a healthy resting order is a known quantity, and
  treating it as a reason to stay halted would make the rule mean "never resume
  while trading", which the first operator to meet it would route around.

  Evidence: `test_resuming_normal_requires_reconciliation`, mutation-confirmed
  three ways - removing the gate, applying it to every mode rather than only to
  `normal`, and narrowing it to `conflict` alone. Each mutant is killed by the
  assertion that names its property.

  WHAT EACH GUARANTEE STILL NEEDS. G-052/G-054 have their pre-send refusals from
  Phase 5 but no external fence to be single against. G-053 has its property proved - nothing
  promotes automatically - and needs the operator-facing workflow that makes a
  deliberate promotion safe, which is what G-059's reconciliation
  gate now refuses to let an operator skip. G-055 has both its
  refusal and its renewal, and needs the gateway loop that calls them plus an
  accepted bound. G-056 has authorization and independence, and needs the
  authenticated edge that assigns operator callers. G-057 and G-058 need stop
  and mass-cancel as operations, not just a lane that would carry them. G-060
  needs emergency actions recorded into cluster state, which means new command
  bytes and therefore a reviewed byte contract. G-057a needs the closed-order
  case driven end to end.

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
