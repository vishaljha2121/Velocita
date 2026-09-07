# V2.1 optimization and portfolio-execution scope

Status: deferred backlog; execute only after V2 functional completion and
release-wide correctness acceptance

## 1. Purpose

V2 prioritizes correct deterministic economic state, canonical replay, bounded
memory, and fail-closed behavior. V2.1 will optimize measured CPU, cache, and
owner-loop costs without changing those semantics. V2.1 is also the earliest
release allowed to introduce reviewed multi-strategy portfolio netting; that is
an economic feature, not an assumed performance optimization.

This document prevents two opposite mistakes:

1. prematurely complicating V2 based on source-code appearance rather than
   measurements; and
2. forgetting known scaling risks once the functional build is complete.

Every V2.1 change must demonstrate a measured bottleneck, preserve canonical
bytes and replay results unless a separately reviewed schema change is
intentional, and retain fixed-capacity behavior.

## 2. Known concern: full-state snapshot validation

`valid_state(const TradingState&)` is called only:

- before `encode_state_snapshot`; and
- after a snapshot payload has been decoded into transactional scratch.

It is not called by `apply`, source admission, ordered-rule evaluation, or the
per-command decision reducer. Snapshot support is not yet integrated into the
live Raft owner loop, so the validator is not currently a command hot-path
cost.

The validator has bounded but partly quadratic complexity. With `P` policies,
`S` sources, `E` source-event receipts, `Q` request receipts, `D` decisions,
`R` rules, and `F` features, its current upper-bound shape is:

```text
O(P^2 + P*R + S^2 + E^2 + E*S + Q^2 + D^2 + D*F)
```

Current compile-time capacities are:

| Region | Capacity | Maximum pairwise duplicate comparisons |
|---|---:|---:|
| Policies | 16 | 120 |
| Sources | 16 | 120 |
| Source-event receipts | 128 | 8,128 |
| Request receipts | 256 | 32,640 |
| Decisions | 16 | 120 |

The full state therefore performs roughly 44,000 bounded validation and lookup
operations, depending on rule/feature occupancy. This is not unbounded input
complexity, but it must be measured before snapshot work is scheduled near the
Raft control plane.

Deep validation must remain mandatory on decode. Digest-valid bytes can still
describe semantically impossible state; publishing such a candidate could
violate policy uniqueness, request idempotency, source causality, shadow
authority, or decision-record pairing.

## 3. Known concern: per-command receipt lookup

`apply` calls `find_receipt` for every valid command before dispatch. The
current implementation linearly scans as many as 256 `RequestReceipt` objects.
Unlike snapshot validation, this is already algorithmically on the command hot
path and may be the more important optimization target.

Decision and policy lookups are also bounded linear scans, currently over 16
records each. They remain secondary measurement candidates rather than assumed
bottlenecks.

## 4. Required V2.1 measurements

Add allocation-audited microbenchmarks on the development host, Raspberry Pi,
and NUC for:

- `valid_state` at empty, 25%, 50%, 75%, and 100% occupancy;
- snapshot payload sizing, validation, canonical encoding, SHA-256, full
  encode, canonical decode, semantic validation, and destination publication;
- `find_receipt` and complete `apply` at the same receipt occupancies, with
  hits at the first, middle, and last slot plus misses;
- policy, source, and decision lookup at their capacity boundaries; and
- cold-cache and warm-cache runs reported separately where the harness can
  control them honestly.

Record p50, p90, p99, maximum, cycles or CPU time where available, allocation
count, state size, snapshot size, compiler, optimization mode, cache-line size,
CPU governor, temperature, and power-throttling state. Do not infer a service
SLO from a microbenchmark.

The Raft integration benchmark must separately measure:

- owner-loop maximum tick gap;
- snapshot capture/copy pause on the owner thread;
- off-thread validation/encoding duration;
- durable snapshot-write and sync duration;
- command latency during snapshot creation; and
- retry amplification and leader stability during the checkpoint.

## 5. Candidate optimizations, in evidence order

### 5.1 Keep snapshot work off the owner loop

The first architectural control is isolation, not faster loops. The owner
thread should capture a bounded immutable state image at a deterministic Raft
index. Validation, encoding, hashing, and storage should execute away from
heartbeat, replication, and command scheduling. Completion returns through a
bounded channel, and WAL compaction cannot cross an unconfirmed durable
snapshot index.

The capture mechanism requires its own measurement: copying the current
69,832-byte `TradingState` synchronously may still create a visible owner-loop
pause. Double buffering or an immutable arena generation is considered only if
that measured pause violates the tick budget.

### 5.2 Replace quadratic validation scans if measured

Use caller-owned, fixed-capacity validation scratch such as deterministic
open-addressed sets for request identities, event identities, and decision IDs.
This can reduce duplicate detection from quadratic to expected linear work
without heap allocation.

Collision handling, maximum probe count, table-full behavior, and clearing cost
must be explicit and bounded. The optimized validator must accept and reject
exactly the same state corpus as the reference validator.

