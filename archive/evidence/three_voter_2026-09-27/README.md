# Three-voter topology — 2026-09-27 (CP-059)

Owner decision (2026-09-27): move from the two-voter pilot to three voters,
nuc1 (voter 1), Pi (voter 2), nuc2 (voter 3); tests first, then implement.

## Defect found by the first RED

`V2Node::send_peers` stopped taking outbound frames for EVERY peer as soon as one
peer's offer was refused. With two voters that is harmless (there is one peer).
With three, a dead follower's first refused frame blocked heartbeats and appends
to the live follower, so a cluster with two of three voters alive committed
nothing. Aeron refuses offers to a peer only after its publication liveness
timeout, so on real hardware the stall starts a few seconds after the voter dies.

Fix: a refused offer holds back only that peer. `OutboundMailboxes::take` and
`ReplicatedKernel::take_outbound_frame` accept a list of held peers and return
the next frame for any other peer; a held peer's frames stay in their slots and
keep coalescing (latest-per-kind replacement), so memory stays bounded and no
frame is discarded. The one stashed envelope per peer is still retried first,
byte for byte, and is never replaced while held.

## Tests (RED first)

| Test | RED before the fix |
|---|---|
| `v2-node-tests` `three_voters_commit_without_an_unreachable_follower` | `timed out waiting for commit: one follower unreachable` |
| `v2-outbound-mailbox-tests` `held_peer_does_not_stall_others` | did not compile (no held-peer API) |
| `v2-node-process-tests` step 6 (three real processes, real Aeron, SIGKILL) | added after the fix; the full-revert mutant reproduces the stall (below) |

Also new: `three_voters_elect_a_new_leader_when_the_leader_is_lost`,
`three_voters_refuse_to_commit_without_a_quorum`, the strengthened stash-retry
assertion in `partition_and_backpressure_recover` (blocked for 600 ms of real
time, across several 200 ms library-default heartbeats), `v2-deploy-template-tests`
(three-host templates against a hand-written topology), and
`deploy/v2/observability/rule-tests/velocita-v2-alerts.test.yml` (promtool; moved out of
`rules/` on 2026-09-28, see M3 log finding 32).

Oracles: committed results from the independent reducer; convergence by equal
applied index and state digest across voters; templates against a hand-written
table from the deployment log; alert rules against hand-written series.

## Mutants (compiled directly from scratch copies; killing assertion named)

| Mutant | Killed by |
|---|---|
| M0 full revert: one blocked peer blocks all (node tests) | `timed out waiting for commit: one follower unreachable` |
| M0 in the node BINARY (process test, real Aeron) | `three voters: one follower dead: client did not commit (exit 3): TIMEOUT` |
| M2 retried stash not marked held | `a held envelope was replaced while its peer was blocked` |
| M4 held list not passed to the kernel | `the held envelope for a stopped voter was replaced` |
| M3 mailbox ignores the held list | `held frames lost` |
| M1 only the in-loop refusal returns | near-equivalent (one turn of delay; the next turn's retry marks the peer held); not counted |
| T1–T6 template mutants (wrong link secret, port, missing voter, caller endpoint, node id, ingress) | each by its intended assertion; T5 by the parser's own refusal |
| R1 at-risk threshold `< 2`; R2 at-risk ignores unreadable pages | promtool failure at 8 m and 12 m respectively |

The first process-test version SURVIVED M0: it committed inside Aeron's
liveness window, before any offer was refused. It now waits until the leader's
`peer_backpressure` rises by 20 before committing.

## Results

- Release (macOS arm64, cacheline 64): 111/111.
- ASan+UBSan (RelWithDebInfo): mailbox, node and process suites pass.
- The node still never compacts, so a returning voter catches up from retained
  log entries. Enabling compaction with three voters needs the online snapshot
  installer first (`tests/v2_snapshot_catchup_red.cpp` stays RED).
