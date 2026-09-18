# True Markets documentation discrepancies

A running record of places where `docs.truemarkets.co` is contradictory, absent,
or has changed under us - and what each one cost.

WHY THIS FILE EXISTS. Every row below changed a decision or blocked one. Kept
together they answer a question that is otherwise unanswerable six weeks later:
"why is this built the way it is?" The answer is usually "because the venue did
not say, and guessing was the more expensive option."

RULE THIS FILE ENFORCES BY EXISTING: a value we inferred must never become
indistinguishable from one the venue documented. Anything below marked INFERRED
or UNVERIFIED must stay that way in code and config until the venue says
otherwise.

---

## D-1. Limit orders: quickstart contradicts the endpoint reference

| | |
|---|---|
| Status | RESOLVED against the endpoint reference |
| First seen | 2026-09-15, re-confirmed 2026-09-17 |

The quickstart says:

> "Limit orders are supported only on the CeFi assets returned by this endpoint"

The endpoint reference describes cancelling "a resting DeFi limit order", lists
"Execute a resting DeFi limit order cancellation" as its own operation with its
own signing step, and documents an `active` status meaning "limit order resting
in order book".

WHAT WE DID. Believed the endpoint reference. A page that documents the
cancellation of a thing is stronger evidence that the thing exists than a summary
sentence on a quickstart page.

WHAT IT COST. The project's entire premise. If the quickstart were right, a
passive market-making strategy on DeFi would be impossible and the correct output
would have been a venue-level NO-GO.

---

## D-2. The instrument type changed under us

| | |
|---|---|
| Status | OPEN - instrument type is now UNVERIFIED |
| First seen | 2026-09-17 |

On 2026-09-15 `GET /positions` read "Query a user's open **Hyperliquid perpetual**
positions". On 2026-09-17 the same endpoint reads "Returns a read-only list of
the authenticated user's open positions". Hyperliquid and "perpetual" appear on
none of the pages re-read; the quickstart now frames the worked example as spot.

WHAT WE DID. Amended ADR 0004 rather than withdrawing it. Its REASONING about
what V2 cannot represent is untouched and still correct for any perpetual
surface; what was withdrawn is the claim that the DeFi surface is VERIFIED
perpetual. Instrument type is recorded as unverified, NOT as spot - absence of a
word is not evidence.

RESOLVED 2026-09-17 BY THE CATALOG. `GET /v1/conductor/assets` reports
`type: spot` on all 210 DeFi and all 3 CeFi assets. That is positive evidence
from a field rather than the absence of a word, so instrument type moves from
UNVERIFIED to VERIFIED SPOT. See `evidence/V2_TRUEMARKETS_CATALOG_2026-09-17.md`.

WHAT IT COST. Two days of Phase 9 planning aimed at an execution target that ADR
0004 forbade, and a near-miss: the block was discovered by the project owner
noticing the docs disagreed with our own evidence file, not by us.

LESSON RECORDED. A venue's documentation is not a stable input. Verification has
a DATE, and a decision resting on it inherits that date.

---

## D-3. The create-order request schema does not render

| | |
|---|---|
| Status | RESOLVED 2026-09-18 by the MCP server's own tool schema. See D-22. |
| First seen | 2026-09-17 |

`/apis/retail/create-order/` shows only the endpoint line - "POST /orders -
Creates a new order. For DeFi orders, returns unsigned payloads for client
signing". The request body schema does not render; it may sit behind an
expandable section the fetch does not execute.

CONSEQUENTLY UNKNOWN: the price field name, and whether `qty_unit` accepts a
base-denominated quantity.

WHAT WE DID. `VenueOrderSchema` carries a `verified` flag and
`encode_limit_order_body` REFUSES without it.

WHY A GUESS WOULD BE WORSE THAN THE BLOCK. Many APIs ignore unrecognised fields.
A guessed price field that the venue drops turns a limit order into a MARKET
order - crossing the spread, paying taker fees on every quote, inverting the
maker economics, and reporting success. Nothing on our side detects it
afterwards.

HOW TO CLOSE IT. Open the page in a browser where the expander runs, or read the
schema from an authenticated OPTIONS/GET if one is offered. Not by trying a
field name against the live endpoint.

---

