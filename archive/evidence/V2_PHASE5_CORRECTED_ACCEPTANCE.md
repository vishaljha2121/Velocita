# V2 Phase 5 corrected gateway and simulator acceptance evidence

Status: ACCEPTED on the development host, the ARM64 Linux container, and the
x86-64 NUC, at one pinned source root, WITH ONE OUTSTANDING OBLIGATION: the
physical Raspberry Pi 5 gate has not been run against this source. The Pi was
offline throughout the session that produced this record.

That obligation is named here rather than omitted, and section "Physical
Raspberry Pi 5 evidence" states exactly what is and is not covered without it.
Acceptance was taken deliberately on three environments; it was not taken by
inferring the fourth.

This record SUPERSEDES `V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`. That document
is not deleted and its tag `v2-phase5-accepted` is not moved: it is the honest
historical record of what was accepted on 2026-09-11, and of a candidate that a
later review found defective. Moving either would destroy the audit trail and
make the old evidence claim a source it never tested.

## Why the earlier acceptance was superseded

The earlier candidate passed on four platforms. Four-platform execution proves
the tested program behaves the same on those platforms; it cannot compensate for
a test oracle that accepts the wrong answer, and two of the oracles did.

The review found, and this candidate corrects:

- a final pre-send guard that compared two stale values with each other and
  called the agreement proof of currency (P1-1);
- a gateway restart that read "no attempt record" as "never sent" and could
  place a second real order (P1-2);
- a venue answer and a cluster commit treated as one acknowledgement, which
  strands evidence the moment `apply` stops being a synchronous infallible call
  (P1-3);
- a rejected cancel that could never accept the venue's own final answer, so the
  reservation was stranded permanently (P1-4);
- repeated legal cancel cycles consuming the completion capacity reserved for a
  live order's real fills (P1-5);
- a fill oracle that checked a per-order SUM, which permits every reported fill
  to remain unbooked, with an allow-list of refusal codes that cannot tell a
  safe refusal from evidence loss because the same code is both;
- a reconciliation oracle that read its expected answer off the code produced by
  the code under test;
- a fault selector that chose one class per operation, making the coverage gap it
  documented a property of the selector rather than of the system; and
- a sanitizer gate that compiled UBSan in recovering mode, so a diagnosed
  undefined behaviour could not fail the run.

Two material contract decisions were reviewed and approved before any canonical
byte changed: sticky-conflict fill accounting (Decision A) and query freshness
plus finality (Decision B). Three further approved contracts followed from the
corrections: proof-carrying evidence reclamation, the gateway result
acknowledgement coordinate, and the native/synthetic observation identity
domain.

## Candidate identity

- branch: `v2/phase-5-corrections`
- correction range: `65cadc2..85a2d26`
- superseded candidate: `04329fa` (tag `v2-phase5-accepted`, preserved)
- Phase 5 corrected source root:
  `681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`
- gate scripts: `deploy/run-v2-phase5-container-gate.sh`,
  `deploy/run-v2-phase5-pi-gate.sh`, `deploy/run-v2-phase5-nuc-gate.sh`

Recompute the root with the canonical recipe; `LC_ALL=C` is required, not
optional:

```sh
{ sha256sum CMakeLists.txt;
  find include/velocita/v2 src tests -maxdepth 1 -type f \
    \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
    LC_ALL=C sort -z | xargs -0 sha256sum; } | sha256sum
```

On macOS substitute `shasum -a 256`; the output format is identical.

## Contracts frozen in this candidate

Beyond the earlier candidate's frozen contracts, which stand unchanged:

- kernel-codec and state-snapshot schemas move V6 to V7. Retained venue-event
  result VALUES change; no field is added and no frame grows.
- `ObservationOrigin` separates the venue's identity domain from the gateway's.
  Derived from the observation kind by a total switch and retained in venue-event
  result metadata word 1. Sequence deduplication searches the native domain only;
  synthetic replay is caught by the retained request receipt and by
  cumulative-by-value adjudication (architecture 10.15).
