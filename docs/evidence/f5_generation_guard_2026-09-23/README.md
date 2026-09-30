# CP-032: refuse unsupported generation layouts before old-layout recovery

2026-09-23. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; all remediation
uncommitted. This is a transitional startup guard, not generation publication,
snapshot installation or F5 closure. No new persistent bytes were encoded.

## Failure and smallest fix

The approved selector codec existed, but old-layout recovery ignored a selector
beside root-level identity/hard state. It could therefore return ownership of the
old runtime even when a valid selector named another, missing generation. Current
startup cannot safely interpret that layout. Refusal is the only supported action.

`VerifiedIdentityStartup::recover` now checks three reserved names immediately
after directory locking/name verification, before identity/metadata resync.
`validate_ownership` repeats the check so a previously created token cannot bypass
it. Each descriptor-relative `fstatat` uses `AT_SYMLINK_NOFOLLOW`: any entry type
refuses; only ENOENT means absent, and other lookup failures report I/O failure.
The native error `generation_layout_unsupported` has no serialized representation.

Bootstrap's existing unknown-content refusal is unchanged (generic
`unexpected_contents`, not the new error). No generation decoding, activation,
migration, directory scanning or fallback was added. Roots without these markers
retain their previous identity-startup behavior. Three cold-start name lookups
are bounded O(1); no per-order path changed and no latency measurement is claimed.
The trusted-directory/cooperative-lock assumption remains: these checks are not
fencing against an adversarial same-user writer ignoring the lock.

## Independent checks and provenance

- `worker-red.txt`: actual pre-guard production startup linked from fresh CP-031
  Release archives exits 1, accepting the selector naming a missing generation.
  Earlier compilation problems are explicitly excluded from behavioral evidence.
- `worker-green.txt`: direct current-source GREEN and existing identity suite.
- Main expanded recovery cases to all three names times regular file, directory
  and dangling symlink; checked complete file/name images and zero recovery
  checkpoints before refusal. Existing-token insertion has a separate assertion.
- The legacy identity-boundary fixture deliberately preserves opaque WAL bytes.
  It is NOT a valid-history/kernel recovery proof; existing kernel-startup and
  history suites supply their own coverage. Main corrected that potential claim.
- Independent read-only review confirmed ordering and absence of an ownership
  bypass. It noted bootstrap error typing and the cooperative-lock scope above.
- Direct source mutants separately removed the recover call, removed the token
  revalidation call, and changed the guard's no-follow stat to following stat.
  All exit 1 at the intended behavioral assertion, not a compile/link failure.
  These are guard-sensitivity checks, not original RED provenance. Raw outputs:
  `mutation-recover.txt`, `mutation-token.txt`, `mutation-symlink.txt`.

Mutation compilation used cacheline 64, C++20/O2, warnings, current test source
and a single modified `v2_identity_startup.cpp` linked before freshly rebuilt
Release archives (`replication`, `kernel`, `contracts`, `core`, `willemt_raft`),
plus pthread. No production file was mutated. Deletions match exactly
`refuse_unsupported_generation_layout(owner.value);` and
`refuse_unsupported_generation_layout(directory_fd_);`; the third replaces only
the helper's `AT_SYMLINK_NOFOLLOW` argument with 0. Mutant copies/binaries are in
`/private/tmp/velocita-generation-guard-mutations.5hZgg5` and are not dependencies.

## Verification

Main rebuilt the complete test aggregate before CTest. Raw configure/build/test
logs are alongside this file. Release 68/68 (21.81s), focused startup 3/3,
C++ Debug ASan+UBSan 68/68 (163.04s), tooling 40/40, inventory 68 host / 65 V2.
Independent CTest inventory comes from the gate script's separately maintained
required-name array, not from accepting CTest's own answer as the expected set.

Reproduction:

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake -S . -B /private/tmp/aeron-phase67-audit-build
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

The second build root is an existing Debug configuration with C++
`-fsanitize=address,undefined -fno-sanitize-recover=undefined
-fno-omit-frame-pointer`, cacheline 64. Raft C remains uninstrumented; leaks are
disabled. No new TSan, model, target, power-loss or trading-acceptance claim.

Prepared-base format/layout/migration proposal:
`../../F5_GENERATION_STORAGE_REVIEW.md` remains OWNER REVIEW REQUIRED.
Archive-loss, lagger-catchup and owner-fsync integration REDs remain open.
No staging/commit/tag, device/venue, protected-path or credential action.
