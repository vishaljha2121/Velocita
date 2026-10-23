# V2 Phase 3B decision-frame schema evidence

Date: 2026-09-04

- Behavior commit: `3f65187`
- Decision: retain as a green host increment; Phase 3 remains unaccepted

## Objective

Make causal source provenance and policy-validity time inputs explicit before
the Phase 3C reducer is allowed to create authoritative decision records.

## TDD evidence

The focused RED build reached the changed contract tests and failed because the
new public contract did not exist:

```text
error: use of undeclared identifier 'kPolicyArtifactSchemaVersion'
error: no member named 'source_id' in 'velocita::v2::DecisionFrame'
error: no member named 'event_time_ns' in 'velocita::v2::DecisionFrame'
```

GREEN host evidence:

- focused Debug contract/codec/evaluator suites: 3/3 passed;
- complete Release suite: 14/14 passed; and
- complete V2 ASan/UBSan suite: 11/11 passed.

The Release contract benchmark reports:

```text
max_policy_payload_bytes=516
max_decision_frame_payload_bytes=421
order_effect_payload_bytes=165
venue_observation_payload_bytes=116
max_envelope_bytes=65620
```

These are size ceilings, not Raft throughput or latency claims. The timing
samples were collected on the development host and are not promoted as target
hardware evidence.

## Accepted experimental behavior

- payload schemas are independently versioned: policy artifact V1, decision
  frame V2, order effect V1, and venue observation V1;
- decision-frame V2 adds source ID, source generation, source sequence, event
  time, and receive time in a fixed canonical position;
- the two-feature V2 golden frame is 181 bytes with SHA-256
  `9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3`;
- the frozen 141-byte V1 corpus has SHA-256
  `d382981729fab2d7209f1c8c3175afdcbc1654196f069fa7ba86acd4e756adbf`
  and is rejected with both payload and contract reason `unsupported_schema`;
- V2 truncation and trailing data retain distinct typed outcomes;
- missing source identity fails with `invalid_identity`; and
- missing or non-positive timestamps fail with `invalid_time_range`.

## C++ and codec review

The decoder first proves that two schema bytes are readable. It then reads the
little-endian `uint16_t` discriminator and rejects every version other than V2
before applying the larger V2 length contract. This ordering is essential: if
the length check ran first, a valid shorter V1 record would be mislabeled as a
damaged V2 record.

`DecisionFrame` stores strongly typed source identity values rather than raw
integers. The wrappers remain fixed-width value types, so canonical encoding
explicitly writes their underlying 64-bit representations and does not copy
C++ object padding or depend on host struct layout.

The fixed payload grew by 40 bytes: three 64-bit source fields plus two 64-bit
timestamps. With 32 fixed-width features, the bounded maximum is 421 bytes.
No decoder indexes a variable region until the declared feature count and exact
expected length have been checked.

Event and receive times are each required to be positive. The contract does not
enforce `receive_time >= event_time`: without a declared shared clock domain,
that comparison could reject valid data or disguise clock skew as causality.
Phase 3C consumes only the carried values and may not read a local wall clock.

## Compatibility and limits

The decoder does not synthesize missing V1 provenance and no migration utility
is provided. There are no production V2 decision records to migrate; invented
source or time values would corrupt audit meaning.

This increment does not yet:

- compare frame provenance with an admitted replicated source receipt;
- apply policy validity intervals or stable rejection precedence;
- create or retain `DecisionId` records;
- update command/result, snapshot, or replay schemas for authoritative
  evaluation;
- create risk, orders, or outbox effects; or
- establish ARM64, Raspberry Pi, Raft, durability, performance, or live-trading
  acceptance.

Those claims remain behind Phase 3C and 3E gates.
