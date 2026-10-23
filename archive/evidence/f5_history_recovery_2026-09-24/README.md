# CP-046: selected-generation history recovery

2026-09-24. Branch `v2/phase-5-corrections`, HEAD `d44a4a9`, remediation
uncommitted. Owner explicitly approved the CP-045 history bridge, candidate budget
and recovery approach. This is host implementation evidence for the recovery
side of E1, not online snapshot installation or deployment acceptance.

## RED and smallest GREEN boundary

`recovery-red.txt`: freshly compiled cold fixture, snapshot8193 and full bound
WAL8193, refused by the former deployment quota guard. The intended failure was
`deployment quota cannot cover accepted archive obligations`, not malformed bytes.
The permanent equivalent now lives in `tests/v2_history_recovery_tests.cpp` and
the updated `v2_install_history_bridge_contract_tests.cpp`.

The implementation retains the live8192-entry bound but accounts snapshot-covered
history separately. Cold startup validates a bounded whole WAL, recovers state
and live suffix, then stays quarantined. One serial-worker job validates and
preflights the missing archive range before publishing it in bounded segments.
Worker completion is acquired and checked against generation, sequence, cut,
coverage/term and unchanged owner coordinates before service resumes.

The 1GiB maximum is a candidate/recovery-WAL file cap including suffix and
preallocation, not a whole-deployment disk allowance. Existing archive quotas
include exact planned history plus worst-case pending suffix reservation. Old
generations, archive files and independent backup obligations remain separate.
This path never authorizes deletion or raises live capacities.

## Permanent behaviors and independent oracles

- 8193 valid snapshot-covered records: decode catalog and every archive segment;
  compare each original index, term, ID, type and full payload, not digest alone.
  Preserve exact WAL bytes, independently authored state, then commit a new
  command and reconstruct its state after destroying/restarting the node.
- Pause worker before publication: a large election tick must not change term,
  role, leader, index, commit, applied state or digest. Refuse proposal and
  higher-term peer mutation; withhold output and authority.
- Pause after durable catalog publication but before completion: catalog presence
  alone must not release owner quarantine. This is a pause, not a crash-image test.
- Worker checkpoint exception: latch closed without rewriting WAL; destruction
  then recovery retries successfully. This is NOT injected syscall EIO.
- Small WAL cap refuses before archive publication; archive object quota1 refuses
  the full8193-record plan with no partial catalog or segment publication.
- Partial-progress restart image: archive8192, snapshot8193, full retained WAL;
  validate old overlap but publish only missing record8193. This is a constructed
  restart image, not a live generation switch or power-cut sweep.
- Existing result-contract fixture S2/C4/D5 pumps recovery, then async hard-state
  completion; exact originally authored result5 appears once. Committed cold
  history emits no invented process-local notifications.
- Real acquired recovery completion: independently corrupt generation, sequence,
  cut, covered-through or error. Each latches storage failure without releasing
  quarantine/output or changing cold state. Valid acceptance releases the fence;
  a duplicate does nothing.

`history-overlap-red.txt` preserves a genuine draft defect: preflight counted
already-archived rows as new work and returned conflict (enum10). Fixed by charging
only `(A,S]` while still validating all retained WAL. This was not a stale-generation
error and was not an existing accepted behavior.

## Guard mutations

Scratch source copies only; production was not edited for mutants. Build current
dependencies, compile changed translation unit directly and link before its static
library (cacheline64). Completion suite includes its scratch translation unit.

1. Remove history pending from the common persistence gate. The initial suite
   caught only the pending-status assertion; a paused-only run detected the change
   too late at completion. We strengthened the permanent tick-state oracle. Final
   paused-only mutant now dies exactly at `paused history recovery tick mutated
   fenced owner state`; baseline passes. Both weaker attempts are preserved.
2. Remove full-plan quota preflight. Mutant dies at `archive quota preflight
   published a partial catalog`, proving the externally observable side effect,
   not merely another later refusal.
3. Remove the history completion sequence equality. A scratch diagnostic identifies
   the sequence case; mutant dies at `corrupted history completion did not latch
   storage failure`. Generation case passed its expected refusal first.

## Verification limits and corrections

Final commands/results: `verification.txt`; exact independent gate-name inventory:
`inventory.txt`. Release and C++ Debug ASan+UBSan are host-only. Raft C remains
uninstrumented, leak detection off, UB nonrecovering/halt. No new TSan, physical
device, passive Python model, storage-performance or RSS measurement this turn.

The first parallel Release/sanitizer run was invalid as joint evidence: the older
replication suite shares a fixed temporary path and one run failed deleting the
other's directory. Subsequent runs are sequential. Another failure was the old
result fixture expecting immediate service/commit; it now pumps both required
completions without weakening its original exact-outcome oracle. Failed logs stay.

Native host sizes: history job8920, ordinary persistence job168, replica124440,
retry buffer32768 bytes. These exclude heap payload/descriptors, encoder, archive
verification/catalog scratch, Raft and thread stacks. One segment's input payload
is bounded; total process memory is NOT4MiB. WAL scans are streaming, but each
archive publication reverifies growing prefixes. Total I/O is not proven linear;
the prior review claim was corrected. Cold constructor verification still blocks
startup; worker pumping avoids owner-thread archive I/O but sacrifices voter
availability. Two voters cannot form quorum while one is recovering.

`lagger-red.txt` is freshly built and still fails: compacted12, lagger applied0,
seven failed append replies. Candidate building, selector activation, same live
ledger preservation and fencing actual stale writes across that switch remain
the next implementation. No E1/E2/E3 or F5/F6/F7 closure follows from this checkpoint.
Independent-copy authority, authenticated transport and target re-gates remain
required. Pi is deferred, not waived. No protected paths, tags or external systems
were changed; no staging or commit performed.

Scratch retained at `/private/tmp/velocita-cp046.MP67gE`, including directly built
RED/mutant sources and binaries. Raw logs below are copied verbatim, not rewritten
to hide intermediate failures.