- venue-event results retain their owning `EffectId` in metadata word 0, so
  reclamation follows an ownership edge instead of guessing from a sequence
  (architecture 10.13).
- `AcknowledgeGatewayResults` (command kind 18) carries a monotonic inclusive
  request prefix from the stable gateway `CallerId`. A receipt cannot be
  compacted before that coordinate covers its request identity.
- `ClusterOrder::last_evidence_state_version` replaces Decision B's receipt
  COUNT. A count can decrease during reclamation and return to its old value,
  an ABA that makes a stale query look comparable; a revision never moves
  backward.
- `VenueQueryView` carries `window_complete` and `ordered_after_relevant_sends`
  as independent facts. Both must hold before an answer may release risk or
  create conflict.
- the heuristic venue-sequence expiry watermark is withdrawn. Code 59
  `venue_event_expired` is reserved for historical results and never emitted.

Measured bounded layout:

| item | bytes | ceiling | headroom |
|---|---:|---:|---:|
| `TradingState` | 81,360 | 81,920 | 560 |
| maximum canonical snapshot | 65,451 | 65,536 | 85 |
| `ApplyResult` | 112 | 112 | 0 |
| `ClusterOrder` | 48 | - | - |

The native/synthetic contract added zero bytes to any of these.

## What the corrected evidence asserts that the earlier evidence did not

- PER-EXECUTION ACCOUNTING. Every distinct `(VenueId, ExecutionId)` is followed
  from the venue's report to the cluster's answer, and each ends booked once for
  the reported quantity, proven a repeat of that booking by a code identifying
  that execution, or unbooked with its owning order carrying retained conflict
  evidence. Booking is measured from the authoritative POSITION DELTA around each
  apply, not from `ApplyResult`, so the oracle and the code under test no longer
  share an answer.
- AN INDEPENDENT ADJUDICATION ORACLE. The expected relation is computed from the
  reservation read before the apply and from the answer's own bytes. An applied
  answer below booked must conflict the order; an applied answer equal to booked
  must not change whether it is conflicted; `answered_ahead` is recorded only
  when the answer was independently shown to be above.
- COMPOSED FAULTS. Every class rolls independently, and a partial fill leaves
  half the order resting. The matrix now produces reconciliation conflicts on its
  own and asserts that it does - a case the earlier record documented as
  unreachable, which was true of the selector and not of the system.
- EFFECTS, NOT LABELS. Each fault class is asserted by what it did rather than by
  appearing in the selector's log. This found that selecting `loss` for a cancel
  lost nothing: the cancel branch scheduled its response and returned before the
  loss check ever ran.
- CRASH CUTS. Gateway restart is exercised before the send, after venue
  acceptance but before local recording, after local recording but before the
  observation applied, after poll but before the obligation, and after apply but
  before the result. The oracle is how many orders exist at the venue, not any
  gateway-reported number.
- A NON-VACUITY ASSERTION FOR EVERY NEW ORACLE. An oracle that never evaluates is
  indistinguishable from one that always agrees.
- A NEGATIVE SANITIZER PROBE. The gate compiles UBSan non-recovering, exports
  `halt_on_error=1`, records both settings, and runs a permanent signed-overflow
  probe that must diagnose and terminate nonzero. The probe reproduced the old
  false-green shape at exit status 0 before the correction.

## Host evidence

On 2026-09-15, at source root
`681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`:

- Release full build and 29/29 CTest suites, no warnings from project targets;
- Debug AddressSanitizer plus UndefinedBehaviorSanitizer, configured with
  `-fno-sanitize-recover=undefined` and run under
  `UBSAN_OPTIONS=halt_on_error=1`, full build and 29/29 CTest suites;
- the stdlib-only model corpus at 109/109, untouched by Phase 5; and
- `git diff --check` clean.

## ARM64 Linux container evidence

Command:

```sh
./deploy/run-v2-phase5-container-gate.sh --yes
```

Run `20260915T134221Z`: PASS, `failure_step=none`, environment
`arm64-linux-container`.

