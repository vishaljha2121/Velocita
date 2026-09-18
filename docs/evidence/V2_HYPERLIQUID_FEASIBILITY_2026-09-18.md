# Hyperliquid as a venue — first read, 2026-09-18

Prompted by the project owner's reframing: True Markets is a DEMONSTRATION
target, and serious trading is intended on Hyperliquid, because True Markets is
not yet mature enough to depend on for support.

Read-only. `POST /info` only, which is a query endpoint. No order endpoint was
called and no credential was used or needed.

## Everything D-12 blocks on is published here

`POST https://api.hyperliquid.xyz/info {"type":"spotMeta"}` returns **200 with no
authentication**:

- **501 tokens**, each carrying `szDecimals` and `weiDecimals`
- **328 spot pairs**, e.g. `{"tokens":[1,0],"name":"PURR/USDC","index":0}`

```
USDC  szDecimals 8  weiDecimals 8
PURR  szDecimals 0  weiDecimals 5
HFUN  szDecimals 2  weiDecimals 8
```

`szDecimals` is the size increment. That is the LOT the adapter needs and True
Markets publishes nowhere (D-12, D-14).

## The price rule, confirmed on live data rather than from the manual

`POST /info {"type":"l2Book","coin":"PURR/USDC"}`, 20 levels a side:

```
bids  0.12809  0.12808  0.12788  0.12782  0.12778
asks  0.12835  0.12863  0.12869  0.12873  0.12876
```

Every price is a STRING. Across the book, prices carry 2 to 5 significant
figures and 2 to 5 decimal places - the decimal count VARIES while the
significant-figure count is capped at five. That is exactly the documented rule
("up to 5 significant figures, no more than `MAX_DECIMALS - szDecimals` decimal
places"), now observed rather than assumed.

Sizes are `1156.0`, `279.0`, `222.0` - whole units, matching PURR's
`szDecimals: 0`.

SO HYPERLIQUID'S DOCUMENTATION IS ACCURATE, which is itself the finding worth
recording after a week of True Markets' not being.

## What this promotes and what it demotes

DEMOTED: D-12, D-14, D-18 and D-19 - tick, lot, and where to get market data -
stop being blockers on a Hyperliquid venue. They remain open for True Markets.

PROMOTED: D-15 becomes the CENTRAL problem rather than a footnote. The venue's
real price rule is significant figures, and `InstrumentExecutionProfile` carries
a single `quote_micros_per_tick_lot` while `VenueInstrumentSpec` carries a single
`tick_quote_micros`. Neither can express "five significant figures". A cached
tick is correct only while price stays inside one decade, and the live book above
spans three decimal-place counts in twenty levels.

That is now a contract question on the critical path, not a lookup we are waiting
on.

## What does NOT follow

USING HYPERLIQUID DATA TO QUOTE ON TRUE MARKETS IS ONLY SOUND FOR THE
`hypercore-*` ASSETS. For those, True Markets routes to Hyperliquid and it is the
SAME BOOK - not an external proxy. For the 201 `solana`, `base`, `robinhood` and
`arbitrum` assets, True Markets routes elsewhere, and a Hyperliquid price has no
defined relationship to where the order would actually rest. Quoting one venue's
book from another's prices is not a data source; it is an assumption with money
behind it.

PERPETUALS ARE STILL EXCLUDED. Hyperliquid is primarily a perpetuals venue and
ADR 0004 stands: V2 cannot represent a position change it did not authorize.
Hyperliquid SPOT - 328 pairs - is available and is not affected by that decision.
Trading Hyperliquid perps seriously means reopening ADR 0004 and building the
unsolicited-position-change observation kind it already names as the recommended
design.

## Why the Phase 9 seam matters now

`VenueTransport` exists so the gateway cannot tell which venue it is talking to.
That was built before this reframing and is what makes two adapters - a True
Markets one for the demonstration, a Hyperliquid one for trading - a matter of
two implementations behind one seam rather than a fork in the gateway.
