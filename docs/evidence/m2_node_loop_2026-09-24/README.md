# M2 step 4: V2Node owner loop over a fake transport

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted.
`include/velocita/v2/node.hpp`, `src/v2_node.cpp`, `tests/v2_node_tests.cpp`.

## What the node does

One owner thread drives one `ReplicatedKernel`, with the WAL worker started at
construction (the first production use of worker mode by default). Each turn is
bounded and runs in order: peer ingress (authenticate → session fence → kernel),
durability pump, Raft tick, peer egress (stashed envelope first), caller ingress
(authenticate → fence → decode → CallerId check → propose), committed results.
Any kernel or transport fault latches the node failed (fail-stop).

## RED first, then two real defects found by the tests

The RED was the stub timing out waiting for a leader (`red.txt`).

1. **Dropping busy frames broke elections.** The first implementation dropped a
   peer frame whenever the kernel reported `busy` (hard state in flight). In
   worker mode that is exactly when vote requests arrive: nodes split-voted
   forever (term 12 after 3,500 rounds). Fix: hold one authenticated body per
   peer and retry it before reading more; stop reading while it is held.
2. **Harness time.** Feeding 10 ms of simulated time per round outran real
   fsyncs. The harness now feeds real elapsed time, as the binary will.

## Coverage and evidence honesty

Scenarios: not-leader redirect naming the actual leader; admitted plus
committed results matching the independent reducer from every voter; forged
frame at term 1000 with no effect; replayed authentic frame refused by the
fence; caller impersonation refused before the log; a restarted node's
old-session frame retired; partition then heal; backpressure redelivering the
exact stashed bytes; a deterministically parked WAL worker (metadata checkpoint
seam) holding an authentic frame and delivering it after release.

Conservation law checked at the end of every scenario: every fenced frame is
accepted, decoder-refused or held; none vanish.

Five mutants, all killed by the intended assertion (`mutants.txt`). Two needed
test strengthening first, in the same session:
- `busy_frames_dropped` SURVIVED the first version: with real time, elections
  eventually succeed despite drops. The parked-worker scenario and the
  conservation law now kill it.
- `stash_dropped` would have survived "backpressure recovers" (Raft retransmits);
  the test instead asserts that the first frame offered after unblocking is the
  refused envelope, byte for byte.

A mutant run hung once: a failing assertion unwound while a worker was parked,
and the node destructor joined it. The harness now releases every gate and
destroys nodes before their gates. That was a test-harness defect, not a
production one.

Release 104/104; ASan+UBSan 104/104 run alone (`release-tests.txt`, `sanitized-tests.txt`).

## Not covered yet

The `not_ready` refusal path is not exercised (it needs a pending history
recovery). Aeron transport, the binary, the metrics page, real processes and
network come in step 5. The node-test timing is real-time, so it takes ~10 s;
it passed three consecutive runs but has no long soak yet.
