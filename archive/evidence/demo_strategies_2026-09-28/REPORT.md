# Demo rule strategies — full battery with measured True Markets costs (2026-09-28)

Costs per side (bp) from `truemarkets_markup_2026-09-28/quotes_buy_sell_1631utc.json`:
- BTC (as CBBTC): buy 109.9 / sell 123.4 mean; worst quote buy 114.1 / sell 124.1
- SOL (as SOL): buy 100.3 / sell 98.2 mean; worst quote buy 101.7 / sell 99.1

Profit factor = sum of daily log gains / sum of daily log losses. p = share of null draws (each fully re-optimized) scoring at least the real result.

## Summary

| Asset | Strategy | IS choice | IS perm p | WF PF | WF total | WF max DD | Hold total (same dates) | WF perm p | Random-walk p |
|---|---|---|---|---|---|---|---|---|---|
| BTC | trend | 125 | 0.005 | 1.077 | +134.6% | 57.5% | +169.1% | 0.060 | 0.011 |
| BTC | dip_in_uptrend | 6 | 0.437 | 0.721 | -46.9% | 54.3% | +169.1% | 0.697 | 0.457 |
| BTC | calm_trend | 80 | 0.304 | 0.967 | -23.4% | 65.1% | +169.1% | 0.418 | 0.215 |
| SOL | trend | 75 | 0.028 | 1.039 | +47.0% | 75.1% | +314.4% | 0.338 | 0.039 |
| SOL | dip_in_uptrend | 8 | 0.199 | 0.968 | -5.4% | 33.1% | +314.4% | 0.284 | 0.176 |
| SOL | calm_trend | 120 | 0.619 | 1.216 | +332.0% | 43.0% | +314.4% | 0.070 | 0.501 |

## BTC · trend

In-sample 2018-01-01 .. 2023-07-31; grid scores: 50: 1.040, 75: 1.070, 100: 1.142, 125: 1.213, 150: 1.183, 175: 1.108, 200: 1.141, 225: 1.127, 250: 1.072. Best **125**.

Walk-forward out-of-sample 2020-12-31 .. 2026-08-31; choices: 2020-12-31: 150, 2021-04-01: 125, 2021-07-01: 125, 2021-09-30: 125, 2021-12-30: 50, 2022-03-31: 125, 2022-06-30: 125, 2022-09-29: 125, 2022-12-29: 125, 2023-03-30: 125, 2023-06-29: 125, 2023-09-28: 125, 2023-12-28: 125, 2024-03-28: 125, 2024-06-27: 200, 2024-09-26: 175, 2024-12-26: 150, 2025-03-27: 150, 2025-06-26: 150, 2025-09-25: 50, 2025-12-25: 150, 2026-03-26: 150, 2026-06-25: 100

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| walk-forward, mean measured cost | 1.077 | +134.6% | +16.2% | 57.5% | 0.57 | 9.5 |
| walk-forward, worst measured quote | 1.076 | +131.4% | +15.9% | 57.7% | 0.56 | - |
| hold BTC (one measured buy) | 1.048 | +169.1% | +19.1% | 76.6% | 0.59 | - |

Null tests: in-sample permutation p = 0.005 (1000), walk-forward permutation p = 0.060 (200), random walk p = 0.011 (1000).

True Markets confirmation (CBBTC history, in-sample choice 125), 2025-01-14 .. 2026-08-31:

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| True Markets prices | 0.807 | -32.1% | -21.2% | 45.4% | -0.89 | - |
| Binance prices, same dates | 0.822 | -29.7% | -19.4% | 45.8% | -0.79 | - |

## BTC · dip_in_uptrend

In-sample 2018-01-01 .. 2023-07-31; grid scores: 2: 0.701, 3: 0.802, 4: 0.792, 5: 0.890, 6: 0.893, 7: 0.732, 8: 0.636. Best **6**.