## D-4. Post-only is not documented for DeFi

| | |
|---|---|
| Status | OPEN - economics unverified |
| First seen | 2026-09-15 |

The exchange overview describes a central limit order book with price-time
priority and does not mention post-only. Nothing scopes post-only to DeFi either
way.

WHAT WE DID. `ActionTemplate::post_only` is a frozen Phase 5 contract and stays.
The gateway refuses to send an effect a venue cannot represent rather than
approximating it, so a post-only effect against a venue lacking the capability is
a refusal and not a silent taker order.

WHAT IT COSTS IF ABSENT. A "maker" quote that crosses pays taker fees, which
inverts the expected edge. The cost is bounded here only because the intended
quote size is small.

---

## D-5. No DeFi fee schedule; gas is sponsored (operator report)

| | |
|---|---|
| Status | PARTLY RESOLVED - gas answered, fees still open |
| First seen | 2026-09-15; updated 2026-09-18 |

The published fee schedule is scoped to CeFi. No maker/taker schedule is
published for DeFi.

GAS IS SPONSORED BY TRUE MARKETS, reported by the project owner 2026-09-18.
UNVERIFIED in documentation - it appears nowhere on the pages read - and recorded
here as an operator report rather than a venue statement, per D-8's precedent
where the owner's recollection proved better than the docs.

WHAT THAT CHANGES, AND IT IS NOT SMALL. Gas was the term most likely to
disqualify the strategy: at a 20 USDC notional, one cent of gas is 5 bps and ten
cents is 50 bps against a measured 26 bps spread on PURR. Sponsorship removes the
per-fill FIXED cost entirely, which means there is no longer a minimum viable
order size imposed by cost. Small-notional, high-turnover quoting becomes
economically possible rather than arithmetically dead.

WHAT REMAINS OPEN is the fee schedule, which is now the ONLY unknown cost term.
It is also the one that decides whether turnover is cheap or self-defeating: a
10 bps taker fee on both legs costs 20 bps of a 26 bps spread, while a zero or
rebated maker fee makes volume generation nearly free. Question 4 in
`QUESTIONS_FOR_TRUEMARKETS.md` is now the single highest-value thing to ask.

---

## D-6. Increment sizes are published for CeFi only, and are price-dependent

| | |
|---|---|
| Status | OPEN |
| First seen | 2026-09-15 |

Nine tick buckets by price range are published for CeFi, with a minimum notional
of 1.00 in every bucket. No DeFi table is published.

TWO CONSEQUENCES. `InstrumentExecutionProfile` carries ONE tick size, so a cached
profile goes stale when the price crosses a bucket boundary - a real staleness
hazard, not a theoretical one. And the adapter's `VenueInstrumentSpec` derives
its tick from the same source, so a bucket crossing would break
`spec_agrees_with_profile` and stop sending. That refusal is the correct
behaviour and also a live operational risk to plan for.

---

## D-7. Market-data mechanics are partly undocumented

| | |
|---|---|
| Status | OPEN |
| First seen | 2026-09-15 |

A DeFi WebSocket `/v1/defi/market` with an `orderbook` channel is documented.
Not documented: sequence numbers or checksums for gap detection, public trade
prints, stated trade direction, venue event timestamps, and rate limits.

WHAT IT COSTS. Without sequence numbers, gaps are invisible - and invisible gaps
become "results". Without rate limits, collector cadence is a guess that risks a
ban.

---

## D-8. Operator report not present in the documentation

| | |
|---|---|
| Status | UNVERIFIED - recorded, not relied upon |
| First seen | 2026-09-17 |

The project owner reports that the DeFi surface settles on **Base** and
**Hyperliquid**, and that the catalog includes **Robinhood tokens**. None of this
appears in the documentation as read on 2026-09-17.

CONFIRMED 2026-09-17, ALL THREE. The catalog reports chains
`solana` (112), `base` (73), `robinhood` (20), `hypercore-spot` (3),
`hypercore-perp` (1) and `arbitrum` (1). The owner's recollection was better than
the documentation.

AND IT IS WORSE THAN A TWO-CHAIN SPLIT: there are SIX chain labels under one
`venue=defi`, so the adapter cannot treat "defi" as one venue with one gas
regime. `hypercore-perp` holds exactly one asset - USDC, the settlement token,
itself typed `spot` - so no perpetual instrument is listed.

