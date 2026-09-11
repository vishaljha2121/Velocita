# V2 deferred functional scope

Status: deferred backlog. Execute after the MVP run, before anything long-lived.

Updated: 2026-09-10

## Purpose, and how this differs from V2.1

`V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md` holds work that makes V2 FASTER without
changing what it can do. This document holds work that changes WHAT IT CAN DO:
availability defects, coverage gaps, and functional limits that were consciously
accepted to reach an MVP.

The distinction matters because the label decides whether an item is ever
mandatory. An optimisation is optional forever. An availability defect is
optional only until the system runs long enough to reach it.

Every item below states its BLAST RADIUS and its TRIGGER - the condition that
converts it from deferred to required. An item with no trigger is genuinely
optional; most of these have one.

## 1. Unreclaimed arenas are lifetime ceilings

Severity: availability defect, bounded blast radius, no recovery path.
Trigger: the first cluster expected to outlive its ceiling.

### 1.1 What the defect is

Architecture §8.6 fixed exactly this defect for request receipts, and named it
precisely: "nothing ever reclaimed a request receipt ... the kernel accepted
exactly `kMaxRequestReceipts` commands in its lifetime and then refused
everything permanently with no recovery path ... Both are availability defects,
not tuning questions."

That fix was applied to receipts, venue events, decisions, orders, reservations,
outbox effects, and source events. It was NOT applied to three arenas, which
remain lifetime ceilings:

| arena | capacity | what it counts | reclaimed? |
|---|---:|---|---|
| `policies` | 16 | policy artifacts ever published | NO |
| `sources` | 16 | distinct market-data source identities ever seen | NO |
| `caller_cursors` | 8 | distinct caller identities ever admitted | NO |

`state.policy_count` only ever increments (`src/v2_kernel.cpp`, the
`PublishPolicy` path). Superseding a policy sets its lifecycle but does not free
its slot. On the seventeenth publish, `PublishPolicy` returns
`policy_capacity_exceeded` and returns it forever. A snapshot restore does not
help - the arena is part of the snapshot. There is no operator path to free a
slot.

### 1.2 Why this is NOT a correctness issue

No economics are wrong. Nothing is double-counted, no risk is lost, no identity
is reused, no evidence is dropped. The refusal is typed and fail-closed, exactly
as designed. The cluster keeps trading on its active policy indefinitely:
decisions still evaluate, orders still place, fills still book, reconciliation
still runs.

What is lost is the ability to ROTATE. The model pipeline can no longer ship.

### 1.3 Why "optimisation" is also the wrong label

Calling it optimisation implies it is optional forever, and it is not. The system
degrades from "a platform that takes new models" to "a platform running one
frozen model", permanently, with no way back short of rebuilding state from
scratch. §8.6 already ruled on how to classify this shape: availability defect.

### 1.4 Blast radius, precisely

- POLICIES (16). Sixteen `PublishPolicy` commands per cluster lifetime. If the
  model pipeline publishes one artifact per retrain, that is sixteen retrains.
  This is the item most likely to be reached in practice and the reason this
  document exists.
- SOURCES (16). Sixteen distinct `SourceId` values per lifetime. A source's
  GENERATION advancing does not consume a slot, so a feed that reconnects or
  restarts is free; only genuinely new feed identities cost one. Low risk if
  `SourceId` is assigned per feed, high risk if it is assigned per process
  instance.
- CALLER CURSORS (8). Eight distinct `CallerId` values per lifetime. Measured
  peak in the Phase 5 horizon run was 1. Low risk if caller identities are
  per-ROLE (edge, gateway, operator); a real risk if they are per-process, since
  eight restarts would exhaust it. The assignment convention is not currently
  written down anywhere, which is itself worth fixing.

### 1.5 What fixing it involves

The same shape as §8.6's decision reclamation, and the same hazards:

1. a reference pass - a policy is reclaimable only if no retained decision,
   order, outbox effect, or executable bundle still names its
   `(policy_id, policy_version)`;
2. supersession is necessary but not sufficient: the active and staged executable
   bundles pin their policies, and `materialize_order_effect` reproduces
   historical bytes from retained provenance, so anything it can still be asked
   about must survive;
