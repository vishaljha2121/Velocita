# Minara momentum rotation — permutation-test battery (2026-09-28)

Data: Binance spot daily closes 2018-01-01 .. 2026-08-31 (3165 days), universe BTC, ETH, SOL, PENDLE, ZEC, PENGU; first close per asset: BTC 2018-01-01, ETH 2018-01-01, SOL 2020-08-11, PENDLE 2023-07-03, ZEC 2019-03-21, PENGU 2024-12-17.
In-sample 2018-01-01 .. 2023-07-31; out-of-sample from 2023-08-01.
Costs 30 bp per side (assumption); fill at next close; Monday rebalance; invested fraction 0.5 x BTC-regime exposure.

## 1. In-sample optimization

momentum_days → profit factor: 20: 1.195, 30: 1.186, 40: 1.182, 50: 1.229, 60: 1.187, 70: 1.201, 80: 1.180, 90: 1.217, 100: 1.193, 110: 1.195, 120: 1.201, 130: 1.235, 140: 1.241, 150: 1.247, 160: 1.259, 170: 1.271, 180: 1.271

Best: **180 days**, profit factor 1.271 (Minara's 90: 1.217).

| Series | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| strategy, 180d, in-sample | 1.271 | +895.6% | +50.9% | 38.6% | 1.42 |
| BTC buy & hold, in-sample | 1.031 | +118.5% | +15.0% | 81.2% | 0.56 |

## 2. In-sample permutation test

1000 permutations, each fully re-optimized. **p = 0.025**. null median 1.123, 95th pct 1.246, max 1.393; real 1.271.
Low p (< 0.05): the optimized in-sample score is rarely matched by an optimizer run on shuffled history. High p: the optimizer finds this much 'edge' in noise.

## 3. Walk-forward (3-year train, 91-day step)

Out-of-sample 2020-12-31 .. 2026-08-31. Chosen windows: 2020-12-31: 50, 2021-04-01: 50, 2021-07-01: 60, 2021-09-30: 170, 2021-12-30: 180, 2022-03-31: 160, 2022-06-30: 160, 2022-09-29: 160, 2022-12-29: 160, 2023-03-30: 180, 2023-06-29: 150, 2023-09-28: 150, 2023-12-28: 150, 2024-03-28: 140, 2024-06-27: 130, 2024-09-26: 130, 2024-12-26: 160, 2025-03-27: 130, 2025-06-26: 160, 2025-09-25: 20, 2025-12-25: 80, 2026-03-26: 80, 2026-06-25: 80

| Series | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| walk-forward strategy | 1.191 | +802.6% | +47.4% | 47.5% | 1.18 |
| fixed 90d (Minara) same dates | 1.218 | +1392.6% | +61.1% | 44.4% | 1.33 |
| BTC buy & hold same dates | 1.048 | +172.1% | +19.3% | 76.6% | 0.59 |

Minara's headline window 2023-08-01 .. 2025-08-01 inside our walk-forward: walk-forward, 1.130, +93.4%, +38.9%, 47.5%, 0.95

## 4. Walk-forward permutation test

200 permutations of the data after the first training window. **p = 0.055**. null median 1.084, 95th pct 1.192, max 1.270; real 1.191.

## 5. Random-walk test

1000 Gaussian random-walk histories (each asset's own drift and volatility, no correlation), fully re-optimized. **p = 0.016**. null median 1.121, 95th pct 1.244, max 1.368; real 1.271.

## Cost sensitivity (walk-forward, out-of-sample)

| Cost per side | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| 0 bp | 1.214 | +1045.9% | +53.7% | 45.5% | 1.29 |
| 10 bp | 1.208 | +968.1% | +51.8% | 46.2% | 1.25 |
| 30 bp | 1.191 | +802.6% | +47.4% | 47.5% | 1.18 |
| 60 bp | 1.174 | +681.1% | +43.7% | 52.2% | 1.10 |

## True Markets confirmation (fixed 90-day rule, no volume screen)

True Markets daily history 2023-09-28 .. 2026-09-27; compared 2025-04-01 .. 2026-08-31, the first day the True Markets run could hold a position to the last day both sources cover.

| Series | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| TM prices, 8 assets (adds HYPE, VVV) | 1.288 | +146.3% | +88.7% | 33.7% | 1.62 |
| TM prices, same 6 assets as Binance | 1.028 | +7.9% | +5.5% | 45.4% | 0.33 |
| Binance prices, 6 assets | 1.459 | +314.2% | +172.2% | 19.8% | 2.01 |

## Caveats

- Universe chosen with hindsight by Minara; the tests above cannot measure that bias.
- Costs are assumptions until True Markets' DeFi fee schedule and RFQ markups are known (D-5).
- Daily closes only: fills at the next close ignore intraday slippage.
- Random-walk null ignores fat tails and cross-asset correlation; the permutation nulls keep both.
- The grid (20..180) was fixed before any result was seen; a best value on its edge is reported, not chased by widening the grid afterwards.
- Runtime 71 s with 15 workers.
