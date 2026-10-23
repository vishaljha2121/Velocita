# Effect 25: resolve a venue-failed order that can never land

Contract: [ADR 0027 amendment of 2026-10-16](../../../../docs/adr/0027-unsent-effect-release-and-reviewed-baseline.md).
Owner approvals 2026-10-16: the automatic gateway path, a read-only recovery-only gateway start after the
review window with an owner-minted TM token, a scoped commit and native qualification.

## What changed

| Area | Change |
|---|---|
| Kernel (`v2_swap.cpp`, `swap.hpp`) | `Outcome::expired_unexecuted`: from `attempted`/`unknown` only; zero amounts; zero transaction exactly for this outcome; releases the reservation; counts in the settlement window; a later different settlement is a conflict; snapshots revalidate it |
| Chain proof (`v2_swap_settlement_evidence.cpp`) | `prove_unexecuted_on_chain`: finalized `isBlockhashValid` false at slot S, `getBlockTime(S)` at least 60 s after the envelope, owner history with `minContextSlot` S paged back past creation, every candidate's message compared with the retained payload |
| Admission (`v2_swap_settlement_admission.cpp`) | `admit_expired_unexecuted_settlement`: exact retained identity, zero journaled transaction, TM terminal `failed` with 0 executed, 0 fee, leaves present, no hash, then the chain proof |
| Production (`v2_swap_runtime_production.cpp`) | dispatch to the new path when TM reports no transaction; `swap_venue_failed_order` mapping; `WARNING settlement unresolved stage= code=` once per change; startup predecessor check |
| Fences | gateway/signer predecessor fence, submission resolution, prepare session and store predecessor accept a zero transaction only for this outcome |
| HTTPS allowlist (`v2_swap_runtime_https.cpp`) | `swap_canonical_rpc_request` admits finalized `isBlockhashValid`, `getBlockTime`, and bounded `getSignaturesForAddress` with `minContextSlot` (optionally `before`) |

## Evidence (Mac, Release, cache line 128)

- Full suite: [ctest-summary.txt](ctest-summary.txt), 206/206.
- New unit tests in `tests/v2_swap_failed_settlement_tests.cpp`: `chain_proven`, `chain_refusals`, `rpc_templates`,
  `expired_admission`, `expired_reducer`; `tests/v2_swap_runtime_production_tests.cpp` startup and mapping cases.
- Runtime drills on the in-process three-voter cluster (`tests/v2_swap_gateway_runtime_tests.cpp`): mode 18 (six continuous
  effects, 2 and 5 expire, restarts), 19 (recovery-only restart resolves an expired effect: the effect 25 shape),
  20 (crash after the settlement commits), 21 (crash before it commits).
- Mutations: [mutations.json](mutations.json), 36/36 killed with recompilation proven by object and binary hashes
  (`tests/mutation_swap_expired_settlement.py`). Two first-run survivors were a harness assertion string and a missing
  mid-proof provider-change test, both fixed and rerun.

## Native qualification

36de930 qualified on NUC1 (59 ctest, 54 continuous effects). On the Pi the gateway runtime suite hit its 120 s ctest
limit (it already took 116 s at ba299ff; the new modes added the rest). Its first Pi attempt also failed once in mode 0,
which passed 15/15 on rerun; both attempts are archived on the Pi as `bounded-native-20261013-36de930…-attempt1` and the
timed-out run. `64e6f9f` raises only that limit to 300 s and qualified on both hosts:
[qualification-64e6f9f.json](qualification-64e6f9f.json) (Pi runtime suite 143.9 s). NUC1 binaries are byte-identical to
36de930. Voter images with in-container hash and keep-id checks: [packaged-images.json](packaged-images.json).

## Not yet done

The halt, rolling voter upgrade, owner recovery-only start and renewal: [OWNER_STEPS.md](OWNER_STEPS.md).
All voters must run this build before an `expired_unexecuted` settlement commits.
