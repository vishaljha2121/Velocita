# The paper gateway stopped itself while holding a position, 2026-10-09 13:02 UTC

Incarnation `abd1e149a26d4062ba5bbba2d90b3305`, images `ledger-f55209e`. Nothing had been deployed
since the cutover on 2026-10-08. Found at 15:12 UTC during a routine read-only check.

## What happened

At 13:02:03 UTC the gateway printed `STOP new_risk cause=refused_evidence rearmable=0` and has sent
no order since. The account holds 12 lots (0.12 SOL) bought at $110.59. The strategy has since
authorized an exit; it cannot be sent. Kernel and venue agree on 12 lots, the cluster is healthy,
and the kernel loss limit is far away (window loss $2.53 of $10).

State at the stop: 95 orders (48 buys, 47 sells), $1,253.80 of volume, fees $2.51, cash $9.20.

## Cause, from the committed log

The gateway does not print which report was refused or why, so the voter's archived log was copied
from nuc1 and replayed from the first entry through the kernel source of the deployed commit
(`replay_log.cpp` here; 8,107 commands). Every gateway report in the incarnation was applied except
one:

| Log entry | Sender | Command | Kernel's answer |
|---|---|---|---|
| 6405 | strategy | authorize | `stale_risk_state` (its position version was one behind; normal after a fill) |
| 6408 | strategy | authorize | applied: order 95, buy 12 lots at 11059 |
| 6409 | gateway | order acknowledged, order 95 | applied: live |
| 6410 | gateway | fill, order 95, 12 lots, fee 26,542 | applied: filled, position 12 |
| 6412 | gateway | acknowledge results through 20284 | applied |
| 6413 | gateway | **order reconciled, order 95, venue order 95, 12 lots** | **`venue_observation_not_applicable`** |

So about a second after order 95 had been acknowledged and filled, the gateway asked the venue about
the same order, was told "known, 12 lots filled", and reported that as a reconciliation. The kernel
refuses any order event for a finished order, and the gateway treats any refused report as a reason
to stop until a person restarts it.

The report was true and agreed with what the kernel had booked. Nothing was wrong with the order,
the fill or the money.

## Why the gateway asked (found later on 2026-10-09)

The gateway's copy of the state was one log entry old: it showed order 95 acknowledged and not yet
filled, although the gateway had already been told the fill was committed. Three facts combine.

1. **Every voter answers a committed command, not only the leader.** Each voter sends the caller
   the result when it applies the entry, and the gateway takes the first answer. (Each of the three
   voters had sent about 17,370 results in this incarnation.)
2. **An entry can commit before the leader has it on disk.** Two followers that have written it are
   a quorum. The leader then tells the followers the new commit point and they apply the entry, but
   the leader itself waits for its own disk before it applies. The leader at 13:02 UTC was the Pi,
   the voter with the slowest disk; the gateway runs beside the voter on nuc1.
3. **The leader served state reads while in that condition.** It refused them while a hard-state
   write was in flight, but a log block in flight did not count.

So nuc1 answered "fill committed" at once, the gateway asked the Pi for the state, and the Pi
answered from before the fill. The gateway saw a resting order with no confirmation, asked the
venue about it, and reported the answer. By then the Pi had applied the fill, and the kernel
refused the report.

The strategy's next command (entry 6411) arrived right behind the fill. That is what carried the
new commit point to the followers a few milliseconds after the fill committed; without it they
would have learned it at the next heartbeat, by which time the Pi's disk had usually caught up.
That is why it happened once in 153 orders.

**Reproduced.** `a_leader_with_a_slow_disk_never_makes_the_gateway_question_a_filled_order` in
`tests/v2_gateway_node_tests.cpp` runs three voters with the leader's block writes delayed by
150 ms. Before the fix the gateway asked the venue about 3 of 6 filled orders. The earlier
720 clean round trips did not show it because that soak uses two voters, where the leader's own
disk is always part of the quorum.

## What was changed (not deployed)

1. **Kernel.** A reconciliation report for a finished order that agrees with the kernel is answered
   `already_applied`, not refused. "Agrees" is narrow: for a filled order, the venue knows the order
   and reports exactly the booked quantity; for a rejected or cancelled order, the venue has no
   record of it and nothing was ever filled. Anything else is refused as before, and the gateway
   still stops on it.
2. **Gateway diagnostics.** The stop line for a refused report now carries the kernel's code, the
   kind of report, the order and the log index.
3. **Voter, the cause.** A leader that has committed entries it has not applied answers a gateway
   state read with "busy, try again"; the gateway already retries that answer.
4. **Gateway, second guard.** A state read older than a result the gateway has already seen
   committed is discarded. It counts as a fence: new risk stops, the read is retried at the state
   interval, and the gateway re-arms by itself after the quiet period once reads are current. This
   also covers a deposed leader that still answers. Counter:
   `velocita_gateway_stale_state_reads_total`.

1 is in the schema-11 kernel; 2 and 4 are in the gateway image; 3 is in the voter binary and needs
no schema change. All of them wait for the next incarnation.

## What to do now

- Restarting the gateway on nuc1 clears the latch. It starts, compares the venue's book with the
  kernel's (they match), arms, and the waiting exit goes out.
- Until these changes are deployed the same thing can happen again whenever the Pi leads. It costs
  a stop, not money.

## Reproducing the analysis

    c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -I<f55209e>/include replay_log.cpp \
        <f55209e>/src/{v2_local_generation,v2_authority_barrier,v2_static_identity,v2_artifact_digest,v2_digest,v2_execution_catalog,v2_wire,v2_payload,v2_kernel,v2_kernel_codec,v2_action_resolver,v2_rule_evaluator,v2_state_snapshot}.cpp -o replay_log
    ./replay_log v2-execution-catalog.bin 14461687259106969866 <archive segments in publication order>

The archive segments and the catalog file are on each voter under
`data/node-N/kernel/v2-archive/`. They were copied read-only and are not stored here.
