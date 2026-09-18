# Round-trip swap cost on Solana, measured 2026-09-18

## What was measured, and what it is not

USDC -> token -> USDC, quoted through Jupiter's public quote endpoint. Jupiter
SIMULATES a route and reports what you would receive; no transaction was built,
signed or sent, and no True Markets account was involved.

THESE ARE A LOWER BOUND ON THE TRUE MARKETS COST, not the cost. True Markets
routes through its own aggregator and may add a markup, which sits on top of
everything below. ADR 0011 section 7 shows the markup dominates the result
entirely - a factor of 200 between a 0 bps and a 100 bps markup - so nothing
here should be quoted as a True Markets figure.

`c` below is the round-trip cost fraction from ADR 0011:

    max_volume = loss_budget x 2 / c

## Why it decides the design

Cost per unit volume spans three orders of magnitude across assets, and impact
grows superlinearly with size. So asset selection dominates, and many small
round trips beat few large ones. Both conclusions are visible in the raw table.

Slippage tolerance on every quote was 50 bps; `restrictIntermediateTokens` was
set, which is the safer routing mode.

## Raw results

```
 asset     size   cost bps     cost $  buy imp%  sell imp%   hops
   SOL       25        0.4      0.001     0.000      0.002 2/   1
   SOL      100        0.3      0.003     0.004      0.001 1/   1
   SOL      500        0.7      0.033     0.000      0.007 2/   2
   SOL     2000        0.1      0.020     0.000      0.001 1/   2
   JUP       25        1.6      0.004     0.031      0.000 2/   2
   JUP      100        1.5      0.015     0.033      0.000 2/   3
   JUP      500        5.1      0.257     0.058      0.000 3/   3
   JUP     2000       10.6      2.122     0.109      0.000 5/   4
   JTO       25       48.4      0.121     0.538      0.000 3/   1
   JTO      100       46.5      0.465     0.439      0.026 1/   1
   JTO      500       46.5      2.325     0.443      0.022 1/   1
   JTO     2000       71.6     14.315     0.576      0.140 5/   5
 DRIFT       25       71.5      0.179     0.625      0.090 3/   3
 DRIFT      100      112.3      1.123     0.822      0.303 3/   2
 DRIFT      500      316.4     15.820     1.860      1.329 3/   2
 DRIFT     2000      771.8    154.351     4.158      3.714 5/   6
     W       25       58.2      0.146     0.103      0.480 1/   3
     W      100       82.2      0.822     0.242      0.582 1/   2
     W      500      197.7      9.883     0.855      1.131 3/   2
     W     2000      356.5     71.305     1.654      1.943 4/   4
  HYPE       25   ERROR 400
  HYPE      100   ERROR 400
  HYPE      500   ERROR 400
  HYPE     2000   ERROR 400
```

The HYPE rows errored because the mint address used was reconstructed from a
truncated note rather than read from the catalog. That is an error in the probe,
not a finding about the asset; D-21 is the row that governs which HYPE is meant,
and the address must come from the REST catalog (D-11) before HYPE is measured.

## Reproduce

    python3 tools/roundtrip-cost.py 25 100 500 2000

## Volume implied, at the measured lower bound

For SOL at c = 0.5 bps, a loss budget buys 2/c = 40,000x its own size in volume.
That figure is the OPTIMISTIC END of ADR 0011's table and assumes zero venue
markup, which has not been measured. Do not plan against it until `get_quote`
has been compared to a Jupiter route for the same notional.
