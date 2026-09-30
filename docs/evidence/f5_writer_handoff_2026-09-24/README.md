# CP-049: serial writer target handoff

2026-09-24, `v2/phase-5-corrections`, HEAD `d44a4a9`; uncommitted remediation.
This enables the already-approved E1/E3 installer. It is NOT online installation
or an E-exit acceptance record. Native borrowed-pointer control, no new durable
or transport bytes. Both target lifetimes remain the install owner's obligation.

## What is exercised

- Actual old file sync paused while target handoff is requested; new writes and
  maintenance refuse until acquired handoff completion. Full authored payloads
  reconstruct in the correct old/new files.
- Empty-pop race forced with the existing optional storage-checkpoint mechanism:
  worker observes empty queue, owner submits old block then switch, worker resumes.
  Rechecking submissions AFTER acquire preserves the old target for that block.
- Stale expected generation, generation rollback, reused successful sequence,
  duplicate completion, active maintenance exclusion and 64 real arena blocks.
  The saturation oracle compares every complete authored record, not only counts.
- The original real three-voter lagger still fails: compacted12/applied0/commit0,
  seven failed AppendEntries replies. `lagger-red.txt` is the intended failure.
  Initial invocations omitted the required fresh fixture path / supplied an
  already-existing directory; those usage refusals are not behavioral RED evidence.

## Evidence honesty

The implementation draft preceded its new tests. Subsequent direct mutation
failures are not retroactively labelled a chronological test-first cycle.
Removing only the post-acquire submission recheck causes malformed complete WAL
records in the forced interleaving (`submission-race-mutant.txt`). Test and actual
mutated WAL translation unit compiled directly; no CMake mutation result assumed.

An early completion-ring-guard mutant failed once and later passed. That result
was scheduling-sensitive and is withdrawn, not counted as guard proof. A
deterministic replacement now parks the worker at two consecutive idle passes:
the first admits the switch after publishing an old completion; the second proves
the switch was evaluated while that completion remained unconsumed. Removing only
the completion-ring guard fails the exact ownership assertion in
`completion-drain-mutant.txt`. Thus both queue guards have independently forced
interleaving evidence; the earlier unreliable result stays withdrawn.

Focused TSan instrumented the adversarial test and actual WAL translation unit;
support libraries were uninstrumented. `focused-tsan.txt` is not whole-replication
or whole-process TSan coverage. C++ ASan/UBSan regression uses nonrecovering UB,
halt-on-error, leak detection off; pinned Raft C is uninstrumented.

## Reproduction and remaining work

Release build: `/private/tmp/velocita-full-audit.lYB4P5/release`.
Sanitized build: `/private/tmp/aeron-phase67-audit-build`.
Offline Raft: `/private/tmp/velocita-cp043.SaGIxe/raft-build-suffix`.
Always rebuild `velocita-all-tests` before CTest. Run Release and sanitizer CTest
sequentially: existing older fixtures share fixed directory names.

Direct mutation artifacts: `/private/tmp/velocita-cp048.rDj2EZ/wal-switch-race-mutant/`.
The scratch prefix predates this checkpoint; it does not mean the test was part
of CP-048 acceptance. Final verification is in `verification.txt` when complete.

Next remains the actual online generation installer with the SAME result ledger,
verified nonserving candidate, old-job drain, selector uncertainty handling,
history bridge, result reconstruction and ACK only through snapshot S. Worker
switch completion grants none of those authorities. Authenticated process
transport, independent-host evidence and NUC/Pi gates remain outstanding.
