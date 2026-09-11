# V2 Phase 5 gateway and simulator acceptance evidence

Status: HOST EVIDENCE RECORDED, TARGET GATES PENDING. Not accepted.

## Objective and promotion rule

Phase 5 makes external delivery safe: the cluster hands a gateway exact canonical
bytes, the gateway may refuse or defer but never originate or modify economics,
an ambiguous outcome is reconciled before any resend that could duplicate risk,
and nothing unresolved is ever presented as healthy.

Promotion requires the same pinned source root to pass the full Release and
ASan+UBSan suites on the development host, on native ARM64 Ubuntu in the
constrained container gate, and on the physical Raspberry Pi 5 with clean power
evidence. This document must record all three before its status can become
accepted. It currently records one.

## Candidate identity

- branch: `v2/phase-4-risk-orders-outbox`
- Phase 5 base: `dd77e61420c4ff5428fcd409cebdfebbe545cc77` (Phase 4 accepted)
- Phase 5 source root:
  `8d31c33e66a2345e0e8152ff42876b8eb7036f0250d9048f705cd1c6bee0bbc4`
- gate scripts: `deploy/run-v2-phase5-container-gate.sh`,
  `deploy/run-v2-phase5-pi-gate.sh`
- rollback point: `dd77e61420c4ff5428fcd409cebdfebbe545cc77`

The root covers `CMakeLists.txt` and the bounded V2 headers, sources and tests
selected by the gate manifest. Gate scripts and this narrative are reviewed
separately and do not alter that content root.

The root recorded here was computed on the development host with `shasum -a 256`.
The gate recomputes it inside the container with `sha256sum`. The two must agree;
until a gate has run once, treat the value as unconfirmed rather than verified.

## Contracts frozen in this candidate

- `VenueObservation` schema V2, 116 bytes, now carrying a seventh kind,
  `order_reconciled = 7`. Purely additive: no existing kind changed, no existing
  golden was invalidated, and the frame size is unchanged.
- gateway-local state - delivery attempts, reconciliation attempts, claimed and
  attempted effect states - is NOT consensus state and never enters `TradingState`.
- `QueryDisposition` cannot express an economic judgement. The gateway decides
  only whether it holds a trustworthy answer; the reducer decides what a
  trustworthy answer means.

Measured bounded layout, unchanged from Phase 4:

| item | bytes | ceiling | headroom |
|---|---:|---:|---:|
| `TradingState` | 81,168 | 81,920 | 752 |
| maximum canonical snapshot | 65,259 | 65,536 | 277 |

## Behavioral evidence

Permanent deterministic and randomized tests cover:

- the final pre-send guard as a pure function that reads no clock, mutates
  nothing, and yields the exact canonical bytes or nothing at all;
- a deterministic adversarial venue over virtual time with seeded faults, test
  only by CMake construction, reaching all nine G-033 classes;
- delivery planning that decides ambiguity before sends, so no tick can both
  declare an outcome unknown and retry it;
- redelivery that requires proof nothing reached the venue, with cancels as the
  reasoned exception since a duplicate cancel cannot create risk;
- reconciliation split so the gateway judges only answer trustworthiness while
  the reducer adjudicates match, conflict, and missing evidence;
- the full cancel lifecycle including redelivery, rejection, silence, and final
  reconciliation;
- a thirty-two seed fault matrix asserting no duplicate venue order, no fill lost
  without a trace, and no unresolved state presented as healthy - plus its own
  non-vacuity, so it cannot pass after it stops reaching its hard paths;
- retention horizon measurement under nominal and conflict-inducing loads; and
- an exhaustive breadth-first enumeration of the reachable order lifecycle, whose
  27-position inventory is asserted so a future edit cannot silently add or
  remove one.

The reducer still owns no clock, allocator, lock, network, filesystem, random
source, or venue handle.

## Host evidence

On 2026-09-10, at source root
`8d31c33e66a2345e0e8152ff42876b8eb7036f0250d9048f705cd1c6bee0bbc4`:

- Release full build and 29/29 CTest suites;
- Debug AddressSanitizer plus UndefinedBehaviorSanitizer full build and 29/29
  CTest suites;
- the stdlib-only model corpus at 109/109, untouched by Phase 5; and
- `git diff --check` clean.

Measured retention horizons under nominal adversarial faults, 48 seeds x 40
cycles (architecture §10.5.2): zero capacity refusals, zero orders reclaimed
while holding unresolved exposure, 24 order groups reclaimed, maximum order age
105 state versions, and both attempt bounds reached - 3 of 3 sends, 4 of 4
queries.

## Target gates

NOT RUN. Both scripts exist and are pinned to the candidate root.

ARM64 container:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase5-container-gate.sh --yes
```

Physical Pi, after explicit source synchronization and `sudo -v`:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase5-pi-gate.sh --yes
```

Record for each: the observed source root, compiler and kernel versions, runtime
cache-line size, CPU and memory limits, suite results, CPU governor, firmware
`throttled` before and after, and temperature before and after.

## Guarantee adjudication

Phase 5 closes G-025 and G-026: delivery is at least once by stable `EffectId`,
and an ambiguous outcome is explicitly unknown and reconciled before any resend
that could duplicate risk.

G-074a remains open by design and is now MEASURED rather than assumed. §10.5.3
records that every unresolved conflict permanently consumes one slot in the
decision, order, reservation and outbox arenas at once, so sixteen simultaneous
conflicts saturate the cluster. Operator escalation is load-bearing, not a
convenience.

## Limitations and deferred work

- No Raft integration, no WAL, no real venue, and no process boundary. The
  gateway is pure functions plus an in-process shell; §9.1.5 records that
  credential isolation under G-066 is a deployment property this does not yet
  prove.
- The simulator's faults do not compose, so fills never arrive out of order end
  to end and the matrix cannot produce a reconciliation conflict. Direct unit
  tests cover the branch. See `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md` §2.
- Three arenas - policies, sources, caller cursors - are never reclaimed and are
  therefore lifetime ceilings. An availability defect of the same class §8.6
  fixed for receipts, deliberately deferred past the MVP. See
  `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md` §1.
- The 80-KiB state capacity remains provisional pending NUC validation, which is
  now more load-bearing: it is what makes the sixteen-conflict bound immovable.
- This evidence authorizes no paper or live order delivery and makes no
  end-to-end latency, availability, or profitability claim.
