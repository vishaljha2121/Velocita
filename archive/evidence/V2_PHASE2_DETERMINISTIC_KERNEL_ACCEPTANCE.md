# V2 Phase 2 deterministic-kernel acceptance

Date: 2026-09-04

- Decision: accepted as the deterministic kernel foundation
- Accepted branch: `v2/phase-2-deterministic-kernel`

## Accepted capability

Phase 2 provides a bounded, single-owner C++ reducer for:

- immutable policy publication, validation, activation, and supersession;
- monotonic strategy-mode, kill-epoch, and executor-generation control;
- strict normalized source generation/sequence admission;
- request and source-event idempotency/conflict detection;
- canonical command/result codecs;
- canonical state snapshots and state digests; and
- handwritten plus generated model-based replay equivalence.

The reducer performs no I/O, reads no clock or random source, and owns no
background thread. Normal business rejection is represented by typed results.

## Explicitly not accepted

Phase 2 does not evaluate decision frames, reserve risk, create orders, create
outbox effects, communicate with a venue, run inside Raft, or prove durable
recovery. Those claims belong to later phases.

## Evidence chain

Accepted physical Pi checkpoints:

- source admission:
  `~/.local/share/velocita/v2-source-admission-pi-gates/20260903T192942Z`;
- canonical state snapshot:
  `~/.local/share/velocita/v2-state-snapshot-pi-gates/20260903T204111Z`;
- canonical replay:
  `~/.local/share/velocita/v2-canonical-replay-pi-gates/20260903T235904Z`;
- generated model replay:
  `~/.local/share/velocita/v2-model-replay-pi-gates/20260904T123522Z`.

The final generated-model source root was:

```text
d6eb59adecac3a486c2eee83151df02d66e7425865dd5076ad34ac0beeb96668
```

The expected and observed roots matched. All 10 sanitized V2 suites passed
under Ubuntu 26.04/GCC 15.2 on Raspberry Pi 5 ARM64. Firmware reported
`throttled=0x0` before and after; temperature moved from 43.55 C to 48.50 C.

The deterministic generated corpus executes 3,072 original commands and
18,432 snapshot-suffix commands. Its reviewed aggregate digest is:

```text
1eadcbdef0f9a244b935afdb3dd3be9c6e6f42fffe913509cc3baaea303357a0
```

## C++ acceptance boundary

The central API remains a value-oriented transition:

```cpp
ApplyResult apply(TradingState& state,
                  const Command& command,
                  ApplyScratch& scratch) noexcept;
```

`TradingState` has one mutable owner. `Command` is immutable during apply.
Scratch is caller-owned, making temporary-memory cost visible. Fixed-capacity
arrays avoid allocator-driven behavior in the reducer. Canonical serialization
never copies native object layout, padding, pointers, `std::variant` indexes, or
container internals onto the wire.

## Promotion rationale

The evidence closes the mechanics needed by later semantic phases:

1. commands can be identified and deduplicated;
2. source history can be admitted deterministically;
3. state can be serialized and restored canonically;
4. suffix replay reproduces uninterrupted execution; and
5. an independently structured model agrees with production behavior.

This is sufficient to begin Phase 3 decision semantics without importing
Phase 4 risk/order/outbox behavior into the same review surface.

## Rollback

Phase 2 introduces only V2 code and formats. V1/V1.1 branches, tags, images,
wire formats, storage, and evidence remain preserved. Return to
`v2/phase-1-domain-contracts` to remove Phase 2.
