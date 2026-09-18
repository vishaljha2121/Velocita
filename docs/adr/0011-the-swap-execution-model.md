# 0011. The swap execution model, and volume as the objective

Date: 2026-09-18
Status: Proposed - the byte contract change in section 5 needs approval before encoding

BYTE CONTRACT CHANGE. `OrderEffect` V2 -> V3. Goldens and corpus digests move.

## 1. Context: the venue is not an exchange

D-22 through D-26 established, from the venue's own machine-readable tool
schemas rather than from prose, what True Markets actually exposes:

    get_quote      -> indicative price, binding on nothing
    trade_prepare  -> "Create a market order"
    trade_execute  -> submit signatures

There is no price parameter, no order type, no time-in-force, no cancel, no
open-orders query, and no order book. Three verbs, and the middle one is a
market order. This is a request-for-quote swap venue.

V2 was designed against a central limit order book. `OrderEffect` carries
`PriceTicks`; Phase 8 built mass-cancel; ADR 0008 reconciles resting orders.
**On this venue none of that has a counterpart.** There is nothing to rest and
nothing to cancel. That is a fact about the venue, and the mechanisms remain
correct for Hyperliquid, which has a real book. What this ADR must not do is
pretend the two venues are the same shape.

## 2. The objective changes what "good" means

The objective for this iteration is MAXIMUM VOLUME TRADED, for a leaderboard.

On a book venue, volume is capital-constrained: you post, you wait, you get
filled. On a swap venue capital RECYCLES - a round trip returns your money minus
costs - so capital is not the constraint at all. The constraint is the loss
budget:

    volume_per_round_trip = 2 x notional          (a buy leg and a sell leg)
    cost_per_round_trip   = notional x c          (c = round-trip cost fraction)

    max_volume = loss_budget x 2 / c

Volume is therefore bounded by `1/c`, and NOTHING ELSE. Every architectural
question reduces to: what makes `c` small, and what stops `c` from silently
becoming large?

That single equation is why this ADR looks the way it does. It is not a
market-making system with the quoting removed; it is a cost-minimisation system,
and cost minimisation has a different risk model.

### What was measured

Round-trip cost, USDC -> token -> USDC, quoted through Jupiter on 2026-09-18.
These are simulated routes, not executions, and they are a LOWER BOUND on the
True Markets cost because any venue markup sits on top (see section 7).

| asset | $25 | $100 | $500 | $2000 |
|---|---|---|---|---|
| SOL | 0.4 bps | 0.3 bps | 0.7 bps | **0.1 bps** |
| JUP | 1.6 | 1.5 | 5.1 | 10.6 |
| JTO | 48.4 | 46.5 | 46.5 | 71.6 |
| DRIFT | 71.5 | 112.3 | 316.4 | 771.8 |
| W | 58.2 | 82.2 | 197.7 | 356.5 |

Two facts fall out, and both are load-bearing:

**Cost per unit volume varies by three orders of magnitude across assets.**
SOL round-trips for a fraction of a basis point; DRIFT costs 772 bps at $2000.
At `max_volume = 2 x budget / c`, a $100 loss budget buys roughly $4M of SOL
volume or $28k of DRIFT volume. ASSET SELECTION IS THE STRATEGY. There is no
cleverness in timing that recovers a factor of 140.

**Impact is superlinear in size.** DRIFT costs 71 bps at $25 and 772 bps at
$2000 - 11x the rate for 80x the size. So for a fixed volume target, many small
round trips cost less than few large ones, bounded below by per-trade overhead
and rate limits rather than by economics.

The system this project already is - deterministic, replicated, hard-bounded,
built to sustain a high rate of small decisions without ever double-sending -
happens to be exactly the machine this objective wants. That is a genuine fit
and not a retrofit.

## 3. Decision: a swap is an operation, not a new effect type

`EffectOperation` gains `swap_market`. It does NOT get a parallel `SwapEffect`
struct.

