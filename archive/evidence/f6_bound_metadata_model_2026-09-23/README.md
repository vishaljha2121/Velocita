# CP-027: actual bound-metadata persistence model

2026-09-23. Host-only verification of existing production ordering; NOT a new
production fix or a physical power-loss result. Branch `v2/phase-5-corrections`,
HEAD `d44a4a9`, all remediation uncommitted. The owner approved CP-026 semantic
choices A/B; archive/install bytes and asynchronous Raft fences remain pending.

## Coverage and independent expectation

The new `tests/v2_bound_metadata_persistence_model_tests.cpp` compiles the real
`src/v2_bound_metadata.cpp` behind test-only POSIX shims. This ports CP-014's proof
approach from the old generic MetadataStore to the identity-bound store actually
used by V2. Production I/O, byte layout and persistence order are unchanged.
No macro/interposition reaches the other binaries. The old model suite is retained
for the older implementation; its passing result is not substituted for this one.

The model separately owns volatile/durable inode bytes and volatile/durable
directory names. File fsync makes data durable; directory fsync makes replacement
publication durable. A cut freezes the model during exception cleanup as well.
Expected old tuple is `(term=7, vote=17, commit=3)`; expected new tuple is
`(47,257,127)`, not a value learned by reading the result under test.

The old112-byte frame is independently packed and checked against an existing
literal golden. New bytes are independently packed from the authored tuple. The
packer does not call the production metadata encoder; it shares SHA-256 and the
verified identity implementation, so this is an ordering oracle, not independent
cryptographic/identity-codec certification. Recovery uses the actual bound decoder,
then checks the authored semantic values and exact acknowledged bytes.
Main additionally materializes the model's durable bytes/names into a fresh
in-memory filesystem and calls actual BoundMetadataStore::load; it must agree
with the authored-tuple oracle. This does not invoke full node startup/temp policy.

Normal save has14 modeled syscall boundaries; valid pre-existing-temp replacement
has21; a first short write followed by retry has15. Every after-call power cut on
those paths is swept; EIO-before-call is swept on absent/existing-temp normal paths.
Interrupted saves must recover a complete old or new authoritative frame. A save
returning success must recover exactly the new frame. Natural ENOENT inventory
results also count as completed calls. No arbitrary host directory is removed;
main removed unnecessary creation of real empty directories from this pure model.

Main review also removed catch-all fixture handling: unexpected model STL/logic
exceptions must fail the test, not masquerade as expected storage failures.

## Provenance and limitations

This is a new verification test for existing GREEN production behavior. The
source-copy sync-removal checks below are **retrospective mutations**, not original
pre-fix REDs. CP-026's real catch-up/archive-loss REDs remain unfixed and separate.

The model does not prove actual device/controller/fsync behavior, arbitrary sector
tears, spontaneous writeback, concurrency, symlink/path races, transport fencing,
or successful full-node restart with a leftover partial temp. Such a temp may
correctly quarantine startup under the existing policy. The assertion here is the
authoritative target's old-or-new durability, not automatic cleanup or availability.
No missing/foreign-hard-state precondition was bypassed to make the fixture pass.

## Reproduction

Baseline CMake target: `v2-bound-metadata-persistence-model-tests`, included in the
aggregate all-tests target and independent gate inventory. Build BEFORE CTest.
Direct source-copy mutants compile with the implementation override:

```sh
c++ -std=c++20 -O1 -DNDEBUG -Wall -Wextra -Wpedantic \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  '-DBPM_BOUND_METADATA_IMPLEMENTATION="/private/tmp/f6-bound-model-review.3txKm4/no-file-sync.cpp"' \
  -Iinclude tests/v2_bound_metadata_persistence_model_tests.cpp \
  -L/private/tmp/velocita-full-audit.lYB4P5/release \
  -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator \
  -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread \
  -o /private/tmp/f6-bound-model-review.3txKm4/no-file-sync
```

Directory mutant substitutes `no-directory-sync.cpp` and output of that name.
Each copy differs from production by deleting exactly one save-time fsync/error
branch. Original sources/build libraries are never mutated. Main independently
compiles and runs both; see retained raw logs and exact patches below.

## Final verification

| Check | Result | Evidence |
|---|---|---|
| Fresh Release build + full regression |60/60,10.90 seconds | `release-tests.txt` |
| Focused actual bound model |14/21/15 boundary counts, pass | `focused-green.txt` |
| Fresh Debug C++ ASan+UBSan build + full regression |60/60,146.57 seconds | `sanitized-tests.txt` |
| File-sync-only mutant | Compile0/run1: recovered invalid bound metadata | `no-file-sync.patch`, `no-file-sync.txt` |
| Directory-sync-only mutant | Compile0/run1: acknowledged47/257/127 not recovered | `no-directory-sync.patch`, `no-directory-sync.txt` |
| Independent required inventory |60 host/57 V2, exact set; no disabled/skip contract | `release-inventory.json` checked by deploy/validate-ctest-inventory.py against gate list |
| Python tooling |40/40 | `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py'` |
| Whitespace | `git diff --check` clean | Main direct check |

Sanitizer root `/private/tmp/aeron-phase67-audit-build`; C++ flags include
`-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer`.
Run env `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. Raft C remains
uninstrumented. No leak, TSan, physical power-loss, target acceptance or performance
claim. Python model109 remains historical. Protected tracked paths clean; untracked
model untouched. F6 async/install composition and all F5 recovery obligations stay
open despite this model-port closure.
