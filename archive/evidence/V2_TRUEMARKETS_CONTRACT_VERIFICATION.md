# True Markets venue contract verification — 2026-09-15

Answers the five blocking questions in `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`
against `docs.truemarkets.co`, read-only. No credential was used, no order
endpoint was called, and nothing below is inferred from a plausible default.

OPERATING CONSTRAINT: this account can trade **DeFi only**. Most of the venue's
published specifics are explicitly scoped to CeFi, so that scoping is recorded
per row rather than averaged away.

## Verdict first

| # | Blocking question | DeFi answer | Confidence |
|---|---|---|---|
| 1 | Resting limit orders? | **YES** | verified |
| 2 | Post-only available? | **Not documented by True Markets** | unverified |
| 3 | Public order book? | **Yes** | verified |
| 4 | Maker rebates / tiered fees? | **No DeFi schedule published** | unverified |
| 5 | Chain / underlying venue? | **Hyperliquid**. Gas undocumented | venue verified |
| 6 | Instrument type | **Perpetual futures, not spot** | verified |

**The project is NOT a venue-level no-go.** The stop condition in the must-confirm
doc — "if (1) or (3) is no, stop" — does not fire. Both are yes.

BUT THE INSTRUMENT IS A PERPETUAL, which the must-confirm doc did not think to
ask about, and which breaks an assumption Phase 5 is built on. See section 6; it
is the largest open item on this page.

## 1. Resting limit orders on DeFi — YES

An earlier revision of this document recorded this as an unresolved
contradiction, because the quickstart states:

> "Limit orders are supported only on the CeFi assets"

That statement is wrong, or scoped to its own market-order example. Two
independent reference pages say otherwise in terms that cannot be read any other
way.

From `POST /orders/{id}/cancel`:

> "Venue-polymorphic cancel. The `payloads` field is the signal:"
>
> **"DeFi cancellation (Hyperliquid): Resting orders return `200 OK` with
> unsigned payloads."**
>
> Eligibility: "CeFi orders in `pending` or `active` status, and **DeFi orders in
> `active` status**."

From the Gateway API overview's status enumeration:

> "`active` — **limit order resting in order book**"

So DeFi orders reach `active`, `active` means resting in the book, and the cancel
path treats DeFi resting orders as a first-class case with its own signing step.
A venue does not ship a two-step signing endpoint, an eligibility rule and a
status value for an object that cannot exist.

TREAT THE QUICKSTART AS STALE ON THIS POINT. It is worth recording that a
quickstart was the least reliable page on the site here, because the usual
heuristic is the opposite.

Still not directly confirmed, and the gap is narrow but real: every DeFi-limit
statement above concerns CANCELLING one. The `POST /orders` CREATE schema is
rendered client-side and could not be extracted, so the exact field combination
that creates a resting DeFi limit order — the type value, whether a price field
is required, whether a venue selector is needed — is unread. That is a schema
question now, not a feasibility question.

## 1b. The DeFi order lifecycle, from the Gateway API overview

Quoted because it does not match the Phase 5 state machine and Phase 9 has to
bridge the difference:

> `initialized` — "awaiting client signature (DeFi orders, CeFi buy orders
> needing funding bridge)"
> `pending` — "submitted to venue, awaiting settlement"
> `active` — "limit order resting in order book"
> `complete` — "fully filled"
> `canceled` / `cancel_pending` — "user-canceled or exchange-rejected"
> `failed` — "rejected or execution reverted"

> "The `payloads` field in the create response is the signal: if it's non-empty,
> you need to sign and call execute. If it's empty or absent, the order is
> already submitted."

Base URLs, and note that a UAT environment exists:

> Production `https://api.truemarkets.co/v1/gateway`
> UAT `https://api.uat.truemarkets.co/v1/gateway`
> "All endpoints require a JWT access token in the `Authorization: Bearer
> <token>` header."

A UAT environment means the create-order schema question above, and the whole
Phase 9 adapter, can be resolved against a real API without risking funds.

## 2. Post-only — not documented

`/documentation/exchange-overview` enumerates the order model exhaustively and
post-only is absent from it:

> Order types: **Limit** and **Market**.
> Time in force: **GTC** and **IOC**.
> Matching: "a central limit order book platform ... price-time priority".

Under G-063 an undocumented flag is unsupported. That has a direct consequence
for the frozen Phase 5 contracts: `ActionTemplate::post_only` is
`PostOnlyMode::required` in the current fixtures, and `evaluate_pre_send` refuses
with `venue_cannot_represent_intent` when the venue cannot express the exact
intent. Against this venue the guard would refuse every order, correctly.

The economic cost of dropping post-only is bounded and small HERE, which is worth
stating because it usually is not: with maker at 0 bps and taker at 4 bps, an
accidental cross costs 4 bps rather than forfeiting a rebate. Post-only is a
safety rail against crossing, not the basis of the edge.

Note also the exchange overview describes the CeFi book. Whether the DeFi surface
is also price-time-priority CLOB is not stated.

## 3. Public order book — yes, both surfaces

From `/market-data/overview`:

- DeFi WebSocket `/v1/defi/market`, channel **`orderbook`**, alongside
  `priceCandles`, `trendingAssets`, `surgingAssets`, `referencePrices`.
- CeFi WebSocket `/v1/cefi/market`, channels `depth`, `trade`, `ebbo`, `auction`.
- CeFi documents "Unauthenticated Endpoints", so some market data is public.

NOT documented, and each is a real collection hazard rather than a nicety:

- depth levels published per book;
- sequence numbers or checksums, so gap detection is currently impossible —
  and per the must-confirm doc, "gaps become invisible, and invisible gaps become
  results";
- rate limits;
- whether DeFi market data requires authentication.

## 4. Fees — verified for CeFi, absent for DeFi

`/documentation/fee-schedule`, last updated 2026-08-18, programmatic at
`GET /v1/cefi/operations/fees/schedule`:

| Tier | Maker | Taker | Applies to |
|---|---:|---:|---|
| Band 0 (only band) | **0 bps** | **4 bps** | Institutions, Individuals, Applications |

> "All fees are shown in basis points (bps) of the executed notional: 1 bp =
> 0.01%, so 4 bps = 0.04%."

Two findings, and the second is the one that matters here:

- THERE ARE NO REBATE BANDS. `/journeys/market-maker` states "the top maker bands
  are negative, a rebate paid to you", but the published schedule has a single
  band with maker = 0. Maker fills are free; they earn nothing. Any edge is pure
  spread capture.
- THE SCHEDULE IS **CeFi ONLY**. No DeFi fee schedule is published anywhere on
  the site. For a DeFi-only account this is an unverified economic input, and the
  must-confirm doc's own warning applies: a wrong maker fee inverts the sign of
  the expected edge.

## 5. Underlying venue is Hyperliquid; gas still undocumented

Named once, in passing, on the cancel page:

> "**DeFi cancellation (Hyperliquid)**: Resting orders return 200 OK with
> unsigned payloads."

That single word resolves more than the rest of the site combined, because it
identifies what the gateway is actually brokering to. It also imposes a
discipline: EVERYTHING KNOWN ABOUT HYPERLIQUID DIRECTLY IS OUT OF SCOPE FOR THIS
DOCUMENT. We trade through True Markets' abstraction, not against Hyperliquid, so
Hyperliquid's own fee schedule, its post-only flag and its gas model are
properties of a system we do not call. Importing them here is precisely the
failure the must-confirm doc warns about: "a value we inferred must never become
indistinguishable from one the venue documented".

What it legitimately changes is the RESEARCH PLAN. The questions "does a resting
book exist", "is there depth", "what does a fill cost on-chain" now have a known
place to be answered from primary sources, and the answers can then be checked
against what True Markets actually exposes. Any Hyperliquid fact that survives
that check gets recorded as a True Markets fact with a True Markets source.

The gas question specifically remains open and still deserves its billing as the
term most likely to disqualify the strategy. What is established is the SHAPE of
the DeFi flow, which implies a signer and therefore a chain account:

> `POST /orders` — "Creates a new order. For DeFi orders, returns **unsigned
> payloads for client signing**."
>
> `POST /orders/{id}/execute` — submits "client-provided signatures for the
> unsigned payloads".

So the client holds a key and signs, and the account API carries `create-wallet`,
`verify-wallet` and `execute-wallet-export`. Client-side signing of on-chain
payloads normally means the client bears gas, but the documentation does not say
so and this record will not assume it.

At 1 USDC per quote, a per-fill on-chain cost can exceed the entire spread, so
this stays blocking for the economics even though it no longer blocks the
architecture.

## 6. The DeFi surface is PERPETUALS, and this is the finding that matters

From the positions endpoint index:

> "**Query a user's open Hyperliquid perpetual positions.**"
>
> `GET /positions` — "Returns a read-only list of the authenticated user's open
> positions. **Data is fetched on demand from the venue; nothing is persisted.**"
>
> `GET /portfolio` — "Unified portfolio across CeFi and DeFi venues: valued
> balances, open positions, and **PnL** in a single call."

Perpetual futures, not spot. Three consequences, in increasing order of how much
they cost to fix.

### 6.1 V2 cannot represent a position change it did not authorize

This is architectural and it is not a Phase 5 defect — Phase 5 was built for a
model where every position change descends from an effect the cluster authorized,
which is true of spot limit orders and false of perps.

`apply_observe_fill` requires a matching SUBMIT effect before any fill can book:

```cpp
if (outbox == nullptr || outbox->operation != EffectOperation::submit) {
    return fill_result(state, ApplyCode::venue_observation_not_applicable, ...);
}
```

A perp venue changes a position without an order in at least three ways:

- LIQUIDATION. The venue force-closes the position. There is no effect, so no
  `effect_id`, so no command in the entire kernel can carry it.
- AUTO-DELEVERAGING. Someone else's liquidation closes part of yours.
- FUNDING. A periodic cash flow against an open position. Not a fill, not a fee,
  not an order — V2 has no term for it anywhere in `RiskAccount`.

Offer any of these to the reducer today and it answers
`venue_observation_not_applicable`, correctly by its own contract, and the
cluster's position silently diverges from the venue's. The divergence is
detectable — `GET /positions` is authoritative and the gateway can query it — but
there is currently nothing the gateway could SUBMIT to close the gap.

The honest options for Phase 9, none of them free:

1. A new observation kind for unsolicited position change, adjudicated the way
   `order_reconciled` is: cumulative by value, no effect identity required. This
   is the smallest change that preserves the invariant "the reducer owns
   economics", and it is a byte-contract change requiring review.
2. Treat any unexplained divergence as `conflict` and escalate. Safe, cheap, and
   turns an ordinary funding payment into an operator incident several times a
   day, which means it will be ignored and then it protects nothing.
3. Trade spot on CeFi instead, where the authorized-effect assumption holds — but
   this account is DeFi-only, so it is not available.

### 6.1.1 MVP V1 decision and deferred design — 2026-09-15

MVP V1 will not trade perpetuals. Consequently the verified True Markets DeFi
surface is not a V1 execution target, and no perpetual accounting byte contract
is being added merely to keep that venue nominally in scope. A taker-only or
normally-flat strategy was considered and rejected as a safety substitute:
opening and closing are separate external actions, so a crash or partition can
still leave a position exposed to liquidation, ADL, or funding between them.

Option 1 above is also insufficient as written. The existing
`VenueObservation` is scoped to a cluster order and effect, while liquidation and
ADL need not have either; funding is a cash/equity movement rather than a
position movement. One cumulative position value also lacks the execution price,
fee, realised PnL, and immutable event identity required for an auditable
adjustment, and an unordered snapshot could overwrite newer fill evidence.

The approved direction for the later perpetual iteration is ledger-first:
separate commands for identified external executions and identified funding
settlements, plus an authoritative venue-account snapshot used as a
reconciliation check. Exact bytes remain unfrozen until read-only verification
establishes which event identities and ordering/finality fields True Markets
actually exposes. `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md` §6 owns the trigger and
implementation constraints.

### 6.2 Margin is against a position, not against an order

`RiskAccount` carries `reserved_margin`, `initial_margin_ppm` and
`max_reserved_exposure`, so margin machinery exists. It reserves against an
ORDER and releases when that order resolves. Perp maintenance margin is assessed
continuously against an open POSITION and moves with the mark price. These are
different quantities and the second has no representation.

### 6.3 Leverage changes what a 20 USDC budget means

A 20 USDC budget on spot bounds the loss at 20 USDC. The same budget as perp
margin bounds nothing by itself: notional is a multiple of it and the loss
surface scales with the multiple. Whatever leverage the venue defaults to is now
a required input to the economics, alongside the fee and gas terms — and it is
undocumented here.

## What this changes in the frozen Phase 5 contracts

Recorded now because the adapter is Phase 9 and these are cheaper to know early
than to discover against a live key.

- CONFIRMED FIT. DeFi has no modify: `PATCH /orders/:id` is "Changes the price
  and/or quantity of an **active CeFi limit order**", CeFi-scoped. V2 already
  refuses native replacement (G-018, `replacement_not_supported`) and requires
  cancel → reconcile → re-authorize. On DeFi the venue agrees, so the restriction
  costs nothing.
- CONFIRMED FIT. GTC exists, matching `TimeInForce::good_til_cancel`.
- MISFIT TO RESOLVE. `PostOnlyMode::required` cannot be satisfied — see §2.
- NEW AMBIGUITY WINDOW, now confirmed by the status enumeration. DeFi is
  prepare → sign → execute, so ONE economic intent crosses the process boundary
  TWICE, each crossing with its own ambiguous outcome. Phase 5's delivery model
  assumes one send per effect. An effect prepared but whose execute is ambiguous
  is a state `DeliveryAttempt` does not distinguish, and reconciliation by client
  order identity may not find a prepared-but-unexecuted order at all.
- A LIFECYCLE STATE V2 DOES NOT HAVE. `initialized` means the gateway holds
  unsigned payloads: the venue knows the intent, the order is NOT in the book,
  and no venue risk exists yet. V2's nearest states both assert something
  stronger — `dispatch_authorized` says nothing has been sent, `acknowledged`
  says the venue has it. Mapping `initialized` onto either is wrong in a way that
  matters: onto the first, a crash loses a signable payload the venue is holding;
  onto the second, the cluster believes it has exposure it does not have.
  Phase 9 needs the distinction, and the cheapest place to put it is the outbox
  effect state rather than the order lifecycle, since it describes delivery
  progress rather than economics.
- STATUS MAPPING, otherwise clean: `active` → `live`, `complete` → `filled`,
  `canceled` → `cancelled`, `cancel_pending` → `cancel_pending`, `failed` →
  `rejected`. `pending` ("submitted to venue, awaiting settlement") sits between
  V2's `acknowledged` and `live` and is the second state needing a decision.
- TICK SIZE IS PRICE-BANDED, NOT PER-SYMBOL. `/documentation/increment-sizes`
  publishes nine buckets by price range for CeFi, from tick 1 at $500,000+ down
  to 0.0000000001 below $0.00001, with **min notional 1.00 in every bucket**.
  `InstrumentExecutionProfile` carries one tick size, so a cached profile goes
  stale when the price crosses a bucket boundary. No DeFi table is published.

The 1.00 minimum notional, if it also holds on DeFi, confirms that a 1 USDC quote
is feasible — the one placeholder in `research.example.toml` that turns out to be
right.

## Sources

- https://docs.truemarkets.co/
- https://docs.truemarkets.co/trading/quickstart
- https://docs.truemarkets.co/trading/overview
- https://docs.truemarkets.co/documentation/exchange-overview
- https://docs.truemarkets.co/documentation/fee-schedule
- https://docs.truemarkets.co/documentation/increment-sizes
- https://docs.truemarkets.co/market-data/overview
- https://docs.truemarkets.co/journeys/market-maker
- https://docs.truemarkets.co/apis/retail/get-order-status/
- https://docs.truemarkets.co/apis/retail/create-order/
- https://docs.truemarkets.co/apis/retail/execute-cancel-order/
- https://docs.truemarkets.co/apis/retail/modify-order/
- https://docs.truemarkets.co/apis/retail/prepare-cancel-order/
- https://docs.truemarkets.co/apis/retail/true-markets-gateway-api
- https://docs.truemarkets.co/apis/retail/orders
- https://docs.truemarkets.co/apis/retail/list-positions
- https://docs.truemarkets.co/apis/retail/get-portfolio
- https://docs.truemarkets.co/getting-started/create-an-account
- https://docs.truemarkets.co/cli/authentication
