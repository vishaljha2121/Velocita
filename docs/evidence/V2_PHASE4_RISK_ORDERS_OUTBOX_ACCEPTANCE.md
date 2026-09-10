# V2 Phase 4 risk, orders, and outbox acceptance evidence

Status: candidate; host gates pass, ARM64 container and physical Raspberry Pi
gates pending

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
- Phase 4 source root:
  `a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`
- branch: `v2/phase-4-risk-orders-outbox`
- Phase 4 start tag: `v2-phase4-handoff-start` at
  `c440b2c148c614c9e0368d4ce75060f6da90ccec`
- rollback point: `330cfe1b8f63699a22b0ad4ca4ea3de302a0c65e`

The source root covers `CMakeLists.txt` and the bounded V2 headers, sources, and
tests selected by the gate manifest. Gate scripts and this narrative are reviewed
separately and do not alter that content root.

## Canonical contracts frozen in the candidate

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

Result: pending.

Physical Pi command, after explicit source synchronization and `sudo -v`:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase4-pi-gate.sh --yes
```

Result: pending. Acceptance requires `throttled=0x0` before and after, with
temperature and CPU-governor evidence retained by the wrapper.

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
