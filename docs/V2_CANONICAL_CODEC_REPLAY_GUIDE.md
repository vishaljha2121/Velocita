# V2 canonical codec and replay guide

Date: 2026-09-03

Purpose: explain what was built in commits `a04094e` and `0a20e4f`, why the
boundaries exist, how the C++ implementation works, what the build gates prove,
and what remains before Phase 2 is complete.

This is educational and experimental documentation. Nothing in this increment
can place an order.

## 1. Executive view

The reducer previously accepted a C++ `Command` object and returned an
`ApplyResult`. The state snapshot codec could serialize `TradingState`. That was
not enough to prove real recovery because a durable log contains bytes, not live
C++ objects.

The new path is:

```text
canonical command bytes
        |
        v
validate frame, schema, length, kind, digest, and fields
        |
        v
typed immutable Command
        |
        v
deterministic apply(TradingState, Command)
        |
        v
typed ApplyResult
        |
        v
canonical result bytes
```

Recovery can now be stated in terms of persistent artifacts:

```text
apply every encoded command from an empty state
                    ==
restore a canonical snapshot + apply the remaining encoded commands
```

The equality is byte-for-byte for suffix results and the final snapshot, not
merely "the values looked similar."

## 2. What was built

### 2.1 Canonical command and result codec

Relevant files:

```text
include/velocita/v2/kernel_codec.hpp
src/v2_kernel_codec.cpp
tests/v2_kernel_codec_tests.cpp
```

The command frame contains:

| Field | Purpose |
|---|---|
| magic | Reject data from the wrong protocol |
| payload length | Bound parsing and reject truncation/trailing data |
| schema version | Prevent silent reinterpretation after format changes |
| command kind | Select one explicit command alternative |
| command payload | Identity plus command-specific fields |
| SHA-256 | Detect corruption of schema, kind, and payload |

The maximum command frame is 608 bytes. The result frame is fixed at 110
bytes. Both use explicit little-endian integer encoding.

Why not serialize `sizeof(Command)`? C++ object representation may contain
padding, implementation-defined enum sizes, and `std::variant` bookkeeping.
Those bytes are not a stable storage or network contract. The codec serializes
logical fields one at a time.

### 2.2 Byte-level replay adapter

Relevant files:

```text
include/velocita/v2/replay.hpp
src/v2_replay.cpp
tests/v2_replay_tests.cpp
```

`apply_encoded_kernel_command` composes existing components. It does not add
business policy:

```cpp
ReplayApplyResult apply_encoded_kernel_command(
    TradingState& state,
    std::span<const std::byte> command_bytes,
    ApplyScratch& apply_scratch,
    KernelCommandCodecScratch& codec_scratch,
    std::span<std::byte> result_output) noexcept;
```

Its sequence is:

1. preflight the result-buffer capacity;
2. decode and validate command bytes into caller-owned scratch;
3. invoke the pure deterministic reducer;
4. encode the reducer result into canonical bytes; and
5. return typed status plus command/result frame digests.

Malformed bytes and undersized output fail before `apply`, so those expected
failure modes cannot mutate authoritative state.

## 3. Deep C++ mechanics

### 3.1 `std::span` is a non-owning bounded view

`std::span<const std::byte>` is conceptually a pointer plus a length. It does
not allocate, copy, extend lifetime, or own the referenced memory.

The `const` applies to the bytes: decode may inspect but not modify them. The
result uses `std::span<std::byte>` because encoding writes into caller-owned
storage.

This makes ownership visible:

```text
caller owns arrays/buffers
codec temporarily borrows spans
borrow ends when the call returns
```

The lifetime rule is critical: neither codec nor replay adapter may retain a
pointer or span after returning.

### 3.2 Caller-owned scratch makes resource bounds explicit

`KernelCommandCodecScratch` owns a reusable `Command candidate`. Decode fills
the candidate, validates it completely, then exposes it to the reducer. There
is no hidden `thread_local`, global mutable state, or per-command heap owner.

This matters operationally because a hot path whose memory demand is explicit
can be provisioned and tested. Hidden allocation turns overload into allocator
latency or process death.

### 3.3 `std::variant` is not a wire discriminator

`CommandBody` is a `std::variant` because, in memory, exactly one command type
is active. Its alternative index is a library-layout detail, so the wire codec
uses a separately frozen `KernelCommandKind` registry with values 1 through 8.

That separation permits internal refactoring without silently renumbering the
persistent protocol. Changing a registry value is a schema migration, not a
cleanup.

### 3.4 Two digests solve two different identity problems

`CommandIdentity::payload_digest` belongs to idempotency semantics. It answers:

```text
Did caller X reuse sequence Y for the same logical payload?
```

The frame SHA-256 belongs to transport/storage integrity. It answers:

```text
Were these encoded schema/kind/payload bytes corrupted?
```

They must not be conflated. A valid frame may carry an idempotency digest that
conflicts with a previously retained request; that is a validly encoded
business conflict and must be handled deterministically by the reducer.

### 3.5 Transactional decode

Decode does not progressively mutate `TradingState`. It builds a candidate in
scratch and rejects bad frames before reducer entry. Snapshot decode follows a
similar pattern: validate candidate state, then publish it to the destination.

This resembles a database transaction:

```text
parse -> validate -> commit assignment
```

The result encoder runs after state mutation. Its reachable inputs are only
the reducer's closed enum/value set, and output capacity is checked before
mutation. Therefore an encode failure after `apply` represents a programming
invariant violation, not a recoverable client error. Future changes to
`ApplyResult` must update validation, encoding, and tests together.

### 3.6 `noexcept` is part of the operational contract

The replay boundary is `noexcept` because it uses fixed storage, spans, value
types, and typed error returns. No normal malformed-input path throws.

