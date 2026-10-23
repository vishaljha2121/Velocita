# CP-044 — composed asynchronous persistence crash-image evidence

Host-only test work on `v2/phase-5-corrections`, HEAD `d44a4a9`, with the
pre-existing uncommitted remediation preserved. No production behavior or
canonical byte contract changes in this checkpoint.

## Question and independent oracle

Does the actual worker-backed reducer preserve its promises when a higher-term
AppendEntries replaces an uncommitted suffix, then commits the replacement?
The fixture authors the old and replacement command bytes independently. Entry1
is committed before the model starts; entries2–3 are initially uncommitted.
The durable image is examined BEFORE recovery can re-sync files or directories.
Losing a committed prefix is never excused by a subsequent startup refusal.

The same filesystem model now observes both bound metadata and WAL operations.
This is necessary because a directory sync can persist earlier renames from
either component. The existing compaction model observed archive/snapshot/WAL,
not this combined hard-state transaction. Three name schedules are retained:
explicit directory sync, eager names, and independently eager WAL replacement.
They are selected model behaviors, not an exhaustive filesystem model.

A separate ballot-only scenario must expose missing metadata directory durability
without a later WAL directory sync accidentally repairing it. Baselines must
publish actual protocol promises; looping without progress is not success.

Each of the three schedules reaches20 composed cuts and5 ballot cuts:75 total,
plus three zero-event committed-seed recoveries and six successful baselines.
The exact metadata tuples are M0=(term2,vote-1,commit1), M1=(3,-1,1),
M2=(3,-1,3). Old WAL permits M0/M1; truncated WAL requires M1; replacement WAL
permits M1/M2. Applied/results require M2. ACK3 requires the exact replacement
WAL, but does not by itself mean commit3. The independent ballot trace requires
(7,2,0) whenever the vote grant is observable.

Active metadata must decode to an authored tuple even if a temporary file is
invalid. A separately inspected invalid temporary frame requires the specific
metadata startup refusal; otherwise recovery must succeed. Recovery/refusal
must preserve exact WAL records, and successful recovery applies the exact
checkpoint. Arbitrary startup exceptions are not counted as safe refusal.

## Review corrections, not production REDs

- The first draft asserted a committed prefix without committing it. The fixture
  now establishes and checks the durable checkpoint and applied state first.
- The first draft used the startup metadata loader to inspect the active tuple.
  That loader also checks temporary-file lineage; an unfinished temporary file
  can cause a legitimate refusal while the active metadata remains valid.
  Inspect active bytes separately from restart admissibility.
- Compiler/import/main-wrapper and portable temporary-path corrections are
  harness development, not evidence of a production defect.
- The direct decoder initially looked at the owner root instead of the selected
  runtime-generation directory. Main review corrected the path in both traces;
  that failure likewise was not a production ordering defect.
- Independent review tightened permissive metadata inequalities to exact tuples,
  covered the truncated image's higher-term prerequisite, and removed broad
  exception-as-safe-recovery acceptance. Main added zero-event seed recovery.

## Negative controls and regression

Direct-source mutants, linked before the unchanged production archives:

- Remove only metadata store directory fsync: exit1 at ballot baseline,
  `granted ballot lacks exact durable term/vote tuple`.
- Remove only WAL replacement-file fsync/fdatasync: exit1 at explicit-name
  composed cut11, `crash image lost or changed committed prefix 1`.

These are permanent-test sensitivity checks, not claimed pre-existing product
REDs. Normal production passes all75 cuts. The model observes actual metadata
and WAL translation units through test-only syscall interposition; neither
production code nor the pinned Raft fork changed in this checkpoint.

Release91/91 passed after rebuilding; independent gate inventory matches exactly
91host/88V2. Tool regressions24+16 passed. See `verification.txt` for final
sanitizer results, commands and limitations. Existing compaction tests remain
registered; the only change to their source is a fixture-only main guard and
an unused-function annotation for reuse by the new suite.

## Scope limits

This is a deterministic host durable-image model with cuts after intercepted
filesystem events. It does not enumerate CPU interleavings, torn sectors,
arbitrary device writeback, physical power loss, or hardware durability. The
existing paused-worker and completion-corruption suites cover different
boundaries. No live generation installer exists yet, so delayed writes across
E1 activation remain unproven. E1/E2/E3 and F5/F6/F7 acceptance remain open.

No NUC/ARM64/Pi, latency, RSS, passive-model corpus, or live trading claim is
made here. The reused model freezes after successful intercepted events and
returns EIO; operation failures must coincide with that exact reached cut.
Counts are not independent physical-device guarantees. No claim is made that
every possible crash image or every protocol/thread interleaving was explored.
