# Volume battery on cbBTC: verdict, 2026-09-30

## Verdict

**No strategy here has an edge worth anything. Every one buys volume at the fee.**

- **Volume per dollar lost is about $86 for every candidate that trades.** That is 10,000 divided by 116.6 bp, the mean measured cost per side. A $12 weekly loss therefore buys about **$1,020 of volume a week**, and $10 buys about $860.
- **The strategy only decides how fast and how evenly the budget is spent.** It does not change how much volume the budget buys.
- **Two small edges show up, and neither matters.**
  - Churn: in-sample p = 0.035 on 131,040 round trips, but it beats the null by 0.01 bp, and its walk-forward p is 0.218.
  - The paper tooling's dip (30 min / 20 bp / 10 bp): fixed-parameter permutation p = 0.030 and random-walk p = 0.020, at +0.7 bp against a 116.6 bp fee (0.6 %). It was not pre-registered and has no walk-forward.
  - Both are statistically detectable and economically nil. Neither buys measurably more volume per dollar.
- **At 25 lots the $25 capital runs out before the $12 cap does.**
  - Churn buys start being killed after about $6-8 of loss at research prices, and after about $5.7 at September prices (BTC about $84k).
  - At 12 lots (about $8-10 of BTC), the $12 cap binds first, with no capital kills.
- **Paper as deployed has no weekly loss limit and no top-up.** Churn at 12 lots would lose about $16-18 in 2-5 hours. After that the venue kills every buy, about 1,400 a day. **An operator halt at $12 per week is required.** The kernel has no weekly loss limit.

## Recommended paper runs (flags as the paper tooling takes them)