3. paired dense compaction, since indices move - and the Phase 4 lesson applies
   directly: SWEEP BEFORE BORROW. Any index or reference taken before compaction
   silently names a different slot afterwards, with no crash and no sanitizer
   report;
4. admission-triggered, on full, matching the convention already chosen for
   decisions rather than inventing a second one; and
5. identity must not be lost - re-presenting a reclaimed `(policy_id,
   policy_version)` must still refuse rather than re-execute, which is what
   receipts do for decisions.

Sources and caller cursors need the same treatment or an explicit written
argument that their identity sets are genuinely finite and small.

### 1.6 Trigger

Any of:
- a cluster intended to run past sixteen policy publishes;
- a `CallerId` convention that is per-process rather than per-role; or
- promotion beyond an MVP run, since none of these have a recovery path and all
  of them fail permanently rather than transiently.

## 2. The simulator's faults do not compose

Severity: test coverage gap. Trigger: before Phase 5 acceptance is claimed to
cover G-033 in full.

`SimulatedVenue::choose_fault` returns ONE fault per operation by ordered first
match, so every fault class is mutually exclusive with every other for the whole
of that operation's response set.

Concretely unreachable today:
- a partial fill whose later messages are lost, so an order can never be
  partially booked and then go silent through the ordinary path;
- reordered FILLS - reordering is only ever applied to acknowledgements and
  cancellations, so fills never arrive out of order end to end, even though
  Phase 4's acceptance lists "cancel/fill reorderings" as covered (it is covered
  by hand-built permutation tests, which construct commands directly); and
- a duplicate that arrives LATE, after the cluster resolved and reclaimed the
  group, which is the `order_not_found` path.

The root cause is a taxonomy error rather than a probability one. `Fault` mixes
three different kinds of thing:
- TRANSPORT properties - delay, loss, duplication, reorder - which belong to a
  MESSAGE and which a real transport applies independently per message;
- VENUE behaviours - rate limit, disconnect, partial fill, fill after cancel -
  which belong to an OPERATION or a session; and
- DATA INTEGRITY - inconsistent query - which belongs to an ANSWER.

Forcing three categories into one pick-one enum makes cross-category composition
impossible, and cross-category is where the interesting bugs live.

Fixing it means splitting the axes and moving per-message faults to
`schedule()` time rather than `send()` time. It makes the seed discipline
STRONGER, not weaker: `choose_fault` currently short-circuits, so a firing fault
skips the remaining rolls and the draw stream advances by a variable amount;
independent per-axis rolls consume a fixed number every time.

Cost: every seed changes, so §10.5's measured horizon tables must be retaken.
That is honest churn - evidence gathered under a weaker fault model deserves to
be regathered under a stronger one - but it re-opens acceptance evidence and so
cannot be a quiet refactor.

## 3. Sixteen unresolved conflicts saturate the cluster

Severity: hard operational bound, correct and fail-closed. Trigger: any
deployment without a staffed operator path.

Measured in §10.5.3. A conflict is terminal-but-unresolved by design and is never
reclaimed, so every unresolved conflict permanently consumes one slot in the
decision, order, reservation and outbox arenas simultaneously. Sixteen
simultaneous conflicts and the cluster stops accepting new decisions.

This is correct behaviour - refusing beats freeing capacity against exposure that
may still exist - but it makes the G-074a operator escalation path LOAD-BEARING
rather than a convenience.

The capacity cannot be raised to soften it, and the reason is worth recording
because the arena sizes look independent and are not:

- `TradingState` is 81,168 bytes against an accepted 81,920 ceiling. One more
  decision record costs 704 of the 752 remaining.
- Request receipts are 40,960 bytes, half the entire state - but they cannot
  simply be cut, because VENUE EVENT RECEIPTS LIVE INSIDE THE SAME ARENA.
  `accepted_venue_event_count` scans `state.receipts` for results whose detail is
  a fill or venue event; `kMaxVenueEventReceipts = 64` is a sub-budget of
  `kMaxRequestReceipts = 256`.