### 5.3 Add a derived bounded receipt index if measured

A fixed-capacity receipt index can reduce per-command lookup cost. It must not
become an independent source of truth: retained canonical receipts remain
authoritative. The index must be deterministically rebuildable after snapshot
restore and its miss/hit result must be checked against reference linear-scan
semantics in differential tests.

Whether the index is stored inside runtime state or in explicitly owned
derived scratch is a V2.1 layout decision. It must not silently change snapshot
bytes, object budgets, or replay identity.

### 5.4 Preserve decode validation

Incremental invariants maintained by `apply` may allow a trusted live state to
use a cheaper encode precondition check, but they do not replace full semantic
validation of restored bytes. Any trusted-encode fast path requires tests that
prove all reducer transitions preserve the invariants assumed by that path.

### 5.5 Multi-strategy portfolio netting and intent classes

V2 deliberately resolves exact per-policy economic proposals and does not
silently net them. V2.1 may add a bounded portfolio coordinator for multiple
strategies operating one account, but only through a separately reviewed schema
and reducer transition.

The motivating target-position example is:

```text
strategy A target = +3 lots
strategy B target = -2 lots
portfolio target  = +1 lot

execution delta = portfolio target
                - confirmed position
                - conservatively valued committed/pending effects
```

If confirmed and pending exposure are zero, the externally executed delta is
`+1`, not independent `buy 3` and `sell 2` orders. This may reduce gross
turnover, spread crossings, fees, slippage, and venue messages. It may also
reduce externally reported volume, forgo maker economics, introduce aggregation
latency, and complicate strategy attribution. It is therefore never enabled as
an executor-side heuristic.

The design must distinguish at least two intent classes:

- `TargetPositionIntent`: a signed target exposure that may be netted with
  compatible target intents for the same account, instrument, valuation epoch,
  and policy authority; and
- `QuoteIntent`: exact bid/ask slot intentions used by market making, which may
  not be cancelled out merely because their current signed net is zero.

The replicated OMS/risk transaction owns aggregation. The executor receives
one exact committed effect and has no authority to combine, resize, reprice, or
reinterpret strategy intents.

The first reviewed capacity proposal is four strategy lanes, 32 total ordered
rules, 16 deduplicated action templates, one active portfolio bundle, and one
staged bundle. These are backlog candidates rather than accepted constants;
layout and snapshot experiments must prove them before they become contracts.

Risk must cap both dimensions:

```text
net target   = abs(sum(signed strategy targets))
gross target = sum(abs(strategy target))
```

Net exposure alone is insufficient because removing one offsetting strategy can
reveal the larger underlying position. Required controls include per-strategy,
per-instrument, account-wide gross/net, pending-effect, and unknown-effect
limits with checked arithmetic and typed capacity failure.

Before implementation, V2.1 must decide and test:

1. target-versus-delta semantics and compatibility keys;
2. the deterministic aggregation boundary, using replicated sequence/index
   semantics rather than an unreplicated wall-clock window;
3. treatment of pending, rejected, late, duplicate, and unknown effects;
4. fill and P&L attribution, including whether an internal offset creates only
   counterfactual strategy accounting or an explicit virtual transfer;
5. interaction with cancel-only, reduce-only, halt, and dynamic risk authority;
6. gross and net safety limits and arithmetic overflow;
7. exact replay and snapshot representation; and
8. evidence that reduced external turnover improves the selected economic
   objective rather than merely reducing leaderboard volume.

This feature is parked for V2.1. It must not delay or alter the current V2
policy-schema, resolver, reducer, and replay completion path.

## 6. TDD and acceptance gates

Before implementation, freeze a reference validation corpus containing every
accepted state and every rejection reason. Then require:

1. reference and optimized validators agree on all directed and generated
   states;
2. canonical command, result, and snapshot bytes remain unchanged unless a
   reviewed schema migration explicitly says otherwise;
3. full replay and every snapshot suffix retain identical results and final
   state digests;
4. no heap growth, locks, I/O, clocks, or randomness enter the deterministic
   reducer;
5. lookup tables and queues have compile-time capacities and explicit
   exhaustion behavior;
6. Release, sanitizer, pinned ARM64 container, physical Pi, and NUC gates pass;
7. the optimized component shows a material target-hardware improvement, not
   only a host microbenchmark win; and
8. integrated snapshot creation respects the owner-loop tick-gap, leader
   stability, retry-amplification, and command-latency budgets established
   before the experiment.

If an optimization adds complexity without a repeatable material improvement,
retain the simpler V2 implementation.

## 7. Explicit V2 boundary

V2 may add measurement hooks needed to establish the baseline, but it will not
redesign snapshot validation or receipt lookup solely because their loops look
expensive. Functional V2 phases continue under the existing correctness and
layout gates. Optimization begins only after V2 is complete and the V2.1
experiment has an approved metric, budget, and rollback plan.
