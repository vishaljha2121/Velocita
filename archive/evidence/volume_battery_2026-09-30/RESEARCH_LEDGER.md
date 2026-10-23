# Research ledger: volume candidates on cbBTC (pre-registered)

Written 2026-09-30 19:54 UTC, BEFORE any BTCUSDT 1 s data for this study was
downloaded or any result was seen. Book ch52 §"Repeated search and
overfitting": every trial, grid, seed, split and selection rule is listed here.
Anything changed after results were seen is appended below the line with the
reason, never edited above it.

## Owner decisions this study works under (2026-09-30)

- Objective: leaderboard VOLUME on TrueMarkets.
- Loss budget: $10-12 per WEEK maximum. Losing is acceptable if it buys volume.
- Capital: $25.
- Rapid round-trip execution is acceptable to the owner. This OVERRIDES the
  earlier project rule that "volume must come from trades with a stated reason"
  (docs/evidence/volume_strategy_2026-09-28/README.md item 4). Recorded as the
  owner's decision, not the research agent's.

## Data (fixed)

- Price proxy for cbBTC: Binance BTCUSDT 1-second klines, public archive
  data.binance.vision, each zip verified against its published SHA-256 before
  parsing. Monthly files 2026-03 .. 2026-08; daily files 2026-09-01 .. 2026-09-27.
- A tick at time T uses the close of the latest 1 s bar that ended at or before
  T (as-of join; a bar is available at its close time).
- Daily closes for the 125-day trend before the tick period: Binance daily
  archive (checksum-verified). Inside the tick period: the last tick of each UTC day.

## Splits (fixed)

- Research period: 2026-03-02 00:00 UTC .. 2026-08-31 00:00 UTC, 26 whole
  Monday-start weeks. All selection happens here.
- Walk-forward inside the research period: 8-week training window, 2-week step
  (out-of-sample weeks 9..26 = 18 weeks). Parameters chosen on training weeks only.
- HOLDOUT (untouched): 2026-08-31 00:00 .. 2026-09-28 00:00 UTC, 4 weeks. Run
  ONCE, after the recommendation is written below, as the first honest estimate.

## Execution model (the live system's, not an idealised fill)

- One decision per tick, ticks every 30 s.
- FOK fill at the tick's reference price, measured per-side cost charged at the fill.
- After every fill the next tick is lost (kernel stale_risk_state re-sync): the
  earliest opposite trade is two ticks later.
- Long-only, at most one position of one clip. 1 lot = 0.00001 BTC.
- Capital $25: a buy whose notional + buy cost exceeds cash is killed and recorded.
  Kernel exposure cap: notional x (1 + 150 bp fee hold) > $25 is refused and recorded.
- Weekly loss cap (operational runs): a buy is not sent if the week's loss so far
  plus the round-trip cost of the clip would exceed the cap; recorded.
