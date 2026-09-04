# V2 Phase 3 pre-3C layout-guardrail evidence

Date: 2026-09-04

- Behavior commit: `e650f31`
- Decision: retain as a green host guardrail; Phase 3 remains unaccepted

## Objective

Make payload growth and resident working-set growth visible before Phase 3C
adds authoritative retained decisions. The gate separates three values that
must not be conflated:

1. canonical payload bytes used by codecs, transport, and durable records;
2. C++ object bytes reported by `sizeof`; and
3. an arena slot stride rounded independently for the declared target cache
   line.

## TDD evidence

The focused RED build failed at the intended missing contract:

```text
fatal error: 'velocita/v2/layout.hpp' file not found
```

GREEN host evidence:

- focused Debug layout suite: 1/1 passed;
- complete Release suite: 15/15 passed; and
- complete V2 ASan/UBSan suite: 12/12 passed.

The machine-readable layout report for the declared 64-byte target is:

```text
target_cache_line_bytes=64
decision_frame_size_bytes=432
decision_frame_alignment_bytes=8
decision_frame_budget_bytes=448
decision_frame_arena_slot_stride_bytes=448
decision_frame_payload_budget_bytes=448
policy_artifact_size_bytes=912
policy_artifact_alignment_bytes=8
policy_artifact_budget_bytes=1024
policy_artifact_payload_budget_bytes=576
rule_evaluation_size_bytes=8
rule_evaluation_budget_bytes=16
apply_result_size_bytes=88
request_receipt_size_bytes=136
kernel_command_budget_bytes=640
kernel_result_budget_bytes=128
trading_state_size_bytes=57152
trading_state_budget_bytes=65536
decision_record_hot_cold_split_status=pending_phase3c
```

## Enforced behavior

- `VELOCITA_TARGET_CACHE_LINE_SIZE` is a required public compile definition,
  defaults to 64 in CMake, and is explicitly fixed to 64 by the ARM64 gate;
- the value must be a power of two and at least `alignof(std::max_align_t)`;
- canonical policy, decision-frame, kernel-command, and kernel-result byte
  ceilings are compile-time assertions;
- `DecisionFrame`, `PolicyArtifact`, `RuleEvaluation`, and `TradingState`
  object ceilings are compile-time assertions;
- `DecisionFrame` arena stride is derived from `sizeof(DecisionFrame)` and the
  target cache line, never from its canonical encoded length; and
- the ARM64 gate stores `layout-report.txt` and appends the same key/value data
  to its build metadata.

Raising a ceiling requires explicit review of working set, arena capacity,
snapshot size, and cache/TLB effects. It must not be treated as routine build
maintenance.

## C++ interpretation

`static_assert` executes during compilation. A field addition that crosses a
reviewed ceiling prevents every dependent target from compiling, before a
benchmark or deployment can accidentally bless it. This is stronger than a
runtime warning, but it only enforces a declared budget; it does not prove that
the chosen budget is fast.

The arena stride of 448 bytes is the smallest multiple of 64 that can contain
the current 432-byte `DecisionFrame`. Its 16-byte internal fragmentation is an
arena cost, not part of the 421-byte canonical payload. The object itself is
not given `alignas(64)` because its current ownership and access pattern do not
justify imposing that alignment on every instance.

False sharing requires independently written data on the same coherence line.
The current reducer is single-owner, so padding all contracts would increase
working set without eliminating a demonstrated coherence race. Phase 3C will
instead define a compact hot retained-decision record and separate cold audit
provenance, then report their sizes and access-oriented budgets.

## Limits and next gate

This increment supplies guardrails; it does not yet implement a decision
record, hot/cold retained storage, an allocator, authoritative evaluation,
ARM64/Pi acceptance, Raft integration, persistence, risk, orders, or trading.

Before Phase 3C turns green, its layout report must replace
`pending_phase3c` with concrete hot/cold record sizes, strides, capacities, and
total working-set cost. The existing 64 KiB `TradingState` ceiling has only
8,384 bytes of headroom, so blindly embedding a decision-record array should
fail and trigger the intended storage-layout review.
