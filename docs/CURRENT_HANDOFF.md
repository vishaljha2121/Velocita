# Current development handoff

Updated: 2026-09-07

Purpose: compact working memory for the next agent session. Verify this file
against Git before acting; do not reconstruct the project from chat history.

## Current branch and HEAD

- Branch: `v2/phase-4-risk-orders-outbox`
- Phase 4 base commit: `c25c45fe0d05978b9855685622b39fccc1481b84`
- Handoff checkpoint: annotated tag `v2-phase4-handoff-start`
- Accepted Phase 3 checkpoint: annotated tag `v2-phase3-accepted`, commit
  `8438d0d`

Run `git rev-parse HEAD` and `git show --no-patch --decorate HEAD`. At handoff,
the branch tip must be the commit named by `v2-phase4-handoff-start`.

## Dirty-worktree status

The following pre-existing work is intentionally uncommitted and must be
preserved. It belongs to passive model/market-data research and the proposed
Pi/NUC topology, not to Phase 4:

```text
 M CHANGELOG.md
 M docs/README.md
?? docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md
?? docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md
?? docs/adr/0002-two-machine-pi-nuc-deployment-topology.md
?? model/
```

Do not delete, reset, stage, or include these paths in Phase 4 commits. Inspect
`git status --short` at the start and before every commit. Commit Phase 4 files
by explicit path rather than using `git add .`.

## Last accepted phase

Phase 3—policy artifacts and authoritative decision evaluation—is accepted on
the host and physical Raspberry Pi 5.

Accepted behavior:

- an admitted canonical `DecisionFrame` is evaluated deterministically against
  an atomically activated feature-schema/executable-policy/instrument/venue
  bundle;
- bounded rules use reviewed array-order, first-match precedence;
- strategy control modes dominate economic policy;
- a matched template resolves to an exact `EconomicActionProposal` using
  checked fixed-point values and explicit venue/instrument constraints;
- authoritative and shadow outcomes, provenance, and exact proposals are
  retained in bounded hot/cold decision records;
- canonical command/result/snapshot schema V3, direct apply, snapshot replay,
  and generated-model replay agree;
- authoritative apply remains pure, bounded, transport-free, storage-free, and
  venue-free; and
- cache-line/layout ceilings are enforced, including a 64-byte hot decision
  record.

Acceptance evidence:

- Git tag: `v2-phase3-accepted`
- Evidence document:
  `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md`
- Physical Pi evidence directory:
  `~/.local/share/velocita/v2-phase3-acceptance-pi-gates/20260907T212930Z`
- Accepted source root:
  `f2d350757c9f2f71f0eab4e2600af982bea44d56027082fafc70764c77073f5b`
- Pi result: all Phase 3 suites passed; `throttled=0x0` before/after;
  43.55 C -> 49.60 C.
- In-memory Pi measurements only—no Raft/WAL/network:
  32-rule worst case p50/p99 `222/241 ns`, action resolution `185/204 ns`,
  authoritative reducer `740/778 ns`.

The measurements above are development signals, not end-to-end throughput or
venue-latency claims.

## Current phase

Phase 4—risk reservation, orders, and transactional outbox.

Objective: make an accepted authoritative economic decision atomically create
all four of the following, or none of them:

1. risk reservation;
2. cluster-owned order intent;
3. retained idempotency receipt/result; and
4. committed outbound effect with deterministic `EffectId` and payload digest.

Phase 4 owns end-state guarantees G-010 through G-020 and contributes to
G-021 through G-026 in `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`.

## Implemented behavior relevant to Phase 4

Already available as inputs/foundations:

- strong IDs including `DecisionId`, `ClusterOrderId`, and `EffectId`;
- bounded `EconomicActionProposal` created by Phase 3;
- canonical `OrderEffect` contract and payload codec as a boundary type;
- active policy, executor generation, kill epoch, and strategy mode;
- request deduplication and bounded decision hot/cold records;
- canonical kernel command/result/snapshot encoding and deterministic replay;
- compile-time object/payload ceilings and target cache-line reporting.

Not implemented yet:

- authoritative risk-limit or reservation ledgers;
- cluster order records and order lifecycle reducer;
- transactional outbox state/lifecycle;
- fill/venue-event deduplication;
- atomic decision -> reservation + order + effect transition;
- Phase 4 command/result/snapshot schema changes;
- Phase 4 model/property/replay corpus and target gate.

