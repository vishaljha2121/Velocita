# V2 Phase 3C decision-record layout evidence

Date: 2026-09-04

- Behavior commit: `c4283f6`
- Decision: retain the contract as a green host increment; reducer integration
  and Phase 3 acceptance remain pending

## Objective

Freeze the bounded hot/cold retained-decision representation before it can
change `TradingState`, snapshot bytes, or replay behavior.

## TDD evidence

The focused RED build failed because `DecisionRecordHot`,
`DecisionRecordCold`, their budgets, capacities, and arena strides did not
exist. The smallest implementation then passed:

- focused Debug layout suite: 1/1;
- complete Release suite: 15/15; and
- complete V2 ASan/UBSan suite: 12/12.

The reviewed 64-byte-target report is:

```text
decision_record_capacity=16
decision_record_hot_size_bytes=64
decision_record_hot_budget_bytes=64
decision_record_hot_arena_slot_stride_bytes=64
decision_record_cold_size_bytes=472
decision_record_cold_budget_bytes=512
decision_record_cold_arena_slot_stride_bytes=512
decision_record_packed_object_footprint_bytes=8576
decision_record_arena_footprint_bytes=9216
decision_record_hot_cold_split_status=contract_ready_reducer_pending
```

## What is hot and why

The hot record contains decision identity, policy identity, state/control
versions, matched-rule evidence, typed outcome, proposed/effective directives,
evaluation mode, and strategy mode. Those fields support bounded ID lookup,
idempotency, downstream authority checks, and outcome consumption.

The cold record contains the same decision ID as a join invariant, the command
payload digest, and the complete `DecisionFrame`. The frame carries features,
source provenance, timestamps, scope, readiness, position/risk versions, and
raw evidence digest. It is read for conflict diagnosis, audit, snapshot/replay,
or deterministic re-evaluation—not for the ordinary hot ID scan.

This is structure-of-arrays at the record-class level: hot and cold arrays will
share an index but not a cache footprint. It does not introduce pointers, heap
ownership, or concurrent mutation.

## Capacity and memory economics

Sixteen records are a bootstrap safety capacity, not a production retention
claim. At current sizes, packed objects require 8,576 bytes; independently
rounded arena slots require 9,216 bytes. The 640-byte difference is internal
fragmentation paid only by an arena choosing those strides.

The records are not yet members of `TradingState`. Adding either array without
updating canonical snapshot, digest, replay, capacity atomicity, and the
top-level state budget would create invisible authoritative state and is
therefore prohibited. Replicated expiry or durable audit archival is required
before this bounded table can support an indefinite live stream.

Follow-up: Phase 3C subsequently integrated both arrays with the reducer,
canonical command/result codec, snapshot, replay corpus, and independent
reference model. The original paragraph above records the deliberate boundary
of this earlier layout-only checkpoint; it is not the current implementation
status.

## C++ notes

Both records are trivially copyable and compile-time size checked. They are not
serialized with `memcpy`; canonical codecs must encode logical fields because
object padding and host layout are not protocol bytes.

The hot object is naturally 64 bytes on the reviewed ABI but is not declared
`alignas(64)`. `sizeof == 64` and address alignment are separate properties.
The future arena controls slot alignment; forcing alignment on every ordinary
stack or aggregate instance would change its ABI without evidence of
cross-owner false sharing.
