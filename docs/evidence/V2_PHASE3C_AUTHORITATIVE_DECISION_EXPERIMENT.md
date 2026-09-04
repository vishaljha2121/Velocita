# V2 Phase 3C authoritative decision experiment

Date: 2026-09-04

Status: host candidate green; physical Raspberry Pi ARM64 gate pending

## Objective

Turn the pure ordered-rule evaluator into a bounded authoritative reducer
command while preserving the core replicated-state-machine invariant:

```text
same canonical state + same canonical command
    -> same result bytes + same next-state bytes
```

This increment evaluates and retains decisions. It does not reserve risk,
create an order, create an outbox effect, or contact a venue.

## Implemented behavior

- `EvaluateDecisionFrame` carries a non-zero `DecisionId`, explicit
  authoritative/shadow mode, and the canonical `DecisionFrame`.
- Wire kind 9 is explicit and independent of `std::variant` ordering.
- Kernel codec schema V2 adds the decision command and 16 decision-result
  bytes; schema V1 is rejected as unsupported rather than reinterpreted.
- The reducer applies the approved validation precedence: frame, exact active
  policy, scope, source evidence, event-time/readiness, ordered rules, then
  control authority. `halted` remains the categorical early exit.
- A typed `decision_id_conflict` distinguishes economic-identity reuse from a
  request-identity conflict.
- Authoritative, shadow, rejected, and suppressed outcomes retain paired hot
  and cold records. Shadow results always have effective `no_action`.
- Snapshot schema V2 preserves the records and enlarged request results using
  canonical logical fields rather than object bytes.
- Structurally bounded but semantically invalid decision frames reach the
  reducer and produce a deterministic `invalid_frame`; unbounded feature
  counts are rejected by the codec before apply.

## TDD and replay evidence

The RED decision-kernel test initially failed on the absent command, result,
state region, and semantics. The completed host candidate passes:

- complete functional suite: 16/16;
- Release V2 suite: 13/13; and
- ASan/UBSan V2 suite: 13/13.

Handwritten tests cover every decision reason, approved precedence, control
suppression, shadow authority, exact request retry, same-decision retry,
typed decision conflict, and capacity atomicity.

The independent model now evaluates decisions as separately written logic.
For 32 fixed seeds, each 96-command history agrees after every command on the
typed result, canonical result bytes, and canonical state. Six snapshot cuts
per history reproduce every suffix result and final snapshot. The reviewed
aggregate corpus digest is:

```text
f1888ae93ad0b3e3bfdc55ea0d6b4119c8539d33e178ba30fbb99043a6b76df2
```

The handwritten replay corpus includes both an executable authoritative
decision and a retained invalid-frame rejection. Its final state digest is:

```text
595c90f7e8443dbb5af8e81d425e98da53e386846c040e8d4346c5b94c681eb9
```

## Layout and capacity review

```text
DecisionRecordHot:       64 B object,  64 B arena stride
DecisionRecordCold:     472 B object, 512 B arena stride
decision capacity:       16
TradingState:        69,832 B / 73,728 B object budget
kernel command max:     608 B / 640 B canonical budget
kernel result:          126 B / 128 B canonical budget
snapshot max:        57,997 B / 65,536 B ceiling
```

The `TradingState` review ceiling increased from 64 KiB to 72 KiB because the
two fixed decision arrays add 8,576 packed bytes. This is an object-working-set
budget, not a wire or snapshot size. The maximum canonical snapshot remains
below 64 KiB because unused capacity and C++ padding are not serialized.

The result frame has only two bytes of reviewed headroom. That is intentional:
another result field requires a new schema/layout decision rather than silent
growth.

## C++ engineering notes

- The reducer and codecs are `noexcept`, use caller-owned fixed storage, and
  perform no I/O, clock access, random access, locks, or heap growth.
- Hot lookup scans only the 64-byte hot records. Cold feature/provenance data
  stays outside that ordinary scan.
- In-memory structs are never persisted with `memcpy`; padding and ABI layout
  are not canonical protocol.
- Decode remains transactional: a candidate is published only after framing,
  digest, bounds, enum, and state-invariant checks all pass.
- Capacity exhaustion may advance the command receipt/state version, but it
  cannot partially overwrite the retained decision region.

## Limitations and next gates

- Sixteen decisions are a bootstrap fail-closed capacity, not indefinite live
  retention. Replicated expiry/archive is required before production.
- No duration, rules-visited, or high-watermark metrics exist yet.
- No risk, order, effect, or executor authority exists in this increment.
- Physical Pi validation is still required for this exact source root.
- Phase 3D economic action-template decisions remain the next architecture
  review before Phase 4 effect creation.