---

## D-9. `initialized` meaning "awaiting client signature" is load-bearing

| | |
|---|---|
| Status | DEPENDENCY - documented today, and the design fails if it changes |
| First seen | 2026-09-17 |

The Gateway API overview describes the DeFi order lifecycle status
`initialized` as "awaiting client signature (DeFi orders, CeFi buy orders ...)".

WHY IT IS IN THIS FILE RATHER THAN TAKEN FOR GRANTED. ADR 0009 resolves the
venue's two-step send into one `SendOutcome`, and reports an AMBIGUOUS FIRST STEP
as `pre_send_failure` - "nothing is at the venue". That is only true because an
`initialized` order cannot fill: it is awaiting a signature that was never sent,
so it holds no exposure.

IF THE VENUE COULD EVER ACTIVATE AN ORDER WITHOUT THE CLIENT SIGNATURE, that
mapping is unsafe - step one would create live risk while the gateway is told
nothing was sent, and the effect would be retried into a second live order.

Given D-2, where a documented fact about this venue changed under us inside two
days, a dependency of this weight belongs on a list somebody re-reads rather
than inside a comment nobody revisits.

---

## D-10. Cloudflare blocks non-browser clients, and says nothing useful

| | |
|---|---|
| Status | OPEN - the adapter must handle it |
| First seen | 2026-09-17 |

Python's `urllib` receives `HTTP 403` with body `error code: 1010` on every
request to `api.truemarkets.co`, including plain unauthenticated GETs. `curl`
with a browser user-agent receives `200` for the identical URL.

NARROWED 2026-09-17, AND THE FIRST VERSION OF THIS ROW OVERSTATED IT. A RAW
PYTHON TLS SOCKET sending browser-like headers receives `200`. So the block is on
the USER-AGENT, not on the TLS fingerprint - `urllib` announces itself as
`Python-urllib/3.x` and trips the rule. Any client that sets a browser
user-agent passes, which makes this a one-line fix in the adapter rather than a
transport constraint.

WHY IT IS STILL DANGEROUS. No API error appears. A client on a default HTTP
library sees a uniform 403 on everything and will naturally conclude its
credentials or its request envelope are wrong - when nothing reached the API at
all. We spent an auth-envelope investigation on exactly this before noticing
`1010` is a Cloudflare code, and the fix turned out to be a header.

---

## D-11. The asset catalog needs no authentication

| | |
|---|---|
| Status | RESOLVED - and it corrects an assumption of ours |
| First seen | 2026-09-17 |

`GET /v1/conductor/assets` returns 200 with no `Authorization` header, and the
authenticated response is byte-identical.

WHAT THIS CORRECTS. The catalog confirmation procedure was written on the
assumption that a credential was required, and the project owner was asked for
one on that basis. It was not needed. `model/`'s `ReadOnlyHttp` - which
deliberately prohibits the auth endpoint and can never hold a credential - could
have fetched this the whole time.

---

## D-12. Tick size, lot size, fees and minimums are published NOWHERE

| | |
|---|---|
| Status | OPEN - blocks priced orders |
| First seen | 2026-09-17 |

No field on any of the 213 catalog entries mentions tick, lot, fee, minimum,
increment or precision. `market_data` carries supply figures only and `networks`
is empty. Four plausible metadata paths 404: `/v1/conductor/assets/{id}`,
`/v1/conductor/pairs`, `/v1/conductor/markets`, `/v1/conductor/instruments`.

WHAT IT BLOCKS. `VenueInstrumentSpec` exists precisely to carry tick and lot
separately, because the replicated profile stores only their product and the
factors cannot be recovered from it. With no source for either,
`spec_agrees_with_profile` can never be satisfied and the adapter cannot render a
priced order. This is now the narrowest blocker on the execution path.

---

## D-13. The auth envelope is undocumented but now verified

| | |
|---|---|
| Status | RESOLVED by experiment |
| First seen | 2026-09-17 |

