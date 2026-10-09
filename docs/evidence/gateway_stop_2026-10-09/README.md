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

## What is not yet explained

Why the gateway asked. It queries the venue only for an order its copy of the state shows as open.
By its own ordering it had already seen the fill committed and had read the state again. Reading
the code did not find how its copy could still show order 95 open, and 720 clean round trips in the
in-process harness produced no such query. This happened once in 153 orders across two
incarnations, on hosts where a commit takes about 140 ms.

## What was changed (not deployed)

1. **Kernel.** A reconciliation report for a finished order that agrees with the kernel is answered
   `already_applied`, not refused. "Agrees" is narrow: for a filled order, the venue knows the order
   and reports exactly the booked quantity; for a rejected or cancelled order, the venue has no
   record of it and nothing was ever filled. Anything else is refused as before, and the gateway
   still stops on it.
2. **Gateway.** The stop line for a refused report now carries the kernel's code, the kind of
   report, the order and the log index, so the next one takes minutes to read, not a log replay.

Both are in the schema-11 line and need the next incarnation (kernel) and gateway image.

## What to do now

- Restarting the gateway on nuc1 clears the latch. It starts, compares the venue's book with the
  kernel's (they match), arms, and the waiting exit goes out.
- Until the kernel fix is deployed the same thing can happen again. It costs a stop, not money.

## Reproducing the analysis

    c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -I<f55209e>/include replay_log.cpp \
        <f55209e>/src/{v2_local_generation,v2_authority_barrier,v2_static_identity,v2_artifact_digest,v2_digest,v2_execution_catalog,v2_wire,v2_payload,v2_kernel,v2_kernel_codec,v2_action_resolver,v2_rule_evaluator,v2_state_snapshot}.cpp -o replay_log
    ./replay_log v2-execution-catalog.bin 14461687259106969866 <archive segments in publication order>

The archive segments and the catalog file are on each voter under
`data/node-N/kernel/v2-archive/`. They were copied read-only and are not stored here.
