# CP-018 — parallel F5 reproduction/contracts and F6 worker-error correction

2026-09-22, branch v2/phase-5-corrections, base HEAD d44a4a9. All remediation
uncommitted. Owner requested parallel closure with MVP focus; two bounded workers
handled error-latch implementation and capacity review. Main owned integration,
new REDs, the polling-bypass correction, framing proposal and acceptance claims.

## F5: newly executable open failures, not GREEN acceptance

`tests/v2_history_identity_red.cpp` is a standalone diagnostic, outside passing
CTest until the reviewed contracts are implemented. Both deployments have valid
startup identities, same catalog/voter numbers and different incarnations.

- `metadata-red.txt`: valid foreign metadata accepted under the target sidecar.
- `wal-red.txt`: copying foreign WAL changes recovered kill epoch from 1 to 9.
- `snapshot-red.txt`: copying a real compacted foreign snapshot likewise restores
  kill epoch 9 instead of target history's 1.
- `peer-red.txt`: a vote message emitted by another actual kernel, encoded and
  decoded through the existing wire codec, changes target term from 0 to 1.
- `missing-hard-state-red.txt`: a voter with a verified durable self-vote at term1
  reopens at term0 after its metadata is moved aside. The file remains retained
  under the diagnostic's private root; no user data is removed.

All five current-source runs exit1 for the printed intended reason. The first
snapshot fixture had too few entries to compact; main corrected setup to four
committed entries and retained only the intended final RED as evidence. Original
setup failure is NOT a production defect. On conversion to CTest, require exact
typed refusal/unchanged tree plus healthy controls, not any exception as success.

History diagnostics directly compiled against rebuilt Release archives; shared
source was never mutated to fabricate failures. Metadata tuples may be identical
across lineages today: the missing binding is precisely why provenance cannot be
distinguished. WAL/snapshot cases additionally demonstrate changed economic state.

## F6: worker exceptions and permanent failure

Previously an exception from WalWriter::poll in the background thread escaped
the thread entry point. New code catches it, stores exception_ptr, then publishes
an atomic permanent failure flag with release ordering. Owner reads acquire
before inspecting/rethrowing the exception. Only the worker writes the captured
exception; it is not reset/reused concurrently. stop always joins even after the
worker clears running. Destructor does not rethrow. Kernel service boundaries
and is_leader consult the latch; diagnosis/recovery remain possible.

Main review found a bypass in the first worker draft: direct submit/poll could
resume a failed writer and publish a completion. Main added `writer-retry`, built
and observed `failed writer published durability through direct poll retry`
BEFORE fixing it. submit now refuses latched writers and poll rethrows the
permanent failure before touching storage. `writer-retry-green.txt` passes.

`threaded-flush` injects an actual background sync failure. A condition variable
establishes that the injection was reached, and stop/join establishes completion
of latch publication. No sleep is the success oracle; a five-second wait bound
only turns missing progress into a test failure. Assertions cover no usable
leadership, no protocol/result/proof service and no writer restart. The raw-writer
case asserts no durability completion or written-block progress after retry.

This is owner-boundary error observation, not instantaneous cancellation across
threads. A failure racing after a boundary check is not claimed to retract prior
messages. No volatile completions are published by a failed sync batch. Existing
quorum-durability and F4 proof checks remain independently required.

## Evidence provenance and process corrections

The worker did NOT capture the original thread-exception RED before editing.
Its patch is therefore not claimed as original RED-first. Main's polling-bypass
RED/GREEN is original; the following isolated mutants are retrospective sensitivity
checks, not substitutes for missing original evidence:

- Remove only failed-flag publication: threaded test fails because the kernel
  remains usable as leader after observed writer failure.
- Remove the kernel's worker-failure checks: same intended leadership assertion
  fails. Exact isolated patches and logs are retained.

The worker also tried copying a CMake build directory whose absolute cache paths
redirected to the existing sanitizer build. That violated the requested isolated-
build workflow. No result from that attempt is accepted here. Main inspected the
actual Debug sanitizer flags and rebuilt both real roots before final testing.
Future workers must not copy CMake caches as isolation; main owns shared builds.
All mutations in this evidence were main-run isolated source copies/direct builds.

## Final verification

- Release build then CTest: **47/47**.
- Debug ASan+UBSan build then CTest: **47/47**. Existing C++ flags
  `-fsanitize=address,undefined -fno-omit-frame-pointer`; runtime
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. No new Raft-C
  instrumentation or LeakSanitizer claim.
- Focused TSan: **threaded-flush and writer-retry pass** with halt_on_error=1.
  Direct compilation instrumented the test, v2_wal_arena.cpp, v2_replication.cpp
  and storage.cpp; remaining release archives were not instrumented. This is
  focused shared-worker-path evidence, NOT full-program/all-Raft TSan coverage.
- Tools **40/40**, passive model **109/109**, bytecode writing disabled.
- Inventory remains47 total/44 V2: added cases in an existing suite, not a new
  passing suite; five intentionally RED diagnostics remain explicit.

Raw logs here; working root `/private/tmp/f5-f6-parallel.IqHRl4`. Release root
`/private/tmp/velocita-full-audit.lYB4P5/release`; sanitizer root
`/private/tmp/aeron-phase67-audit-build`, runtime TMPDIR
`/private/tmp/f4-sanitized-runtime.IpVp53`. Source-derived size probe is retained
as output; proposed arithmetic is not implemented framing or benchmark evidence.

## Next contract and scope

`../../F5_BOUND_FRAME_BYTE_REVIEW.md` proposes exact framing (56-byte header,
32-byte SHA trailer), payloads and bounds; no encoding before approval. Maximum
local record1,901, two/block3,802 of4,064 payload; local snapshot65,587 bytes;
max16-entry peer frame29,084 within proposed32 KiB. Main corrected a worker's
initial peer arithmetic and checked all maxima with a current-header compile.

`../../F5_F7_MVP_CLOSURE_PLAN.md` records parallel dependencies, candidate budgets,
result-reservation review and deferrable optimizations. Candidate quotas are not
approved constants. Main has NOT approved a distributed credit protocol merely
because a worker suggested it; local admission/backpressure alternatives need
analysis before paying that complexity.

F5/F6/F7 remain OPEN. Thread error handling is not asynchronous Raft hard-state
fencing, archival, snapshot transfer or full recovery acceptance. No new bytes,
credentials, venue, target, source freeze, commit or tag action. Pi deferred, not
waived. Protected/unrelated work preserved.
