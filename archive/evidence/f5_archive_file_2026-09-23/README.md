# CP-030 read-only archive-file verifier evidence

Scope: test-first implementation of a read-only POSIX file reader around the
approved CP-028 archive codec. This is not publication, coverage, commitment,
durability, independent-copy, or deletion evidence.

Owner approved the four CP-029 directions before this increment. New persistent
selector/coverage/receipt bytes and unresolved budgets remain separate reviews.
Main led integration and review with two bounded workers. No staging/commit/tag,
target, venue or actual credential action; all pre-existing dirty paths retained.

## Original RED

The new test was compiled against an intentional implementation stub that always
returned `open_failed`. The executable exited at its first positive assertion:

```
valid archive file refused
```

Command (using fresh compilation of the current CP-028 sources and existing
dependency archives, not a shared CMake build target):

```
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude \
  tests/v2_archive_file_tests.cpp src/v2_archive_file.cpp \
  src/v2_archive_segment.cpp src/v2_static_identity.cpp src/v2_digest.cpp \
  -Lbuild-v2-release -lvelocita_v2_kernel -lvelocita_v2_evaluator \
  -lvelocita_v2_contracts -lvelocita_core \
  -o /private/tmp/archive_publication_red/v2_archive_file_tests
/private/tmp/archive_publication_red/v2_archive_file_tests \
  /private/tmp/archive_publication_red/case
```

Raw retained output: `red.log`. The failing fixture directory was preserved.

## GREEN

After the smallest implementation, the same strict direct build passed the
filesystem suite. The independently compiled syscall-shim suite also passed
7-byte short reads, open/stat/read/lookahead `EINTR`, read and close errors,
early EOF, extra byte, and post-read size/inode/link/stat changes. The real-file
suite additionally covers zero and sparse-over-cap regular files, FIFO, symlink,
hard link, corrupt digest, exact-size buffer, and oversized caller buffer. Raw
outputs: `green.log` and `main-io-green.log`.

No shared CMake directory or target was edited by this lane.

## Main integration and independent checks

Main added a second permanent suite compiling the production reader verbatim
behind POSIX syscall shims, using the existing independently authored CP-028
golden. The shim returns seven bytes per read and checks full payload equality,
explicit coordinates, unchanged metadata on failure, exactly one close and safe
open flags. It injects open/stat/read/lookahead EINTR, EIO, early EOF, extra data,
changed size/inode/link count and close EINTR. These are controlled syscall
responses, not measured real-filesystem races or a power-loss model.

Main's initial stub test compiled and failed at valid short-read acceptance;
`main-io-stub-red.txt` retains it. A later open-EINTR failure had a generic
assertion without a named case; it is retained in `main-io-interrupt-red.txt` but
not claimed as a named production-guard RED. By the named-case rerun the worker's
fix was present, so that output is correctly labeled
`main-io-interrupt-named-green.txt`. This is new-component TDD, not repair of the
old archive-loss integration failure.

Direct source-copy mutants, all compile successfully and exit1:

| Removed guard | Intended failing case | Evidence |
|---|---|---|
| post-read stable descriptor check | `size-change` accepted | mutant-stat.txt/.patch |
| EOF lookahead refusal | `trailing` accepted | mutant-eof.txt/.patch |
| codec failure propagation | literal short archive accepted | mutant-codec-named.txt, mutant-codec.patch |

Mutants were compiled directly, not by replacing sources through CMake. Main
added filename/error reporting to the real-file assertion to identify the codec
mutant specifically; the earlier generic `mutant-codec.txt` is superseded.
These are retrospective guard-sensitivity checks, not original behavioral REDs.

Main added the production reader to the replication library and two CTest suites
to CMake and the independent gate inventory: **65 host / 62 V2**. No existing
suite was removed. Main fresh build then Release65/65 (final log
`release-tests-final.txt`); full C++ Debug ASan+UBSan65/65 (155.23s,
`sanitized-tests.txt`), followed by rebuild and focused final2/2 after the sole
test-diagnostic-text change (`sanitized-focused-final.txt`). UBSan nonrecovering
and halt-on-error, leaks disabled; Raft C remains uninstrumented. No TSan/model/
target/latency claim. Tooling40/40 and exact independent inventory validation
passed (`tooling-tests.txt`, `inventory-validation.txt`, `release-inventory.json`).

Build roots: `/private/tmp/velocita-full-audit.lYB4P5/release` and
`/private/tmp/aeron-phase67-audit-build`, both reconfigured and built before CTest.
Current caller-buffer cap is inherited from CP-028; the reader itself allocates
nothing, confirmed on the real-file success path. Larger caller buffers are
allowed, but files over4MiB are refused before reads. The existing CP-028 codec
suite owns exact4MiB-valid-object coverage; this new suite does not duplicate it.

Corrected during review: invalid-identity fixture originally followed hard-link
creation and would hit the wrong guard; moved earlier. `fstat` failure now has
its own typed error rather than asserting a file type that was never read.
Open/stat EINTR retries are explicit; close is never retried because descriptor
reuse makes that unsafe. Nonaliasing and the fd-versus-basename proof boundary
are explicit API preconditions. No new file names, protocol or canonical bytes
were frozen by this increment.

## Limits

The test does not simulate a malicious concurrent equal-size writer, filesystem
power loss, publication ordering, independent storage, or target-device behavior.
The directory and files are exclusively owned trusted inputs by contract. A
successful call verifies the opened descriptor, not that the basename still
names the same inode after return. The implementation performs no allocation,
but caller storage and whole-process memory are outside that claim.
