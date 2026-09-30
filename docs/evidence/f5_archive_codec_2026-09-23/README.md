# CP-028: approved portable archive codec, not reclamation

2026-09-23. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation remains
uncommitted. The owner approved the exact contract in
`../../F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md` before implementation. No snapshot,
repair, quota-ledger, publication, asynchronous fence or deletion bytes were
implicitly approved. No source freeze, tag, device or venue action occurred.

## What changed and why

- Added `archive_segment.hpp/.cpp`: separate portable codec, not a new peer/local
  BoundFrame kind. This keeps the cold 4-MiB object out of ordinary node scratch.
- Added incremental `Sha256` while retaining the one-shot API and digest bytes.
  The one-shot implementation delegates to the same block/finalization logic;
  maintaining two crypto implementations would add divergence risk. No economic
  bytes or fingerprint domains changed.
- Encoding pulls one record at a time from an immutable source. This avoids a
  descriptor array proportional to the 8,192-record cap. Each returned payload
  may use a reusable staging buffer and is consumed before the next source call.
- Decoding validates the complete immutable input before any visitor. A corrupt
  later record therefore cannot partially deliver a supposedly valid archive.
  It still is NOT an atomic multi-command transaction: a later visitor refusal
  leaves the already-consumed prefix, and success metadata remains unpublished.
- Added three CTest suites and the independent gate inventory entries. No existing
  test, acceptance requirement or lifecycle behavior was removed. No `compact()`
  caller or log-cell release path uses this component.

The codec preserves every strictly decodable canonical input, including requests
that would refuse during application. Type 101 checks its own codec and expected
authority domain, not current catalog/effect/epochs or equality between gateway
and cluster incarnation. Those are independent identities and historical requests
can legitimately yield no proof. Re-adjudicating them while archiving would lose
evidence. Type 0 never calls the reducer.

## Independent expectations and boundary tests

The worker's literal one-command archive and main's 628-byte two-marker archive
were packed separately with Python `struct` and `hashlib`, not the archive codec.
The main golden has indices 17/18, terms 4/6, predecessor term 3, entry IDs
INT32_MIN/-7, and two exact 186-byte marker payloads. Gateway incarnation is
`a0..af`, requested catalog is `80..9f`, request sequence 3, effect 5, kill epoch
7 and executor generation 11. Both differ deliberately from cluster/catalog
identity. Shared manifest is
`eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc323`;
authority domain is
`28c6283ffd06603e0e483836a876faf9f48c398f7fe6754f8b9eec0ac0fb6363`;
segment digest is
`d4f1956600b19c575528fe22a0887e1895e33d78d2eed0eb4d7b26c26cbf4fa1`.
Literal full bytes and full-payload visitor assertions are in the permanent test.

Coverage includes:

- Same shared identity with different local voter ID produces the SAME archive;
  foreign incarnation and an unverified identity refuse without callbacks.
- Every truncation and one-bit change at every byte of the 628-byte golden;
  trailing data and resealed invalid count/range/term/reserved/type/lineage.
- A bad second record exposes no first record. Inner marker domain and command
  schema faults are rehashed at every enclosing layer, so outer hash rejection
  cannot masquerade as typed validation. A bad record digest with a correct outer
  digest separately exercises the record check.
- A valid repeated-command history remains encodable/decodable; canonical input
  history is not filtered into successful state transitions only.
- Exactly 8,192 valid records from a reused source staging buffer succeeds in
  two passes; 8,193 refuses without calling the source or touching output.
- Exactly 4,194,304 bytes succeeds: 2,250 maximum 1,789-byte commands, 61 markers,
  59 100-byte commands, and 128 segment-overhead bytes. This independent count
  arithmetic yields 2,370 records. One additional command refuses unchanged.
  Maximum-payload fixtures use the existing command encoder; they test archive
  capacity/typed preservation, not independent correctness of that inner codec.
- Index 1 requires predecessor term 0; a nonzero predecessor there refuses.
  Non-genesis zero/too-new predecessors, term regression and index gaps refuse.
- Insufficient output, invalid preflight and preflight-source failure preserve
  output. Second-pass source failure returns zero publishable bytes; output may
  be partial. Visitor refusal preserves destination metadata but not prior
  callback side effects. Null visitors still validate and publish metadata.
- SHA literal known vectors include empty/abc, 55/56/63/64/65-byte padding edges,
  standard multi-block input, chunking/empty chunks, million-a, independent
  instances, repeated finish and update after finish.

Mutated fixtures use production SHA for resealing, not for generating their
expected acceptance outcome. Literal independent vectors separately check digest
bytes. The separation matters: sharing a hash implementation is not an independent
cryptographic oracle, but it permits targeting semantic guards without stale hashes.

## TDD and mutation provenance

Workers wrote the new suites before their implementations. Their initial REDs
were **compile-only**, reported as missing `Sha256` API and missing archive source.
Those tool outputs were not retained as files; do not represent them as retained
original behavioral RED logs. This is a provenance limitation. No existing
production bug is claimed fixed by those compilation failures.

