# CP-023 — bounded result ownership integrated into replication

2026-09-23. Branch v2/phase-5-corrections, base HEAD d44a4a9. Remediation remains
UNCOMMITTED. Owner separately approved F5_ADMISSION_RESULT_REVIEW §8 before this
integration. No wire/disk/economic bytes, pinned Raft fork or V1 changes here.

## Delivered contract

- Every accepted result-producing command owns a generation-checked reservation
  on every replica. The kernel now allocates one229400-byte ledger at startup;
  the growable committed-result deque is removed. Limits remain960 ordinary plus
  64 protected. Eligibility derives from decoded kind AND operator caller, not a
  trusted caller lane label. Control uses protected first, ordinary on spill.
- Retained payload ownership now includes an optional16-byte handle (24 native
  bytes including optional discriminator/padding). Before apply, identity,
  canonical frame digest, term and index must match. Apply publishes to the owned
  slot; no result-container allocation occurs after economic mutation. Publication
  transfers the obligation from log ownership to ready FIFO until consumer take.
- Local result refusal is typed result_capacity_exhausted; a fixed diagnostic
  refusal counter and reserved/ready counts expose ownership, even after failure.
  These are local C++ APIs, not new replicated schema values.
- Follower planning covers the complete potential new batch. Same-index/term
  matches reuse retained ownership, exactly as pinned Raft does; this does NOT
  add a new same-term content-conflict rule. Markers need no business result.
  If infeasible, process original term/prev with zero entries and commit capped at
  min(advertised,prev). Reply covers only the verified durable prefix. This valid
  control processing returns frame-error NONE plus a capacity counter, not a
  misleading malformed/no-mutation error. No immediate second attempt in that call.
- A replaceable uncommitted suffix contributes its ACTUAL slot pools to planning.
  The plan does not free anything. Successful durable log_pop releases the removed
  reservation; log_offer reserves before Raft increments its accepted log count.
  Payload/map/reservation ownership for a failed offer is cleaned up. Ready slots
  are never made reusable by elections, truncation or snapshot compaction.
- Startup reserves only retained UNCOMMITTED commands before writable WalSegment
  construction. Committed replay suppresses notifications. An oversized suffix
  refuses with unchanged files; it is not silently truncated to fit.

## Why a read-only plan is sufficient here

The owner is single-threaded and the pinned handler is synchronous: after preflight,
only successful tail deletions and the planned log_offer calls can change result
capacity. No subscriber take/reserve callback interleaves. Therefore a bounded
simulation of slot pools AND remaining generations can prove capacity before
entering Raft without speculatively freeing old slots or preallocating spare slots
which a full replacement cannot possess. Unexpected reservation failure after
successful planning is an ownership fault and permanently closes service, not a
normal retryable refusal. Independent review challenged this equivalence.

This is NOT an atomic rollback of a whole AppendEntries. Raft deletes tail-first,
then appends sequentially. An I/O/allocation error after an earlier successful
transition can leave a partial replacement. Ownership must match each successful
transition. Result-capacity infeasibility is preflighted; generic WAL/outbound/log
capacity and allocation failure atomicity are not claimed solved by this ledger.

Reserve/publish/take remain O(1); batch eligibility is at most16 entries. Replacing
a suffix scans retained payload ownership, whose whole-log bound is still open.
Preflight uses bounded arrays (1024 handles,16 eligibility flags; ledger duplicate
check uses1024 bools). Main replaced the worker's quadratic duplicate-handle scan
with that fixed seen array. Hash/codec cost remains bounded by command size; no
new latency measurement or target ABI claim. Native host/cacheline64 measurement:
ReplicatedKernel91472 inline bytes, ledger229400 separately, handle16,
optional-handle24, CommittedResult176. Maps, payload heaps, outbound and Raft
allocations remain outside these totals.

## TDD and regression evidence

Freshly rebuilt the pre-integration source and ran four permanent selectors:
leader, follower, recovery, oversized-recovery. All exited1 for their intended
missing admission/reservation behavior; original outputs retained here. Replacement
was baseline GREEN and remains so; do not relabel it as an original defect.

The historical filename tests/v2_result_admission_red.cpp is now registered as
v2-result-admission-tests; its former open REDs are no longer excluded from CTest.
Added v2-result-admission-edge-tests and BOTH independent target inventory entries.
Permanent tests also cover:

