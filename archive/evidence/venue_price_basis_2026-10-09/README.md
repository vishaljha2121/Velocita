# SOL reference-price basis: corrected close-availability comparison

The October 9 review found that `basis.py` labeled a Binance candle close with its opening
second. The corrected code uses `(close_time_ms + 1) // 1000`, matching the running strategy.
The same True Markets history window was rechecked on October 9. This corrects a timing
measurement; it does not introduce or optimize a trading strategy.

## Inputs and reproducibility

- True Markets SOL reference-price history: `true_markets_sol_1d_5s.json`, unchanged, 10,108
  points covering October 8 05:55 through October 9 05:55 UTC.
- Binance SOLUSDT one-second closes: fetched again with public keyless GET requests, retained
  in `/Users/vishaljha/.cache/velocita-review/continuation/binance-close-available.json`.
  The cache contains 87,000 closes, including fetch padding outside the comparison window.
- `close_available_evidence.json` records hashes of the inputs, script and corrected output.
- `result_close_available.txt` is the corrected output. The original `result.txt` is retained
  as historical evidence of the opening-time comparison; it is superseded for interpretation.
- Cache format `close_available_v1` records the timestamp convention and requested interval.
  The script refuses the older, ambiguous cache format instead of silently reusing it.

```sh
python3 -B docs/evidence/venue_price_basis_2026-10-09/basis.py \
  docs/evidence/venue_price_basis_2026-10-09/true_markets_sol_1d_5s.json \
  "$HOME/.cache/velocita-review/continuation/binance-close-available.json"
```

With that cache present, reproduction makes no network requests. Both original sources are
public; no quote, signing or order call was made.

## Corrected results

| Measure | Result |
|---|---|
| Mean venue/Binance basis at equal availability timestamps | -9.39 bp |
| Standard deviation around that basis | 2.43 bp |
| 5th / 95th percentiles | -12.96 / -5.36 bp |
| Hourly mean range | -10.66 to -7.37 bp |
| Highest five-second return correlation among tested shifts | 0.551 at zero seconds |
| Correlation at -1 / +1 second | 0.544 / 0.535 |
| Coverage | 9,109 regular five-second intervals; 998 longer intervals, up to 55 seconds |

The earlier claim that the venue follows Binance by 1–2 seconds is not supported by the
corrected timestamps. A shallow peak near zero does not identify an exact latency, establish
causality, or measure network delivery time. A correlation of 0.551 is not "half the move";
its square is about 0.304 in a simple contemporaneous linear regression, with no implication
of tradable predictability.

## Execution and cost limits

Reference prices are not executable quotes. USDT/USDC differences, asynchronous observations
and changing basis can affect fills and signals. A constant relative offset can cancel in
percentage returns, but this study does not establish constant basis, independent errors
between legs, or round-trip slippage. It also does not test the strategy on venue data.

Paper charges 20 bp per side. The October 5 finalized CLI/API-key SOL sell measured about
20 bp for that route/account/side/time; the buy fee and complete execution cost are still
unverified. Historical 100/98 bp quotes remain a separate stress scenario, not a reason to
label this route as necessarily charging 1%. See
`../venue_contract_2026-10-05/SETTLED_SOL_SELL_FEE.md`.

This history endpoint is not a continuous one-second feed. Gaps in its five-second sampling
prevent it from being a direct replacement for the current strategy input. Recorded live
venue-feed coverage and a matching execution contract remain necessary.