`POST /v1/auth/api-key/token` accepts
`{"key_id": "<uuid>", "timestamp": <integer>, "signature": "<base64url>"}`
where the signature is ES256 over `{key_id}.{timestamp}` as RAW r||s, sixty-four
bytes, base64url and unpadded. A DER-encoded signature is the wrong shape, a
STRING timestamp is rejected as `invalid request body`, and `keyId` is rejected
in favour of `key_id`. The response field is `access_token`.

None of that is documented. It was established by trying four envelopes and
recording which the venue accepted.

---

## D-16. Market data requires authentication; the asset catalog does not

| | |
|---|---|
| Status | RESOLVED - answers an open must-confirm row |
| First seen | 2026-09-17 |

`MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md` lists "authentication requirement for
market data" as an open field, with "may block collection entirely" as the
consequence if it is required.

WITHDRAWN 2026-09-18. THIS ROW WAS WRONG AND THE REASONING BEHIND IT WAS BAD.

It claimed market data requires authentication because `/v1/market-data/*` and
the orderbook paths answered `401 Jwt is missing` unauthenticated. They do. But
`/v1/prices` answers `401` unauthenticated and PLAIN `404 Not Found` WITH a valid
token - so the 401 is a blanket edge gate on paths the router does not know,
fired BEFORE routing. An unauthenticated 401 on this host says nothing about
whether a path exists, let alone whether it needs a credential.

The Market Data Service API page states the opposite outright: "All endpoints are
public - no authentication or API key is required."

WHAT THE ERROR WAS, stated so it is not repeated: a 401 was treated as evidence
about a path that was never confirmed to exist. Three rows and one ADR were built
on it.

CONSEQUENCE. `model/`'s collector cannot gather market data, by construction:
`ReadOnlyHttp` prohibits the auth endpoint precisely so the research package can
never hold a credential. Collecting book data therefore needs either a separate
credentialled collector outside `model/`, or a deliberate decision to relax that
interlock - which should not be taken quietly.

`/v1/defi/market` responds `400 handshake error: bad "Upgrade" header` to a plain
GET, confirming it is a live WebSocket endpoint reachable without a credential at
the transport level. Whether the socket then demands a token is untested.

---

## D-14. Increment sizes are scoped to CeFi *explicitly*, and DeFi has none anywhere

| | |
|---|---|
| Status | OPEN - supersedes D-6 with a stronger finding |
| First seen | 2026-09-17 |

D-6 recorded that the increment table looked CeFi-scoped. It is scoped
EXPLICITLY, in the page's own words:

> "True Markets' CeFi exchange (the "Exchange") enforces minimum increments for
> all instruments traded on the Exchange."

under the heading "CeFi Exchange Minimum Increments". The page mentions DeFi
nowhere at all.

PAGES CHECKED FOR A DEFI TICK OR LOT, ALL NEGATIVE: `/documentation/increment-sizes`,
`/market-data/overview`, `/apis/retail/true-markets-gateway-api`,
`/apis/retail/create-order/`, and the asset catalog itself (no field on any of
213 entries mentions tick, lot, fee, minimum, increment or precision). The DeFi
WebSocket channel list names `priceCandles`, `trendingAssets`, `surgingAssets`,
`referencePrices`, `orderbook` and `error` - no instrument-metadata channel,
unlike CeFi which has one called `instrument`.

WHY THE CHAIN'S DOCUMENTATION IS NOT THE ANSWER. Tick and lot are MARKET rules,
not chain rules. A chain publishes token decimals - how many atoms make a whole
token - and we already have that from the catalog's `decimals` field. Nothing on
Solana or Base states whether True Markets' matching will accept a price of
1.234567 or round it, because that is True Markets' rule and not the chain's.

THE ONE REAL EXCEPTION IS HYPERLIQUID. `hypercore-spot` and `hypercore-perp` are
Hyperliquid, whose own exchange API publishes spot metadata including size
decimals and tick rules. That is authoritative - but it is the EXCHANGE's
documentation rather than the chain's, and it covers two tradeable assets out of
204.

A POSSIBILITY WORTH HOLDING OPEN. The DeFi surface may have no tick or lot at
all, accepting arbitrary precision down to token decimals. If so the cluster
cannot represent it as things stand: `quote_micros_per_tick_lot` is an integer
number of quote micros, and tick x lot would be a fraction below one. That is a
modelling question, not a lookup, and it changes what a "verified" answer would
even look like.

