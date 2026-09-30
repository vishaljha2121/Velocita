# True Markets public market data — first live read, 2026-09-28

Owner approval (CP-059): GET-only, credential-free calls; order sends mocked.
All calls below went through `model`'s `ReadOnlyHttp` (GET only, state-changing
path fragments refused before a socket opens) from the owner's Mac. No keys,
no POST, no `/orders`.

| Call | Result |
|---|---|
| `GET /v1/conductor/assets?venue=defi` | 200, 325,801 B, 210 spot assets (204 tradeable, 210 active); chains solana 112, base 73, robinhood 20, hypercore-spot 3, hypercore-perp 1, arbitrum 1; `pagination.next_cursor` null. `market_data` holds only supply figures, no prices. |
| `GET /v1/conductor/assets?venue=cefi` | 200, 8,168 B, 3 spot assets, chain null |
| `GET /v1/defi/market/prices?symbol=SOL` | 200 in 228 ms: OHLC for intervals 30s, 1h, 4h, 24h. **No timestamp in the response.** |
| `GET /v1/defi/market/prices/range?symbol=SOL&window=1h` | 200: min/max with ISO-8601 UTC timestamps and `asset_id` |
| `GET /v1/defi/market/prices/history?symbol=SOL&window=10m&resolution=1m` | 200: points `{t, price}` at 1-minute resolution |

Docs (`/apis/marketdata/market-data-service-api`, read 2026-09-28): market data is
public, "no authentication or API key"; numbers are strings to keep precision;
DeFi WebSocket `/v1/defi/market` has channels priceCandles, trendingAssets,
surgingAssets, referencePrices, orderbook.

What this does NOT establish:
- These are reference prices (candles), not executable quotes: no bid/ask, no
  depth, no fee. Executable prices come from `POST /v1/conductor/quotes` (RFQ),
  which is outside the GET-only approval.
- The `/prices` candle has no venue timestamp, so its staleness cannot be
  measured from the payload; `history` points and `range` do carry timestamps.
- Rate limits remain undocumented (D-7).

WebSocket (same day, `model` `cli stream`, stdlib RFC 6455 client, read-only):
subscribe accepted every topic; received reference_prices, price_candles,
trending_assets and surging_assets; NO orderbook message on any topic in 25-45 s.
A 40 s recording stored 59 filtered candle messages (318 KiB) and 30 reference
messages; about 0.66 GiB/day with the default watch list.

Files: three sample payloads (SOL) and `catalog.sha256` (the catalog itself is
not committed; re-fetch and compare digests).
