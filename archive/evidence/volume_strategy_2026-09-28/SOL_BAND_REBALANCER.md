# Band rebalancer (volume strategy) on SOLUSDT hourly closes (2026-09-28)

Binance archive (checksum-verified), 2023-01-01 .. 2026-08-31, 32135 hours (3.67 years), 1 gaps in the hourly series. 50/50 SOL/USDC, rebalanced to 50% when the share leaves 50% +/- band. Un-rebalanced 50/50 over the period: +465.8%.

Volume for a $20 stake = stake x yearly volume multiple.

| Band | Cost/side | Trades/yr | Volume multiple/yr | Volume/yr on stake | Cost drag/yr | Total return | vs un-rebalanced | Volume per $1 cost |
|---|---|---|---|---|---|---|---|---|
| 0.5% | 1 bp | 854 | 23.6x | $471 | 0.24% | +352.5% | -20.0% | $10,000 |
| 0.5% | 5 bp | 855 | 23.5x | $469 | 1.17% | +348.7% | -20.7% | $2,000 |
| 0.5% | 10 bp | 855 | 23.3x | $466 | 2.33% | +343.8% | -21.6% | $1,000 |
| 0.5% | 30 bp | 854 | 22.7x | $454 | 6.81% | +324.7% | -24.9% | $333 |
| 1.0% | 1 bp | 305 | 14.6x | $292 | 0.15% | +355.0% | -19.6% | $10,000 |
| 1.0% | 5 bp | 305 | 14.6x | $291 | 0.73% | +352.5% | -20.0% | $2,000 |
| 1.0% | 10 bp | 305 | 14.5x | $290 | 1.45% | +349.5% | -20.6% | $1,000 |
| 1.0% | 30 bp | 305 | 14.3x | $286 | 4.28% | +337.4% | -22.7% | $333 |
| 2.0% | 1 bp | 94 | 8.2x | $165 | 0.08% | +357.4% | -19.2% | $10,000 |
| 2.0% | 5 bp | 94 | 8.2x | $164 | 0.41% | +355.9% | -19.4% | $2,000 |
| 2.0% | 10 bp | 94 | 8.2x | $164 | 0.82% | +354.2% | -19.7% | $1,000 |
| 2.0% | 30 bp | 94 | 8.1x | $163 | 2.44% | +347.2% | -21.0% | $333 |
| 5.0% | 1 bp | 17 | 3.3x | $66 | 0.03% | +354.7% | -19.6% | $10,000 |
| 5.0% | 5 bp | 17 | 3.3x | $66 | 0.16% | +354.1% | -19.7% | $2,000 |
| 5.0% | 10 bp | 17 | 3.3x | $66 | 0.33% | +353.4% | -19.9% | $1,000 |
| 5.0% | 30 bp | 17 | 3.3x | $66 | 0.98% | +350.5% | -20.4% | $333 |

## Is the rebalancing premium more than noise?

Rebalancing earns a premium even on a random walk (volatility harvesting), so the null is the same hourly moves in shuffled order. 200 shuffles per band, cost-free; p = share of shuffles with a premium at least as large.

| Band | Real premium (log) | Null median | p |
|---|---|---|---|
| 0.5% | -0.2212 | -0.2191 | 0.711 |
| 1.0% | -0.2165 | -0.2194 | 0.343 |
| 2.0% | -0.2120 | -0.2190 | 0.279 |
| 5.0% | -0.2182 | -0.2175 | 0.502 |

## Caveats

- Hourly closes miss intra-hour crossings: live volume at the same band would be higher.
- Costs are assumptions until the True Markets markup is measured (tools/measure-markup.py).
- A shuffled-null p-value > 0.05 means the premium is ordinary volatility harvesting, not exploitable mean reversion; the strategy then pays its costs from volatility alone.
- Runtime 33 s.
