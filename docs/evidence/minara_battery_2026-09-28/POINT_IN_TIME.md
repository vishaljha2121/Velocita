# Minara rotation on a point-in-time universe (2026-09-28)

Candidates: 656 Binance USDT spot pairs ever archived (stablecoins, fiat, leveraged, wrapped and gold tokens excluded by a rule fixed in advance). Each month's universe = top 10 by the previous month's quote volume, plus BTC for the regime filter.
110 coins were ever in the universe; 14 of them no longer trade at the end of the data (BCC, BCHABC, BCHSV, BTT, EOS, ERD, FTM, HIFI, HOOK, LOOM, LRC, MATIC, SXP, YFII). A held coin that stops trading is sold at its last close.
Source: Binance public market-data API (TLS; no published checksum). Daily bars 2018-01-01 .. 2026-08-31. In-sample to 2023-07-31.

Most frequent members by year: 2018: BTC, ETH, BCC, NEO, BNB, LTC, ADA, XRP, EOS, TRX, QTUM, ONT; 2019: BTC, BNB, ETH, XRP, EOS, BCHABC, TRX, LTC, LINK, BTT, ADA, BCHSV; 2020: BTC, BNB, ETH, XRP, BCH, LINK, LTC, EOS, ETC, TRX, ADA, VET; 2021: BTC, ETH, BNB, XRP, ADA, DOT, DOGE, SOL, MATIC, LTC, BCH, LINK; 2022: BTC, BNB, SOL, ETH, XRP, LUNA, AVAX, GMT, DOGE, SHIB, MATIC, ADA; 2023: BTC, BNB, SOL, ETH, XRP, DOGE, LTC, MATIC, ARB, LINK, APT, SHIB; 2024: BTC, BNB, SOL, ETH, XRP, DOGE, PEPE, WIF, AVAX, SUI, 1000SATS, NOT; 2025: BTC, BNB, SOL, ETH, XRP, DOGE, SUI, PEPE, TRUMP, TRX, ADA, XPL; 2026: BTC, BNB, SOL, ETH, XRP, ZEC, DOGE, SUI, TAO, CHIP, NEAR, TRX

## 1. In-sample optimization

momentum_days → profit factor: 20: 0.995, 30: 1.012, 40: 1.003, 50: 0.991, 60: 0.978, 70: 0.979, 80: 0.998, 90: 1.007, 100: 1.004, 110: 1.001, 120: 1.009, 130: 1.035, 140: 1.019, 150: 0.968, 160: 0.998, 170: 1.015, 180: 1.009

Best: **130 days**, profit factor 1.035 (Minara's 90: 1.007).

| Series | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| strategy, 130d, in-sample | 1.035 | +54.7% | +8.1% | 46.4% | 0.39 |
| BTC buy & hold, in-sample | 1.031 | +118.5% | +15.0% | 81.2% | 0.56 |

## 2. In-sample permutation test

1000 permutations, each fully re-optimized. **p = 0.933**. null median 1.144, 95th pct 1.295, max 1.435; real 1.035.
Low p (< 0.05): the optimized in-sample score is rarely matched by an optimizer run on shuffled history. High p: the optimizer finds this much 'edge' in noise.

## 3. Walk-forward (3-year train, 91-day step)

Out-of-sample 2020-12-31 .. 2026-08-31. Chosen windows: 2020-12-31: 140, 2021-04-01: 40, 2021-07-01: 40, 2021-09-30: 30, 2021-12-30: 30, 2022-03-31: 20, 2022-06-30: 20, 2022-09-29: 20, 2022-12-29: 20, 2023-03-30: 20, 2023-06-29: 120, 2023-09-28: 120, 2023-12-28: 170, 2024-03-28: 170, 2024-06-27: 170, 2024-09-26: 170, 2024-12-26: 20, 2025-03-27: 20, 2025-06-26: 20, 2025-09-25: 20, 2025-12-25: 20, 2026-03-26: 20, 2026-06-25: 20

| Series | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| walk-forward strategy | 1.062 | +135.5% | +16.3% | 66.4% | 0.55 |
| fixed 90d (Minara) same dates | 1.032 | +64.1% | +9.1% | 64.3% | 0.42 |
| BTC buy & hold same dates | 1.048 | +172.1% | +19.3% | 76.6% | 0.59 |

Minara's headline window 2023-08-01 .. 2025-08-01 inside our walk-forward: walk-forward, 1.119, +95.3%, +39.6%, 39.3%, 0.92

## 4. Walk-forward permutation test

200 permutations of the data after the first training window. **p = 0.562**. null median 1.077, 95th pct 1.203, max 1.284; real 1.062.

## 5. Random-walk test

1000 Gaussian random-walk histories (each asset's own drift and volatility, no correlation), fully re-optimized. **p = 0.974**. null median 1.159, 95th pct 1.292, max 1.486; real 1.035.

## Cost sensitivity (walk-forward, out-of-sample)

| Cost per side | Profit factor | Total | CAGR | Max DD | Sharpe |
|---|---|---|---|---|---|
| 0 bp | 1.081 | +201.6% | +21.5% | 62.4% | 0.64 |
| 10 bp | 1.074 | +176.4% | +19.6% | 63.9% | 0.61 |
| 30 bp | 1.062 | +135.5% | +16.3% | 66.4% | 0.55 |
| 60 bp | 1.043 | +81.0% | +11.0% | 68.7% | 0.45 |

## Caveats

- Membership is point-in-time by volume; permutations shuffle prices but keep each month's membership, so the nulls test timing within the same monthly universes.
- Selling a delisted coin at its last close is generous for collapses (LUNA-style).
- Costs are assumptions (30 bp per side) until True Markets fees are known; True Markets does not list most of these coins, so this tests the IDEA, not a tradable True Markets strategy.
- Runtime 171 s with 15 workers.