- That sub-budget, not `kMaxClusterOrders`, is the real limit on concurrent
  orders. `venue_completion_capacity_available` refuses a new order unless the
  worst-case future venue events for every live order plus the new one fit in 64.
  A two-lot order costs about seven. So the effective concurrent-order limit is
  around nine, and `kMaxClusterOrders = 16` is already slack.
- Policies are 14,720 bytes and look like easy space - but §1 above shows they
  are an unreclaimed LIFETIME budget, so cutting them halves how many models the
  cluster can ever accept.

Conclusion: a capacity rebalance is NOT available within the current design. The
levers are shrinking `DecisionRecordCold` (584 bytes, dominated by
`DecisionFrame`'s 432, dominated in turn by `features[32]` at 256 - a byte
contract change), or raising the state ceiling, which is itself provisional
pending NUC validation.

## 4. Reconciliation gaps left open on purpose

Each of these was reached, understood, and scoped out rather than missed.

### 4.1 A cancel whose send attempts are exhausted cannot be reconciled

From §10.8.2. Reconciliation about a cancel requires its delivery to be uncertain
- `unknown`, `rejected`, or `reconciling`. A cancel whose attempts ran out is
still `pending`, so it is excluded.

The reason it is excluded is that the useful answer there is "re-authorize a
fresh cancel", and nothing does that. Allowing reconciliation without a
re-authorization story would let the cluster conclude "the cancel did not take"
and then silently abandon the operator's intent.

Fixing it means a re-authorization path: on a trustworthy answer that the order
is still working, mint a fresh cancel effect rather than marking the old one
rejected and stopping.

### 4.2 A venue-initiated cancellation cannot be reported at all

Pre-existing, not introduced by Phase 5. The `cancelled` observation kind
requires `effect->operation == EffectOperation::cancel`, so it can only describe
a cancel the cluster itself requested. A venue that cancels an order on its own -
self-match prevention, session teardown, risk control - has no way to tell the
cluster.

### 4.3 A partially filled order that goes silent is out of scope

From §10.1.8. Its state is `partially_filled`, not `submission_unknown`, so
`need_of` does not target it. The order is known live; only its subsequent fills
are missing, and without a clock there is no way to distinguish "its fill was
lost" from "it has not traded yet".

### 4.4 `Adopt` is not implemented, and that is correct for now

From §10.1.7. A venue reporting more filled than the cluster booked would require
creating a position movement, but a query answer is a snapshot carrying no
execution identity, no price, and no fee - the three fields a fill requires.
Adopting would mean inventing all three, so the divergence is handled as missing
evidence or conflict instead.

`Adopt` becomes possible when reconciliation can obtain PER-EXECUTION evidence -
a fills query returning execution identities - which is a venue-integration
question, not a kernel one.

### 4.5 Balances, settlement state and currency do not exist

From §10.1.1. Architecture §10 requires reconciliation to compare them; they
appear nowhere in `TradingState`, `OrderEffect`, or `VenueObservation`, and
`FeeMicros` carries no currency tag. Deferred deliberately rather than invented,
so that a comparison is not written against fields nothing produces.

## 5. Smaller recorded items

- NATIVE AMEND AND OVERLAPPING REPLACEMENT remain disabled. Only the reviewed
  serial replacement lifecycle (cancel, reconcile, freshly authorize) is
  accepted. Unchanged from Phase 4.
- `include/velocita/v2/wire.hpp` carries an unwired Phase 1 `MessageKind`
  vocabulary that does not correspond to `KernelCommandKind` and is not a
  transport (§9.1.8). It should be either wired or deleted before anything
  assumes it means something.
- THE PLANNER MATERIALIZES EFFECTS TWICE. `plan_delivery` calls
  `evaluate_pre_send` per candidate, which calls `materialize_order_effect`,
  which scans up to 256 receipts; the shell then calls it again for each selected
  effect. Roughly four thousand comparisons per tick. This belongs in V2.1, not
  here: it is a cost, not a limit, and the current design has a real virtue - the
  planner and the guard cannot disagree, because the planner asks the guard.
- THE `CallerId` ASSIGNMENT CONVENTION is not written down anywhere. Whether it
  is per-role or per-process decides whether §1.4's caller-cursor ceiling is
  harmless or serious.
