# True Markets execution cost (D-5) — measured 2026-09-28 15:21 UTC

`tools/measure-markup.py` (read-only: login + `get_quote` only; nothing signed or
sent) on a second, unfunded account (the owner's usual address had stopped
receiving login codes). Quotes returned with an "Insufficient balance" issue but
complete. `quotes.json` keeps the numbers; unsigned transaction payloads, the
signing organization id and wallet address are omitted.

| Buy SOL with | Fee (USDC) | SOL out | All-in vs TM 30s ref | All-in vs Binance 1s | Route price after fee vs Binance |
|---|---|---|---|---|---|
| $25  | 0.25 (1.00 %) | 0.21027964  | +97.3 bp | +96.8 bp  | -4.2 bp |
| $100 | 1.00 (1.00 %) | 0.841142519 | +97.1 bp | +102.5 bp | +1.4 bp |
| $500 | 5.00 (1.00 %) | 4.208386269 | +90.6 bp | +99.5 bp  | -1.5 bp |

FINDINGS
1. The cost is a flat 1 % fee on notional, charged in USDC (fee_asset = USDC
   mint), identical at $25, $100 and $500. The route price after the fee is at
   market (within about +/-5 bp of Binance at the same second). All-in: about
   100 bp per side. The web app shows "Fees $0.00" on its display conversion;
   the fee appears only in a routed quote.
2. Quotes carry a `quote_id` and an unsigned Solana transaction for the account's
   custodial signer: executing a quote means signing that payload. This package
   never calls a signing or submit tool.
3. Sell-side cost not measured; assumed symmetric until quoted.

CONSEQUENCES
- Volume strategy: volume ~= loss budget / cost per unit volume = budget / 1 %.
  A $20 loss buys about $2,000 of volume; $100 buys $10,000. Buying
  leaderboard volume at the retail fee is not viable unless the reward is large
  or a lower fee tier applies (volume tiers / maker rebates were advertised only
  on the CeFi trading-firm path, D-4).
- Any return strategy must clear about 2 % per round trip: only low-turnover
  strategies with a large per-trade edge can work on this path.

## Buy AND sell, SOL (Solana) and cbBTC (Base) — 2026-09-28 16:31 UTC

`quotes_buy_sell_1631utc.json`: 12 quotes (2 assets x $10/$25/$100 x buy/sell);
sells sized in base units from the True Markets reference price, never from a
quote. All-in cost per side vs the True Markets 30 s reference (mean of the
reference fetched before and after each quote):

| Asset | Fee | Buy all-in | Sell all-in | Round trip |
|---|---|---|---|---|
| SOL (solana) | 1.00 % USDC, both sides | +99.5 / +99.8 / +101.7 bp | +97.2 / +99.1 / +98.2 bp | ~197-201 bp |
| cbBTC (base) | 1.00 % USDC, both sides | +114.1 / +107.9 / +107.7 bp | +122.5 / +123.7 / +124.1 bp | ~230-238 bp |

The fee is flat across sizes and sides. On Base the route adds 8-24 bp over
the fee (sell side worse); SOL's route is at market. These per-asset, per-side
figures are the cost model of every backtest from here on (no assumed costs).