`noexcept` does not magically make unsafe code safe. If a supposedly
non-throwing operation throws, the process terminates. We therefore support the
claim with bounded data structures, no hot-path allocation, compiler warnings,
and sanitizer tests.

## 4. What the replay test proves

The deterministic corpus has ten encoded records covering:

- policy publish, validate, and activate;
- executor-generation and kill-epoch fences;
- ordered source admission;
- an exact duplicate crossing the snapshot boundary;
- another accepted source event;
- a source-sequence gap rejection; and
- a mode transition guarded by current fences.

The test executes two histories:

```text
FULL:   C1 C2 C3 C4 C5 C6 C6 C7 C8 C9

SPLIT:  C1 C2 C3 C4 C5 C6 -> snapshot -> restore -> C6 C7 C8 C9
```

It proves:

- every suffix `ApplyResult` encodes to identical bytes;
- exact retry behavior survives snapshot/restore;
- typed rejections are deterministic during replay;
- final snapshots are the same length and exactly equal;
- final state digests match; and
- the reviewed corpus digest is
  `596ec54fb24f55b91579330e08cefa63b3af8d39c1e277cd835c5af4e1b566c8`.

The golden digest is a change detector. If it changes, we stop and determine
whether we found a bug or intentionally changed semantics. We never casually
replace it just to make a test green.

## 5. What the build gates mean

### 5.1 Host Release gate

All 12 repository suites passed. Release mode checks the optimized code path
and ensures V2 additions did not regress the V1 tests.

### 5.2 Instrumented build

All 9 V2 suites passed with benchmark instrumentation enabled. Compile-time
feature flags can change layouts and conditional code; this gate proves the V2
contracts remain coherent in that build variant. It is not a performance
benchmark.

### 5.3 ASan and UBSan

All 9 V2 suites passed with AddressSanitizer and UndefinedBehaviorSanitizer.

- ASan looks for problems such as out-of-bounds and use-after-free access.
- UBSan looks for undefined operations such as invalid shifts or signed
  overflow in instrumented paths.

Passing means the tests did not trigger those defects. It does not prove that
unexecuted paths are defect-free.

### 5.4 ARM64 Ubuntu gate

The code was compiled and executed as native `aarch64` Linux userspace in the
pinned Ubuntu 26.04/GCC 15.2 container. This catches architecture, compiler,
endianness-assumption, and Linux-specific issues that a macOS-only test misses.

Evidence:

```text
commit:      0a20e4fcf8ebe8e75fb34f8fd4221beaade08480
source root: 810f8c4db00d6ae37061694ab3be6f775b3312f0b837c09b00c589903d5e04aa
Release:     12/12 pass
instrumented V2: 9/9 pass
ASan/UBSan V2:   9/9 pass
```

The source root hashes the relevant source manifest plus `CMakeLists.txt`. It
lets the Pi gate prove that rsync delivered the exact reviewed source even when
the Pi checkout has no usable Git metadata.

### 5.5 What the ARM64 container does not prove

It does not prove Raspberry Pi thermals, power integrity, SD-card behavior,
kernel scheduling, or physical-machine performance. Those require the pinned
Pi gate:

```bash
BUILD_JOBS=2 ./deploy/run-v2-canonical-replay-pi-gate.sh --yes
```

## 6. TDD chronology

The replay work followed RED, GREEN, REFACTOR/EVIDENCE:

1. RED: add a test referencing a missing replay implementation; configuration
   failed because `src/v2_replay.cpp` did not exist.
2. GREEN: implement the smallest decode/apply/encode adapter.
3. GOLDEN: run the deterministic corpus, inspect the resulting digest, and
   freeze it as a reviewed compatibility fixture.
4. REGRESSION: run all old and new tests.
5. SAFETY: run ASan/UBSan.
6. PORTABILITY: run the pinned ARM64 Ubuntu/GCC gate.
7. TARGET ACCEPTANCE: pending the exact-source physical Pi gate.

The RED failure matters. It demonstrates that the new test can detect absence
of the capability rather than passing accidentally against old behavior.

## 7. What remains unproven

This checkpoint does not yet provide:

- randomized/model-based command generation;
- a real Raft log carrying V2 command frames;
- durable snapshot file publication or crash selection;
- snapshot installation into a follower;
- WAL prefix compaction and receipt retention policy;
- reconciliation-gated authority restoration;
- orders, reservations, positions, fills, or an execution outbox;
- venue connectivity; or
- throughput/latency capacity claims.

The strongest accurate statement is: the current bounded kernel has a
canonical byte boundary, and the reviewed corpus replays equivalently across a
snapshot boundary on host and containerized ARM64.

## 8. CTO review lens

When reviewing a replicated system, ask four separate questions:

1. **Meaning:** Are commands and outcomes semantically unambiguous?
2. **Determinism:** Do identical committed bytes and prior state yield identical
   state and output?
3. **Durability:** Which bytes survive process, machine, and storage failure?
4. **Authority:** Which component may cause an irreversible external effect,
   and how is stale authority fenced?

This increment advances meaning and determinism. It does not complete
durability or external-effect authority. Keeping those claims separate is an
engineering leadership discipline: a green codec test is not a production OMS.

## 9. Next recommended increment

After the physical Pi checkpoint, add a bounded deterministic generated-command
model. Compare the production reducer against an intentionally simpler
reference model across many seeds, snapshot cut points, duplicates, conflicts,
and typed rejections.

That catches interaction bugs beyond the ten-record golden corpus. The design
decision to review is the model's scope: keep it limited to current Phase 2
policy/control/source semantics, or first add reconciliation-gated authority
restoration and model that larger state machine. The lower-risk sequence is to
model current semantics first, then extend the model alongside each new command.
