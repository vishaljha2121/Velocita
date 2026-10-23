# CP-022 — standalone result ledger and numeric peer admission

Historical checkpoint: CP-023 subsequently integrates the kernel after separate
§8 approval; see `../f5_result_integration_2026-09-23/README.md`. Original evidence
and limitations below describe CP-022, not the latest runtime status.

2026-09-23. Branch v2/phase-5-corrections, HEAD d44a4a9. Entire remediation tree
remains UNCOMMITTED. No new wire/disk/economic bytes, target gate or acceptance.

## Delivered versus still open

Implemented the owner-approved §7 ResultLedger component:1024 slots,960 ordinary
and64 protected, protected-first control allocation with ordinary spill, two free
lists and one ready FIFO. Reserve/publish/take/release use no dynamic allocation.
Slots bind identity, term, index and canonical command digest; generation-checked
handles distinguish reuse. `matches` means an unpublished reservation, not a ready
result. Ready results cannot be released through the cancellation API. Exhausted
generation space refuses new reservations without wrapping and still permits drain.

Compile-time assertions pin the current native object to229400 bytes:224 per slot
and24 header. Handles are16 bytes, scoped to one noncopyable/nonmovable ledger
instance; they are not transferable cross-ledger capabilities. Owner-thread only.
CommittedResult moved verbatim into its own header so the component and kernel
share the type without circular inclusion. No message or native result layout
changed. One new CTest suite plus the independent target-gate inventory entry.

IMPORTANT: ReplicatedKernel still uses its existing growable result deque. The
component is linked/tested but NOT instantiated by the kernel. No claim of live
result bounds, allocation-free kernel publication, startup reservation coverage
or full F5 closure follows from these component tests. The separate follower
control-only saturation protocol in F5_ADMISSION_RESULT_REVIEW §8 awaits approval.
It was expressly excluded from §7; do not half-integrate only the leader path.

Independent parallel work also closes basic signed-coordinate admission gaps:
negative message terms, vote log coordinates, append prev/commit coordinates,
nonpositive entry terms, invalid response indices and unrepresentable successor
indices refuse before Raft. Header/reply term0 remains representable. No new
cross-field/monotonic checks or transport-authentication guarantee is claimed.

## TDD and independent review

The component worker first ran a compiling inert scaffold: exit1, `initial
capacities`. This is behavioral RED, not missing-header compilation. Later
strengthening checks1024 saturation/FIFO, pool restoration, exact binding, invalid
handles, ready/nonreserved behavior, generation exhaustion and every operation's
allocation count. Main corrected a vacuous stale-publish test: reusing a slot for
a DIFFERENT binding let identity checks hide a missing generation check. It now
reuses the EXACT same binding, so only the generation distinguishes ownership.
Main also added publication/drain after consuming the final generation.

Main rebuilt the current pre-ledger-integration library, then compiled the
diagnostic now named `tests/v2_result_admission_red.cpp`. These failures remain
OPEN, intentionally outside passing CTest:

| Selector | Current intended RED, exit1 |
|---|---|
| leader | 961st ordinary result obligation admitted without consuming a result |
| follower | full follower admitted unreserved result obligations |
| recovery | restored uncommitted suffix has no result reservations |
| oversized-recovery | oversized uncommitted history bypassed result capacity on recovery |

Its replacement selector currently PASSES on the old unbounded implementation:
a fully uncommitted960-entry suffix is replaced and only the new identities
produce results. This is a baseline to PRESERVE, not a RED or evidence that future
bounded reservation transfer is implemented. The source was initially called
v2_result_admission_tests.cpp while recording baseline outputs, then explicitly
renamed _red to avoid confusing it with the passing suite inventory.

### Mutations and evidence correction

Main directly rebuilt isolated worker source-copy mutants with the strengthened
current suite; each exits1 at its intended assertion:

- publish generation check removed -> stale publish acted on identical binding;
- release generation check removed -> stale release acted on identical binding;
- protected routing removed -> control did not consume protected pool first.

The signed-coordinate worker's initial RED/GREEN binaries used repository build/
archives, not main's freshly rebuilt temporary baseline. Its extra prefix test
failed in that manually relinked binary; an attribution to concurrent kernel
changes was unsupported and withdrawn. Those cached-library results are NOT
credited as verified-current original RED/GREEN evidence. Main's fresh full builds
pass all cases. Main separately recompiled a source-copy mutant removing the new
coordinate checks against fresh current archives; admission exits1 with `invalid
higher-term coordinate reached Raft`. This is retrospective sensitivity evidence,
not a retroactively invented pre-edit RED. No production source/shared cache was
mutated during these checks.

## Reproduction / verification

Main work: `/private/tmp/f5-result-ledger.txskZL`; component worker originals:
`/private/tmp/result-ledger.G36hgg`. Release root:
`/private/tmp/velocita-full-audit.lYB4P5/release`; Debug sanitizers:
`/private/tmp/aeron-phase67-audit-build`. Both freshly built velocita-all-tests
before CTest.52/52 Release and52/52 ASan+UBSan. C++ flags remain
`-fsanitize=address,undefined -fno-omit-frame-pointer`, runtime
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. Raft C is NOT
instrumented. No new TSan, physical power-loss, performance or target claim.

Tooling40/40. Passive model corpus not rerun;109/109 remains CP-020 historical
evidence. Independent inventory matches52 host/49 V2 suites. git diff --check
clean; protected tracked paths clean, model untracked/untouched. No staging,
commit, source freeze, venue or credential action.

Standalone ledger compile: C++20/O1/cacheline64, include/, current suite plus
src/v2_result_ledger.cpp. Kernel diagnostic: additionally tests/, cached Raft
headers, and fresh archives in order v2_replication, v2_kernel, v2_evaluator,
v2_contracts, core, willemt_raft; -pthread. RESULT_ADMISSION_CASE selects leader,
follower,recovery,oversized-recovery,replacement. Do not include the four known
REDs in a passing acceptance claim or silently delete them.

## Next boundary

Obtain §8 control-only fallback approval, then integrate local/follower planning,
per-entry reservation ownership, pre-apply checks, no-allocation publication,
durable suffix replacement and restored uncommitted reservations together. Prove
capacity refusal cannot partially append and that storage failure cleanup reflects
actual successful Raft transitions. Result slot generation exhaustion must not
be discovered as a normal publication failure after apply.

Outbound/log quotas, replacement/install/archive, F6 bound metadata persistence
model port/async fences and F7 frozen target evidence remain OPEN. Pi deferred,
not waived. No live-trading authorization or whole-process memory claim.
