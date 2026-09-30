# CP-014: sync-sensitive metadata ordering evidence

2026-09-22, host only. Base HEAD d44a4a9 on v2/phase-5-corrections; remediation
remains uncommitted. The owner approved ADR0014 direction after CP-013, including
provisional retention of atomic metadata replacement. No new production bytes,
asynchronous Raft protocol, source freeze, device/venue run, commit or tag here.

## What changed and why

Added `tests/v2_metadata_persistence_model_tests.cpp`, its standalone CTest target
and independent gate-inventory entry. The real `src/storage.cpp` is included in
a test-only renamed namespace after system headers have been parsed. Five POSIX
calls are redirected to a deterministic model: open/write/fsync/close/rename.
Production storage code and syscalls are NOT changed. No global interposition
reaches the node or other suites; this target does not link a second storage copy.

Model state distinguishes inode volatile/durable contents and directory
volatile/durable names. File fsync persists bytes; directory fsync persists name
publication. A modeled power cut freezes state immediately, including during C++
exception unwinding: cleanup cannot accidentally complete persistence after the
cut. The fixture starts with an independently encoded durable tuple `(7,2,19)`;
the requested replacement is `(8,3,23)` (term, vote, commit index).

Independent semantic oracle:

- An interrupted or failed replacement may recover the complete old OR complete
  new tuple, never an invalid or mixed tuple.
- A save that returns successfully MUST recover the complete new tuple.
- The recovered image is materialized in private temporary storage and parsed by
  real MetadataStore::load; its result must match the independent semantic oracle.

Coverage sweeps every syscall boundary of the normal replacement and a replacement
whose first write completes only half the bytes. The latter must retry to a
successful uncut save; a cut after the first prefix is also tested. Another sweep
injects `-1`/EIO before each normal-path syscall. A failed directory close AFTER
successful directory sync is permitted to return success by the present code:
the recovered value, not the mere presence of an injected error, decides safety.

## Meaningful mutations and provenance

These are NEW tests of existing production behavior, not a newly discovered
production bug followed by a fix. The baseline was already GREEN. The REDs below
are explicitly isolated-source mutation checks, not fabricated pre-fix evidence.

| Production-source mutation | Actual failure | Why this is the intended oracle |
|---|---|---|
| Remove only MetadataStore::save's file fsync/error branch | `power recovery produced invalid metadata (0 durable bytes)` | Published filename can reference data never made durable |
| Remove only MetadataStore::save's fsync_directory call | `acknowledged save recovered tuple {term=7, vote=2, commit=19}, not the new tuple` | Returning success before durable name publication loses acknowledged hard state |

Exact `.patch` files and raw failure logs accompany this record. Main reviewed the
test, required the frozen-cut semantics, successful short-write retry and actual
load cross-check, then independently directly compiled BOTH final-source mutants.
Both compiled successfully and exited1 at the semantic assertions above. They did
not fail on a syscall-count check, compilation, or an unrelated earlier assertion.
Shared production sources/build roots were never mutated.

Direct compiler recipe (baseline):

```sh
c++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Iinclude \
  tests/v2_metadata_persistence_model_tests.cpp -o /tmp/private-run/model
```

For each mutant, apply ONLY the retained patch to a private storage.cpp copy,
then pass `-DVPM_STORAGE_IMPLEMENTATION='"/absolute/private/storage.cpp"'` to that
same command. Use a real newly created private directory, not the literal example
path. The production include is otherwise the repository's src/storage.cpp.
Agent artifacts: `/private/tmp/velocita-f6-model.7b2G8u`; durable review evidence
is copied here so correctness does not depend on that temporary directory surviving.

Reviewed file hashes (not a source-freeze/acceptance pin):

- src/storage.cpp: `8dab43d64414d96ac62703028c9cc07507d642f102a2fac1dd330c1604645f47`
- test: `2db249843d356980ed00488c8a6739c356dd53a8db1cceef0e1bbb4ddfecf767`

## Limits — do not promote this model into a device proof

This is a deliberately small ordering model, not every permitted filesystem
history. It has no spontaneous background writeback, no partially successful
failed fsync, no exhaustive sector tears, no lying device cache, and no parent
directory/bootstrap crash model. It tests replacing an already durable metadata
file in an existing directory, not the first creation of a deployment lineage.
The short-write sweep is not an exhaustive tear sweep. It does not prove physical
NUC/Pi persistence, filesystem/controller guarantees or owner-loop latency.

The old CP-006 process-exit model could not detect either missing sync. This one
does: that is meaningful progress, but its restricted crash-state space remains
explicit. Retaining atomic replacement stays provisional pending the stronger
failure/writeback coverage, target storage evidence and explicit final amendment
of the original plan's two-slot prescription. F6/G-047 are NOT closed.

Synchronous hard-state writes, bounded asynchronous continuation/output fences,
background writer error reporting and broader storage error paths remain open.
No callback may return early success on the strength of this test. F5 runtime
bounds, archive coverage and peer snapshot replacement are independent obligations.

## Final verification

Built before every CTest invocation. Main full host Release **44/44**, Debug
ASan+UBSan **44/44**, offline tools **40/40**, passive model **109/109**. Sanitizer
runtime uses `UBSAN_OPTIONS=halt_on_error=1`, `ASAN_OPTIONS=detect_leaks=0`;
existing C++ instrumentation only, not newly instrumented Raft C. Independent
inventory is **44 total /41 V2**. `git diff --check` clean. No target acceptance.

Release build: `/private/tmp/velocita-full-audit.lYB4P5/release`.
Sanitizer build: `/private/tmp/aeron-phase67-audit-build`.
Full run outputs, inventory check and direct mutation outputs are retained here.
