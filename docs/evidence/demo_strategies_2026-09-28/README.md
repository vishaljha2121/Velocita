# Demo rule strategies — verdict 2026-09-28

`REPORT.md`: three rule programs (cluster-identical first-match semantics) on
BTC (costed as cbBTC) and SOL, Binance daily archive 2018-01..2026-08, MEASURED
True Markets costs (SOL ~100/98 bp, cbBTC ~110/123 bp per side), 1000 in-sample
permutations, 200 walk-forward permutations, 1000 random walks, all re-optimized.

| | IS perm p | Random-walk p | WF perm p | WF total / max DD | Hold (same dates) |
|---|---|---|---|---|---|
| BTC trend (SMA 125) | **0.005** | **0.011** | 0.060 | +135 % / 57.5 % | +169 % / 76.6 % |
| BTC dip_in_uptrend | 0.437 | 0.457 | 0.697 | -47 % | +169 % |
| BTC calm_trend | 0.304 | 0.215 | 0.418 | -23 % | +169 % |
| SOL trend (SMA 75) | 0.028 | 0.039 | 0.338 | +47 % / 75 % | +314 % |
| SOL dip_in_uptrend | 0.199 | 0.176 | 0.284 | -5 % | +314 % |
| SOL calm_trend | 0.619 | 0.501 | 0.070 | +332 % / 43 % | +314 % |

VERDICT
- BTC trend is the only program whose in-sample edge survives both nulls
  (p 0.005 / 0.011). Out of sample it is borderline (p 0.060), and after
  1.1-1.2 % per side it LAGS buy-and-hold in return (+135 % vs +169 %) while
  cutting the worst drawdown (57.5 % vs 76.6 %). A risk-reduction rule, not a
  return engine.
- Nothing else is established. SOL calm_trend's out-of-sample result is not
  supported in-sample (p 0.62): treat as luck.
- True Markets' own price history reproduces the Binance results on the same
  dates for 5 of 6 programs, which validates the one-day alignment fix. The
  exception (SOL dip: +26.5 % vs +3.4 %) is noise in the sampled TM prices
  creating false dips: another reason not to trade off TM daily history alone.
- DEMO CHOICE: BTC trend, SMA 125, in shadow mode. Chosen because it is the
  one program the battery supports, not because it makes money. It trades
  about 9.5 times a year.
