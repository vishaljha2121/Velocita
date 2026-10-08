# Paper results: stochastic one-second strategy, incarnation c8a6a0a7

Written 2026-10-08 after the edge was stopped with the account flat, and before the paper account
was reset for the next incarnation. Every number is from the paper venue's report and the edge's
ledger as they stood at the stop; the raw records are in `final-paper-session.json` beside this
file, and the retired files stay on nuc1 under
`~/velocita-indicator-paper/retired/stochastic-c8a6a0a7-20261008`.

**This is a simulated result.** Fills are fill-or-kill at the Binance SOLUSDT one-second close with
a 20 bp fee per side. It is not evidence of what True Markets would fill or charge.

## Summary

| | |
|---|---|
| Strategy | `stochastic_1s_cash_v1`, long-only SOL, 0.12 SOL per order |
| First fill to last fill | 2026-10-07 20:14:03 to 2026-10-08 13:25:38 UTC (17.2 hours) |
| Orders filled | 58 (29 round trips), 0 rejected, 0 unknown outcomes |
| Funding | $25.000000 |
| Final cash (flat) | $23.395609 |
| Net result | $-1.604391 |
| Fees paid at 20 bp | $1.607991 |
| Result before fees | $+0.003600 |
| Volume (buys plus sells) | $803.98 |
| Volume per $1 lost | $501 |
| Round trips that made money before fees | 9 of 29 |
| Round trips that made money after fees | 0 of 29 |
| Best and worst round trip before fees | $+0.0552 and $-0.0564 |
| Holding time | median 9.7 min, shortest 0.4, longest 36.7 |
| SOL price range seen | $111.75 to $116.81 |
| Largest drawdown from $25 | $1.6068 |

## What it shows

- **The trading itself was flat.** Before fees the 29 round trips netted $+0.0036. The
  whole loss is the fee: about $0.055 per round trip.
- **At the measured True Markets cost the same fills lose far more.** Re-priced at 100 bp to buy and
  98 bp to sell, fees would have been $7.96 and the result $-7.96, which is
  about $101 of volume per $1 lost.
- **The machinery held.** Every fill reconciled between venue and kernel. The run crossed an
  election burst (term 3 to 16 on 2026-10-08 05:48 to 06:05 UTC), an edge redeploy and a
  twenty-second loss of one voter without an unknown outcome.
- **Sample size.** 29 round trips over 17 hours in one falling market. It says nothing
  reliable about whether the signal has an edge.

The totals include the four fills made on this account before the cash-variant session began; that
session accounts for 54 of the 58 fills.

## Round trips

Times are UTC. Prices are the simulated fill prices in USD per SOL. Amounts are USD for 0.12 SOL.