- Weekly top-up (operational runs): at each Monday 00:00 the book is restored to
  $25 equity if below (the $10-12/week budget implies replenishment). A no-top-up
  run is also reported (the paper venue's fixed $25 quote balance).

## Costs (fixed)

- Main: mean measured cbBTC all-in cost vs TM reference from
  quotes_buy_sell_1631utc.json via research/costs.py (buy ~109.9 bp, sell ~123.4 bp).
- Sensitivity: measured x0.8, x1.25; PaperVenue defaults 115/120 bp (what paper will charge).
- Latency sensitivity: two lost ticks after a fill instead of one; fill price one
  tick (30 s) after the decision.

## Candidates and grids (24 configurations)

Feature schema of these programs (all integers, as the kernel evaluates):
0 trend = price / SMA125(daily, provisional today) - 1 (micros; the existing `trend`)
1 devs  = price / SMA(last w ticks) - 1 (micros)            NEW edge feature
2 phase = flips 0/1 on every decided (non-lost) tick           NEW edge feature
3 held_s = seconds since the open position was bought, 0 if flat   NEW edge feature

| Id | Rules (first match wins) | Grid |
|---|---|---|
| A churn | phase=0 enter_long; phase=1 exit | none (1) |
| B trend-gated churn | trend<=0 exit; phase=0 enter_long; phase=1 exit | none (1) |
| C hold-and-rotate, trend-gated | trend<=0 exit; held_s>=R exit; trend>0 enter_long | R in {30,120,480,1440} min (4) |
| C0 hold-and-rotate, ungated | held_s>=R exit; enter_long | R in {30,120,480,1440} min (4) |
| D dip scalp | devs<=-x enter_long; devs>=+y exit | w in {20,120} ticks, x in {25,50,100} bp, y in {0,50} bp (12) |
| E trend(125), per tick | trend>0 enter_long; trend<=0 exit | none (1) |
| BH buy-and-hold | enter_long once | none (1) |

## Battery (fixed)

- Score ("edge per unit volume"): net P&L / traded volume, in bp, on a signal
  run (25 lots, unlimited capital, no loss cap: a hypothetical signal test per
  ch52). Higher is better. A candidate that never trades scores -inf.
- Selection rule: the grid value with the highest score; ties go to the first.
- In-sample permutation: 200 shuffles of the research period's 30 s log returns
  (jointly, pre-period daily history fixed), full re-optimisation per shuffle.
- Random walk: 200 Gaussian paths with the same 30 s mean and stdev, re-optimised.
- Walk-forward permutation: 100 shuffles of the returns after the first training
  window, full walk-forward per shuffle.
- p = (1 + #null >= real) / (N + 1). Seed 20260930 (+ draw index).
- P(weekly loss > $12) from the null paths: the operational run (cap on,
  top-up on, 25 lots) on every permuted and random-walk path, all weeks pooled.

## Clip sizes reported

25 lots, 12 lots, and clip* = the largest whole number of lots (<= 25) whose
mean uncapped weekly loss at measured costs is <= $12 (none if even 1 lot exceeds).

## Recommendation rule (fixed before results)

If any candidate has in-sample permutation p < 0.05 AND walk-forward
permutation p < 0.10, prefer it. Otherwise (no edge) prefer the candidate with
the most volume per dollar lost in the research period's operational run, and
among near-ties (within 5 %) the one whose weekly loss is most predictable
(smallest stdev of weekly net P&L), because then volume is simply bought at the fee.

-----------------------------------------------------------------------------
Appended after results (with reasons):

### Amendments, 2026-09-30 ~20:35 UTC, after a smoke run

A smoke run (2 null draws per test, for debugging) was made before the full
run. Its REAL-data numbers are the same as the full run's, so the amendments
below were made AFTER seeing research-period results. They are disclosed as
such; none changes a grid, a split, the score, or a null.

1. **Recommendation rule defect.** Ranking by volume per dollar lost alone puts
   D (w=20, x=100, y=50) first: it trades about $16 a week, and its VPD comes
   from a handful of round trips of price noise. The owner's objective is
   volume, which the rule did not encode. Amended: among candidates without
   edge, only those whose operational run averages at least $500/week of
   volume at some reported clip are eligible; the pre-registered VPD and
   predictability criteria then apply unchanged.
2. **clip\* definition.** The owner's phrase "the largest clip that keeps the
   $10-12/week loss cap binding" is read as: the loss CAP, not the $25 capital,
   is what stops trading in a week. The smoke run showed that at 25 lots the
   $25 capital kills churn buys after roughly $6-8 of weekly loss, so the $12
   cap never binds. New clip\* = the largest whole-lot clip <= 25 with zero
   capital kills in candidate A's operational run over the research period
   (A is the fastest spender, so this clip is safe for all). The original
   definition (mean uncapped weekly loss <= $12) is still reported, as clip_u.
3. **One configuration added, not fitted: C0p (budget-paced rotation).** With
   no edge, a rotation period is a pacing choice, not a parameter to optimise.
   R = 7 days / N - 1 min, where N = floor($12 / round-trip cost) at clip\*,
   the measured costs and the research period's median price. Configurations: 25.
4. **Drawdown** is reported within a week (under weekly top-up the cumulative
   drawdown is only the sum of weekly losses); the worst week is also reported.

### Full research run (200 / 200 / 100 draws) and the recommendation, 2026-09-30 ~23:40 UTC, BEFORE the holdout

- Edge rule: no candidate has IS p < 0.05 AND WF p < 0.10.
  - A has IS p 0.035 and RW p 0.075, but WF p 0.218.
  - Its score beats the null median by 0.01 bp (-116.632 vs -116.644 bp): statistically detectable over 131,040 round trips, economically nil.
- The rule with amendment 1 selects A (churn) first. A and C0p are within 5 % on VPD, and A's weekly net stdev is the smallest.
- Paper tooling (enabled 2026-09-30 by the paper agent) defines phase on the clock, rotation on the clock, and dip on minute closes. DEPLOYED.md simulates those exact definitions.
  - It adds one configuration NOT in the pre-registered grid: dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10, the paper agent's parameters, not fitted here.
  - Its fixed-parameter perm p is 0.030 and RW p 0.020, at +0.7 bp over the null median (-115.8 vs -116.5 bp); no walk-forward, and not pre-registered. Configurations tried in total: 25 + 4 deployed = 29.
- C0p (ungated, paced rotation) cannot be run with the paper flags: `rotate` is trend-gated.
- RECOMMENDATION for paper, fixed before the holdout:
  1. `--strategy churn --period-s 60 --order-quantity-lots 12`, with the operator halting buys at a $12 weekly loss.
  2. `--strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 --order-quantity-lots 12`, as the pacing alternative (spends the budget over days rather than hours). It is flagged as outside the pre-registered grid.
- Holdout recommended set: A:12 (research definition) plus the deployed churn and dip at 12 lots.
