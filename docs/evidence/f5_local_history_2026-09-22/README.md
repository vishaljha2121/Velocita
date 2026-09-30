# CP-020 — approved local history binding and crash-safe genesis

2026-09-22. Branch `v2/phase-5-corrections`, base HEAD `d44a4a9`; full remediation
tree UNCOMMITTED. Not a source freeze or deployment acceptance. No target, venue,
credential, commit or tag action. Approved contract: `../../F5_BOUND_FRAME_BYTE_REVIEW.md`.

## Implemented and why

1. New `BoundMetadataStore` encodes exact 112-byte V2BF hard state; validates
   tuple/member semantics and local identity; missing final is typed absence,
   never a default term/vote. It validates complete matching replacement temps
   without promotion. Partial/foreign temps preserve evidence and refuse.
2. Genesis durably publishes zero metadata FIRST and immutable identity LAST.
   Tests exercise all reviewed retry shapes and 13 process-interruption points,
   including partial and complete temp writes. No startup token is serving
   authority, nor permission to reset a voter that lost voting memory.
3. WAL block version2 contains bound records with positive representable index/
   term and validated canonical command or domain-bound marker. Record frames
   are consumed exactly, including snapshot-covered/uncommitted entries. The
   kernel no longer has a legacy flat-WAL member or fallback; any legacy filename
   presence refuses, even an empty file. V1 storage bytes/behavior are unchanged.
4. SnapshotStore requires verified identity, reads at most65,588 bytes, validates
   kind/lineage/local owner and canonical state, and emits the approved bound
   file. Foreign/legacy/unsupported is a typed exception, never optional replay.
   Supported corruption may replay only if kernel log/commit coverage permits.
5. Recovery verifies metadata term covers WAL/snapshot terms, retained index/
   term order and overlap consistency. Snapshot index may exceed the older
   metadata commit checkpoint: its own durability establishes that committed
   prefix. Rejecting that case would break the legitimate volatile-follower
   snapshot regression. Commit-index freshness is not term/vote freshness.
6. Full nonzero corrupt WAL blocks now refuse before writable open rather than
   becoming overwrite frontiers. Complete zeros terminate preallocation; a
   physical short tail yields only a complete prefix, and missing committed
   coverage still refuses. There is no scan beyond damage to invent continuity.

Local decoders/stores expose exact BoundFrameError distinctions through typed
exceptions. Immutable identity values are owned, not borrowed from temporary
callers. Read paths are bounded/no-follow/nonblocking and regular-file checked;
temporary write paths check file/link state before truncation. Trusted stable
owned paths remain an assumption; this is not hostile-same-UID authentication.

## Test-first evidence and corrections

Before implementation, main rebuilt the pre-change archive and directly compiled
the existing history diagnostic. These logs are behavioral REDs, not compiler
failures: `metadata-red`, `wal-red`, `snapshot-red`, `missing-hard-state-red`.
The new bootstrap assertion separately failed because identity existed without
bound zero metadata (`bootstrap-red`). Foreign WAL/snapshot restored kill9 rather
than target kill1; missing hard state restored term0 after term1 participation.

Review found additional traces and main captured RED before their fixes:

| Trace | RED log | Permanent witness |
|---|---|---|
| Full corrupt block becomes empty history | corrupt-block-red.txt | WAL arena complete-corruption test |
| Metadata term behind WAL term accepted | wal-term-red.txt | history-binding wal-term selector |
| Metadata term behind snapshot term accepted | snapshot-term-red.txt | history-binding snapshot-term selector |
| Duplicate snapshot-covered record accepted | covered-history-red.txt | history-binding covered-history selector; also gap/overlap cases |

Two bounded workers owned metadata codec/tests and snapshot codec/tests, then
WAL fixture migration, permanent history tests and independent review. Main owned
genesis, WAL codec/runtime, kernel integration, fixture corrections, final builds,
mutations and documentation. Worker metadata API-missing compile RED and snapshot
dependency compile failures are NOT counted as behavioral RED evidence.

The first full integration run exposed an actual new regression: oversized future
canonical snapshots became optional corruption before inspecting their schema.
The existing permanent replication test caught it; main requested correction and
reviewed the bounded prefix probe. Its final GREEN is part of the full suite.

