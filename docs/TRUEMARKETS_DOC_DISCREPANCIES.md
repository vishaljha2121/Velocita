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
| Status | OPEN - blocks live limit orders |
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

## D-5. No DeFi fee schedule, and no gas cost

| | |
|---|---|
| Status | OPEN |
| First seen | 2026-09-15 |

The published fee schedule is scoped to CeFi. No maker/taker schedule is
published for DeFi, and the per-fill on-chain cost is undocumented.

WHY IT MATTERS MORE THAN IT LOOKS. At a 1 USDC quote, gas is the term most
likely to disqualify the strategy outright - it can exceed the entire spread. A
cost floor computed without it is not conservative, it is wrong.

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

It is required. `/v1/market-data/*`, `/v1/conductor/orderbook` and
`/v1/defi/orderbook` all return `401 Jwt is missing` unauthenticated, while
`GET /v1/conductor/assets` returns 200 without a credential (D-11).

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

## How to add a row

One heading per discrepancy, numbered. Record what the docs say, what we did
about it, and what the discrepancy COST or BLOCKED - that last part is the reason
the file is worth keeping. Link the ADR or evidence file that carries the full
reasoning rather than repeating it here.