---

## D-15. Hyperliquid prices are significant figures, not a tick - which the cluster cannot represent

| | |
|---|---|
| Status | OPEN - a modelling problem, not a lookup |
| First seen | 2026-09-17 |

Hyperliquid's own documentation is precise where True Markets' is silent:

> "Prices can have up to 5 significant figures, but no more than
> `MAX_DECIMALS - szDecimals` decimal places." Integer prices are always valid.
> "Sizes are rounded to the `szDecimals` of that asset."
> `MAX_DECIMALS` is 6 for perps and 8 for spot.

So the allowed price granularity DEPENDS ON THE PRICE. Near 1.2345 the increment
is 0.0001; near 12345 it is 1. There is no fixed tick to put in a field.

WHY THAT MATTERS MORE THAN THE MISSING NUMBER. `InstrumentExecutionProfile`
carries ONE `quote_micros_per_tick_lot`, and `VenueInstrumentSpec` carries ONE
`tick_quote_micros`. Both assume a constant tick. A significant-figure rule
cannot be expressed by either, and a cached value would be correct only while the
price stays inside one decade - which is the D-6 staleness hazard again, but
continuous rather than in nine buckets.

CONSEQUENCE FOR "JUST TRADE HYPERLIQUID ASSETS". Restricting to `hypercore-*`
does buy authoritative, published rules. It does not buy rules the cluster can
hold, and it shrinks the tradable universe to TWO assets - KNTQ and PURR, the
only `hypercore-spot` entries marked tradeable. It also leaves a second gap: the
rules above are HYPERLIQUID's, and we would be trading through True Markets'
gateway, which may validate or round differently on top. Upstream documentation
is necessary and not sufficient.

WHAT WOULD ACTUALLY RESOLVE IT. Either a price-granularity representation the
contract can hold - significant figures are expressible as a rule rather than a
number - or a per-instrument tick that True Markets states for its own gateway.
The first is a byte-contract question; the second is a question for the venue.

---

## D-17. OUR OWN BUG: `model/` cannot fetch an order book

| | |
|---|---|
| Status | OPEN - in a protected path, reported not fixed |
| First seen | 2026-09-18 |

Not a venue discrepancy. Recorded here because it was found the same way and
would otherwise be lost.

`ReadOnlyHttp._guard` matches `STATE_CHANGING_PATH_FRAGMENTS` as SUBSTRINGS, and
the list contains `/order`. So `/v1/market-data/orderbook` and
`/v1/defi/orderbook` are both refused:

```
REFUSED: /v1/market-data/orderbook -> matches state-changing fragment '/order'
REFUSED: /v1/defi/orderbook        -> matches state-changing fragment '/order'
```

`model/` exists to compute book metrics - it ships `BookObservation` and
`features/book_metrics.py` - so THE PACKAGE STRUCTURALLY CANNOT COLLECT THE DATA
IT WAS BUILT TO ANALYSE. There is no escape hatch either: `post_allowlist`
covers POST only, and a GET refusal has no allowlist at all.

WHY THE INSTINCT WAS RIGHT AND THE IMPLEMENTATION WRONG. Over-broad is correct
for this list - the comment "a false refusal costs one allowlist entry, a false
permit costs an order" is the right trade. But over-broad should mean MORE
SEGMENTS THAN NECESSARY, not any path that happens to START WITH THE SAME
LETTERS. An order book is not an order, and no amount of caution makes reading
one dangerous.

FIXED IN `tools/probe-market-data.py` by comparing path SEGMENTS rather than
substrings: `orders`, `order`, `cancel`, `sign` and the rest are refused when
they appear as a whole segment, so `/orderbook` passes and `/orders` does not.
`orders` is kept on the list even for GET, deliberately - this tooling has no
business reading the order book of record, and a path check that reasons about
intent is one that gets argued with.

NOT FIXED IN `model/`, which is a protected path. The same segment-wise
comparison is the suggested change, and it needs no other alteration.

---

## D-18. There is no REST market data; the advertised service is not on this host

| | |
|---|---|
| Status | RESOLVED as a negative - market data is WebSocket-only |
| First seen | 2026-09-18 |

