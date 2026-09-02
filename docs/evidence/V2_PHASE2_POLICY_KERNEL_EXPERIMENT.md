# V2 Phase 2 policy-kernel experiment evidence

- Date: 2026-09-02
- Decision: retain as a green experimental increment; Phase 2 is incomplete
- Branch: `experiment/v2-phase2-kernel`
- Policy-kernel commit: `7bc9e42`
- First ARM64 development-gate commit: `c783ab7`
- Control-authority commit: `6de06ec`
- Extended ARM64 development-gate commit: `2849a52`
- ARM64 source root:
  `8e4cba2743760eb71bb3ee7277d63183c2e706cc3bec5a6d449f0eec21291a72`
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

The second increment adds replicated control authority:

- fail-safe boot in `halted` mode;
- compare-and-set strategy-mode commands fenced by the observed kill epoch and
  executor generation;
- strictly increasing kill epochs that atomically force `halted` mode;
- strictly increasing executor generations that remove new-risk authority;
- preservation of `halted` and `cancel_only` during executor promotion; and
- retained, typed outcomes for stale, repeated, regressing, and malformed
  identified control commands.

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

The control increment followed a second RED/GREEN cycle. The test target first
failed because `TradingState`, `CommandBody`, and `ApplyCode` did not yet expose
control state or commands. The GREEN implementation added only the three
control commands and their reducer rules; the shared receipt machinery was
reused unchanged.

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
10. Trading authority boots halted, with kill and executor generations at
    zero.
11. A strategy-mode command can change authority only when both expected
    generations equal the replicated state.
12. Kill and executor generations strictly increase; equal or regressing
    values produce retained typed rejections.
13. A kill-epoch advance and the forced halt are one reducer transition.
14. Executor promotion cannot retain normal or reduce-only new-risk
    authority; halted and cancel-only authority are preserved.
15. Unknown strategy-mode enum values cannot enter replicated state.

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

`KillEpoch` and `ExecutorGeneration` are distinct `StrongId` instantiations.
They have the same integer representation but are different C++ types, so the
compiler rejects accidental cross-domain assignment or comparison. Their
monotonicity is a replicated fencing mechanism, not a wall-clock ordering
claim.

`SetStrategyMode` is compare-and-set rather than a blind setter. Carrying both
observed generations makes an operator or controller prove which authority
state it evaluated. A delayed pre-kill or pre-promotion command therefore
cannot reopen trading after failover.

Every identified business rejection still advances the replicated state
version once and stores an exact receipt. That makes retries history-stable:
the same request identity cannot be reinterpreted after control state changes.

## Validation matrix

macOS ARM64 with Apple Clang 21:

- full Debug regression: 8/8 pass;
- focused Release policy and control suites: 2/2 pass; and
- focused ASan/UBSan policy and control suites: 2/2 pass.

Native ARM64 Ubuntu 26.04 with GCC 15.2, 4 CPUs, 4 GiB memory, and 256 MiB
`/dev/shm`:

| Configuration | Result |
|---|---:|
| Release, legacy + V2 | 8/8 pass |
| benchmark-instrumented Release, V2 | 5/5 pass |
| ASan + UBSan Debug, V2 | 5/5 pass |

The latest retained committed-source run is `20260902T144010Z` under
`~/.local/share/velocita/v2-phase2-container-gates/`.

## Limits and next tests

This increment is not Phase 2 acceptance and makes no throughput claim. It has
not yet implemented or proven:

- deterministic state digest or canonical snapshot bytes;
- reconciliation proof before restoring `normal` after a kill or executor
  promotion;
- receipt expiry and identity-retention horizon;
- order, reservation, position, fill, outbox, or reconciliation state;
- randomized reference-model comparison or replay corpus;
- allocation instrumentation and reducer apply-latency distribution;
- Raft/WAL integration; or
- Raspberry Pi power, thermal, storage, or scheduling behavior.

Because Phase 1 still awaits its Pi gate, this work remains on an experiment
branch. After Phase 1 acceptance, reviewed green commits can be cherry-picked
onto the official `v2/phase-2-deterministic-kernel` branch.
