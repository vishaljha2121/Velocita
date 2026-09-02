# V2 Phase 2 policy-kernel experiment evidence

- Date: 2026-09-02
- Decision: retain as a green experimental increment; Phase 2 is incomplete
- Branch: `experiment/v2-phase2-kernel`
- Kernel commit: `7bc9e42`
- ARM64 development-gate commit: `c783ab7`
- ARM64 source root:
  `627b38f488aad60d3f0a65874a4bb132e196400c31c2e406f4b615e1095221d3`
- Raspberry Pi acceptance: not run

## Objective

The first Phase 2 increment establishes a pure deterministic state-machine
boundary before adding strategy evaluation, orders, risk, outbox delivery,
Raft, storage, or venue I/O. It implements only:

- immutable command identity as `CallerId + RequestSequence + payload digest`;
- fixed-capacity policy and request-receipt regions;
- proposed, validated, active, and superseded policy transitions;
- one active policy per account/instrument scope;
- monotonic policy versions and immutable policy identity; and
- exact duplicate-result retention plus conflicting-duplicate rejection.

## TDD evidence

The intended RED build reached Apple Clang and failed at:

```text
fatal error: 'velocita/v2/kernel.hpp' file not found
```

Two earlier failures did not count as TDD evidence: one lacked Ninja and one
could not fetch dependencies in the restricted network environment. A valid
RED proves that the missing behavior caused the failure, not that the build
machine was misconfigured.

The smallest GREEN implementation introduced `velocita_v2_kernel`, the closed
`CommandBody` variant, fixed-capacity `TradingState`, typed `ApplyResult`, and
the no-I/O `apply` function. Boundary tests were then added for receipt
exhaustion and state-version exhaustion.

## Core invariants proven by tests

1. An exact duplicate returns the retained original `ApplyResult` and does not
   advance state.
2. Reusing a request identity with another payload digest fails without state
   mutation.
3. A valid identified business rejection is retained, so a later retry cannot
   be reinterpreted against newer state.
4. A policy identity cannot name different artifact digests.
5. A policy family cannot publish a regressing version.
6. Activation requires validation and cannot create two active policies for
   one account/instrument scope.
7. Policy-catalog exhaustion is typed and cannot write past capacity.
8. Receipt capacity is checked before business mutation; the reducer will not
   accept a policy whose idempotency result cannot be retained.
9. State-version exhaustion fails closed rather than wrapping.

## C++ design review

`CommandBody` is a `std::variant` because the set of reducer commands is closed
and every alternative is handled explicitly. The variant is never copied to a
wire or snapshot; those formats remain explicit field codecs.

`TradingState` uses `std::array`, so its capacity and memory ownership are
fixed. `apply` performs linear bounded lookup and no allocation, lock, system
call, clock read, random read, external I/O, or exception-based business
control flow. Linear lookup is intentionally simple for this contract
increment; its `O(N)` cost must be measured before production retention limits
are selected.

Mutation ordering is:

```text
validate command identity
resolve exact/conflicting duplicate
prove receipt capacity
prove state-version capacity
evaluate and mutate policy state
retain the no-throw result
```

All values assigned between policy mutation and receipt insertion are
statically no-throw. This establishes function-level atomicity. It does not
provide crash durability; later Raft and storage phases wrap this reducer in
commit, replay, and snapshot semantics.

The state comment forbids retaining references across `apply`. `std::array`
does not reallocate, but later replicated expiry may reuse a slot; stable
addresses do not imply stable logical identity.

## Validation matrix

macOS ARM64 with Apple Clang 21:

- full Debug regression: 7/7 pass;
- focused Release policy-kernel suite: pass; and
- focused ASan/UBSan policy-kernel suite: pass.

Native ARM64 Ubuntu 26.04 with GCC 15.2, 4 CPUs, 4 GiB memory, and 256 MiB
`/dev/shm`:

| Configuration | Result |
|---|---:|
| Release, legacy + V2 | 7/7 pass |
| benchmark-instrumented Release, V2 | 4/4 pass |
| ASan + UBSan Debug, V2 | 4/4 pass |

The retained committed-source run is `20260902T104922Z` under
`~/.local/share/velocita/v2-phase2-container-gates/`.

## Limits and next tests

This increment is not Phase 2 acceptance and makes no throughput claim. It has
not yet implemented or proven:

- monotonic strategy mode, kill epoch, or executor generation;
- deterministic state digest or canonical snapshot bytes;
- receipt expiry and identity-retention horizon;
- order, reservation, position, fill, outbox, or reconciliation state;
- randomized reference-model comparison or replay corpus;
- allocation instrumentation and reducer apply-latency distribution;
- Raft/WAL integration; or
- Raspberry Pi power, thermal, storage, or scheduling behavior.

Because Phase 1 still awaits its Pi gate, this work remains on an experiment
branch. After Phase 1 acceptance, reviewed green commits can be cherry-picked
onto the official `v2/phase-2-deterministic-kernel` branch.