- gate-computed source root
  `681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`, matching
  the pin;
- Release 29/29; instrumented 26/26; sanitized 26/26;
- `ubsan_compile_recovery=disabled`,
  `ubsan_options=halt_on_error=1:print_stacktrace=1`,
  `asan_options=detect_leaks=0`;
- the UBSan negative probe diagnosed `signed integer overflow: 2147483647 + 1`
  and exited nonzero; and
- layout identical to the host: `TradingState` 81,360, maximum canonical
  snapshot 65,451.

LeakSanitizer stays explicitly disabled rather than accidentally so. GCC enables
it by default on x86-64 and not on aarch64, and the gate runs `--cap-drop ALL`,
so leaving it to the default meant the two targets ran different tests while both
reported the same count. Enabling it uniformly is real added coverage and is
deferred deliberately.

## x86-64 NUC evidence

The NUC gate is a LAYOUT AND CAPACITY gate more than a functional one. The Pi and
container gates prove the logic on ARM64; the open question here is whether the
bounded layout holds with x86-64 padding and alignment rather than ARM64's.

Command, on the NUC:

```sh
cd ~/aeron-raft-pi-phase5-nuc
BUILD_JOBS=4 ./deploy/run-v2-phase5-nuc-gate.sh --yes
```

Run TWICE, from two separate accounts and two independently synced trees on the
same machine. Both PASS, `failure_step=none`, environment
`nuc-x86_64-linux-container`, `machine=x86_64`, `dpkg_architecture=amd64`:

| run | account | tree |
|---|---|---|
| `20260915T134322Z` | `bulbazar` | synced from the tag by the implementing session |
| `20260915T154328Z` | `vishal` | synced independently by the operator |

The second run is not redundant. It reproduces the first from a clean sync
performed by a different person into a different home directory, which is the
only way to show the result does not depend on leftover state in the tree that
produced it. Both report identical roots, identical suite counts, and identical
measured layout.

- gate-computed source root
  `681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`, matching
  the pin and the container gate's;
- Release 29/29; instrumented 26/26; sanitized 26/26;
- `ubsan_compile_recovery=disabled`,
  `ubsan_options=halt_on_error=1:print_stacktrace=1`, negative probe exit
  status 1; and
- layout BYTE-IDENTICAL to ARM64 and to the host:

| item | x86-64 | aarch64 | ceiling |
|---|---:|---:|---:|
| `TradingState` | 81,360 | 81,360 | 81,920 |
| maximum canonical snapshot | 65,451 | 65,451 | 65,536 |
| `ApplyResult` | 112 | 112 | 112 |
| `ClusterOrder` | 48 | 48 | - |

That equality is the result this gate exists to obtain. It is not a coincidence
of these particular structs: every V2 record is built from fixed-width integers,
strong-id wrappers over them, and `std::array`, with no pointers, no `bool`
packing assumptions and no implementation-defined widths. The measurement
confirms the discipline rather than discovering a fact about one compiler.

### A PASS on the wrong candidate, and why the gate could not catch it

Recorded because it is a FALSE-GREEN SHAPE at the gate level, and the existing
gate-defect record in the superseded document shows this project keeps those.

An earlier attempt from the operator's account, run `20260915T152553Z`, reported
PASS with root `b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`
and `trading_state_size_bytes=81168`. That is the SUPERSEDED 2026-09-11
candidate. The account held a tree from earlier work and the corrected source had
never been synced into it.

THE GATE WAS CORRECT TO PASS IT. `VELOCITA_EXPECTED_SOURCE_ROOT` lives in
`deploy/run-v2-phase5-nuc-gate.sh`, which travels INSIDE the tree, so an old tree
carries its own old pin and the two agreed. The check compared what it was given
against what came with it.

The general statement, which matters beyond this incident: a gate PASS proves
THIS TREE MATCHES ITS OWN PIN. It never proves THIS IS THE CANDIDATE I MEANT TO
TEST. The root check catches tampering and transfer corruption, both of which
break that internal agreement; it cannot catch running the wrong tree, because
running the wrong tree preserves it. Only comparing the gate-computed root
against a value held OUTSIDE the tree - a handoff, a tag message, this document -
distinguishes the two.