Malformed-history fixtures use an explicit test-only raw outer-frame writer so
they reach the target recovery guard; using the semantic encoder would reject
their setup and produce a false-positive test. Healthy counterparts restore the
original file and assert recovered state, not merely an exception label. Tests
also retain existing sync/worker/suffix-rewrite failure oracles after migration.

Independent metadata/WAL golden bytes use literal identity plus Python struct,
hashlib and zlib calculation, not codec output as expected output. The WAL literal
is212 bytes, its canonical command100 bytes, and its enclosing block-v2 CRC is
0x0f6edcae. No native-struct dump is a wire oracle.

## Mutation sensitivity (retrospective, isolated copies)

| Removed guard | Observed failure |
|---|---|
| Common manifest equality | foreign metadata was accepted |
| Common local owner equality | wrong-owner metadata was accepted |
| Missing metadata refusal | missing hard state was accepted |
| WAL term <= metadata term | metadata term behind retained WAL term was accepted |
| Snapshot term <= metadata term | metadata term behind snapshot term was accepted |
| Complete-corrupt-block refusal | complete corrupt WAL block accepted as empty segment |

All six mutant binaries exit1 at the intended assertion, not compilation or an
unrelated fixture error. Raw outputs accompany this record. Main directly compiled
each altered source from a private temp copy plus the relevant suite and rebuilt
archives; production sources and shared CMake caches were NOT mutated/copied.
No sync-removal mutant is claimed for the new bound metadata implementation.

## Reproduction and scope

Working evidence: `/private/tmp/f5-local-history.ycnUqh`. Release root:
`/private/tmp/velocita-full-audit.lYB4P5/release`. Sanitized root:
`/private/tmp/aeron-phase67-audit-build`. Both rebuilt from the dirty tree before
CTest; cacheline64. Fresh native probe: TradingState81,392 unchanged;
ReplicatedKernel91,504 inline bytes (previous91,200), excluding heaps/library memory.

```sh
cmake --build <build-root> --target velocita-all-tests -j8
ctest --test-dir <build-root> --output-on-failure
HISTORY_BINDING_CASE=wal-term <release-root>/v2-history-binding-tests
HISTORY_BINDING_CASE=snapshot-term <release-root>/v2-history-binding-tests
HISTORY_BINDING_CASE=covered-history <release-root>/v2-history-binding-tests
```

Direct mutation compile uses C++20/O1/cacheline64, include/, tests/, cached Raft
headers, altered source + tests/v2_history_binding_tests.cpp (or WAL suite), then
libraries in order: v2_replication, v2_kernel, v2_evaluator, v2_contracts, core,
willemt_raft. The two term mutants select their individual cases by environment.

- Release51/51; Debug ASan+UBSan51/51.
- C++ flags: `-fsanitize=address,undefined -fno-omit-frame-pointer`; runtime
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`, private TMPDIR.
  Cached Raft C flags are `-g -std=gnu11`, NOT instrumented. No new TSan claim.
- Tooling40/40, passive model109/109; bytecode writes disabled.
- Independent required inventory:51 host suites,48 V2; both new suites included.
- `git diff --check` clean; protected tracked paths clean, model remains
  untracked/untouched. No unrelated changes staged or reset.

## Explicit limits / next gate

Process cuts leave the OS cache alive; they are NOT physical power-loss evidence.
CP-014's syscall ordering model still executes generic MetadataStore from
storage.cpp. Its continued PASS is historical algorithm evidence and does NOT
cover the new BoundMetadataStore syscall implementation. Port that model and
repeat sync-removal mutations before claiming current V2 sync-sensitive F6 proof.

Same-lineage complete rollback, hostile file/message rewriting, whole-node
memory/work bounds, admitted replacement, portable snapshot installation, verified
audit archive and asynchronous Raft persistence fences remain separate. F5/F6/F7
are OPEN. Next review is `../../F5_ADMISSION_RESULT_REVIEW.md`; candidate capacities
remain unapproved. Pi is deferred to the final stage, not waived. This checkpoint
does not authorize live deployment or claim measured storage latency.
