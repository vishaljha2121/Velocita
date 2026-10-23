# Cached archive consumer-method bypass mutant

Date: 2026-09-23

The same direct link without the mutant passed first:
`compaction fence tests passed` (exit 0).

Scope: test-only translation-unit substitution. The wrapper defines
`verify_current` as `mutant_verify_current` while compiling
`src/v2_replication.cpp`, then implements only that renamed private consumer
method to return `ArchiveStoreError::none`. The production archive-store
implementation and its publication verification remain unchanged. This tests
whether the compaction consumer's existing-coverage revalidation is decisive.

Source: `/private/tmp/cp038-mutants-20260923/cached_coverage_bypass.cpp`

Command:

```text
c++ -std=c++20 -O1 -DNDEBUG -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude -Itests -Ibuild/_deps/willemt_raft-src/include tests/v2_compaction_fence_tests.cpp /private/tmp/cp038-mutants-20260923/cached_coverage_bypass.cpp -L/private/tmp/velocita-full-audit.lYB4P5/release -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread -o /private/tmp/cp038-mutants-20260923/cached_coverage_test
/private/tmp/cp038-mutants-20260923/cached_coverage_test
```

Result: exit 1. Intended assertion killed:

```text
cached archive coverage reclaimed WAL after its named segment was corrupted evidence="/tmp/v2-compaction-fence-4lxYtb"
```

The evidence path is a test-created diagnostic root. The assertion demonstrates
that the consumer-method revalidation prevents cached metadata from authorizing
reclamation after the named immutable object is corrupted.