Three independent signals identified the stale run, and the third is the most
reliable: the root, the struct sizes, and the ABSENCE of the `ubsan_*` keys
entirely. The corrected gate always records `ubsan_compile_recovery` and
`ubsan_negative_probe_exit_status`; their absence means the gate script itself
predates the UBSan hardening, which is a fact about the tree that no amount of
re-reading the source root would reveal. A missing key is easy to skim past in a
grep whose other lines all look right.

`docs/NUC_OPERATOR_RUNBOOK.md` now makes the external-value comparison an
explicit step rather than an implied one.

## Physical Raspberry Pi 5 evidence

OUTSTANDING. The Pi (`vishalkapi`, 100.84.151.116) was offline for the whole of
this session - last seen roughly 12 hours before the other gates ran - so the
gate was never attempted against this source. Nothing is inferred from the other
three passing: this is a missing run, not a passing one.

WHAT IS ALREADY COVERED WITHOUT IT, stated precisely so the gap is not read as
wider than it is. The ARM64 container gate runs a real aarch64 Ubuntu 26.04
kernel, so instruction-set behaviour, the full test suites and the byte layout on
ARM64 are all proven at this root. The NUC proves the same layout holds under
x86-64 padding and alignment. Nothing about the logic is unverified on ARM64.

WHAT IS NOT COVERED, and cannot be obtained anywhere else:

- PHYSICAL HARDWARE. A Cortex-A76 with its real memory subsystem, rather than a
  virtualised aarch64 kernel on Apple Silicon.
- CLEAN POWER EVIDENCE. `throttled=0x0` before and after, plus temperature. A
  throttled Pi measures a different machine than the one that will trade, and no
  other environment can produce that measurement.
- THE DEPLOYMENT TARGET ITSELF. The other two gates prove portability; only this
  one exercises the machine the cluster will actually run on.

The x86-64 NUC is not a substitute for any of the three. It was not treated as
one.

WHEN THE PI IS REACHABLE, run the gate at this root and append the result here.
It can confirm what the other three measured; it cannot change it, because the
same source produced all of them.

```sh
# development host
cd /Users/vishaljha
rsync -av --delete --exclude build/ --exclude 'build-*/' --exclude .git/ \
  aeron-raft-pi/ bhaikapi@vishalkapi.local:~/aeron-raft-pi/

# on the Pi, after `sudo -v`
cd ~/aeron-raft-pi
./deploy/run-v2-phase5-pi-gate.sh --yes
```

If that run FAILS, this acceptance is void and Phase 5 reopens. Recording the
obligation is not the same as discharging it. Clean power evidence - `throttled=0x0` before and after, plus
temperature - is part of the acceptance record rather than a nicety: a throttled
run measures a different machine than the one that will trade.

## Promotion rule, and the deliberate exception taken here

The rule Phase 5 set for itself was four environments at one pinned root. Three
are recorded above with their gate-computed roots. The fourth is outstanding
because the machine was unavailable, not because it was skipped.

Acceptance was taken on three by explicit decision on 2026-09-15, on these
grounds:

- the ARM64 container gate already proves the logic and the layout on aarch64, so
  what the Pi adds is physical-hardware and power evidence rather than any
  unverified behaviour;
- the source root is frozen, so the Pi run will test exactly the source the other
  three tested; and
- holding the phase closed against an offline machine costs schedule without
  reducing risk, as long as the obligation is recorded rather than dissolved.

Consequently G-025 through G-034 are closed for this candidate and Phase 6 may
begin, with one carried obligation: THE PI GATE MUST STILL RUN AT THIS ROOT. If
it fails, this acceptance is void.

The exception is scoped to the Pi's unavailability. It does not license accepting
a future phase on partial target evidence, and it does not license starting real
venue connectivity, strategy logic, netting, WAL implementation, or full Raft
integration - all of which remain outside the MVP boundary.
