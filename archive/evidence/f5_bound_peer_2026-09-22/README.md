# CP-019 — approved common framing and bounded peer admission

2026-09-22, v2/phase-5-corrections, base HEAD d44a4a9. Entire remediation tree is
uncommitted. Owner approved `../../F5_BOUND_FRAME_BYTE_REVIEW.md` after CP-018.
This checkpoint implements common framing and ordinary-peer runtime admission;
it does NOT claim local storage binding, full F5/F6/F7 or deployment acceptance.

## What changed and why

V2BF has explicit little-endian lengths, kind, shared manifest, local-owner scope,
reserved bytes and domain-separated SHA-256. The framing codec validates bounds
before copying/hashing and verifies integrity before classifying identity mismatch.
Successful decode publishes a span borrowing the original immutable input, never
the scratch buffer. This is lifetime-aware framing, not an owning queued message
or validation of metadata/WAL/snapshot body semantics.

Encoding stages domain/header/payload/trailer in caller-owned scratch before
writing output. Main review found that the first draft copied its header before
an overlapping payload; staged publication fixes that aliasing risk. Generic
framing now also requires the approved minimum prefixes/exact metadata length.
Scratch must remain disjoint from input/output. No native-struct serialization.

The only public ReplicatedKernel peer ingress now accepts bound bytes and returns
a typed admission error. There is no raw PeerMessage overload. Outer lineage and
scope validation precede a complete inner-shape preflight (<=16 entries, each
<=1,789 bytes, complete frame <=32 KiB), which precedes V1 decoder allocations.
Decoded messages then validate route/member/candidate, canonical entry type/body
and authority-marker domain before Raft sees them. Failure preserves the caller's
decode destination; integration asserts unchanged term/log/commit/apply/state,
files and outbound messages. All four request/reply variants are exercised.

The sender copies at most sixteen entries per AppendEntries. This is safe with
the pinned Raft code because replies advance next_idx through the actual received
prefix and trigger another send if a suffix remains. A three-voter forty-entry
backlog witnesses a full sixteen-entry batch and commits/applies all forty entries.
It does not claim bounded outbound queues, retained history or consumer lag.

Kernel scratch is cold-allocated once (65,613 bytes, outside TradingState).
Normal decoding still owns bounded vectors. Public hashes establish equality,
not authentication; authenticated deployed peer transport remains separate.

## RED, GREEN and independent review

- `frame-red.txt`: initial framing stub fails the independent golden encode.
- `peer-red.txt`: initial typed peer stub fails `valid peer encoder refused`.
  This direct codec-only RED used cache-line definition128 and existing Release
  archives64; no kernel instance/layout was used in that test. Final integration
  and all CMake runs consistently use64. Do not infer cross-layout ABI evidence.
- The independent literal golden is rechecked with Python `struct.pack` and
  `hashlib.sha256`, not this codec; see `golden-independent.txt`.
- Existing CP-018 `peer-red.txt` recorded an actual foreign kernel's vote changing
  receiver term0->1. `foreign-peer-green.txt` now reports term0->0 and asserts the
  precise foreign_manifest error plus unchanged log/state. The same obligation is
  permanently covered in CTest for every request/reply kind.
- Codec tests cover short outputs, truncation, malformed later entries, count /
  length overflow claims, route/candidate disagreement, unknown type, invalid
  commands/marker domain, maxima and output preservation. Ordinary new/new[]
  allocation instrumentation asserts zero allocations for a shape-invalid batch
  whose third entry is truncated. This is not a whole-program allocation claim.
- Two bounded workers owned generic framing and fixture migration/review. Main
  owned typed peer code, integration, builds, review corrections and evidence.
  Real Cluster routing uses the actual sender identity, not receiver relabeling.
  One catalog latch test initially failed in helper identity construction (sender2
  was not in its one-voter manifest); main corrected it to exact dependency-error
  refusal on empty input, proving the permanent latch runs before peer parsing.
  Initial fixture/compiler/setup failures are not claimed as production REDs.

Isolated source-copy mutants, direct compilation (NO shared-source mutation or
copied CMake cache):

| Removed guard | Selected witness | Observed failure |
|---|---|---|
| Manifest comparison | BOUND_PEER_CASE=admission | refused input changed Raft or reducer |
| Outgoing sixteen-entry cap | BOUND_PEER_CASE=batch | outgoing batch exceeds approved 16 entries |
| Complete shape preflight | BOUND_PEER_CASE=codec | malformed later entry allocated before complete preflight |

Exact patches and outputs accompany this record. These are retrospective guard
sensitivity checks, not original test-first evidence for the batch-cap change.
Original codec stub REDs and prior foreign-traffic RED are recorded separately.

## Reproduction and final host validation

Release root `/private/tmp/velocita-full-audit.lYB4P5/release`, Debug sanitizer root
`/private/tmp/aeron-phase67-audit-build`. Both configured from THIS dirty tree with
BUILD_TESTING and disconnected cached dependencies; every CTest run followed a
build. CMake and independent gate inventory now include the two new suites.

```sh
cmake --build <root> --target velocita-all-tests -j8
ctest --test-dir <root> --output-on-failure
BOUND_PEER_CASE=admission <release-root>/v2-bound-peer-tests
BOUND_PEER_CASE=batch <release-root>/v2-bound-peer-tests
```

- Release **49/49**; Debug ASan+UBSan **49/49**.
- Sanitized C++ flags `-fsanitize=address,undefined -fno-omit-frame-pointer`;
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`, private TMPDIR.
  No new Raft-C, LeakSanitizer, TSan or target-instrumentation claim.
- Python tooling **40/40**, passive model **109/109**, bytecode writing disabled.
- Independent required inventory matches **49** host suites (**46** V2).
- `git diff --check` clean. Protected tracked docs remain clean; protected model
  remains untracked/untouched. No commit/tag, source freeze, venue or target action.

Raw working evidence: `/private/tmp/f5-bound-peer.3d2CC0`; worker original framing
RED `/private/tmp/v2-bound-frame-red.VIQ5XM`. Permanent key logs are copied here.
Direct mutants compile the selected source plus tests/v2_bound_peer_tests.cpp with
C++20/O1/cacheline64, include+tests+Raft include paths, then link rebuilt archives:
v2_replication, v2_kernel, v2_evaluator, v2_contracts, core, willemt_raft. No CMake
cache or link path is copied into another build root.

## Explicitly still RED / next work

Current-source standalone diagnostics are rerun and retained as `open-*.txt`:
foreign metadata accepted; foreign WAL/snapshot restore kill9 instead of kill1;
missing metadata restores a participating voter's term1 as term0. All exit1 for
those intended reasons. Generic framing has not been connected to those stores.
Next: typed metadata/record/snapshot codecs, metadata-first bootstrap/retry crash
tests and recovery integration, preserving typed foreign/unsupported refusal and
the no-legacy-fallback rule. Bytes are already approved; do not ask again.

Whole-node budgets/results, safe replacement admission/install, verified archive
reclamation and asynchronous hard-state fences still require their own reviewed
contracts and implementations. Pi is deferred, not waived. Host tests here cannot
close those guarantees or authorize live trading.