Walk-forward out-of-sample 2020-12-31 .. 2026-08-31; choices: 2020-12-31: 6, 2021-04-01: 6, 2021-07-01: 5, 2021-09-30: 5, 2021-12-30: 5, 2022-03-31: 5, 2022-06-30: 5, 2022-09-29: 5, 2022-12-29: 5, 2023-03-30: 5, 2023-06-29: 5, 2023-09-28: 5, 2023-12-28: 6, 2024-03-28: 6, 2024-06-27: 5, 2024-09-26: 6, 2024-12-26: 6, 2025-03-27: 5, 2025-06-26: 5, 2025-09-25: 3, 2025-12-25: 5, 2026-03-26: 5, 2026-06-25: 3

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| walk-forward, mean measured cost | 0.721 | -46.9% | -10.6% | 54.3% | -0.50 | 8.5 |
| walk-forward, worst measured quote | 0.717 | -47.5% | -10.8% | 54.8% | -0.51 | - |
| hold BTC (one measured buy) | 1.048 | +169.1% | +19.1% | 76.6% | 0.59 | - |

Null tests: in-sample permutation p = 0.437 (1000), walk-forward permutation p = 0.697 (200), random walk p = 0.457 (1000).

True Markets confirmation (CBBTC history, in-sample choice 6), 2025-10-11 .. 2026-08-31:

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| True Markets prices | 0.399 | -5.6% | -6.2% | 8.1% | -1.04 | - |
| Binance prices, same dates | 0.420 | -5.4% | -6.0% | 8.1% | -1.00 | - |

## BTC · calm_trend

In-sample 2018-01-01 .. 2023-07-31; grid scores: 40: 1.048, 50: 1.085, 60: 1.009, 70: 1.077, 80: 1.158, 90: 1.123, 100: 1.096, 110: 1.152, 120: 1.142. Best **80**.

Walk-forward out-of-sample 2020-12-31 .. 2026-08-31; choices: 2020-12-31: 90, 2021-04-01: 80, 2021-07-01: 80, 2021-09-30: 50, 2021-12-30: 40, 2022-03-31: 40, 2022-06-30: 40, 2022-09-29: 40, 2022-12-29: 40, 2023-03-30: 40, 2023-06-29: 120, 2023-09-28: 80, 2023-12-28: 60, 2024-03-28: 60, 2024-06-27: 70, 2024-09-26: 70, 2024-12-26: 70, 2025-03-27: 70, 2025-06-26: 70, 2025-09-25: 60, 2025-12-25: 60, 2026-03-26: 60, 2026-06-25: 60

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| walk-forward, mean measured cost | 0.967 | -23.4% | -4.6% | 65.1% | 0.02 | 9.3 |
| walk-forward, worst measured quote | 0.965 | -24.4% | -4.8% | 65.3% | 0.02 | - |
| hold BTC (one measured buy) | 1.048 | +169.1% | +19.1% | 76.6% | 0.59 | - |

Null tests: in-sample permutation p = 0.304 (1000), walk-forward permutation p = 0.418 (200), random walk p = 0.215 (1000).

True Markets confirmation (CBBTC history, in-sample choice 80), 2025-04-23 .. 2026-08-31:

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| True Markets prices | 0.995 | -0.7% | -0.5% | 29.0% | 0.09 | - |
| Binance prices, same dates | 1.004 | +0.5% | +0.4% | 29.0% | 0.13 | - |

## SOL · trend

In-sample 2020-08-11 .. 2023-07-31; grid scores: 50: 1.267, 75: 1.283, 100: 1.210, 125: 1.242, 150: 1.263, 175: 1.262, 200: 1.220, 225: 1.217, 250: 1.116. Best **75**.

Walk-forward out-of-sample 2023-08-11 .. 2026-08-31; choices: 2023-08-11: 75, 2023-11-10: 50, 2024-02-09: 175, 2024-05-10: 50, 2024-08-09: 250, 2024-11-08: 250, 2025-02-07: 250, 2025-05-09: 250, 2025-08-08: 250, 2025-11-07: 50, 2026-02-06: 250, 2026-05-08: 125, 2026-08-07: 50

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| walk-forward, mean measured cost | 1.039 | +47.0% | +13.4% | 75.1% | 0.51 | 15.7 |
| walk-forward, worst measured quote | 1.038 | +46.2% | +13.2% | 75.2% | 0.51 | - |
| hold SOL (one measured buy) | 1.084 | +314.4% | +59.1% | 76.3% | 0.97 | - |

