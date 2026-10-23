# Future archive-generation startup-guard bypass mutant

Date: 2026-09-23

The same direct link without the mutant passed first:
`archive startup regression tests passed` (exit 0).

Scope: test-only translation-unit substitution. The wrapper defines
`publication_storage_generation` as
`mutant_publication_storage_generation` while compiling
`src/v2_replication.cpp`, then implements only that renamed private consumer
observation to return zero. Archive decoding and every other startup guard remain
production implementations. This isolates the selected-generation comparison.

Source: `/private/tmp/cp038-mutants-20260923/future_generation_bypass.cpp`

Command:

```text
c++ -std=c++20 -O1 -DNDEBUG -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude -Itests -Ibuild/_deps/willemt_raft-src/include tests/v2_archive_startup_tests.cpp /private/tmp/cp038-mutants-20260923/future_generation_bypass.cpp -L/private/tmp/velocita-full-audit.lYB4P5/release -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread -o /private/tmp/cp038-mutants-20260923/future_generation_test
/private/tmp/cp038-mutants-20260923/future_generation_test
```

Result: exit 1. Intended assertion killed:

```text
future-generation: startup served with an archive catalog from future storage generation two
1 archive startup regression(s) failed
```

Only one of the three aggregated startup cases failed, demonstrating that the
future-generation assertion specifically depends on this guard while the
archive-ahead and boundary-term guards remained effective.
