# CP-033: approved immutable generation base and read-only file verification

2026-09-23, branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation remains
uncommitted. Owner approved `../../F5_GENERATION_STORAGE_REVIEW.md` after CP-032.
Two bounded workers (codec implementation and read-only review); main owned the
filesystem verifier, integration, additional negative tests and guard mutations.

## What changed, and what did not

Added `generation_base.hpp/.cpp`: exact 40-byte V2GB seal, explicit little-endian
fields and approved domain-separated digest over header, generation, canonical
local identity, snapshot length and complete immutable bound snapshot bytes.
Genesis requires no snapshot. Snapshot bases require positive representable
coordinates, valid local bound framing, canonical state and catalog dependencies.
The digest is not accepted as a substitute for these semantic checks.

Scratch is fixed and caller-owned, including canonical decode state. Encode stages
the 40-byte frame; decode constructs a small summary and assigns it only on success.
Outputs stay unchanged on errors. Inputs/output/scratch are explicitly nonaliasing.
No native struct layout is serialized. The encode allocation probe reports zero;
this is not a whole-process or filesystem allocation claim.

Added `generation_base_files.hpp/.cpp`: read-only verification of the exact final
selector's immutable base. Validate actual root identity, derive the canonical
generation name, open descriptor-relative non-following directories/files, reject
nonregular/multiply linked/not-owner-only files, bound lengths before reads, check
EOF/post-stat/name bindings and require the closed immutable base namespace.
Decode the full snapshot and compare the resulting base digest with the selector.
On success the caller also owns every original snapshot byte in its supplied
buffer; there is no digest-only reconstruction assertion. Caller must retain
exclusive trusted-root ownership. No descriptor/authority capability is returned.

This does NOT validate mutable WAL/metadata/runtime snapshot, prepare or publish
a generation, acquire the long-lived startup locks, change bootstrap/recovery,
or permit voting. Existing CP-032 startup refusal remains active. Nothing was
removed or migrated. No generic storage framework was introduced. Selected startup,
publication cut tests, runtime snapshot floor and snapshot installation are the
next already-approved integrations, not completed features or new approval gates.

## RED / GREEN and independent oracles

- `worker-red.txt`: compiled new codec refusal stub fails the intended encode
  assertion; this is new-component TDD, not a claim of a historical production bug.
- `main-files-red.txt`: compiled reader refusal stub fails reading a valid sealed
  genesis through its final selector. Worker and main direct GREENs retained.
- `worker-golden.txt` supplies exact fixed input identity/snapshot bytes and the
  independently computed Python SHA-256 preimage/result. Main independently
  recomputed it (`main-golden-check.txt`); complete literal expected seal is in
  the codec test, not obtained from the function under test.
- Tests cover canonical/bound corruption, zero snapshot coordinates, missing
  catalog and a validly framed canonical state with a foreign catalog dependency,
  all 40 truncations/byte corruptions, trailing bytes, short output, rehashed
  invalid header fields, genesis shape and failure-atomic destinations.
- Filesystem tests check complete snapshot payloads/coordinates, all directory
  symlink levels, file symlink/hardlink/owner permissions, missing/oversized/FIFO
  snapshot, extra immutable entries, foreign root identity, relabelled generation,
  selector/base mismatch and no temporary/different-directory fallback.
- Independent reviewer found no scoped production defect. Main strengthened the
  failure assertion to include base metadata, not merely selector/length.

Six direct source mutations each exit 1 at the intended assertion:

| Removed check | Observable failure |
|---|---|
| Root identity equality | Foreign voter root accepted |
| Immutable namespace validation | Unexpected genesis file accepted |
| Selector/base digest equality | Selector selected a different base digest |
| Canonical catalog dependencies | Valid snapshot with missing dependency accepted |
| Seal digest verification | Corrupt seal accepted / destination changed |
| Seal schema guard | Correctly rehashed unsupported header accepted |

Raw `mutation-*.txt` logs retain assertion text. These are retrospective sensitivity
checks, not original REDs. Mutant copies are outside the repo in
`/private/tmp/velocita-generation-base-mutations.B6pZX6`; production files were not
modified for mutation tests. Compile directly with C++20/O2/cacheline64 and the
changed source before fresh Release archives; CMake is not the mutation harness.

## Regression evidence and reproduction

Final Release 70/70 (12.01s). Full C++ Debug ASan+UBSan 70/70 (155.68s), followed
by a rebuilt focused run for final test-only strict-header/truncation additions.
Tooling 40/40; independently checked inventory 70 host / 67 V2. Build-before-CTest
was observed. UBSan is nonrecovering with halt_on_error, leak detection disabled,
and Raft C remains uninstrumented. No new model, TSan, target, latency, publication
durability or whole-F acceptance claim. Stat interruptions fail closed; this
reader is not a hostile same-user filesystem-race defense. libc directory scanning
may allocate; no filesystem zero-allocation claim is made.

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake -S . -B /private/tmp/aeron-phase67-audit-build
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

The existing Debug root uses C++ `-fsanitize=address,undefined
-fno-sanitize-recover=undefined -fno-omit-frame-pointer`, cacheline64.
Original archive-loss, lagger-catchup and owner-fsync REDs remain open. Protected
paths preserved; no stage/commit/tag/source freeze/device/venue/credential action.
