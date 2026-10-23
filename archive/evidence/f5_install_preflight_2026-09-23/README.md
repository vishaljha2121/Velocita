# CP-035: read-only install preflight and result-obligation inspection

2026-09-23, branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation remains
uncommitted. Two **gpt-5.6-sol** workers implemented disjoint bounded components;
main selected/refined interfaces, reviewed behavior, added tests/fixes, integrated
builds and independently mutation-checked. No new wire or persistent format,
activation/transfer authority, economic behavior or resource-pool capacity.

## What is implemented

`snapshot_install_preflight.hpp/.cpp` classifies a caller-validated, immutable
local record view. It checks coordinates, complete contiguous history, terms,
existing 8,192-entry/2,048-byte-cell bounds and equality of captured live/durable
tail. A volatile tail refuses preparation; it is not silently dropped. Normal
writer lag therefore means quiesce/flush/retry later, not permanent corruption.
Snapshot-covered retained prefixes, including covered-only WAL, are supported.

At incoming snapshot index S, a splice requires exact local/incoming boundary
index, term, entry ID, type and payload equality. Matching terms alone do not
establish the available exact evidence required by the install draft. The entire
compatible suffix above S is described, including entries above the local commit
checkpoint. A committed conflict refuses. A mismatching uncommitted boundary or
snapshot beyond the local tail describes no splice, not permission to erase old
evidence. Missing boundary evidence is not represented as a proven conflict.
The old snapshot is a committed prefix even if the metadata commit checkpoint
trails it. Lower-term incoming history cannot cross that floor.

This helper returns borrowed offsets/counts, not a capability. It does not decode
canonical records, verify sender/commit authority, copy/publish a generation,
reconstruct results, delete history or call Raft. Those remain separate mandatory
gates. Caller must retain immutable input ownership; the result grants no borrow
lifetime extension. Complexity is O(n + boundary payload bytes), fixed local
scratch, zero allocations in the measured call, not zero whole-installer memory.

`ResultLedger::visit_obligations` first validates native free/READY/RESERVED
partition, links/counts/tail, occupied coordinates/generations and immutable pool
tags. Before validation succeeds it emits no callbacks. Then it copies READY
views in actual `take()` FIFO order and RESERVED views in physical-slot order.
The latter is enumeration, NOT historical apply ordering. Each copied view has
its original handle and complete binding; READY includes the full result and
RESERVED has a native default result. No native padding comparison/serialization.

The callback gets values, never mutable slot references. It may refuse after an
observed prefix; caller must discard incomplete preparation. Owner-thread-only,
non-reentrant use is required, including no mutation/take through callback context.
This is not a ledger revision/root or concurrent-change detector: reserve/release
can leave the same occupied views with different free-list/generation state.
Final install still requires quiesce and revalidation and retains the same ledger.
No historical outcome is manufactured or published by this inspection API.

Native host sizes (`native-layout.txt`): ledger unchanged **229,400 bytes**;
copied obligation view **296**; suffix entry view **40**; local view **64**;
preflight result **32**. Visitor scratch includes a 1,024-element bool array;
walks are bounded O(1,024), not per-order work. An 8,192-entry descriptor array
would itself cost 327,680 bytes if a future caller materializes it. Input/scratch
ownership must be included in that future install budget; these sizes are not
a whole-process or target-hardware measurement.

## Evidence and corrections

- `result-obligation-runtime-red.txt`: actual compiled refusal stub fails at
  `visit failed`; focused worker and main GREEN retained. The separately named
  `result-obligation-stub-red.txt` contains an initial **compile setup failure**,
  not behavioral RED (cache-line define and incorrectly named enum fixtures).
- `stub-red.txt`, `green.txt`, `sanitizer-green.txt`: worker suffix test outputs
  explicitly labelled **transcribed**. Exact commands and their provenance are
  in `worker-suffix-evidence.md`; not falsely presented as raw redirected logs.
- Main rejected a test that called ordinary beyond-tail catch-up missing history.
  Also required the old-snapshot committed floor, covered-only recovery layout,
  exact supplied-boundary shape and missing-evidence classification. These were
  review corrections, not separately observed original REDs. The floor is later
  independently mutation-checked.
- Main added `result-pool-main-red.txt`: the draft visitor accepted a corrupted
  occupied slot's pool tag. The fix validates immutable per-index pool ownership;
  `result-main-green.txt` passes. This is an adversarial native-validator case,
  not evidence that normal ledger operations historically produced corruption.
- Main tests additionally check READY binding/state/every ApplyResult field,
  unchanged capacities, cycle/zero-generation refusal before callbacks, malformed
  supplied evidence even when the local boundary is absent, empty-genesis gaps,
  and absence of any usable output plan on refusal. Values in the all-field copy
  fixture exercise opaque value preservation, not reachable reducer outcomes.
- `main-original-lagger-red.txt` is a raw main rerun of the unchanged end-to-end
  reproducer against freshly rebuilt current Release libraries: healthy quorum
  compacted through 12, lagger applied/commit still 0 after seven append retries.
  This RED remains open and is not registered as a passing CTest suite.

Five isolated direct-source mutations fail the intended assertion:

| Mutation | Observable failure |
|---|---|
| Omit command digest from copied result binding | READY binding lost fields |
| Omit occupied slot pool validation | Malformed slot emitted a view |
| Ignore exact boundary payload bytes | Byte-only committed mismatch accepted |
| Truncate retained suffix at local commit | Complete unknown suffix lost |
| Condition snapshot floor on metadata commit | Lower-term prefix crossed committed snapshot |

Raw `mutation-*.txt` files retain assertion text; each executable exited 1, not
a compile/link failure. Mutants live outside the repo under
`/private/tmp/velocita-install-preflight-mutants.UrOZyB`. They were compiled directly
with C++20/O2/cacheline64, the relevant test and modified source; no CMake mutation
timestamp inference. These sensitivity tests are not original historical REDs.

## Gates and reproduction

Baseline rebuilt 72/72. Final Release **74/74 (12.36s)**; full C++ sanitizer
**74/74 (159.69s)**, then rebuilt final focused **2/2 (0.37s)** after the last
test-only evidence-shape assertions. Tooling **40/40**; independent inventory
**74 host / 71 V2**; `git diff --check` clean. New suites are separately
inventoried; build precedes CTest. C++ sanitizer scope
is ASan + nonrecovering UBSan/halt-on-error, leak detection off, Raft C not
instrumented. No new model, TSan, NUC/ARM64/Pi, latency or power-loss claim.

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Direct component builds need only the new test + corresponding source and
`-std=c++20 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude`. Current raw main
lagger reproduction used the same link recipe in `worker-suffix-evidence.md`,
current Release archives and fresh child fixture
`/private/tmp/velocita-install-main-red.dpLTgr/fixture` (do not reuse this root).

## Still open / next boundary

These helpers are not yet wired into live install; F5/F6/F7 and the original
archive-loss/catchup/owner-I/O REDs remain OPEN. Exact transfer/coverage/remaining
budget choices need owner review before encoding. Staged owner construction,
preserving the same live ledger, archive-backed outcome reconstruction,
writer quiesce/activation and safe failure after selector rename still need
implementation and end-to-end evidence. Component GREEN is not catch-up GREEN.

The install draft's old broader sealed-base file list and `V2GS` selector are
explicitly superseded by approved CP-031/033 formats. No descriptor/replay receipt
may be slipped into the closed `base/` directory. This documentation correction
prevents a future implementation from violating an already accepted layout.
Protected paths/model untouched. No staging/commit/tag/source freeze, device,
venue, credentials or deployment action. Pi remains deferred, not waived.