Null tests: in-sample permutation p = 0.028 (1000), walk-forward permutation p = 0.338 (200), random walk p = 0.039 (1000).

True Markets confirmation (SOL history, in-sample choice 75), 2023-12-12 .. 2026-08-31:

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| True Markets prices | 0.910 | -52.2% | -23.8% | 87.8% | -0.21 | - |
| Binance prices, same dates | 0.918 | -48.7% | -21.8% | 87.1% | -0.16 | - |

## SOL · dip_in_uptrend

In-sample 2020-08-11 .. 2023-07-31; grid scores: 2: 0.932, 3: 0.918, 4: 0.983, 5: 1.003, 6: 0.985, 7: 1.079, 8: 1.103. Best **8**.

Walk-forward out-of-sample 2023-08-11 .. 2026-08-31; choices: 2023-08-11: 8, 2023-11-10: 8, 2024-02-09: 8, 2024-05-10: 8, 2024-08-09: 7, 2024-11-08: 7, 2025-02-07: 7, 2025-05-09: 7, 2025-08-08: 7, 2025-11-07: 8, 2026-02-06: 8, 2026-05-08: 8, 2026-08-07: 8

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| walk-forward, mean measured cost | 0.968 | -5.4% | -1.8% | 33.1% | 0.05 | 9.8 |
| walk-forward, worst measured quote | 0.966 | -5.7% | -1.9% | 33.3% | 0.04 | - |
| hold SOL (one measured buy) | 1.084 | +314.4% | +59.1% | 76.3% | 0.97 | - |

Null tests: in-sample permutation p = 0.199 (1000), walk-forward permutation p = 0.284 (200), random walk p = 0.176 (1000).

True Markets confirmation (SOL history, in-sample choice 8), 2024-04-16 .. 2026-08-31:

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| True Markets prices | 1.226 | +26.5% | +10.4% | 25.1% | 0.56 | - |
| Binance prices, same dates | 1.024 | +3.4% | +1.4% | 27.1% | 0.18 | - |

## SOL · calm_trend

In-sample 2020-08-11 .. 2023-07-31; grid scores: 40: 0.000, 50: 0.814, 60: 0.653, 70: 0.674, 80: 0.880, 90: 0.790, 100: 0.702, 110: 0.922, 120: 1.164. Best **120**.

Walk-forward out-of-sample 2023-08-11 .. 2026-08-31; choices: 2023-08-11: 120, 2023-11-10: 120, 2024-02-09: 90, 2024-05-10: 90, 2024-08-09: 90, 2024-11-08: 90, 2025-02-07: 90, 2025-05-09: 90, 2025-08-08: 90, 2025-11-07: 90, 2026-02-06: 60, 2026-05-08: 60, 2026-08-07: 60

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| walk-forward, mean measured cost | 1.216 | +332.0% | +61.3% | 43.0% | 1.12 | 12.7 |
| walk-forward, worst measured quote | 1.215 | +330.0% | +61.1% | 43.1% | 1.12 | - |
| hold SOL (one measured buy) | 1.084 | +314.4% | +59.1% | 76.3% | 0.97 | - |

Null tests: in-sample permutation p = 0.619 (1000), walk-forward permutation p = 0.070 (200), random walk p = 0.501 (1000).

True Markets confirmation (SOL history, in-sample choice 120), 2024-04-15 .. 2026-08-31:

| Series | Profit factor | Total | CAGR | Max DD | Sharpe | Trades/yr |
|---|---|---|---|---|---|---|
| True Markets prices | 0.913 | -42.7% | -20.9% | 62.4% | -0.17 | - |
| Binance prices, same dates | 0.907 | -45.0% | -22.2% | 62.1% | -0.20 | - |

## Caveats

- Daily closes; a decision fills at the next close. Intraday price at the real fill will differ.
- Costs measured at $10-$100 on one day; larger size or other days may cost more (route price).
- BTC is costed as cbBTC (Base); cbBTC tracks BTC 1:1 but trades on a different venue path.
- Runtime 159 s with 15 workers.