The documentation index advertises a "Market Data" API with real-time quotes,
book and candles, and the market-data overview names REST endpoints ("Get price
candles by symbol", "Get CeFi instrument price history"). Twenty-nine paths were
probed with a valid token. NONE of them serve market data.

READING THE TWO 404 SHAPES APART IS WHAT MADE THIS CONCLUSIVE:

| response | meaning |
|---|---|
| `{"type":"not_found", ..., "request_id": ...}` | the prefix ROUTES to the API; this path is not one it serves |
| `Not Found` (plain text) | the edge does not route this at all; nothing reached the API |

`/v1/market-data/*` returns the PLAIN form, so that prefix does not exist on
`api.truemarkets.co` - the advertised service is elsewhere, or is not REST.
`/v1/conductor/*`, `/v1/account/*` and `/v1/defi/market*` return the JSON form,
so they route; within them, only `GET /v1/conductor/assets` (200) and
`/v1/conductor/quotes` (405, so POST-only) exist at all.

WHY THE EARLIER 401s MEANT NOTHING. Unauthenticated, `/v1/market-data/orderbook`
answered `401 Jwt is missing`; with a valid token the same path answers `404`.
The auth gate sits IN FRONT of routing, so an unauthenticated 401 says nothing
about whether a path exists. This is precisely why the first use of a token was a
probe rather than a collector - building against those 401s would have produced a
collector aimed at paths that were never there.

CONSEQUENCE. The book is reachable only over the WebSocket at
`/v1/defi/market`, which a plain GET confirms is live (`400 handshake error: bad
"Upgrade" header`). Inferring a tick size for D-12 therefore requires a
WebSocket client, and `curl` on this host has no `wss` support.

---

## D-19. The Market Data Service is documented but not locatable

| | |
|---|---|
| Status | OPEN - blocks every market-data route |
| First seen | 2026-09-18 |

`/apis/marketdata/market-data-service-api` describes a service with REST
**Prices** (current candles, windowed and point-in-time history, batched
lookups, min/max ranges) and **Market Cap** endpoints, plus the two WebSockets.
It states three things that matter:

> "All endpoints are public - no authentication or API key is required"
> "Prices, market caps, and other numeric values are returned as strings to
> preserve full decimal precision"
> WebSocket details live in separate AsyncAPI specs.

NONE OF IT IS REACHABLE. On `api.truemarkets.co`, every plausible path -
`/v1/prices`, `/v1/prices/candles`, `/v1/market-cap` and six more - returns a
PLAIN `404 Not Found` with a valid token, which by D-18's test means the edge
does not route them at all. Five plausible hostnames do not resolve:
`marketdata`, `md`, `market-data`, `prices` and `data` under
`truemarkets.co`.

So the service has a documented API, a documented auth posture, and no
discoverable address. Either it is on a domain not guessable from the
documentation, or it is not deployed.

THE ONE GENUINELY USEFUL FACT SALVAGED: numeric values are returned as STRINGS to
preserve decimal precision. That is the right choice for tick inference and it
means a price arrives exactly as the venue represents it, rather than through a
float. When the service is located, its price strings are the evidence D-12
needs.

WHAT TO ASK. The base URL of the Market Data Service, and the AsyncAPI spec for
the DeFi WebSocket - the subscribe envelope is `{"type":"subscribe","topics":[..]}`
by experiment, but every documented channel name (`orderbook`, `priceCandles`,
`referencePrices`, `trendingAssets`) is rejected as `unknown_channel`, so the
documented channel list does not match the live socket either.

---

## D-20. True Markets' `decimals` is token precision, not the order lot

| | |
|---|---|
| Status | OPEN - would cause rejected or mis-sized orders |
| First seen | 2026-09-18 |

True Markets publishes one `decimals` per catalog asset. For both `hypercore-*`
assets it matches Hyperliquid's `weiDecimals` exactly - the TOKEN's precision -
and it is NOT the order size increment:

| symbol | TM `decimals` | HL `weiDecimals` | HL `szDecimals` |
|---|---:|---:|---:|
| PURR | 5 | 5 | **0** |
| KNTQ | 8 | 8 | **2** |

PURR orders must be in WHOLE units. A reader of the True Markets catalog alone
would conclude 0.00001 PURR is tradable - wrong by five orders of magnitude - and
would find out by having orders rejected or silently rounded.

CONSEQUENCE. `VenueInstrumentSpec.lot_base_atoms` cannot be derived from the True
Markets catalog. For the hypercore assets Hyperliquid supplies it directly; for
the other 201 assets there is no source at all, which is D-12 unchanged.

This also narrows D-12: the missing facts are not "tick and lot" generically but
specifically an order-size increment and a price rule, neither of which the
catalog's `decimals` is.

---

## D-21. One symbol, several instruments: `HYPE` is three different things

| | |
|---|---|
| Status | OPEN - a modelling requirement, not a venue fault |
| First seen | 2026-09-18 |

The catalog carries `HYPE` on `solana` and `CBHYPE` on `base`, while Hyperliquid
Core carries its own native `HYPE`. Same underlying, three venues, three decimal
conventions - 9, 18 and 8.

Nothing is wrong with the venue here. The finding is about US: a symbol is not an
instrument, and the adapter must key on `(venue, chain, address)` rather than on
a ticker. `InstrumentId` already forces that discipline inside the cluster, but
any config, research note or operator instruction written in terms of "HYPE"
names three different books.

WHY IT MATTERS MORE THAN IT SOUNDS. The measured Hyperliquid Core book for HYPE
is 1.4 bps deep; True Markets' HYPE is a bridged Solana token whose book we
cannot see at all (D-19). Using the first as market data for the second is the
cross-venue error in its most tempting form - it would look reasonable and the
mistake would be invisible until a fill arrived at a price that came from
another market entirely.

An earlier note in the feasibility evidence claimed HYPE was not on True Markets.
That was wrong and is corrected there; the project owner spotted it.

## D-22. The order schema was never behind a broken page - it is published as an MCP tool

| | |
|---|---|
| Status | RESOLVED. Closes D-3. |
| First seen | 2026-09-18 |
| Evidence | `docs/evidence/V2_TRUEMARKETS_MCP_SCHEMA_2026-09-18.md` |

`docs.truemarkets.co/mcp/` publishes two MCP servers. Both answer `tools/list`
over plain HTTP, and the reply carries a JSON Schema per tool. That is a
MACHINE-READABLE, VENUE-AUTHORED contract - a strictly better source than the
REST reference prose that failed to render in D-3, and better than anything the
browser DevTools route would have produced.

`trade_prepare`, read from the server on 2026-09-18:

```json
{"properties": {
   "side":       {"type":"string","description":"Order side (buy, sell)"},
   "base_asset": {"type":"string","description":"Token symbol or contract address to trade"},
   "qty":        {"type":"string","description":"Amount to trade"},
   "chain":      {"type":"string","description":"Blockchain network (solana, base) for an on-chain swap. Omit for an exchange (CeFi) order."},
   "qty_unit":   {"type":"string","description":"Quantity unit: 'base' (token amount) or 'quote' (USDC amount). Defaults to 'quote' for buys and 'base' for sells."}},
 "required": ["side","base_asset","qty"]}
```

So `qty_unit` DOES accept a base-denominated quantity - the open half of D-3.
The other half, the price field name, is answered by its absence: see D-23.

WHAT THIS COST. Nothing, in the end, but only because the guess was refused.
`encode_limit_order_body` has been refusing with `schema_unverified` since it was
written. Had it shipped a plausible price field name instead, it would have been
wrong in the specific way D-3 warned about - and the venue would have accepted
the order anyway, as a market order, silently.

---

## D-23. There is no limit order, no cancel, and no order book. The venue is market-swap only.

| | |
|---|---|
| Status | RESOLVED, and it invalidates an architectural premise |
| First seen | 2026-09-18 |
| Supersedes | D-1, D-4, D-6, D-14 for the DeFi path |

`trade_prepare` is described by the venue as "**Create a market order**". Its
schema has no price, no order type, no time-in-force, and no client order id.
Across all seventeen tools on the trading server there is:

* no tool that places a priced order;
* no tool that cancels anything;
* no tool that lists open or resting orders;
* no tool that returns an order book, a best bid/offer, or a quote depth.

The trading vocabulary is exactly three verbs: `get_quote`, `trade_prepare`,
`trade_execute`. That is a request-for-quote swap, not an exchange.

WHY THIS IS THE MOST EXPENSIVE ROW IN THIS FILE. V2 is built to post and manage
resting liquidity. `OrderEffect` carries `PriceTicks`; the adapter renders price
and quantity separately; Phase 8 built mass-cancel; ADR 0008 reconciles resting
state. On this venue NONE of that has a counterpart. There is nothing to rest,
nothing to cancel, and no price to post.

D-1 recorded that the quickstart and the endpoint reference disagreed about
whether DeFi limit orders exist. The MCP server is a third source and it settles
it: on the surface the venue actually exposes to programmatic clients, they do
not. D-1's "RESOLVED against the endpoint reference" conclusion was wrong.

WHAT IT CHANGES. The maker economics this project was designed to capture are
unavailable here. Every True Markets trade crosses a spread and pays it. That is
a fact about the venue, not a defect in the cluster - and the cluster's
mechanisms remain correct for Hyperliquid, which does have a book. But any claim
that V2 "market-makes on True Markets" would be false, and must not be made.

---

## D-24. `qty_unit`'s default depends on the side

| | |
|---|---|
| Status | OPEN as a hazard; mitigated by always sending the field |
| First seen | 2026-09-18 |

> "Defaults to 'quote' for buys and 'base' for sells."

An omitted `qty_unit` means USDC on a buy and tokens on a sell. The same request
body, with `side` flipped and nothing else changed, denominates `qty` in a
different currency.

WHAT WE DO ABOUT IT. The encoder emits `qty_unit` EXPLICITLY on every order,
including when the value equals the default. A field whose meaning depends on
another field is a field that must never be left to a default, and "the default
happened to be right" is not a property any test can pin down.

This is also why the same rule already applied to `chain`: see D-25.

---

## D-25. Omitting `chain` silently switches venue from DeFi to CeFi

| | |
|---|---|
| Status | OPEN as a hazard; mitigated by always sending the field |
| First seen | 2026-09-18 |

> "Blockchain network (solana, base) for an on-chain swap. **Omit for an
> exchange (CeFi) order.**"

Absence is not "unspecified", it is a different venue with different custody,
different settlement and a different fee schedule. A dropped field routes the
order somewhere else entirely, and the request stays valid.

This also contradicts nothing in the docs so much as it reveals what they never
said: the REST reference describes DeFi and CeFi as separate endpoints, and the
MCP surface merges them into one call distinguished only by a field's presence.

TWO CONSEQUENCES. The encoder always sends `chain`. And the chain vocabulary is
`solana, base` ONLY - so the `hypercore-spot`, `hypercore-perp`, `robinhood` and
`arbitrum` entries in the asset catalog (D-11) are listed but not addressable by
this API. PURR is `hypercore-spot`. **The decision to "stick to PURR" cannot be
executed on this venue** and is withdrawn; HYPE-on-solana (D-21) is reachable,
because it is a bridged Solana token.

---

## D-26. `trade_prepare` is sometimes itself the terminal action

| | |
|---|---|
| Status | OPEN - drives the two-step send design |
| First seen | 2026-09-18 |
| Related | D-9, ADR 0007 |

> "Returns an order_id and, when signing is required, unsigned payloads for the
> client to sign. **If no payloads are returned the order was submitted
> directly.**"

So one call has two behaviours, and which one happened is knowable only by
inspecting the response. A client that crashes after sending `trade_prepare` and
before reading the reply cannot tell whether it has a pending unsigned order or
a LIVE SUBMITTED ONE.

This is worse than the ambiguity `resolve_two_step_send` was built for, which
assumed prepare was always inert. It is the same shape as D-9's `initialized`
problem and must be resolved the same way: on recovery, treat the outcome as
UNKNOWN and reconcile against the venue before sending anything else. With no
open-orders tool (D-23), reconciliation has to go through `get_balances`, which
is a materially weaker signal - it shows position, not orders.

---

## How to add a row

One heading per discrepancy, numbered. Record what the docs say, what we did
about it, and what the discrepancy COST or BLOCKED - that last part is the reason
the file is worth keeping. Link the ADR or evidence file that carries the full
reasoning rather than repeating it here.
