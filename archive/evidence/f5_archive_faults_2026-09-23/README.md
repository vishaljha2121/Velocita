# CP-040: archive and compaction persistence fault evidence

2026-09-23; branch `v2/phase-5-corrections`, HEAD `d44a4a9`, dirty worktree.
This increment validates CP-039's E2 local laboratory compaction. It changes
tests/build inventory/documentation, not production behavior or canonical bytes.
No new production defect was established; the defects corrected here were in
test reachability, retry injection and recovery oracles.

## What is exercised and why

| Suite | Actual coverage | Independent observable outcome |
|---|---|---|
| `v2-archive-publication-fault-tests` | 139 reached EIO sites across fresh dependency creation, first publication, replacement, and three selected second-object failures; three successful retry cases | No success/capability on essential EIO; source bytes unchanged; restart either refuses without altering evidence or exposes exact complete history |
| `v2-archive-persistence-model-tests` | 124 durable-image cuts in the actual archive implementation: fresh33 and replacement29 under two namespace schedules | Accepted history matches authored command bytes and tuples; successful publication must survive modeled loss of unsynced data |
| `v2-compaction-io-fault-tests` | Nine worker-thread snapshot/WAL syscall failures in actual two-voter laboratory compaction | Service latches closed; restart preserves committed state, archived tuples1..3 and durable uncommitted suffix4 |

The syscall suite traces each successful small scenario first, then injects EIO
at every essential reached ordinal. The trace identifies injection locations; it
does NOT supply the expected history. One absent optional catalog open is
nonessential because production independently verifies absence with `fstatat`.
Second-object write, file sync and no-replace failures must leave the first
immutable segment present. Multi-object coverage is targeted, not every ordinal.

One-shot EINTR on write/fsync must subsequently succeed, and a real short write
must finish with a durable capability. Checked-close injection actually closes
the descriptor before reporting failure: retrying close can affect a reused
descriptor, and merely returning EIO without closing would leak in the harness.
The fault hooks use a fixed trace and return errno, not exceptions through
production noexcept APIs.

Recovery is tested with instrumentation disarmed. Verification must not modify
names or bytes. When accepted, every archive record's index, term, entry identity,
entry type and payload are compared against the intended input; sorted indices
must be exactly1..cut. Old/new complete prefixes are allowed after uncertain
publication; refusal is also allowed. Success requires the new complete prefix.
This is a safety oracle, not a claim that all salvageable images remain available.

## Durable-image model, not process-kill theatre

The model compiles the actual archive-store source through syscall substitutions.
File synchronization persists inode bytes; directory synchronization persists
names independently. It explores explicit-sync-only names and eager namespace
persistence, without implicitly persisting unsynced file bytes. At each selected
event it freezes the durable graph, refuses further modeled mutations, materializes
that graph into a different directory and invokes production recovery.

This matters because killing a process does not discard the operating system's
page cache. A successful restart after process death alone cannot prove fsync
ordering. Conversely, this bounded model is not every allowed filesystem
schedule, torn sector, controller failure or device power-loss experiment.
It covers the archive transaction, NOT the complete archive/snapshot/WAL durable
image. Successful baselines must recover; cuts may recover or refuse.

Two direct-source mutants independently omit regular-file syncs or directory
syncs. Both fail at the intended assertion:
`successful publication had no restartable durable evidence`.
See `sync-mutations.txt`: compile errors are explicitly excluded, and these are
sync-class mutations, not an individual mutation check of every call site.

## Integrated compaction trace

The fixture commits kill-epoch commands1..3 on two laboratory voters, stops peer
routing, then persists uncommitted command4 locally. Its pre-fault WAL payloads
are checked against independently encoded intended commands before becoming the
history-preservation oracle. A nonempty suffix ensures WAL rewrite actually runs;
a fully compacted empty tail would miss that path.

After archive verification the test injects snapshot write, file sync, checked
close, rename or directory sync failure. At the WAL rewrite checkpoint it injects
pwrite, file sync, rename or directory sync failure. It asserts the actual worker
armed and fired each case, withholds leadership/results after the failure,
restarts, verifies kill_epoch3/applied3, and checks exact archived records and
suffix4. Recovery leaves the fault image unchanged.

CMake compiles the real snapshot/WAL implementation into test-only hooked object
files. There is no global libc interposition or production callback alteration.
The same object sources receive sanitizer flags. Linux and macOS use their
respective production fdatasync/fsync routes; Linux behavior has NOT been run in
this increment. A ten-second monotonic deadline bounds the harness wait, not
production latency. Existing CP-039 parked-worker tests cover the pending fence.

## Corrections to earlier test claims

- `store-syscall-sweep.log` and the prior README are superseded. The early suite
  missed ordinals, did not prove fresh-creation recovery, treated a second error
  as a retry test, and hit first-object work instead of the intended second object.
  The final suite above replaces those claims and verifies exact recovered bytes.
- `harness-reachability-timeout.txt` is NOT a product RED. A fixed number of
  scheduler yields and incomplete syscall hooking failed to reach the target.
  Immediate pending-state assertion, a real deadline and actual instrumented
  source objects correct the harness; no Raft patch was justified.
- `integrated-io-fault-green.txt` is an intermediate direct build, retained for
  history. Final CMake verification supersedes its build recipe.
- Two bounded Sol workstreams were requested. One worker nevertheless spawned a
  nested reviewer; this violated the delegation limit, was stopped and disclosed.
  Main took ownership of the final store harness and verified all integrated work.

## Verification

Final command lines/output are retained in `release-verified.txt`,
`sanitized-verified.txt`, `focused-verified.txt`, `sync-mutations.txt` and
`tooling-and-inventory.txt`. Builds must finish before CTest.

- Host Release:85/85,15.60s.
- Host C++ Debug ASan+UBSan:85/85,228.88s.
- New focused suites:3/3,1.63s.
- Tooling Python:40/40. Independent inventories:85 host/82 V2, no skipped suites.
- Both sync-class mutants: exit1 at the intended assertion.
- Partial file fingerprints: `focused-source.sha256`; NOT a complete gate pin.

ASan/UBSan uses nonrecovering UB and halt_on_error; leak detection is off. Linked
Raft C is uninstrumented. No new TSan, NUC/ARM/Pi, memory-peak, latency, Python
model-corpus or physical power-loss result is claimed. Protected paths untouched;
no stage, commit, tag, deployment, venue or device interaction.

## Remaining exits

E2 still needs the composed archive/snapshot/WAL durable-image sweep and live
independent-copy evidence before two-host reclamation. The nine syscall cases
do not replace that composed proof. Archive namespace/byte faults now have the
stronger local evidence above, not merely four high-level throw checkpoints.

E3 real hard-state/suffix-truncation paths remain synchronous. E1 actual lagger
transfer/install/activation is still absent. Broad E1/E2/E3 and F5/F6/F7 stay OPEN;
this record is not phase acceptance. Pi is deferred, not waived.
