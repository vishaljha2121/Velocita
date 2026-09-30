# CP-039: integrated local archive compaction evidence

2026-09-23; branch `v2/phase-5-corrections`, base HEAD `d44a4a9`, dirty working
tree. This is development evidence, not a new frozen target acceptance.
Approved contract: [CP-038](../../F5_ARCHIVE_CATALOG_BYTE_REVIEW.md).

## What is actually exercised

`v2-archive-compaction-tests` drives real `ReplicatedKernel::compact()`, checks
independently specified historical command bytes in catalog-named archive objects,
removal from the live WAL, and preservation after restart and a second cut.
Unread READY results remain deliverable. A two-voter pilot identity cannot use
local-only coverage for reclamation.

`v2-compaction-fence-tests` parks the actual storage worker and proves the owner
does not progress, release results/authority or remove WAL while waiting. Four
transaction checkpoints throw and recover committed state; a previously published
archive can finish after restart. These four checkpoints are not every write,
sync, rename, short-write or hardware failure boundary.

The store suite exercises exact maximum command packing: 8,192 records require
four bounded objects under one catalog publication. Early cuts account for
remaining accepted liabilities before any object write. Ordinary admission and
protected completion/control margins differ. Permission, namespace, missing old
object, stale generation and capability-reuse cases refuse.

## REDs and corrections

| Evidence | Intended failure and correction |
|---|---|
| `original-archive-red.log` | Original snapshot-only compaction removed every raw copy of a committed command; integrated archive coverage now precedes release. |
| `capacity-deferral-red.txt` | An underfilled cut exhausted packed-object escrow and killed service; capacity-only completion now cancels the snapshot and retains original history. |
| `cached-coverage-red.txt` | Cached metadata authorized release after a named segment changed; worker now revalidates/resyncs existing evidence before capability minting. |
| `startup-boundaries-red.txt` | Re-encoded future-generation, ahead-of-history and conflicting snapshot-boundary artifacts were accepted; cold startup cross-checks them before writable WAL open. |
| `startup-cross-boundary-term-red.txt` | Individually valid artifacts formed a decreasing archive-to-WAL term boundary; combined history now refuses. The snapshot-bridge fixture hits the WAL check first, not an independent proof of the defensive snapshot check. |
| `store-full-catalog-red.log` | Valid append at inventory capacity was mislabeled stale generation; now returns capacity with actual files unchanged. |
| `sanitized-tests.txt` | Eight suites reproduced `SnapshotStore::load()` worker-stack overflow; fixed-size save/load workspaces moved to heap. |

The first failed Release run (`release-tests.txt`, 81/82) exposed an untyped
archive/WAL conflict exception incompatible with the existing history-binding
contract. The refusal is now `BoundWalError(invalid_payload)`.

`release-final.txt` and `sanitized-final.txt` are also **failed intermediate runs**,
despite their names: each 81/82. They caught the old allocation test assuming all
codec scratch remained on the stack. Fixed input lookahead remains 65,588 bytes;
the updated oracle bounds total fixed decoder workspace plus bounded exception
overhead. It still rejects file-proportional allocations. Do not count these as
acceptance or overwrite the failed logs.

The codec-stub RED in `catalog-red.log` is a component test, not the original
production data-loss defect. `store-review.md` describes an earlier one-segment
implementation, superseded by the current four-segment transaction. Fixture
migrations for the asynchronous API and pinned Raft's minimum snapshot-log count
are not product defect evidence.

## Verification

Final source rebuilt successfully before each full run:

| Check | Actual result | Evidence |
|---|---|---|
| Host Release | 82/82, 18.44s | `release-verified.txt` |
| Host Debug C++ ASan+UBSan | 82/82, 200.87s | `sanitized-verified.txt` |
| Focused archive/fence/snapshot suites | 7/7, 1.31s | `focused-verified.txt` |
| Tooling Python unit tests | 40/40, 1.063s | `tooling-and-inventory.txt` |
| Independent required inventory | 82 host / 79 V2, exact match | `tooling-and-inventory.txt` |
| Focused worker TSan | passed, no report | `wal-maintenance-tsan.txt` |
| Final whitespace check | `git diff --check` exit0 | main verification |

`focused-source.sha256` records the implementation and focused test files used
for the final rebuild. It is a partial diagnostic fingerprint, NOT the complete
gate source root or a deployment pin.

Build commands (both must finish successfully before their CTest run):

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j6
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j6
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

Sanitizer scope: Debug C++ ASan+UBSan, `-fno-sanitize-recover=undefined`, UB halt,
leak detection off. The linked Raft C library is not instrumented in this host
configuration; do not report whole-program C coverage or leak coverage.

Two direct-source mutation checks are recorded in `mutant-cached-coverage.md`
and `mutant-future-generation.md`. Both unmodified baselines passed; the consumer
method bypasses failed at the intended assertions. They are method-substitution
mutants, not claims that every guard has been individually removed and tested.
`mutant-snapshot-allocation.txt` additionally reintroduces a file-size-proportional
allocation into the current snapshot reader and fails at the intended revised
workspace-budget assertion. Production source and shared builds were not mutated.

The standalone WAL-maintenance suite was compiled from its production sources
with `-fsanitize=thread` and passed without a TSan report. This is focused worker
handoff coverage, not TSan coverage of the entire replica/archive transaction.
Command and result: `wal-maintenance-tsan.txt`.

## What this does NOT close

- Broad E1 actual lagger installation is still absent; its contract witnesses do
  not implement transfer or activation.
- Broad E2 live reclamation still requires independently authenticated durable
  destination evidence. No local flag substitutes for that requirement.
- E2 needs systematic syscall/persistence-image sweeps beyond four transaction
  checkpoints, plus capacity/follower integration and deployment validation.
- E3 hard-state and suffix-truncation paths remain synchronous. Only this actual
  compaction transaction now uses the shared worker/fence.
- No NUC/ARM/Pi gate, peak process-memory measurement, latency result, fresh Python
  model result or real device power-loss claim. Protected model code was untouched.

Archive work is bounded but not constant-time: publication revalidates previously
named evidence (up to the configured logical-byte limit). The current cold open
also has bounded quadratic filename matching at the 4,096-object inventory cap;
do not call all startup work linear or measured low latency. Owner-side copies
are bounded by 8,192 retained records; their latency and worker peak memory still
need measurement. Correctness takes priority over optimization at this checkpoint.