Main independently compiled the final source and directly compiled nine source-copy
mutants (not CMake target substitutions). Each mutant compiles, exits 1 and hits
the intended assertion. Patches and stdout are retained alongside this file:

| Disabled or damaged behavior | Intended observed assertion |
|---|---|
| SHA total length replaced by buffered length | stream length 64 digest mismatch |
| SHA buffered suffix omitted | stream length 3 digest mismatch |
| Record digest check | bad record digest hidden by valid outer digest |
| Segment digest check | segment digest footer corruption accepted |
| Record ordinal check | resealed second-record index gap accepted |
| Record term-order check | resealed decreasing terms accepted |
| Whole-segment first-pass rejection | invalid later record reached an earlier visitor |
| Marker authority-domain check | foreign marker authority domain accepted |
| Inner command decoder check | resealed unsupported command schema accepted |

These are retrospective guard tests, not original production-defect REDs. Initial
archive mutant copies raced the worker's remaining source edits; their diffs had
unrelated changes. They are retained under `pre-stable-mutants/` and NOT credited
as single-guard evidence. Main regenerated every credited archive mutant only
after the worker declared production files stable. Each final patch changes one
intended condition. The initial `No rule to make target` build attempt is also
retained, explicitly a missing CMake regeneration, not a product/test RED.

## Ownership and performance limits

Host cache-line ABI 64 measurements: Sha256 112 bytes, archive scratch 2,120,
record view 40, pull-source descriptor 24, metadata 80. The command scratch,
separate decoded command and fixed hash work arrays still use memory; object
sizes are NOT peak stack or process RSS. Output/input storage is caller-owned and
may be up to 4 MiB. Allocation probes bracket codec operations after fixture
construction and observe zero C++ allocations on those paths. No claim is made
about the future filesystem/publisher, a process-wide heap bound or device latency.

The current decoder consumes a contiguous immutable span, not a file reader.
A future file-streaming layer must pin an immutable object/version across both
passes. Encoder payload/output nonaliasing and source/input immutability are
explicit caller preconditions, not runtime proofs against arbitrary mutation.
Borrowed spans must not escape synchronous callbacks or cross queue boundaries.
Work is linear in bytes/records but is not yet integrated into a bounded cold-I/O
owner turn. No per-order archive work or benchmark improvement is claimed.

## Regression, reproduction and open failures

Fresh final builds precede each full CTest run; see `release-build-final.txt`,
`release-tests.txt`, `sanitized-build-final.txt`, `sanitized-tests.txt` and
`tooling-tests.txt`. Inventory is independently compared with the gate script:
63 host / 60 V2 suites (`inventory-check.txt`). The three new names are
`v2-incremental-digest-tests`, `v2-archive-segment-tests` and
`v2-archive-segment-adversarial-tests`.

Final observed results: Release **63/63** in 11.65s; C++ ASan+UBSan **63/63** in
161.37s; Python tooling **40/40**. Timings are host observations, not benchmark
or latency acceptance. Final builds emitted no compiler warnings. The earlier
sanitizer build caught an unused fixture helper while its worker was still adding
the cap test; the final build includes that test and is the credited result.

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure

cmake -S . -B /private/tmp/aeron-phase67-audit-build
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py'
```

These commands reuse the verified existing build caches: Release and Debug with
C++ `-fsanitize=address,undefined -fno-sanitize-recover=undefined
-fno-omit-frame-pointer`, cache-line ABI 64. Do not assume an arbitrary directory
with those names has matching flags. Raft C is uninstrumented; leak detection is
off; UBSan halts. Worker sanitizer linkage to Release archives is not credited
as a substitute for main's full C++-instrumented build. No TSan or target run.
The passive-model 109 result remains historical, not rerun here.

The final direct mutant build form is:

```sh
c++ -std=c++20 -O1 -DNDEBUG -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude \
  tests/v2_archive_segment_adversarial_tests.cpp /path/to/mutated-archive.cpp \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_kernel.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_evaluator.a \
  /private/tmp/velocita-full-audit.lYB4P5/release/libvelocita_v2_contracts.a \
  -o /path/to/mutant-test
```

Main also rebuilt and reran BOTH original CP-026 integration diagnostics against
fresh current Release libraries. They still fail at the original intended
assertions: lagger remains applied/commit 0 after peers compact through 12;
compaction through 3 loses the pinned raw command at 1 despite a valid snapshot.
See `integration-catchup-still-red.txt` and `integration-archive-loss-still-red.txt`.
They remain outside passing CTest and retain their qualified negative oracles.
Do not turn the archive-loss test green by writing an unverified sidecar.

Next: reviewed durable publication/dependency coverage and generation-bound
compaction permission, then transactional snapshot catch-up and fenced repair.
F5/F6/F7 stay OPEN. Pi remains deferred, not waived; this component does not make
NUC shadow or live trading launch-ready. Protected paths and the untracked model
were not changed; no staging or commits occurred.