| # | Flags | Why | Expected per week at the $12 cap | Paper prediction: PaperVenue 115/120 bp, fixed $25, no top-up |
|---|---|---|---|---|
| 1 | `--strategy churn --period-s 60 --order-quantity-lots 12` plus an operator halt at $12 weekly loss | The pre-registered rule's pick: most volume per dollar, and the most predictable loss (weekly net stdev $0.05) | Volume $1,020 (p5-p95 $1,011-1,030); loss $11.92 (p5-p95 $11.82-11.98). Holdout: $1,020, -$11.91 | **With the $12 halt:** reached after 1.8 / 2.1 / 2.2 h (p5 / p50 / p95; 30 s ticks), or 2.7 / 3.2 / 3.4 h at 36 s ticks. Volume $1,000-1,023, net -$11.81 to -$12.00, 106-134 fills. **Without the halt (as deployed):** first capital kill after 2.4-3.3 h (holdout 2.2-2.5 h; 36 s: 3.5-4.9 h). Volume $1,330-1,530, net -$15.7 to -$18.1, then every buy is killed |
| 2 | `--strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 --order-quantity-lots 12` plus the same halt | Pacing: spends the budget over days, not in two hours. Outside the pre-registered grid (these are the paper agent's parameters) | Volume $931 (p5 / p50 / p95 $484 / $1,002 / $1,051); loss $10.77. Holdout: $1,014, -$11.69 | **With the halt:** cap reached in 17 of 26 weeks, median 113 h. First-24 h volume $93 / $186 / $286. Week volume $484 / $1,002 / $1,051, net -$5.56 / -$11.85 / -$11.96. **Without it:** week net p5 / p50 / p95 -$17.16 / -$12.89 / -$5.56; a capital kill in 7 of 26 weeks, after about 100-167 h |

- **Candidates I did not recommend:**
  - `trend_churn` and `rotate` behave like churn when the trend is up, which it was throughout the September holdout.
  - When the trend is down they trade nothing. In the research period they traded in only 10 and 8 of 26 weeks.
  - The pre-registered runner-up, C0p (ungated rotation paced to the budget: R = 213 min at 16 lots, about 161 min at 12 lots), cannot be expressed with the paper flags, because `rotate` is trend-gated.

### Where my definitions differ from the paper tooling's

`DEPLOYED.md` simulates the tooling's own definitions, so the paper run should be compared with DEPLOYED.md, not with REPORT.md.

- **Churn.**
  - The tooling uses clock phase: parity of floor(t / 60).
  - My A flips phase on each decided tick.
  - They are identical at exact 30 s ticks. With slower or irregular ticks, or an extra lost tick, the clock version waits for the next odd minute. There is an oracle test for exactly that case.
- **Rotate.** The tooling exits during the first 60 s of each 30-minute clock period. My C exits 30 minutes after the buy, which gives a 31-minute cycle.
- **Dip.**
  - The tooling uses an SMA of 30 completed minute closes, with strict `<` and `>`.
  - My D used 10- or 60-minute tick SMAs.
  - UNVERIFIED: whether the tooling's SMA includes the open minute, whether a "minute close" is the last tick or the 1 s close at :59, and the comparison operators.
- **Cadence.** The simulation uses exact 30 s ticks. Live ticks come at the 30 s throttle plus the roughly 12 s True Markets refresh. The 36 s runs in DEPLOYED.md cover that: weekly volume at the cap is unchanged and only the time to reach it grows.

## Owner decisions this work runs under (2026-09-30)

- The objective is leaderboard volume on True Markets.
- The loss budget is $10-12 a week. Losing is acceptable if it buys volume.
- Capital is $25.
- **Rapid round-trip execution is acceptable to the owner.** This overrides the earlier project rule that volume must come from trades with a stated reason (`../volume_strategy_2026-09-28/README.md` item 4). It is the owner's decision, not a research finding.

**Wash trading (book ch25 §"Wash trading and self-match", 25-surveillance.tex l.107).**
- Wash trading is volume without a genuine change in beneficial ownership. Self-match is the same participant on both sides.
- Each leg here is a swap routed by the venue against third-party liquidity (the route on Base). Ownership changes on every leg, and we are never on both sides of one match. By that definition this is not self-match.
- The owner has accepted rapid round trips as compliant with True Markets. **UNVERIFIED:** I have not read True Markets' terms or leaderboard rules on this.

## Method (book ch52, 52-backtesting-simulation-replay.tex)

- **Honesty rule (§"The honesty rule", l.37).**
  - Costs are OBSERVED (quotes_buy_sell_1631utc.json).
  - Every P&L and volume figure is BACKTESTED (Binance prices with modelled FOK fills).
  - Null distributions are SIMULATED.
  - Every table is labelled with its class.
  - The paper run will be SIMULATED fills (PaperVenue) on the real decision path. It is the forward test.
- **Fees, capital and latency (§"Latency, fees, impact, and capital assumptions", l.265-306; §"Capital and margin bias", l.391).**
  - Costs are charged at the fill and reduce the cash available for the next buy.
  - $25 capital, the kernel exposure cap (150 bp fee hold) and the one-position limit are enforced.
  - Every refused or killed order is counted: Kills/day, Expo/day and Bound/day in REPORT.md.
  - The lost re-sync tick after every fill is modelled.
  - Sensitivities cover two lost ticks, a fill 30 s late, and 36 s cadence.
- **Impact and price uncertainty.** Every candidate is reported at measured costs x0.8 / x1.0 / x1.25 and at the PaperVenue defaults (115/120 bp).
  - Volume per dollar moves with cost: $107 / $86 / $69 per dollar.
  - Volume at the cap scales inversely with cost: $1,280 / $1,020 / $815 a week for churn at 12 lots.
- **Repeated search (§"Repeated search and overfitting", l.365).**
  - `RESEARCH_LEDGER.md` holds the pre-registration, the disclosed amendments made after a smoke run, the recommendation, and the count of 29 configurations.
  - Seed 20260930. Research period 2026-03-02 .. 2026-08-31 (26 weeks). Walk-forward uses an 8-week train and a 2-week step.
  - The untouched holdout 2026-08-31 .. 2026-09-28 (4 weeks) was run once, after the recommendation was written. It is the first honest estimate.
- **Unrealistic fills (§"Unrealistic fills", l.381).** FOK fills are at the reference price, as PaperVenue does. The one unmeasured residual is movement between the decision and the fill, and "fill 30 s late" bounds it (no change).
- **Forward testing wins (§"Forward testing wins", l.437).** The paper run buys the evidence. The prediction intervals above are what it will be compared against. A miss should improve the model, not select a nicer period.

## Data provenance

- **Ticks.** Binance BTCUSDT 1-second klines from data.binance.vision:
  - Monthly archives 2026-03 .. 2026-07, and daily archives 2026-08-01 .. 2026-09-27.
  - Each file was verified against its published SHA-256 CHECKSUM before parsing. The digests are in `SOURCES.txt`.
  - Sampled into 30 s (and 36 s) ticks by an as-of join: a tick uses the latest bar that had closed by its time.
  - No tick used a price older than 0 s, because the archive had no gaps.
- **Trend.** The 125-day trend history is the Binance daily archive (checksum-verified), 2025-06 onward.
- **Costs.** The mean measured cbBTC all-in cost: buy 109.9 bp, sell 123.4 bp (`../truemarkets_markup_2026-09-28/`).
- **Proxy.** BTCUSDT stands in for the True Markets cbBTC reference price.

## Files

- `REPORT.md`: the research period in full.
  - The battery for A, B, C, C0, C0p, D and E (IS permutation 200, random walk 200, WF permutation 100).
  - Operational tables at 25, 16 and 12 lots, weekly spreads, the sensitivity grid, and a paper-week prediction.
- `DEPLOYED.md`: the paper tooling's four strategies as they define them.
  - Research period at 30 s and 36 s cadence, a fixed-parameter battery (100 + 100 draws), and the paper-as-configured prediction.
- `HOLDOUT.md`: the one holdout run.
  - Note: its "Inside?" column requires every holdout week to fall inside the research p5-p95 band. For churn that band is only about $20 wide, so a "NO" there means a week drifted a few dollars, not a miss. The means match.
- `RESEARCH_LEDGER.md`, `SOURCES.txt`, `research_results.json`.
- Code:
  - `model/src/velocita_model/research/tick_data.py`
  - `model/src/velocita_model/research/volume_battery.py`
  - `model/src/velocita_model/research/volume_battery_report.py`
- Tests: `model/tests/test_volume_battery.py`.
- Reproduce (the data is cached in `~/.cache/velocita-research/`):
  `PYTHONPATH=model/src python3 -m velocita_model.research.volume_battery_report {research|deployed|holdout} --out-dir docs/evidence/volume_battery_2026-09-30 [--recommended A:12]`

## Not verified

- The True Markets reference-price construction; the proxy may differ at 30 s horizons, which matters most for the dip.
- Whether killed or refused orders also cost a re-sync tick. The model assumes only fills do.
- The paper tooling's minute-close details, listed above.
- Costs rest on three quotes per side from one afternoon.
- True Markets' rules on rapid round trips, and how the leaderboard counts volume (both sides, at what price).
