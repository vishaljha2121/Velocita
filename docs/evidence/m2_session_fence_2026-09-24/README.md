# M2 step 2: session fence and session-epoch file (decision C)

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted.
`include/velocita/v2/session_fence.hpp`, `src/v2_session_fence.cpp`.

RED first: `tests/v2_session_fence_tests.cpp` against a stub; failed at
"fence decision differs at step 0" (`red.txt`). GREEN after implementation.

Oracles: hand-derived fence decision table; epoch-file goldens from
`tests/v2_session_fence_goldens.py` (Python struct + zlib CRC-32).

Unit coverage: stale epochs retired permanently; equal/lower sequences refused;
a new epoch may restart its sequence; refusals never mutate fence state;
sequencer never wraps; create refuses an existing file; advance matches the
goldens; every bit flip, torn and extended file refuses without rewriting;
exhausted counter refuses unchanged; leftover temporary never promoted;
symlinks never followed; private 0600 regular file.

Crash model: `tests/v2_session_epoch_persistence_model_tests.cpp` compiles the
production source with fsync/write/renameat redirected; only fsynced bytes and
directory entries survive. All 15 cuts pass: an epoch returned to a caller is
never reissued after a crash.

Eight mutants, all killed by the intended assertion (`mutants.txt`): five logic
mutants by the unit test; removing the file fsync, the post-rename directory
fsync, or the create directory fsync by the crash model ("epoch 2 reissued
after crash; highest returned was 4").

Model limits: it models fsync-ordered durability on one directory, not
filesystem-specific reordering or real power loss (E1-P4/P5 on hardware).
Release 101/101, ASan+UBSan 101/101 run alone (`release-tests.txt`,
`sanitized-tests.txt`).