| # | Buy time | Buy | Sell time | Sell | Held (s) | Before fees | Fees | After fees |
|---|---|---|---|---|---|---|---|---|
| 1 | 2026-10-07 20:14:03 | 116.27 | 2026-10-07 20:24:58 | 116.23 | 655 | -0.0048 | 0.0558 | -0.0606 |
| 2 | 2026-10-07 20:30:17 | 116.37 | 2026-10-07 20:33:26 | 116.25 | 188 | -0.0144 | 0.0558 | -0.0702 |
| 3 | 2026-10-07 22:01:31 | 115.74 | 2026-10-07 22:16:00 | 115.69 | 869 | -0.0060 | 0.0555 | -0.0615 |
| 4 | 2026-10-07 22:22:41 | 115.78 | 2026-10-07 22:39:33 | 115.95 | 1011 | +0.0204 | 0.0556 | -0.0352 |
| 5 | 2026-10-07 22:49:06 | 116.01 | 2026-10-07 22:53:07 | 115.95 | 241 | -0.0072 | 0.0557 | -0.0629 |
| 6 | 2026-10-07 23:00:22 | 116.02 | 2026-10-07 23:08:27 | 115.99 | 484 | -0.0036 | 0.0557 | -0.0593 |
| 7 | 2026-10-07 23:14:48 | 116.11 | 2026-10-07 23:35:48 | 116.32 | 1259 | +0.0252 | 0.0558 | -0.0306 |
| 8 | 2026-10-07 23:58:28 | 116.30 | 2026-10-08 00:00:44 | 116.22 | 135 | -0.0096 | 0.0558 | -0.0654 |
| 9 | 2026-10-08 00:08:58 | 116.26 | 2026-10-08 00:09:57 | 116.21 | 58 | -0.0060 | 0.0558 | -0.0618 |
| 10 | 2026-10-08 00:14:03 | 116.31 | 2026-10-08 00:25:14 | 116.31 | 670 | +0.0000 | 0.0558 | -0.0558 |
| 11 | 2026-10-08 00:48:36 | 116.35 | 2026-10-08 01:18:38 | 116.69 | 1802 | +0.0408 | 0.0559 | -0.0151 |
| 12 | 2026-10-08 01:34:37 | 116.59 | 2026-10-08 01:38:36 | 116.43 | 238 | -0.0192 | 0.0559 | -0.0751 |
| 13 | 2026-10-08 02:15:36 | 116.29 | 2026-10-08 02:51:41 | 116.58 | 2164 | +0.0348 | 0.0559 | -0.0211 |
| 14 | 2026-10-08 04:28:07 | 115.52 | 2026-10-08 04:37:47 | 115.05 | 579 | -0.0564 | 0.0553 | -0.1117 |
| 15 | 2026-10-08 04:54:03 | 115.07 | 2026-10-08 05:30:45 | 115.53 | 2201 | +0.0552 | 0.0553 | -0.0001 |
| 16 | 2026-10-08 05:33:38 | 115.67 | 2026-10-08 05:35:44 | 115.57 | 126 | -0.0120 | 0.0555 | -0.0675 |
| 17 | 2026-10-08 05:37:29 | 115.68 | 2026-10-08 05:38:09 | 115.61 | 39 | -0.0084 | 0.0555 | -0.0639 |
| 18 | 2026-10-08 06:07:04 | 115.30 | 2026-10-08 06:20:18 | 115.26 | 794 | -0.0048 | 0.0553 | -0.0601 |
| 19 | 2026-10-08 06:43:19 | 114.93 | 2026-10-08 07:00:34 | 115.22 | 1035 | +0.0348 | 0.0552 | -0.0204 |
| 20 | 2026-10-08 07:11:49 | 115.41 | 2026-10-08 07:34:19 | 115.65 | 1350 | +0.0288 | 0.0555 | -0.0267 |
| 21 | 2026-10-08 07:39:41 | 115.69 | 2026-10-08 07:40:07 | 115.64 | 26 | -0.0060 | 0.0555 | -0.0615 |
| 22 | 2026-10-08 08:20:45 | 115.09 | 2026-10-08 08:23:33 | 114.95 | 168 | -0.0168 | 0.0552 | -0.0720 |
| 23 | 2026-10-08 08:29:24 | 115.05 | 2026-10-08 08:30:22 | 114.99 | 57 | -0.0072 | 0.0552 | -0.0624 |
| 24 | 2026-10-08 08:45:59 | 115.00 | 2026-10-08 08:52:59 | 114.88 | 420 | -0.0144 | 0.0552 | -0.0696 |
| 25 | 2026-10-08 09:02:12 | 115.02 | 2026-10-08 09:15:37 | 115.03 | 805 | +0.0012 | 0.0552 | -0.0540 |
| 26 | 2026-10-08 09:26:43 | 115.05 | 2026-10-08 09:44:14 | 115.13 | 1051 | +0.0096 | 0.0552 | -0.0456 |
| 27 | 2026-10-08 10:01:18 | 115.15 | 2026-10-08 10:05:42 | 115.06 | 264 | -0.0108 | 0.0553 | -0.0661 |
| 28 | 2026-10-08 12:21:20 | 113.19 | 2026-10-08 12:26:03 | 113.03 | 283 | -0.0192 | 0.0543 | -0.0735 |
| 29 | 2026-10-08 13:13:18 | 112.70 | 2026-10-08 13:25:38 | 112.53 | 740 | -0.0204 | 0.0541 | -0.0745 |
