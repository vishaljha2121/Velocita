# The running rule, replayed on a week of public data

Run 2026-10-09 for live promotion plan item 21. It answers one question: what does the strategy
that is trading on paper earn before costs, and what does it cost to run?

## Method

`replay.py` feeds Binance SOLUSDT one-second bars (keyless GET) through the same `Indicators` and
`Signals` classes the edge runs (`model/src/velocita_model/shadow/stochastic_second.py`), with the
edge's rules around them: act on the previous bar's signal, long only, one 0.12 SOL clip, no entry
within 60 s of an exit, fill at the bar's close in whole cents. It does not model the $25 balance,
the 256-order limit or the drawdown stop, so the totals are what the rule would do if nothing
stopped it.

## Does the replay match the live system?

Yes. Restricted to the first paper session (2026-10-07 20:14 to 2026-10-08 13:27 UTC):

| | Round trips | Volume | Before fees |
|---|---|---|---|
| Recorded live session (`../stochastic_cash_2026-10-07/RESULTS.md`) | 29 | $803.98 | +$0.0036 |
| Replay of the same window | 29 | $803.96 | +$0.0036 |

## Seven days, ending 2026-10-09 05:53 UTC (`result.txt`)

| | |
|---|---|
| Round trips | 328, about 47 a day |
| Volume | $9,339.86, about $1,334 a day |
| Before fees | -$0.90, which is -0.96 bp of volume |
| Winning round trips | 33.8% |
| Time holding a position | 35.9%; median hold 9.5 minutes |

| Cost assumption | Fees | Net for the week | Volume per $1 lost | $10 drawdown reached |
|---|---|---|---|---|
| Paper: 20 bp a side | $18.68 | -$19.58 | $477 | after 3.4 days |
| Measured: 100 bp buy, 98 bp sell | $92.46 | -$93.36 | $100 | after 0.8 days |

Every full day in the week was negative before fees.

## Thirty days, ending at the same time (`result_30d.txt`)

Added later on 2026-10-09 to check that the week was not unusual.

| | |
|---|---|
| Round trips | 1,475, about 49 a day |
| Volume | $39,835.14, about $1,328 a day |
| Before fees | -$0.50, which is -0.12 bp of volume |
| Winning round trips | 31.9% |
| Time holding a position | 37.3%; median hold 8.8 minutes |

| Cost assumption | Fees | Net for the 30 days | Volume per $1 lost | $10 drawdown reached |
|---|---|---|---|---|
| Paper: 20 bp a side | $79.67 | -$80.17 | $497 | after 3.8 days |
| Measured: 100 bp buy, 98 bp sell | $394.37 | -$394.86 | $101 | after 0.9 days |

Eleven of the thirty full days were positive before fees and nineteen negative; the largest day
either way was about 50 cents. The month says the same as the week: nothing before costs, so the
cost is the result.

## What this says

- **The rule has no edge before costs.** It is flat to slightly negative. That is consistent with
  the ten-indicator battery (`../ten_indicator_battery_2026-10-07/PARTIAL_REPORT.md`), where every
  family lost about 70% over two years at 30 bp and only a zero-cost run was positive.
- **So the result is the cost, almost exactly.** With nothing earned before fees, each dollar of
  volume loses the round-trip cost divided by two: about $1 per $100 at the measured cost. No
  change to the signal moves that unless it earns something of the order of 100 bp a trade.
- **Against the stated tolerance.** $10 to $12 of loss a week buys about $1,000 to $1,200 of
  volume a week at the measured cost. This rule trades that much in a day.

## Limits

- Thirty days, one asset, one period of the market.
- Fills are at the Binance close. The venue's own price differs by a few basis points
  (`../venue_price_basis_2026-10-09/`), which is small next to the cost.
- Every order is assumed to fill completely at its limit.
- The measured cost comes from the owner's earlier quote capture, not from this run.