- full ordinary/protected pools, exact identities/FIFO, drain/retry and counters;
- non-operator cannot borrow protected capacity; eligible control on normal lane
  still has the same result eligibility as its follower representation;
- a whole batch with only one slot free cannot append its first of two entries;
- fifteen matching entries plus one new entry need only one new reservation;
- higher terms, compaction, markers and later-index duplicate notifications;
- no replayed committed notifications after snapshot/WAL recovery;
- full uncommitted suffix replacement, failed-prev truncation and truthful replies;
- deterministic failure on the second tail rewrite: one entry durably removed,
  exactly959 reservations retained, service closed, restart commits precisely the
  surviving identities (no removed or failed replacement identity);
- controls borrowing ordinary slots, then replacement by ordinary commands which
  requires recognizing those actual ordinary slots rather than the old kind.

The old WAL-lane fixture initially failed because its small records now hit the
independent result limit before WAL capacity. Changed it to larger ordinary policy
records so it still isolates and proves reserved WAL blocks. It also asserts a
failed WAL offer leaves no orphan result reservation. This is a fixture adaptation,
not weakening either production limit.

The ledger-preflight worker's original RED was a missing-method COMPILATION error,
not the requested compiling inert scaffold. Record that limitation explicitly;
do not call it behavioral RED. Its current source-only unit/sanitizer tests pass.
Integration REDs above ARE behavioral failures on the original kernel.

## Mutations

Isolated source copies, directly compiled with current test sources and fresh
Release dependency archives; production source and shared archives were not mutated.

| Removed/broken guard | Intended failure |
|---|---|
| Follower batch preflight | batch capacity refusal appended a partial suffix |
| Recovery reservations | restored uncommitted suffix has no result reservations |
| Suffix release | replacement fails with uncommitted result capacity exhausted |
| Release before durable truncation | failed durable truncation released a surviving result reservation |
| Failed-offer reservation cleanup | failed WAL append leaked an unaccepted result reservation |

These five mutations do not claim exhaustive sensitivity of every guard. Existing
CP-022 ledger generation/pool mutations remain historical component evidence.
The new adversarial edge cases initially passed the integrated baseline; they are
additional coverage, not invented original REDs. No physical power-loss claim:
the rewrite fault is an injected I/O checkpoint exception.

## Reproduction and acceptance boundary

Temporary work/evidence: /private/tmp/f5-result-integration.dGAwkC.
Build roots: /private/tmp/velocita-full-audit.lYB4P5/release and
/private/tmp/aeron-phase67-audit-build. Build velocita-all-tests before CTest.
Sanitizers: C++ address/undefined, frame pointers; runtime detect_leaks=0 and
halt_on_error=1. Raft C is NOT instrumented. No new TSan or target test claimed.
Final suite outputs and tooling results are retained beside this record.

Final current-source verification: Release54/54 (8.24s), ASan+UBSan54/54 (96.80s),
tooling40/40. Independent gate inventory matches54 host/51 V2 suites. These run
times are test execution records, NOT latency benchmarks. Earlier intermediate
runs experienced much longer elapsed times; no performance conclusion is drawn.
Passive model not rerun;109/109 is CP-020 historical. git diff --check clean;
protected tracked paths clean and model remains untracked/untouched.
Final logs are final-source-release-tests.txt and final-source-sanitized-tests.txt;
the earlier verified-* logs precede a test-fixture warning cleanup and correction
of an old comment which incorrectly claimed log_offer runs after Raft acceptance.
Final builds have no compiler warnings; no test or production check was disabled.

Direct mutant recipe: C++20/O1/cacheline64, include/ and tests/ plus pinned Raft
headers; current selected test source + isolated mutant replication source;
fresh libraries v2_replication, v2_kernel, v2_evaluator, v2_contracts, core,
willemt_raft, then pthread. RESULT_ADMISSION_CASE selects partial/recovery/
replacement for the first three. The last two use the edge/replication suites.

Full F5 remains OPEN: outbound/log quotas and failure policies, replacement
voting admission, transactional install/catch-up and verified archive before
last-copy deletion. F6 bound-metadata persistence-model port and async fences,
F7 frozen NUC/Pi gates, processes and M3a observability remain separate. Pi is
deferred, not waived. A slow consumer on either of two voters can stop new commits;
64 control slots cannot bypass a missing earlier log entry. No target latency,
whole-process memory ceiling, live-trading permission, commit or source freeze.
