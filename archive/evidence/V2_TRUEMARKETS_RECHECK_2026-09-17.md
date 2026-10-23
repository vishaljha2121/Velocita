# True Markets contract RE-CHECK — 2026-09-17

Re-reads the pages `V2_TRUEMARKETS_CONTRACT_VERIFICATION.md` (2026-09-15) relied
on. Read-only, no credential used, no order endpoint called.

TRIGGERED BY A DISAGREEMENT, which is the right reason to re-check: the project
owner reported not seeing perpetuals in the documentation, while the two-day-old
verification recorded them as verified. One of the two had to be wrong.

## What changed

| Fact | 2026-09-15 | 2026-09-17 |
|---|---|---|
| `GET /positions` description | "Query a user's open **Hyperliquid perpetual** positions" | "Returns a read-only list of the authenticated user's open positions" |
| Instrument type | Perpetual futures, verified | Not stated anywhere read |
| Hyperliquid named | Yes, on list-positions | Not found on any page read |
| Quickstart framing | - | "spot trading", example is a spot market order |

Pages read today: `/trading/overview`, `/trading/quickstart`,
`/apis/retail/list-positions`, `/apis/retail/create-order/`,
`/apis/retail/get-order-status/`, `/apis/retail/execute-cancel-order/`.

## What did NOT change

RESTING DEFI LIMIT ORDERS ARE STILL DOCUMENTED, by the same evidence path the
original verification used - the cancel endpoint, not the quickstart:

> "Submits the client's signatures over the payloads returned by
> `POST /orders/{id}/cancel` to cancel a **resting DeFi limit order**."

The sidebar still separates "Cancel a limit order (CeFi or DeFi)" from "Execute
a resting DeFi limit order cancellation", so DeFi resting orders remain a
first-class case with their own signing step.

The quickstart still says "Limit orders are supported only on the CeFi assets
returned by this endpoint", which is the same contradiction the original
verification resolved against the endpoint reference. Unchanged, and resolved the
same way: the endpoint that cancels a resting DeFi limit order is stronger
evidence than a sentence on a quickstart page.

## What this does and does not establish

ESTABLISHED: the documentation no longer describes the tradable surface as
perpetual, and describes the worked example as spot.

NOT ESTABLISHED: that the underlying venue changed. Documentation being
genericised is not the same event as an instrument changing, and the three
readings consistent with today's pages are (a) the venue moved to or clarified
spot, (b) the wording was generalised while the surface is unchanged, or (c) the
earlier read caught a page version since revised. Absence of the word
"perpetual" is not positive evidence of spot settlement.

WHAT WOULD SETTLE IT, in increasing order of cost:

  1. The `create-order` request schema, if it publishes an instrument or
     contract type. Today's page renders only "POST /orders - Creates a new
     order. For DeFi orders, returns unsigned payloads for client signing"; the
     body schema did not render and may be behind an expandable section.
  2. An authenticated `GET /v1/conductor/assets?venue=defi`, which would name
     the instruments directly. Read-only and permitted by `ReadOnlyHttp`.
  3. An authenticated `GET /positions` response shape.

None of these is an order submission. `POST /orders` remains the only endpoint
that can create an external effect and is refused by `ReadOnlyHttp` before a
socket is opened.

## Consequence

ADR 0004's factual premise is no longer supported by the documentation. The ADR
is amended rather than withdrawn: its REASONING about what V2 cannot represent is
untouched and remains correct for any perpetual surface, and it stays the
governing decision for perpetuals. What changes is that "the verified DeFi
surface is perpetual" is no longer a verified fact.

The MUST-CONFIRM discipline applies unchanged: a value we inferred must never
become indistinguishable from one the venue documented. Instrument type is
therefore recorded as UNVERIFIED as of today - not as "spot" - and confirming it
is a precondition of connecting, exactly as that document requires.
