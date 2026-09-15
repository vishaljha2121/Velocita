# V2 Phase 5 corrected gateway and simulator acceptance evidence

Status: CANDIDATE. Host and ARM64 container evidence recorded below; x86-64 NUC
and physical Raspberry Pi 5 evidence are recorded as each gate completes. This
document does not claim acceptance until all four are present.

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

Pending. The NUC gate is a LAYOUT AND CAPACITY gate: the open question is whether
the bounded layout holds with x86-64 padding and alignment rather than ARM64's.

## Physical Raspberry Pi 5 evidence

Pending. The Pi was offline at the time of writing (`vishalkapi`, last seen 12h
before this run). Clean power evidence - `throttled=0x0` before and after, plus
temperature - is part of the acceptance record rather than a nicety: a throttled
run measures a different machine than the one that will trade.

## Promotion rule

This candidate is not accepted until the same pinned source root passes on all
four environments and this document records each with its gate-computed root.
Until then Phase 5 remains REOPENED, G-025 through G-034 remain open, and Phase 6
does not begin.