WHY. The gateway, the risk reservation, the kill epoch, the executor generation,
the idempotency and the audit trail are identical for a swap and for a limit
order. A second effect type would duplicate all of it and create a second path
that Phase 8's mechanisms would have to be taught about separately - which is
how a safety mechanism comes to cover one path and not the other.

`price` is ignored when `operation == swap_market` and MUST be zero. A validator
enforces that rather than a comment, because a non-zero price on a swap means
somebody believed it was being honoured.

## 4. Decision: the chain is part of the venue, not a field

D-25: omitting `chain` does not mean "unspecified", it routes the order to a
CeFi exchange with different custody, different settlement and a different fee
schedule - and the request stays valid.

So `chain` is not a parameter of an order. It identifies WHERE THE ORDER GOES,
which is what `VenueId` already means. True Markets becomes three venue ids:

    truemarkets-solana
    truemarkets-base
    truemarkets-cefi

WHAT THIS BUYS. The failure D-25 describes - a dropped field silently changing
venue - becomes unrepresentable. You cannot omit the chain, because there is no
chain field to omit; the venue id selects the encoder, and each encoder emits
its own `chain` literal or deliberately omits it for CeFi. A whole class of
mistake is deleted rather than guarded.

It also makes the risk model honest: per-venue limits now actually separate
on-chain exposure from exchange exposure, which a single `truemarkets` venue id
would have silently pooled.

CONSEQUENCE: `hypercore-spot`, `hypercore-perp`, `robinhood` and `arbitrum`
appear in the asset catalog but are not addressable, because `trade_prepare`
accepts solana and base only. **PURR is `hypercore-spot`, so the decision to
trade PURR is withdrawn.** HYPE is reachable as a bridged Solana token (D-21),
but see D-21's warning about which HYPE.

## 5. Decision: quantity is denominated in the currency the side spends

This is the byte contract change.

A swap's `qty` is denominated by `qty_unit`, which is `base` (tokens) or `quote`
(USDC). D-24: its default DEPENDS ON THE SIDE - quote for buys, base for sells.

That default is not arbitrary, and it is worth seeing why, because the reason
tells us what to store. **Each side should be denominated in what it spends:**

* a BUY spends USDC. Denominating in quote micros means the amount at risk is
  exactly the number in the effect, so the risk reservation is EXACT rather than
  an estimate that a price move invalidates.
* a SELL spends tokens. Denominating in base lots means the inventory disposed
  of is exactly the number in the effect.

Denominate either side the other way and the quantity that matters becomes a
function of a price nobody has yet observed. So the venue's defaults are the
risk-correct choice on both sides, and we adopt them - but we store the basis
EXPLICITLY and send it EXPLICITLY on every order.

WHY STORE WHAT COULD BE DERIVED FROM `side`. Because deriving it means the rule
lives in two places - the reducer and the encoder - and if they ever disagree,
a buy is placed for 113 USDC instead of 113 SOL. Storing it costs one byte and
makes the divergence impossible. It also puts the denomination in the replicated
log, where ADR 0007 says the audit lives.

### The change

```
enum class QuantityBasis : std::uint8_t {
    base_lots = 0,      // `quantity` is authoritative; `quote_micros` is zero
    quote_micros = 1,   // `quote_micros` is authoritative; `quantity` is zero
};
```

`OrderEffect` gains:

```
QuantityBasis  quantity_basis{QuantityBasis::base_lots};
std::int64_t   quote_micros{};
```

166 B -> 176 B after padding. Exactly one of `quantity` / `quote_micros` is
non-zero, enforced by a validator, so a malformed effect cannot be committed and
then argued about.

`base_lots` remains the only legal basis for a limit order, so Hyperliquid is
unaffected and the existing goldens change only by the new fields' zero values.

## 6. Decision: the price bound is enforced at the signing step

`trade_prepare` takes no slippage parameter. `get_quote` is explicitly
indicative and is not bound to any subsequent order - there is no quote id
threaded into `trade_prepare`. So there is no API-level way to say "do not fill
me worse than X", and a market order with no bound can be filled at any price.

