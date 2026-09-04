# V2 Phase 2 generated model/replay experiment evidence

Date: 2026-09-03

- Behavior commit: `26f0853`
- Decision: retain as a green experimental increment; physical Pi acceptance
  is pending

## Objective

Move beyond a small handwritten corpus by comparing the production byte-level
replay path with a simpler, independently structured reference model across
many deterministic histories and snapshot boundaries.

## Test construction

The test uses 32 fixed seeds and 96 commands per seed: 3,072 commands enter the
model comparison. Each history is also replayed across six snapshot cut points,
adding 18,432 suffix-command executions.

The fixed prefix guarantees coverage of important outcome families. The
seeded generator then varies policy, control, source, exact-retry, and
identity-conflict interactions. Required coverage includes:

- applied and already-applied commands;
- exact request retries and duplicate identity conflicts;
- missing, immutable, non-monotonic, invalid-transition, and scope-conflicting
  policies;
- stale and non-monotonic control fences; and
- source conflicts, generation gaps/regressions, and sequence gaps.

For every original command the test compares:

1. the typed production `ApplyResult` with the reference-model result;
2. canonical result bytes; and
3. canonical production state bytes with a materialized model state.

For every snapshot cut it compares all suffix result bytes and the final
snapshot with the uninterrupted run.

## Reproducibility

The generator is deterministic. Failures report seed, command step, and where
applicable snapshot cut. The reviewed aggregate digest of the 32 final state
digests is:

```text
1eadcbdef0f9a244b935afdb3dd3be9c6e6f42fffe913509cc3baaea303357a0
```

Changing this digest requires semantic review; it must not be refreshed merely
to make a test pass.

## Evidence

Host:

- full Release regression: 13/13 passed;
- focused ASan/UBSan model-replay test: passed.

ARM64 Ubuntu 26.04/GCC 15.2 evidence:

```text
~/.local/share/velocita/v2-phase2-container-gates/20260904T001040Z
```

- Release: 13/13 passed;
- benchmark-instrumented V2: 10/10 passed;
- ASan/UBSan V2: 10/10 passed;
- source commit: `26f08531842ceb7179978a0b9a6ce79e9fc138af`;
- source root:
  `d6eb59adecac3a486c2eee83151df02d66e7425865dd5076ad34ac0beeb96668`.

## C++ design rationale

The reference model intentionally uses `std::vector` and `std::visit`, while
the production state uses fixed-capacity arrays and explicit dispatch. The
oracle is test-only, so heap use is acceptable. The structural difference
reduces correlated implementation errors.

The model reuses frozen domain types and contract validation but independently
implements state transitions and lookup/storage mechanics. It is therefore a
differential semantic oracle, not a second production implementation.

## Limits

- Deterministic generation explores many interactions but is not exhaustive.
- The model covers only currently implemented Phase 2 semantics.
- Capacity exhaustion receives dedicated scenario coverage elsewhere; generated
  histories remain below receipt capacity.
- This does not test real Raft, WAL, filesystem durability, concurrency,
  execution authority, or trading behavior.

## Physical Pi gate

After syncing the committed source:

```bash
BUILD_JOBS=2 ./deploy/run-v2-model-replay-pi-gate.sh --yes
```

The wrapper requires the exact source root recorded above.
