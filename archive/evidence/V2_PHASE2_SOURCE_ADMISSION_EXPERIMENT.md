# V2 Phase 2 source-admission experiment evidence

- Date: 2026-09-03
- Decision: retain strict normalized sequencing as an experimental increment
- Branch: `experiment/v2-phase2-kernel`
- Behavior commit: `33d27f6`
- ARM64 development-gate result: PASS
- ARM64 source root:
  `0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8`
- Raspberry Pi checkpoint gate: PASS
- Raspberry Pi evidence: `20260903T192942Z`

## Objective

Prevent duplicated, reordered, skipped, regressing, or contradictory external
facts from silently becoming authoritative economic state. This increment adds
only normalized source identity, cursor tracking, retained event digests, typed
admission outcomes, and tests. It does not apply fills, change positions,
create orders, reserve risk, emit outbox effects, or contact a venue.

## Reviewed protocol decision

The normalized identity is:

```text
SourceId + SourceGeneration + SourceSequence + canonical event digest
```

The accepted ordering contract is:

```text
new source:       generation 1, sequence 1
same generation:  next sequence = current sequence + 1
new generation:   generation = current generation + 1, sequence resets to 1
```

An exact retained identity and digest returns `already_applied`. The same
identity with a different digest returns `source_event_conflict`. An unseen
gap or regression returns a typed rejection and does not advance the source
cursor.

This is a normalized internal protocol, not a claim that every venue exposes
these exact counters. A venue adapter owns venue-specific reconnect, buffering,
snapshot, and sequence translation. The replicated reducer owns only the
small, deterministic admission rule. Missing evidence may represent a fill or
cancel, so the reducer fails closed rather than processing past a gap.

## TDD evidence

The source-admission test was written before the production types and reducer
branch. The intended RED failed because `RecordSourceEvent`, source state,
source outcomes, and `find_source_cursor` did not exist. The GREEN increment
added the smallest bounded state and transition required by those tests.

The host regression after implementation passes 9/9 test suites, including
`v2-kernel-source-tests`.

The committed source then passed the native ARM64 Ubuntu development gate:

| Configuration | Result |
|---|---:|
| Release, legacy + V2 | 9/9 pass |
| Benchmark-instrumented Release, V2 | 6/6 pass |
| ASan + UBSan Debug, V2 | 6/6 pass |

The retained run is `20260903T184710Z` under
`~/.local/share/velocita/v2-phase2-container-gates/`. It used Ubuntu 26.04,
GCC 15.2, four ARM64 CPUs, 4 GiB memory, 256 MiB `/dev/shm`, and two build
jobs. The source commit recorded by the gate is
`33d27f60e9baea75fd3b6321ee6399d239bbe11c`.

The same source root then passed on the physical Raspberry Pi 5 through the
pinned Ubuntu/GCC Podman gate:

| Pi gate layer | Result |
|---|---:|
| Hardware/power wrapper | PASS |
| Container build/test gate | PASS |
| Release, legacy + V2 | 9/9 pass |
| Benchmark-instrumented Release, V2 | 6/6 pass |
| ASan + UBSan Debug, V2 | 6/6 pass |

The Pi used the `ondemand` CPU governor and reported `throttled=0x0` before and
after the gate. The raw evidence is retained at
`~/.local/share/velocita/v2-source-admission-pi-gates/20260903T192942Z`.
Both the outer `summary.tsv` and inner `container-summary.tsv` report PASS with
exit status zero and no failure step.

## Invariants exercised

1. A source starts only at generation 1, sequence 1.
2. Accepted events advance a source cursor contiguously.
3. A generation changes only by one and resets sequence to one.
4. Exact source-event redelivery is a no-op even under another request ID.
5. A conflicting digest for a retained source identity fails closed.
6. Gaps and regressions do not mutate the accepted cursor.
7. A retained old-generation duplicate remains recognizable after rollover.
8. Zero identities and zero digests cannot enter source state.
9. Source and event-receipt capacity exhaustion is typed.
10. Capacity is proven before cursor mutation, preventing partial admission.
11. Request deduplication and source-event deduplication remain independent.

## C++ mechanism

`RecordSourceEvent` is another alternative in the closed `CommandBody`
`std::variant`. `TradingState` owns fixed-capacity `std::array` regions for
source cursors and source-event receipts. Lookup is bounded linear search. The
reducer performs no allocation, lock, I/O, clock read, or random read.

The transition order is:

```text
validate request identity and resolve request duplicate
prove request-receipt and state-version capacity
validate source identity and digest
resolve exact/conflicting source duplicate
validate generation and sequence continuity
prove source/event capacity
mutate cursor and append source-event receipt
retain the request result
```

The command dispatcher now uses `std::get_if<T>` rather than numeric variant
indices. This keeps dispatch attached to the alternative's type when new
commands are inserted and avoids silently coupling behavior to alternative
position.

## Known limitations

- The event-receipt capacity is 128 and there is no replicated expiry yet.
  Exhaustion deliberately rejects; this is not an indefinite-stream design.
- Source capacity is 16 and has not been sized from a production venue plan.
- No reconciliation command yet proves how an adapter repairs a gap before
  resuming admission.
- No canonical state digest or snapshot codec covers source state yet.
- No randomized reference model, crash replay, or Raft integration exists yet.
- A digest is carried by the command contract; canonical event normalization
  and cryptographic digest calculation belong to later adapter/codec work.

## Acceptance boundary

This increment is retained as green experimental Phase 2 evidence after its
host, ARM64 container, and Raspberry Pi checkpoint gates passed. It is not
complete Phase 2 acceptance and it does not authorize trading. Production use
additionally requires replicated
expiry/checkpointing, reconciliation semantics, canonical snapshot/replay,
Raft integration, and target-hardware evidence.

Run the pinned Raspberry Pi checkpoint gate with:

```bash
sudo -v
./deploy/run-v2-source-admission-pi-gate.sh --yes
```

The script builds and tests in a pinned Ubuntu/GCC Podman environment on the
physical Pi. It checks the canonical source root, leaves the running V1/V1.1
cluster untouched, retains raw evidence outside the repository, and fails if
Raspberry Pi firmware reports current or historical throttling during the
boot. The source manifest—not the Pi's local Git metadata—is the acceptance
identity because the established deployment excludes `.git`; Mac Git remains
authoritative while the Pi receives exact committed source bytes.

The inner gate therefore treats Git metadata as optional on the Pi and pins
the canonical source root instead. It writes container results to
`container-summary.tsv`; the outer hardware/power wrapper writes `summary.tsv`.
This preserves the causal failure layer instead of allowing one summary to
overwrite the other.

The shell script is orchestration, not application implementation. CMake, GCC,
and sanitizers live in the disposable test image, matching the project's
containerized deployment boundary; the Pi contributes the real CPU, kernel,
power, thermals, and resource contention. A host-native toolchain is optional
portability evidence rather than a prerequisite for this acceptance gate.
