# V2 Phase 5 gateway and simulator acceptance evidence

Status: PASSED ON HOST AND ON THE PHYSICAL RASPBERRY PI 5. The ARM64 container
gate has not been run as a separate pass; see "Deviation from the Phase 4
promotion rule" below. Not yet accepted.

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
  `b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`
- gate scripts: `deploy/run-v2-phase5-container-gate.sh`,
  `deploy/run-v2-phase5-pi-gate.sh`
- rollback point: `dd77e61420c4ff5428fcd409cebdfebbe545cc77`

The root covers `CMakeLists.txt` and the bounded V2 headers, sources and tests
selected by the gate manifest. Gate scripts and this narrative are reviewed
separately and do not alter that content root.

The root recorded here is the value the gate itself computed on the physical Pi on
2026-09-11, not a development-host estimate. The first pinned value was wrong and
the gate correctly refused it: it had been computed under a UTF-8 locale, and
manifest ORDER decides the root while COLLATION decides manifest order - UTF-8
ignores `_` and `-` in primary weight where C compares bytes. The recipe now pins
`LC_ALL=C` at the point it is computed, so it is reproducible on any machine.

Recompute it by hand with:

```sh
{ sha256sum CMakeLists.txt;
  find include/velocita/v2 src tests -maxdepth 1 -type f \
    \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
    LC_ALL=C sort -z | xargs -0 sha256sum; } | sha256sum
```

On macOS substitute `shasum -a 256`; the output format is identical.

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

On 2026-09-11, at source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`:

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

## Target gate attempts before the accepted candidate

Two runs on the physical Pi on 2026-09-11 failed before reaching a verdict. Both
failures were in the GATE, not in the code, and both are recorded because each
exposed a real defect in how the candidate was being verified.

ATTEMPT 1 (`20260911T022401Z`) - source root mismatch. The pinned value had been
computed on a development host under a UTF-8 locale while the container computes
under C. Manifest ORDER decides the root and COLLATION decides manifest order, so
identical content hashed differently. Fixed by pinning `LC_ALL=C` inside the gate
recipe itself; a source root that depends on ambient environment is not a source
root.

ATTEMPT 2 (`20260911T022849Z`) - four suites reported "Not Run". The source root
matched and power was clean throughout, but the gate builds an explicit target
list which had drifted from `CMakeLists.txt`: the four Phase 5 suites were never
compiled. Fixed by deriving the gate's build targets from the build system itself
via the `velocita-all-tests` and `velocita-v2-tests` aggregate targets, which
cannot drift as suites are added.

The second failure is worth keeping in the record for its SHAPE. "Not Run" reads
like a behavioural failure and is not one, and the first hypotheses it invites -
char signedness, alignment, endianness - are all wrong. A gate that builds a
different set of things than it runs will always mis-signal in this direction.

## Physical Raspberry Pi 5 evidence

Command, after explicit source synchronization and `sudo -v`:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase5-pi-gate.sh --yes
```

Result: PASS on 2026-09-11, exit status 0, no failing step. Evidence on the Pi at
`~/.local/share/velocita/v2-phase5-acceptance-pi-gates/20260911T183855Z`.

- tested checkout: `04329fad05837e7f2c6e9d8d075ba8e5ede90700`;
- observed source root `b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`
  matched the expected root exactly, over a 60-file manifest;
- Raspberry Pi kernel 7.0.0-1016-raspi, AArch64 host, Ubuntu 26.04.1 LTS ARM64;
- GCC 15.2.0, CMake 4.2.3, 64-byte target and runtime L1D cache line;
- 4 CPUs, 2 GiB container memory, 256 MiB `/dev/shm`, `BUILD_JOBS=2`;
- Release: 29/29 suites;
- benchmark-instrumented: 26/26 V2 suites;
- Debug ASan+UBSan: 26/26 V2 suites;
- CPU governor: `ondemand`;
- firmware power before and after: `throttled=0x0`; and
- temperature: 46.30 C before, 50.70 C after.

Slowest suites under ASan+UBSan on target: `v2-phase4-property-tests` 52.53 s,
`v2-model-replay-tests` 15.75 s, `v2-gateway-delivery-tests` 2.46 s,
`v2-risk-order-outbox-tests` 0.34 s, `v2-lifecycle-reachability-tests` 0.10 s.
The four Phase 5 suites together add under three seconds to the sanitizer phase,
so the cost of the enumeration and fault-matrix work on real hardware is small.

The bounded layout reproduced byte-for-byte on ARM64/GCC: `TradingState` 81,168
bytes against the 81,920 ceiling, 752 bytes of headroom, maximum canonical
snapshot 65,259 of 65,536. That makes the sixteen-conflict saturation bound in
architecture §10.5.3 a hardware fact rather than a host-only measurement.

## Deviation from the Phase 4 promotion rule

Phase 4 recorded three passes: host, a standalone ARM64 container gate, and the
physical Pi. This candidate has host and physical Pi. The standalone container
gate has not been run.

Stated plainly rather than quietly dropped: the Pi gate EXECUTES THE SAME ARM64
container and additionally verifies the physical host is AArch64 and records power
and thermal evidence, so it is strictly stronger than the container gate on the
same machine. A separate container pass would be a parity run rather than new
information.

That is an argument, not an exemption. Until it is run, this document records two
of three and the candidate is not accepted:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase5-container-gate.sh --yes
```

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
