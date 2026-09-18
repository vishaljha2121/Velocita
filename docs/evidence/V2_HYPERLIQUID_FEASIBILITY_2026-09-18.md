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

## Restricting True Markets to the hypercore assets

The tradable set is TWO:

| symbol | TM `decimals` | HL `weiDecimals` | HL `szDecimals` | HL pair |
|---|---:|---:|---:|---|
| PURR | 5 | 5 | **0** | `PURR/USDC`, canonical |
| KNTQ | 8 | 8 | **2** | `@334` KNTQ/USDC, non-canonical |

(`USDC` on `hypercore-spot` is not tradeable; the `hypercore-perp` USDC entry is
the settlement token.)

### TRUE MARKETS' `decimals` IS NOT THE LOT SIZE

This is the finding, and it would have been expensive to discover by having
orders rejected.

True Markets publishes one `decimals` per asset and it matches Hyperliquid's
`weiDecimals` exactly - the TOKEN's precision. It is NOT `szDecimals`, which is
the ORDER SIZE increment and differs for both assets:

* PURR has `weiDecimals` 5 but `szDecimals` **0** - orders must be in WHOLE PURR.
  A reader of True Markets' catalog alone would conclude 0.00001 PURR is
  tradable.
* KNTQ has `weiDecimals` 8 but `szDecimals` **2** - orders round to 0.01.

So `VenueInstrumentSpec.lot_base_atoms` cannot be derived from True Markets'
catalog at all. Taking `decimals` as the lot would produce sizes the venue
rounds or rejects, and for PURR it would be wrong by five orders of magnitude.

### So yes - and Hyperliquid gives more than market data

For these two assets Hyperliquid supplies BOTH the book and the ORDER
CONSTRAINTS that True Markets does not publish. That is not "an external data
source"; it is the venue True Markets routes to, read directly.

THE ASSUMPTION THAT REMAINS, and it must stay marked as one: these are
HYPERLIQUID's constraints, and whether True Markets' gateway passes them through
unchanged - rather than applying its own rounding or minimum - is unverified. It
is a far better grounded assumption than a guess, and question 1 in
`QUESTIONS_FOR_TRUEMARKETS.md` is exactly how to settle it.

### What the universe looks like in practice

`PURR/USDC` is the only canonical pair of the two, and its book is real: twenty
levels a side, around 0.128 USDC, resting sizes of a few hundred to a thousand
PURR - so roughly 25 to 130 USDC a level. Against a 20 USDC budget that is a
workable book rather than a dead one.

KNTQ trades as `@334` against USDC and `@254` against USDH; both non-canonical.

## Which instrument, measured rather than assumed

The universe is small only THROUGH TRUE MARKETS. True Markets lists three
hypercore assets; Hyperliquid spot has 328 pairs, including 23 tokenised
equities and commodities - SpaceX, Tesla, Apple, Google, SPY, QQQ, gold, silver.

But listed is not tradable. Top-of-book, measured 2026-09-18:

| pair | spread | top-5 depth (bid/ask) | levels |
|---|---:|---|---|
| **HYPE/USDC** | **1.4 bps** | $12.0k / $34.6k | 20 / 20 |
| PURR/USDC | 26 bps | $1.4k / $12.6k | 20 / 20 |
| SPCX (SpaceX) | 3,068 bps | $64 / $209 | 20 / 3 |
| TSLA | 14,272 bps | $86 / $662 | 17 / 9 |

THE TOKENISED EQUITIES ARE NOT MARKETS. A 30% spread on SpaceX with sixty-four
dollars of depth, and a 143% spread on Tesla, are books with nothing in them.
Quoting there is not market making; it is being the only participant, and a 20
USDC order would be a meaningful fraction of the entire visible book.

HYPE IS THE OUTLIER AND THE OBVIOUS TARGET. A 1.4 bps spread with twelve to
thirty-five thousand dollars in the top five levels is a real, contested market -
the only one of the four where a passive quote competes rather than defines the
price. It is also NOT AVAILABLE ON TRUE MARKETS at all, which is a further
argument for trading Hyperliquid directly rather than through a gateway that
lists three of its assets.

### The contract holds both candidates today

| | szDecimals | lot | price | sig-fig increment | tick (micros) | `quote_micros_per_tick_lot` |
|---|---:|---|---|---|---:|---:|
| HYPE | 2 | 0.01 HYPE | 91.446 | 0.001 | 1000 | **10** |
| PURR | 0 | 1 PURR | 0.12809 | 0.00001 | 10 | **10** |

Both land on exactly 10, both exact. So the existing
`InstrumentExecutionProfile` can carry either instrument at today's prices; what
it cannot do is follow them across a decade boundary, which is what the
significant-figure rule in the profile is for.

## Why the Phase 9 seam matters now

`VenueTransport` exists so the gateway cannot tell which venue it is talking to.
That was built before this reframing and is what makes two adapters - a True
Markets one for the demonstration, a Hyperliquid one for trading - a matter of
two implementations behind one seam rather than a fork in the gateway.
