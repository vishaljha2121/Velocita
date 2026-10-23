# CP-041: composed archive → snapshot → WAL persistence model

2026-09-23; branch `v2/phase-5-corrections`, HEAD `d44a4a9`, uncommitted
remediation. This completes CP-040's next bounded host-model step for E2.
It is not phase acceptance, live independent-copy evidence or hardware proof.

## Behavior exercised

The actual two-voter laboratory kernel commits commands1..3, retains an exact
durable but uncommitted command4, then executes real asynchronous compaction.
A second scenario first compacts1..3, commits4..6 and retains uncommitted7,
so old archive catalog and snapshot replacement are exercised too.

The test compiles the actual archive, snapshot and WAL sources through test-only
syscall hooks. It does not reimplement the publication algorithm. The replication
owner, maintenance worker, capability/fence checks and cold recovery are real.
CMake's hook objects are private to this executable; production is unchanged.
Archive's public `ArchiveStore::open` is not macro-renamed: its raw root open is
read-only, while archive mutation is through the instrumented openat/write paths.

| Scenario | Reached boundaries per schedule | Schedules | Cuts |
|---|---:|---:|---:|
| First compaction:cut3, durable suffix4 | 45 | 3 | 135 |
| Replacement compaction:cut6, durable suffix7 | 36 | 3 | 108 |
| Total | | | 243 |

The schedules persist directory names only on explicit synchronization, eagerly
persist individual name changes, or independently persist only the WAL rename.
Actual directory sync always persists its complete current namespace. File sync
persists that inode's bytes independently of its names. Rename is one atomic
remove-old/replace-new delta, not a pair of unrelated adds.

Why the third schedule matters: snapshot and WAL share a parent directory. A
later WAL directory sync can hide an omitted snapshot directory sync in a
successful run. Letting the WAL rename persist independently exposes the crash
window before that later sync. The corresponding actual-source mutant passes
the first two schedules and fails this third one at the intended ordering check.

## Oracles and C++ ownership

Before compaction, command bytes are compared with independently intended
commands; full pre-cut tuples are retained as a preservation oracle. Replacement
history includes1..3 captured before their initial removal, not just later WAL.
The independently known kill epoch and applied boundary accompany the full-state
digest check. This validates preservation, not the kernel's entire economics.

At every materialized crash image, BEFORE recovery can re-sync anything:

- WAL must equal either the exact old WAL or the exact retained suffix.
- If its committed prefix is absent, the matching new snapshot and the entire
  exact catalog-named archive prefix must already exist and validate.
- Thus even a startup refusal cannot hide deletion before prerequisites became
  durable. An unexpected WAL or missing suffix fails without asking recovery.

Recovery itself may refuse an interrupted image, preserving names and bytes.
If it accepts, the archive/WAL union must contain exactly the intended history
and suffix with every index, term, entry identity, type and payload checked.
A constructor exception is caught separately; failed assertions after admission
cannot be counted as a safe refusal. Successful baseline requires an actual
returned compaction cut, snapshot at that cut, exact catalog coverage, and WAL
containing only the uncommitted suffix. Deferral/no-op is not success.

Model inode data is separately owned. Read-only descriptors pin inode lifetimes
so overwritten names cannot let inode-number reuse change historical identity.
A write-only production descriptor needs a read alias for model capture; dup
alone would retain write-only access. The active model spans worker shutdown,
and a lifetime guard joins before its storage disappears. Hooks flag unexpected
threads/internal errors; they cannot silently become tolerated publication EIO.
Instrumentation verifies actual archive, snapshot and WAL operations were reached.

These dynamic maps, byte copies and pins belong to the small test fixture, not
hot production arenas. No new native or persisted production memory layout exists.

## Negative controls and review

`mutations.txt` records three actual-source mutations against the same final
test object and ABI-compatible64-byte-cache-line libraries:

1. Omit snapshot directory sync: fails `new WAL became durable before the
   matching snapshot namespace` in the selective WAL schedule.
2. Omit replacement WAL file sync: fails exact durable WAL prefix/suffix.
3. Drop the first suffix record: fails the same exact-byte/tuple oracle.

These are targeted mutation kills, not proof of every guard. The original code
already passes; no new production defect was established or fixed in this step.
Two bounded Sol workstreams produced the draft and independent review; main
corrected the oracles/instrumentation, ran mutations and owns final verification.
Final independent review found no remaining correctness blocker.

`harness-corrections.txt` distinguishes draft mistakes from product REDs. The
prior CP-040 mutation evidence also records a corrected cache-line definition
mismatch; both older sync-class mutants were rerun with64 and still fail at the
intended assertion. Historical mixed-definition commands are superseded.

## Verification

- Rebuilt host Release:86/86,18.08s (`release-verified.txt`).
- Rebuilt host Debug C++ ASan+UBSan:86/86,241.77s (`sanitized-verified.txt`).
- Focused composed model:1/1,243 cuts,2.88s (`focused-verified.txt`).
- Tooling unit tests:40/40 (`tooling-verified.txt`).
- Independent gate inventory:86 host/83 V2, no skipped/disabled tests (`inventory.txt`).
- Production source fingerprints match CP-040; partial fingerprints in
  `focused-source.sha256` are NOT a complete source root or deployment pin.

Sanitizers use nonrecovering UB and halt_on_error, leak detection off. The linked
Raft C library is uninstrumented. No new TSan, Python model-corpus, NUC/ARM/Pi,
latency, RSS or physical power-loss claim. Protected paths untouched; no
stage/commit/tag/deployment/venue/device action.

## Scope and next exit

This bounded model starts from a known durable fixture and cuts after successful
modeled mutation/sync calls, returning EIO and freezing its durable graph.
It explores three namespace schedules, not all possible POSIX/filesystem
behaviors. Unsynced bytes are discarded; torn sectors, controller caches,
device lies, arbitrary partial writeback and concurrent filesystem adversaries
are outside the model. It does not confuse process death with power loss.

Local composed ordering now has the requested host evidence. Broad E2 remains
open for independent authenticated destination durability and target gates.
Next is E3 actual hard-state/suffix-truncation integration, then E1 actual lagger
transfer/install/activation. The existing F6 direction is approved; exact
continuation design still needs its all-call-site proof and any genuinely new
material contract choice reviewed before encoding. Broad E1/E2/E3 and F5/F6/F7
remain OPEN. Pi is deferred, not waived.

The existing actual-fsync hard-state diagnostic was rebuilt and rerun to pin
that next task: campaign4/4, vote4/4, commit2/2 sync calls remain on the protocol
owner, all three fail their intended assertion. See `next-hard-state-red.txt`.
They are excluded from the expected-green CTest inventory until implemented;
their failure is existing E3 work, not a regression introduced by this test.
