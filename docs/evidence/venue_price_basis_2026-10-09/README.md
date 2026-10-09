# How far is the venue's SOL price from the price the strategy decides on?

Measured 2026-10-09 for live promotion plan item 13 (checkpoint review B5: "the price and the cost
are not the venue's"). One day of data, 2026-10-08 05:55 to 2026-10-09 05:55 UTC.

## What was compared

- **Venue:** True Markets' public reference price for SOL, from
  `GET https://api.truemarkets.co/v1/defi/market/prices/history?symbol=SOL&window=1d&resolution=5s`.
  No key. 10,108 points. Stored here as `true_markets_sol_1d_5s.json`.
- **Decision price:** Binance SOLUSDT one-second closes, the series the paper strategy reads.
  Public, no key, 86,705 bars, fetched by `basis.py` and not stored (it can be fetched again).

Both are GET calls with no credential. Nothing was quoted, signed or sent to an order path.
`python3 basis.py true_markets_sol_1d_5s.json` reproduces `result.txt`.

## Results

| Question | Answer |
|---|---|
| Level | The venue's price is 9.4 bp below Binance on average. Hour by hour the mean stays between 7.4 and 10.6 bp below. |
| Noise around that level | Standard deviation 2.5 bp. 90% of points lie between 5.4 and 13.0 bp below. 0.5% are more than 10 bp from the mean. |
| Timing | The venue follows Binance by about 1 to 2 seconds. The 5-second returns match best with Binance taken 1 to 2 s earlier (correlation 0.55), and the match falls off faster when Binance is taken later. |
| How alike the moves are | Half. At 5 seconds the correlation is 0.54, so about half of a venue move at that scale is not in Binance's. |
| Coverage | The public history is not continuous at 5 s. 998 of 10,107 intervals were longer than 5 s (up to 55 s), which leaves 47% of the day without 5-second points. |

## What it means for the paper strategy

- **The price source is a small error.** A constant 9 bp offset cancels over a round trip, because
  both legs carry it. What does not cancel is the noise, about 2.5 bp a leg, so about 3.5 bp a
  round trip. Paper prices are rounded to one cent, which is 0.9 bp at $110.
- **The cost assumption is the large one.** Paper charges 20 bp a side, 40 bp a round trip. The
  measured cost is about 100 bp to buy and 98 bp to sell, 198 bp a round trip. That gap is 45 times
  the price-source noise.
- **The signal would differ a little on the venue's own series.** The stochastic rule looks at where
  the price sits in its 15-minute range. A 2.5 bp disagreement is a few percent of a typical
  15-minute range, enough to move a crossing by a bar or to flip a marginal one, not enough to
  change the character of the signal. This is an estimate, not a test: the test is to replay the
  rule on a recorded venue series.
- **The strategy cannot simply switch to the venue's public history.** It needs a continuous
  one-second series and 20 minutes of warm-up, and the history endpoint has no 5-second points for
  half the day. The venue's WebSocket reference prices, which nuc1 already records, are the
  candidate; their coverage was not measured here.

## What is still unknown

- The executable price. A reference price is not a quote. What a live order pays is the venue's
  quote, which comes from a request-for-quote call that is outside the keyless-GET approval. The
  100 bp figure comes from the owner's earlier quote capture, not from this measurement.
- Whether the 1 to 2 second lag is stable. One day is one sample.
- Whether the basis behaves in a fast market. The day's range was 953 bp, with no sharp move tested
  separately.

## Limits of this measurement

- One day, one asset.
- The venue's points are compared with the Binance close of the same second. The venue's own
  timestamps are whole 5-second marks, so timing finer than about a second cannot be read from it.
- Binance quotes in USDT and the venue in USDC. Part of the 9 bp is the difference between the two
  dollar tokens, and it can move.