The bound has exactly one place it can live. `trade_prepare` returns unsigned
payloads; **an unsigned payload does not execute.** So:

    prepare -> read the expected output from the payload
            -> compare against the bound the decision committed to
            -> sign ONLY if within bound, otherwise abandon

Refusing to sign is the slippage control. It is the only one available.

AND THIS IS WHY D-26 IS THE MOST DANGEROUS ROW IN THAT FILE. The venue says: "If
no payloads are returned the order was submitted directly." When that happens
there is no signing step - so there is no bound, and the fill is whatever the
market gave. The two-step is not merely a crash-recovery concern; it is the only
mechanism by which a price bound exists at all, and the venue can remove it
unilaterally, per call, with no warning.

CONSEQUENTLY the adapter treats a payload-free `trade_prepare` response as an
UNBOUNDED FILL THAT ALREADY HAPPENED: it records the effect as completed with
unknown price, and raises a reconciliation obligation (ADR 0008) rather than
retrying. Retrying would double the position. With no open-orders tool (D-23),
that reconciliation reads `get_balances` - position, not orders - which is a
materially weaker signal and must be recorded as such rather than trusted.

## 7. What is still unknown, and it is the number that matters

Everything in section 2 rests on `c`, and every measured value of `c` above came
from Jupiter, not from True Markets. **If True Markets adds a markup, `c` is
that markup plus the route cost, and the markup dominates completely:**

| assumed TM markup | effective `c` on SOL | volume per $100 of budget |
|---|---|---|
| 0 bps | 0.5 bps | $4,000,000 |
| 10 bps | 10.5 bps | $190,000 |
| 30 bps | 30.5 bps | $66,000 |
| 100 bps | 100.5 bps | $20,000 |

A factor of 200 between the best and worst case. No further design decision is
worth making until this is measured.

IT IS CHEAPLY MEASURABLE AND REQUIRES NO ORDER. `get_quote` is read-only - it
"shows the amount you'll receive and any fees" - and comparing its output for a
known notional against the Jupiter route for the same notional gives the markup
directly. It needs a login, and nothing else.

That is the next action, and it gates section 2's arithmetic.

## 8. Consequences

**Foreclosed.** No market making on True Markets, no post-only, no queue
position, no maker rebate, no resting liquidity. Any claim that V2
"market-makes on True Markets" would be false. The significant-figure pricing
work (D-15) is MOOT here - there is no price to render - and defers to the
Hyperliquid adapter.

**Preserved.** The reducer, the lease, the kill epoch, the risk reservation and
the two-step send all apply unchanged. `resolve_two_step_send` was built for
exactly the prepare/execute shape the venue turned out to have.

**Weakened, and it must be said plainly.** Phase 8's mass-cancel has no venue
counterpart here. Market orders do not rest, so there is nothing to cancel; the
safety action on this venue is "stop sending", which the kill epoch already
provides. `RequestMassCancel` remains correct for Hyperliquid and remains
meaningful for internal state, but its venue leg on a swap adapter is a no-op
and will be implemented as an explicit, documented no-op rather than quietly
succeeding.

**A new failure mode.** Section 6's unbounded fill has no analogue on a book
venue. It is the largest single economic risk in this design and the loss budget
must be sized for it.

## 9. Note on the objective

Round-tripping an asset to generate leaderboard volume is recognisable as
volume farming, and the cost in section 2 is real money paid to liquidity
providers on every leg. The venue may also exclude it. That is the project
owner's call and has been made.

The engineering claim worth standing behind is not the volume number, which
anyone with a loop and a budget can produce. It is the one underneath it: that
the system generated that volume under a hard loss budget it never breached,
across crashes, with every decision replicated and auditable, and with an
unbounded-fill hazard the venue's own API cannot express. That is a claim about
control, and it is the one this architecture can actually support.
