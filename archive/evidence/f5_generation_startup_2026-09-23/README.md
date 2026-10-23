# CP-034: initial generation publication and selected cold startup

2026-09-23, branch `v2/phase-5-corrections`, HEAD `d44a4a9`. All remediation
remains uncommitted. Implements the already approved
`../../F5_GENERATION_STORAGE_REVIEW.md`; no additional persistent byte contract.
Two bounded workers handled fixture migration and new startup tests/review;
main owned production, the persistence model, integration and regression gates.

## Delivered boundary

Fresh bootstrap requires an empty deployment root. It creates generation 1,
initial bound zero metadata, root identity, an empty immutable genesis base and
its seal. Each dependency is synced before final selector publication. The
160-byte temporary selector is file-synced, renamed, then root-directory-synced.
Only after revalidation does bootstrap return the move-only ownership token.
There is no second root-level metadata store. Existing kernel/store users now
receive the selected runtime path through the same private-constructor token.

The token retains root, generations-parent and selected-runtime directory locks.
Exceptions release descriptors through RAII. Revalidation checks directory
name/inode bindings, immutable identity/base and exact selected value; a valid
successor invalidates an old token even while the old directory still exists.
This relies on trusted directories and cooperative locks, not hostile same-user
writer exclusion. The token must outlive every store and writer using it.

Recovery follows only the final selector. A leftover temporary is inspected as
absent, valid, or invalid and preserved; valid means its frame/identity validate,
NOT that its candidate base exists or that selection is authorized. Neither a
valid nor malformed temporary supersedes a valid final. This retains approved
pre-rename recovery of the old selection. Inspection I/O errors fail closed.
Temp-only roots refuse; there is no scan, newest-directory guess or promotion.

For an immutable snapshot base at S, the runtime snapshot must exist, validate
and be at least S with a nondecreasing term. At S its term and canonical state
digest must match exactly. Catalog dependencies and covering hard-state term
are checked. Missing/corrupt/older snapshots stop rather than being silently
rebuilt from the base. These snapshot fixtures are manually authored valid
selected images, not evidence of a live snapshot-install implementation.

Cold recovery re-syncs selected files/directories, including an existing WAL,
before returning ownership. This can establish durability before full history
semantic validation; it does not rewrite payloads. Complete WAL/command/suffix/
commit coverage validation remains in `ReplicatedKernel` before writable WAL
open. The startup token itself proves neither valid full history nor voting/send
authority. This separation reuses the existing recovery decoder instead of
introducing a second one.

## Changed compatibility and availability

- Removed the transitional reject-all-generation guard because selected startup
  now understands the approved layout. It is replaced, not bypassed.
- Removed legacy-root bootstrap retry/adoption and mixed V1/V2 root acceptance.
  Closed-root ownership requires operator-reviewed migration; refusal preserves
  both histories byte-for-byte. An empty root left at the first parent-sync cut
  remains genuinely fresh and can retry; nonempty incomplete roots stay stopped.
- A rename followed by failed sync never returns a token or resumes an old
  runtime. Restart may validate the visible final and re-establish durability.
  There is no automatic cleanup or implicit reset of voting identity.
- Updated existing fixture routing to the selected runtime. A migrated legacy
  and a new successor fixture initially used a replace-only metadata API on an
  absent file; these setup errors were corrected with explicit encoded initial
  frames, not by loosening production admission. Failed runs are retained.
- Changed three macOS-specific test temp prefixes from `/private/tmp` to `/tmp`
  for Linux portability. This is not target-execution evidence.

## RED, mutation sensitivity, and model scope

`worker-red.txt` captures the previous startup returning the deployment root
instead of a selected runtime. `worker-selector-temp-red.txt` captures the new
diagnostic accessor stub incorrectly reporting a valid temp as absent. These
are intended behavioral/component REDs. Intermediate fixture failures in
`release-tests-first.txt` and `release-tests-second.txt` are not production REDs.

The publication model compiles the actual startup implementation behind narrow
syscall wrappers. It tracks file bytes only after file sync, and directory names
only after directory sync, then materializes that durable image into a different
temporary tree. A returned token requires a durable final selector name; that
name must recover a complete generation-1 base and exact zero voting memory.
The expected persistence state is not obtained from the startup result.

It sweeps all 26 observed mkdir/write/rename/fsync cut points and EIO-before
positions, plus short write, EINTR write and the four checked output closes
(metadata, identity, seal, selector). Native checkpoint tests additionally cover
visible post-rename recovery. This is a genesis publication model, NOT physical
power-loss proof, a complete POSIX fault model, snapshot-base publication, or
live switching. It does not inject open/read/stat/readdir/flock failures.
Existing codec goldens independently cover byte semantics; this model's use of
real codecs is not a second independent oracle for those formats.

Direct-source mutants retain the exact assertion that kills each removed guard:

| Removed guard | Required observable failure |
|---|---|
| Final selector directory sync | Startup returned before selector namespace was durable |
| Base seal file sync | Persisted selector failed to recover a complete durable base |
| Runtime snapshot floor | Invalid below-base runtime was accepted |
| Exact token selection equality | Valid successor did not stale generation-1 token |

These are retrospective test-sensitivity checks, not original RED evidence.
Main mutation copies live under `/private/tmp/velocita-generation-startup-mutants.wnHgzP`;
production files were not altered for mutation runs. Direct C++20/O2/cacheline64
compilation uses changed source before fresh archives, not CMake timestamp
mutation inference. Worker commands/results are retained alongside main logs.

## Gates and scope still open

Final Release **72/72 (12.35s)**, full C++ Debug ASan+UBSan **72/72 (151.83s)**,
tooling **40/40**, independently validated inventory **72 host / 69 V2**.
`git diff --check` passes. Build always precedes CTest. The first sanitizer run
retains the same successor-fixture setup failure as the second Release run; it
is superseded by the full rebuilt final run, not counted as passing evidence.
C++ ASan+UBSan uses nonrecovering undefined behavior and halt-on-error;
LeakSanitizer is disabled and Raft C remains uninstrumented. No new model/TSan,
NUC/ARM64/Pi, latency or physical power-cut claim.

Reproduction:

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake -S . -B /private/tmp/aeron-phase67-audit-build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Original archive-loss, lagging-voter catchup and owner-fsync REDs remain open and
excluded from GREEN acceptance counts. Actual archive publication/reclamation,
snapshot-generation install with unknown suffix/result obligations, fenced
repair, async owner fencing and final target gates remain. F5/F6/F7 NOT CLOSED.
Protected paths/model preserved; no stage, commit/tag, source freeze, device,
venue, deployment or credentials action. Pi is deferred, not waived.
