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

WHY IT IS WRITTEN DOWN. So it is not lost, and so that confirming or
contradicting it later is a finding rather than a surprise. A Base + Hyperliquid
split would mean two chains, two gas regimes, and possibly two instrument types
under one `venue=defi` label - which the adapter would have to keep apart rather
than average.

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

## How to add a row

One heading per discrepancy, numbered. Record what the docs say, what we did
about it, and what the discrepancy COST or BLOCKED - that last part is the reason
the file is worth keeping. Link the ADR or evidence file that carries the full
reasoning rather than repeating it here.
