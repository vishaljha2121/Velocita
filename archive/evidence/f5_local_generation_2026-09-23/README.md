# CP-031: approved local-generation selector codec

2026-09-23. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation remains
uncommitted. Owner approved `../../F5_LOCAL_GENERATION_BYTE_REVIEW.md` after the
plain-language explanation following CP-030. No new unrelated byte/economic
contract is implied. Two bounded workers: implementation and independent review;
main supplied adversarial tests, literal golden, integration and guard mutations.

## Scope and ownership

Added `local_generation.hpp/.cpp` in the contracts library. Pure160-byte codec
plus value-transition validation only; no I/O, allocation, clock, publication,
startup migration, active-generation switch or deletion capability. No existing
economic/peer/WAL/snapshot bytes changed. Native selector value is56 bytes on the
cacheline64 host build; portable frame is160 regardless of native padding.

Encoder validates input, copies value/identity fields, constructs a zeroed fixed
frame and only then copies bytes to caller output. Failure leaves output intact;
extra capacity remains untouched. Decoder checks exact framing, reserved zeros,
domain-separated SHA-256, expected manifest/local owner and value shape, then
assigns destination once. Digest equality uses a fixed32-byte accumulator loop;
this is source-level constant-work comparison, not measured side-channel proof.
Whole-frame staging and delayed assignment support tested value/output and
input/destination overlap. Success-path encode/decode allocation probes report0.

Transition validation checks both value shapes, strict selection sequence+1,
increasing generation and matching previous generation. Generation gaps are
allowed for abandoned preparations. Maximum counters remain serializable and
recoverable; a further advance refuses. Pure checks cannot prove directory
freshness, root ownership, base validity or publication durability.

## RED / GREEN provenance

Worker first implemented an intentional refusal stub. Its compiled suite exited1
at `RED: approved local generation selector did not encode`; retained
`worker-red.txt`. A setup compile without the required cache-line macro is NOT
counted as RED. Main independently compiled its literal fixture against the stub
and exited1 at `independent golden decode`; `main-stub-red.txt`.

After implementation, worker and main direct suites exit0 in `worker-green.txt`
and `main-green.txt`. These are new-component TDD results, not evidence that the
existing snapshot-catchup, archive-loss or owner-fsync integration REDs are fixed.

## Independent expected bytes and adversarial coverage

Main used Python stdlib `struct.pack_into` and `hashlib.sha256`, not the production
encoder, for the literal160-byte golden. Fields: generation8, selection3,
previous7, local voter17, base digest a0..bf, shared manifest
`eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc323`.
Expected trailing digest:
`17d467dd970d21c01cd80118aad34f8d3d0fb10224ee5e5934f8cef403576e50`.

Permanent independent test compares all encoded bytes and decoded fields. It
checks every truncation and one-bit corruption at each of160 byte positions,
trailing bytes, all reserved-byte positions, validly resealed wrong owner/manifest,
zero fields, predecessor ordering, identity refusal, unchanged outputs, exact
output tails, maximum counters/last valid transition and546 authored transition
cells. Semantic invalid-frame tests use production SHA only to reseal, not to
derive the expected acceptance outcome; the literal golden checks digest bytes
independently. Worker tests add allocation and alternate-identity fixtures.

Three source-copy mutants were directly compiled (not swapped through CMake):

| Disabled guard | Intended failure | Retained evidence |
|---|---|---|
| local-voter binding | another configured voter accepted | mutant-owner.txt/.patch |
| selector digest | corrupt generation byte56 accepted | mutant-digest.txt/.patch |
| strict sequence increment | authored transition matrix disagrees | mutant-sequence.txt/.patch |

All compile and exit1 at those assertions. These are retrospective guard tests,
not original production-bug REDs. Implementation source was held stable during
the mutation runs. Other publication/fencing guards do not exist in this codec
and are not claimed as tested.

## Builds and limits

Reconfigured and built before CTest in the established Release and C++ sanitizer
roots. Added two CTest targets and corresponding independent gate-inventory names:
67 host /64 V2, no suite removed. Exact results are in `release-tests.txt`,
`sanitized-tests.txt`, `tooling-tests.txt` and `inventory-validation.txt`.
Fresh Release67/67 (19.80s), C++ Debug ASan+UBSan67/67 (160.90s), tooling40/40;
independent exact inventory validation exits0. `git diff --check` passes.

Reproduction from repository root:

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake -S . -B /private/tmp/aeron-phase67-audit-build
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

The sanitizer cache is Debug C++ ASan+UBSan with nonrecovering undefined behavior;
leaks disabled, Raft C uninstrumented. No new TSan, model, target, latency, actual
power-loss or live-admission claim. Pure codec and transition tests do not test
filesystem selection, prepared-base semantics, interrupted publication, rollback
detection, snapshot integration or repair. Those requirements remain open.

Protected tracked paths and untracked model/ preserved. No staging, commit/tag,
source freeze, venue, device, deployment or actual credential action.
