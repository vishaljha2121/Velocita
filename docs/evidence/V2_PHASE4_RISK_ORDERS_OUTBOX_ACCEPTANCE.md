# V2 Phase 4 risk, orders, and outbox acceptance evidence

Status: accepted on host, ARM64 Ubuntu container, and physical Raspberry Pi 5

## Objective and promotion rule

Phase 4 makes authoritative decision authorization one deterministic transaction:
reserve risk, create one cluster-order intent, retain its idempotency result, and
create one reconstructable outbox effect—or change none of those economics.

Promotion requires the same pinned source root to pass the full Release and
ASan+UBSan suites on the host, native ARM64 Ubuntu 26.04 in the constrained
container gate, and the physical Raspberry Pi gate with clean power evidence.
This document must record all three before its status can become accepted.

## Candidate identity and rollback

- source commit: `db13a9c19caf7d5ac67ce1117b78f6302b6bcd3e`
- gate runner commit: `eca184c8fe9f8aa9d03ddb27c1ec4cf9bc957a98`
- Phase 4 source root:
  `a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`
- branch: `v2/phase-4-risk-orders-outbox`
- Phase 4 start tag: `v2-phase4-handoff-start` at
  `c440b2c148c614c9e0368d4ce75060f6da90ccec`
- rollback point: `330cfe1b8f63699a22b0ad4ca4ea3de302a0c65e`

The source root covers `CMakeLists.txt` and the bounded V2 headers, sources, and
tests selected by the gate manifest. Gate scripts and this narrative are reviewed
separately and do not alter that content root.

## Canonical contracts frozen in the accepted checkpoint

- kernel command/result and state snapshot schema V5; older schemas are rejected;
- `ConfigureRiskLimits` command kind 17 with an 88-byte body, strict expected
  `RiskVersion`, kernel-selected exactly-next version, current kill/executor
  generations, and no balance overwrite;
- OrderEffect schema V2, 166 bytes, with explicit `PostOnlyMode`;
- deterministic cluster order/effect identities and domain-separated payload
  digests, with full payload reconstruction from retained historical provenance;
- applied fill/order-event results use the existing result `DecisionId` as their
  zero-byte order-group ownership edge; and
- bounded caller prefixes, request receipts, venue-event identities, decisions,
  orders, reservations, and outbox effects with typed fail-closed admission.

No canonical byte field was added by the final acceptance corrections. Canonical
values did change when first-admission caller cursors and applied-observation
owners became mandatory. Old unaccepted V5 development snapshots omitting those
values are intentionally unsupported.

## Behavioral evidence

Permanent deterministic and randomized tests cover:

- atomic risk/order/receipt/effect creation and exact duplicate outcomes;
- current-policy, generation, version, risk, arithmetic, and capacity refusals
  before economic mutation;
- partial/full buy and sell fills, position/reservation conservation, duplicate
  and conflicting fill/event identities, and cancel/fill reorderings;
- unknown submit and cancel risk retention, effect-operation scoping, final
  reconciliation, and no blind release;
- safe serial replacement by cancel, final reconciliation, fresh evaluation, and
  fresh authorization under current controls; native amend and overlap refuse;
- byte-identical pending/unknown/cancel effect materialization through control and
  risk-configuration changes, command replay, and snapshot restore;
- dense-arena reclamation without stale semantic identity, including compaction
  inside cancellation and 119 completed orders across the old venue-event bound;
- bounded completion/cancel headroom that refuses new risk before admitted orders
  can be stranded by an arena limit; and
- an independent Phase 4 model/property corpus with seed/step diagnostics and an
  independent signed-position ledger.

The reducer owns no clock, allocator, lock, network, filesystem, random source,
or venue handle. Metrics are a disposable pure projection and are not consensus
state.

## Host evidence

On 2026-09-10, the candidate passed:

- Release full build and 25/25 CTest suites;
- Debug AddressSanitizer plus UndefinedBehaviorSanitizer full build and 25/25
  CTest suites; and
- `git diff --check`.

Measured bounded layout:

| Item | Bytes | Ceiling | Headroom |
|---|---:|---:|---:|
| `TradingState` | 81,168 | 81,920 | 752 |
| maximum canonical snapshot | 65,259 | 65,536 | 277 |
| `ApplyResult` object | 112 | 128 canonical | n/a |

## Target gates

ARM64 container command:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase4-container-gate.sh --yes
```

Result: PASS on 2026-09-10. Evidence:
`~/.local/share/velocita/v2-phase4-container-gates/20260910T151843Z`.

- Ubuntu 26.04 ARM64, GCC 15.2.0, 64-byte runtime L1D cache line;
- 4 CPUs, 4 GiB memory, and 256 MiB `/dev/shm` limits;
- expected and observed source root matched exactly;
- Release: 25/25 suites;
- benchmark-instrumented Release: 22/22 V2 suites; and
- Debug ASan+UBSan: 22/22 V2 suites.

Physical Pi command, after explicit source synchronization and `sudo -v`:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase4-pi-gate.sh --yes
```

Result: PASS on 2026-09-10. Evidence on the Pi and copied to the development
host:
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`.

- tested checkout: `9f61247645140a544a4db6bba1ba1d62dcacb080`;
- expected and observed source root matched exactly;
- Raspberry Pi kernel 7.0.0-1016-raspi, AArch64 host and ARM64 Ubuntu 26.04.1;
- GCC 15.2.0, 64-byte target and runtime L1D cache line;
- 4 CPUs, 2 GiB container memory, and 256 MiB `/dev/shm` limits;
- Release: 25/25 suites in 3.24 seconds;
- benchmark-instrumented Release: 22/22 V2 suites in 3.19 seconds;
- Debug ASan+UBSan: 22/22 V2 suites in 69.26 seconds;
- CPU governor: `ondemand`;
- firmware power before and after: `throttled=0x0`; and
- temperature: 44.10 C before, 50.15 C after.

## Guarantee adjudication

Phase 4 closes G-010 through G-024: atomic authorization, no unreserved effect,
layered caps, checked arithmetic, conservation, fill authority, retained
cancel/unknown risk, safe serial replacement, bounded failure, complete replay,
and stable at-most-once identities/outcomes within the declared retention
contract.

G-025 and G-026 remain open for Phase 5. Stable EffectId and explicit unknown
state exist, but no gateway currently performs external delivery or experiences
a post-send timeout. G-074a also remains open by design: missing final venue
evidence blocks the slot and is visible in metrics, but operator escalation and
recovery belong to Phases 8 and 10.

## Acceptance decision

Accept Phase 4 for source root
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
The physical target reproduced the same canonical source and bounded layout and
passed every required suite without sanitizer failure, undervoltage, or
throttling. The annotated checkpoint is `v2-phase4-accepted`.

This decision authorizes Phase 5 simulator/gateway TDD planning. It does not
authorize venue connectivity, paper/live delivery, or real-money trading.

## Limitations and deferred work

- No gateway, real venue, Raft integration, WAL, or external I/O is exercised;
  those belong to Phases 5-7.
- Native amend and overlapping replacement remain disabled. Only the reviewed
  serial replacement lifecycle is accepted.
- A missing final reconciliation leaves risk and its logical slot blocked. This
  deterministic fail-closed behavior is observable, but operator recovery is the
  G-074a work owned by Phases 8 and 10; Phase 4 invents no wall-clock timeout.
- A venue-session sequence watermark and caller-driven identity expiry remain
  deferred until the Phase 5 simulator/venue contract can justify their horizon.
- The 80-KiB state capacity remains provisional until deferred NUC validation.
- This evidence authorizes no paper or live order delivery and makes no
  end-to-end latency, availability, or profitability claim.
