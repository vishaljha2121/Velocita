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
| 1 | Resting limit orders? | **CONTRADICTED** — see below | unresolved |
| 2 | Post-only available? | **Not documented** → treat as unsupported | verified-absent |
| 3 | Public order book? | **Yes** | verified |
| 4 | Maker rebates / tiered fees? | **No DeFi schedule published** | unverified |
| 5 | Chain and gas cost? | **Not documented** | unverified |

Two of five are answered. One is answered negatively, which is usable. **Two
remain blocking, and one of those is a contradiction in the source itself.**

## 1. Resting limit orders on DeFi — THE CONTRADICTION

Two pages on the same site disagree, and the disagreement decides whether a
market-making strategy is possible on the only surface this account can trade.

AGAINST, from the quickstart:

> "Limit orders are supported only on the CeFi assets"

FOR, from the order endpoint index:

> `POST /orders/{id}/cancel` — "Cancel a limit order (**CeFi or DeFi**)"
>
> `POST /orders/{id}/cancel/execute` — "Execute a **resting DeFi limit order**
> cancellation ... Submits the client's signatures over the payloads returned by
> `POST /orders/{id}/cancel` to cancel a **resting DeFi limit order**."

A venue does not ship a dedicated two-step signing endpoint to cancel an object
that cannot exist. But the quickstart is unambiguous in the other direction, and
a quickstart is the page most likely to be current.

This is exactly the case G-063 names: unsupported or ambiguous venue behaviour is
represented as unknown, never guessed into success. It is NOT resolvable from
documentation, and it is not resolvable by attempting an order — the research
package's `ReadOnlyHttp` refuses any non-GET and any path containing `/orders`
before a socket opens, and that interlock stays.

The safe resolution is a read-only `GET` against the asset catalog filtered to
DeFi, checking whether assets advertise a limit-order capability:

```
GET /v1/conductor/assets?venue=defi
```

which the quickstart documents as the supported filter.

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

## 5. Chain and gas — not documented

No page names the settlement chain, the gas payer, or a per-fill on-chain cost.
What is established is the SHAPE of the DeFi flow, which implies both:

> `POST /orders` — "Creates a new order. For DeFi orders, returns **unsigned
> payloads for client signing**."
>
> `POST /orders/{id}/execute` — submits "client-provided signatures for the
> unsigned payloads".

So the client holds a key and signs, and the account API carries `create-wallet`,
`verify-wallet` and `execute-wallet-export`. Client-side signing of on-chain
payloads normally means the client bears gas, but the documentation does not say
so and this record will not assume it.

This remains the term the must-confirm doc flags as most likely to disqualify the
strategy: at 1 USDC per quote, a per-fill gas cost can exceed the entire spread.

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
- NEW AMBIGUITY WINDOW. DeFi is prepare → sign → execute, so ONE economic intent
  crosses the process boundary TWICE, each crossing with its own ambiguous
  outcome. Phase 5's delivery model assumes one send per effect. An effect that
  was prepared but whose execute is ambiguous is a state the current
  `DeliveryAttempt` does not distinguish, and reconciliation by client order
  identity may not find a prepared-but-unexecuted order at all.
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
