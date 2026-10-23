# Native qualification and Pi investigation — 2026-10-15

Later result: **`ba299ff` qualified on both architectures**. See [final qualification](ba299ff/RESULTS.md).
The failed `b490d68` attempt and investigation below remain historical evidence.

Checkpoint: `b490d6843b53d114a49678304089541bfe192c7a`.
Status: NUC1 qualified; Pi build passed but qualification failed. Not installed or activated.

| Host | Result | Verified artifacts |
|---|---|---|
| NUC1 / x86_64 | 59 CTest targets; 407 Python/CLI tests; eight process cases; nine continuous cases / 54 synthetic settlements passed | 154 |
| Pi / aarch64 | Build 670/670 passed; first continuous case failed after three settlements, before signing effect four. Runner stopped immediately. | 19 |

The NUC runner's `focused_python_tests: 104` is a stale hardcoded field. The sum of actual
`Ran N tests` log records is 407. Original machine evidence is preserved unchanged.
Pi reported `failed/state`, create trace 6, last attempt offered 3. No OOM/capacity guard fired.
Its recorded events show create 4 but no signature/execute 4. The exact failed Raft operation
was not logged, so a failed read versus failed command cannot be established from that run.

## Bounded diagnostics

All diagnostic containers used the pinned aarch64 image, network disabled, one CPU pinned to
core 3, 2 GiB RAM, no production credentials or live service changes. The source/build cache
was mounted read-only. Diagnostic source adds failure-site/link-outcome logging only.

1. Wrong diagnostic UID (1001 instead of Pi's 1004): log creation refused; no test executed.
2. Correct UID: diagnostic objects compiled, linker refused writing its dependency file into
   the read-only build tree; no test executed.
3. Link continued using those objects with dependency output removed: one baseline case passed,
   six settlements. This is diagnostic evidence, not qualification.
4. A fixed three-case diagnostic set, stop on first failure, passed all cases including crashes
   and delayed settlement: 18 settlements. No failed operation reproduced. No further diagnostic
   repetitions are scheduled. All 18 allowlisted diagnostic artifacts were hash-verified.

These passes do not erase the original failure. Root cause remains unproven.

## Identified test configuration mismatch

The process drill configures a 5000 ms timeout, 1 ms polling and `read_outage=0`.
The committed production main defaults to 3000 ms, 250 ms and `read_outage=120000`.
Production already handles transient ordinary read failures; failed commands still stop.
A test-only correction is locally tested (uncommitted) using those production defaults and deliberately dropping
ordinary state reads for 3500 ms after create 4. The regression failed with read recovery
disabled, passed with recovery enabled, and retained six unique signatures/sends/settlements.
This addresses a verified coverage mismatch; it does not prove the original Pi failure was a read.
No production C++ behavior is changed by this test adjustment. New checkpoint/qualification
is required before using changed test evidence for deployment.

## Production readback

At 22:06:42 UTC, the driver is inactive; gateway and signer active with manual-start guards.
Producer index 14: 25000000 quote atoms, zero base/effects/pending/fees/P&L, empty settlements.
This is authenticated-producer telemetry, not a fresh independent TM wallet read.
Incarnation and all journals/funding/sequences are preserved. Nothing was deployed or traded.
Entry drain remains October 16 13:41:34 UTC; review expiry 13:48:34 UTC.

## Test correction completed locally; checkpoint pending

[Correction evidence](outage-correction/summary.json): three scenarios passed, 18 settlements;
fresh-compiled baseline passed and both fresh-compiled mutants failed at the intended assertions.
Disabling read recovery fails before signature four. Removing fault injection fails its coverage
assertion instead of silently passing. The initial mutation run encountered an overlong Unix-socket
path and was **not** counted as a kill. The runner now checks the portable path bound before
compiling; successful mutations used a new short durable path. An initial test compilation error
from a missing declaration was corrected before RED; it was not counted as a test failure.

All 277 committed production source/header files match the isolated local build source exactly.
Only the drill and its mutation runner change. Existing fake crash-recovery, caller21 authorization,
real-format verification, economic oracle, and exactly-once assertions remain in place. The new
case models a recoverable read outage; it does not retry failed commands or relax signer checks.
The known missing-field-initializer warning in the reused test fixture remains recorded.

Proposed scoped checkpoint:
- `tests/v2_swap_continuous_drill.cpp`
- `tests/mutation_swap_continuous_outage.py`
- this strategy-grant README/native evidence and only the new tracker sections

After approval, commit those paths/hunks, bundle `54a5fcb..HEAD`, and run the existing committed
native runner once on each architecture using the preserved build caches. Stop on failure;
retain the original failed Pi evidence. No production installation, operator grant, driver restart
or money movement is part of this checkpoint/qualification. A qualified coordinated deployment
remains a later step. The original failure is not retroactively classified as a proven read outage.