An existing `OrderEffect` value/codec is not evidence that an effect can yet be
created authoritatively. It is only a validated boundary contract.

## Decisions that must be reviewed before broad implementation

Keep the Phase 4 design minimal, bounded, and MVP-focused. Present the proposed
contracts to the user before locking them into canonical bytes:

1. exact risk-account dimensions and conservative reservation formula for
   submit, cancel, and replace;
2. minimum cluster order and reservation lifecycle states;
3. deterministic derivation and collision handling for `ClusterOrderId` and
   `EffectId`;
4. bounded capacities for orders, reservations, fill/event receipts, and
   outbox effects, including full behavior and provisional Pi/NUC headroom;
5. replacement semantics that cannot double reserve or briefly under-reserve;
6. partial-fill, cancel/fill-race, and unknown-submission accounting rules;
7. exact additions to `Command`, `ApplyResult`, canonical schema, snapshot, and
   state digest; and
8. which Phase 4 state is hot versus cold and its cache-line/arena budgets.

Prefer the conservative fail-closed choice when evidence is incomplete. Do not
silently clamp an unsafe proposal into an accepted order; return a typed reason.

## Exact next RED test

After the eight contract decisions above are reviewed, add the first focused
test target `v2-risk-order-outbox-tests` in:

```text
tests/v2_risk_order_outbox_tests.cpp
```

First RED behavior:

> Given a valid authoritative Phase 3 action proposal and sufficient current
> risk capacity, applying the Phase 4 authorization command must atomically
> produce exactly one reservation, one cluster order, and one pending outbox
> effect linked by stable identities. If any required capacity or risk check
> fails, all three counts and economic balances remain byte-for-byte unchanged.

The initial test must also assert:

- the effect references the created order and accepted decision;
- reserved exposure equals the reviewed conservative formula;
- request replay returns the retained IDs without changing any count/balance;
- a same-identity/different-payload retry is a typed conflict; and
- shadow/no-action/rejected Phase 3 decisions cannot enter this transition.

Observe the test fail for the intended missing Phase 4 contract/behavior before
implementing GREEN. Do not implement fill, cancel, replacement, gateway, Raft,
or WAL behavior in the first increment.

## Acceptance evidence required for Phase 4

The phase cannot close until retained evidence proves:

- no effect without reservation and order intent;
- duplicate request/decision returns the same effect, never a second mutation;
- insufficient or stale risk creates no order/effect;
- reservation/filled/remaining quantities conserve under partial fills;
- unknown submission and cancel-pending remain risk-bearing;
- replacement never double-reserves or under-reserves;
- replay reconstructs byte-identical pending/unknown effects;
- snapshot retains receipts/effects through the declared retry horizon;
- bounded full policies fail atomically; and
- fault/property/model tests cannot create an unreserved effect, negative
  reservation, duplicate economics, or replay mismatch.

Evidence must identify commit/source root, canonical schema/digest changes,
Release and sanitizer results, layout/capacity report, ARM64 container result,
physical Pi result, power/temperature state, limitations, and rollback point.

## Unresolved failures and provisional limits

- No known failing accepted Phase 3 test or physical Pi gate.
- Phase 4 has not started, so all Phase 4 guarantees remain unproven.
- The `TradingState` 80-KiB ceiling and current capacities are provisional until
  deferred NUC capacity validation; do not increase them silently.
- Phase 3's benchmark excludes Raft, WAL, networking, and venue I/O.
- TrueMarkets resting-order/post-only/order-book capability remains unconfirmed;
  that blocks adapter/live claims, not Phase 4's venue-neutral OMS work.
- The proposed two-machine topology is not accepted high availability.

## Important ADRs and contracts

Read only these first; chase additional references only when the current task
requires them:

1. `docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`, especially sections 2,
   3, 4, and 9 (Phase 4).
2. `docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md`, especially
   transactional outbox, authority, persistence, and non-negotiable invariants.
3. `docs/V2_ARCHITECTURE_CONTRACTS.md`, especially sections 3 and 7–10.
4. `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`, especially G-010–G-026 and
   deviation tripwires.
5. `docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md` for the accepted input boundary.
6. `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md` for the exact
   accepted checkpoint and limitations.
7. `docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md` only to avoid pulling deferred
   work into V2.

Primary C++ starting points:

- `include/velocita/v2/kernel.hpp`
- `src/v2_kernel.cpp`
- `include/velocita/v2/action_template.hpp`
- `include/velocita/v2/contracts.hpp`
- `include/velocita/v2/kernel_codec.hpp`
- `src/v2_kernel_codec.cpp`
- `include/velocita/v2/state_snapshot.hpp`
- `src/v2_state_snapshot.cpp`
- `tests/v2_decision_kernel_tests.cpp`
- `tests/v2_replay_tests.cpp`
- `tests/v2_model_replay_tests.cpp`
- `tests/v2_layout_tests.cpp`

## Commands needed to continue

Verify the handoff:

```bash
cd /Users/vishaljha/aeron-raft-pi
git status --short
git branch --show-current
git rev-parse HEAD
git rev-parse v2-phase4-handoff-start
git log --oneline --decorate -8
git diff --check
```

Read the bounded context:

```bash
sed -n '1,260p' docs/CURRENT_HANDOFF.md
sed -n '442,484p' docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
sed -n '1,240p' docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md
sed -n '1,340p' docs/V2_ARCHITECTURE_CONTRACTS.md
sed -n '45,115p' docs/V2_END_STATE_GUARANTEE_CHECKLIST.md
```

Run the existing host regression baseline using an out-of-tree build directory
(do not overwrite an existing user build):

```bash
cmake -S . -B build-phase4 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-phase4 -j2
ctest --test-dir build-phase4 --output-on-failure
```

Before a physical Pi gate, synchronize explicitly while preserving Pi-local
Git/build/evidence state:

```bash
rsync -av --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'build-*/' \
  ./ bhaikapi@100.84.151.116:~/aeron-raft-pi/
```

Do not invent a Phase 4 Pi acceptance command until its gate script and
evidence contract exist. The previous accepted Phase 3 gate is
`deploy/run-v2-phase3-acceptance-pi-gate.sh`; it is a regression reference, not
a Phase 4 acceptance gate.

## Explicit non-goals for Phase 4

- no execution gateway or external venue calls (Phase 5);
- no generic V2 Raft integration (Phase 6);
- no asynchronous WAL, block arena, snapshot publication, or compaction work
  beyond extending deterministic in-memory snapshot bytes (Phase 7);
- no executor fencing, automatic promotion, kill transport, or emergency lane
  implementation (Phase 8);
- no TrueMarkets order adapter, credentials, or live/paper order submission
  (Phase 9);
- no Grafana/release-operating expansion beyond metrics contracts (Phase 10);
- no strategy fitting, profitability claim, leaderboard claim, or real-money
  activation (Phase 11);
- no cross-strategy portfolio netting; it is explicitly deferred to V2.1;
- no `valid_state` performance rewrite without measurement; deferred to V2.1;
- no good-to-have feature that is not necessary for the Phase 4 invariants;
  defer such work to V2.2.

## New-session bootstrap prompt

Copy the prompt below into the new session:

```text
Work in /Users/vishaljha/aeron-raft-pi on Phase 4: risk reservation, cluster
orders, and the transactional outbox.

Read completely:
- AGENTS.md, if present
- docs/CURRENT_HANDOFF.md
- sections 2, 3, 4, and 9 of docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
- the directly referenced ADR/contracts listed in the handoff

Inspect Git status, branch, HEAD, the v2-phase4-handoff-start tag, and recent
commits to verify the handoff. Preserve every pre-existing dirty path listed in
the handoff. Never use git add . and do not reset or delete unrelated work.

Do not reconstruct the project from historical chat. Git, tests, ADRs,
contracts, evidence, and docs/CURRENT_HANDOFF.md are authoritative memory.

First explain the eight Phase 4 contract decisions in CTO-level and deep C++
terms, including rejected alternatives, and ask me to review material choices
before freezing canonical bytes. Then continue by TDD from the exact next RED
test in docs/CURRENT_HANDOFF.md. Show RED for the intended reason, implement the
smallest GREEN increment, run focused and regression tests, and keep me educated
on what, why, how, ownership, memory layout, complexity, failure behavior, and
trade-offs.

Maintain MVP focus. Phase 4 must not pull gateway, Raft, WAL, venue adapter,
strategy, netting, or optional V2.2 work forward. Update CURRENT_HANDOFF.md as
the phase advances.
```

