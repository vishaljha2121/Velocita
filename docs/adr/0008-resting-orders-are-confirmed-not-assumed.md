# 0008. A resting order is confirmed, not assumed

Status: accepted, 2026-09-17. Approved by the project owner before encoding.

Amends the containment story in ADR 0007. Changes accepted Phase 4 economics.
Introduces NO byte contract change.

## Context

Writing the G-060 test found that the cluster can believe an order is resting at
a venue that no longer holds it, indefinitely, with nothing raising it. The
reservation stays held, the logical slot stays occupied, and `TradingState` is
silently wrong.

THE FIRST DIAGNOSIS WAS WRONG AND IS WORTH RECORDING. It looked like V2 had no
way to express "the venue does not hold this order", which would have made this
the same class as ADR 0004 - a thing the contract cannot represent - and
therefore a byte contract change. It is not. `order_reconciled` with a zero
`venue_order_id` already means exactly that, and `ReconciliationNeed` is
gateway-local and non-durable. What was missing was not vocabulary but three
rules:

  * `need_of` returns `none` for a healthy resting order, so reconciliation
    NEVER ASKS about one;
  * `order_reconciled` is applicable only to `submission_unknown` and
    `dispatch_authorized`, so even if it asked, the answer would be refused;
  * a native `cancelled` is applicable only against a cancel the cluster
    authored, so the venue telling us unprompted is refused too.

The exposure is not exotic. Exchange-initiated cancellation is routine on crypto
venues - a post-only order that would cross, self-trade prevention, a market
halt, a reduce-only violation - so this fires in normal operation and not only
when someone acts at a console.

## Decision

A RESTING ORDER IS CONFIRMED, NOT ASSUMED. Three changes, none of them to bytes.

1. AN UNSOLICITED `cancelled` MOVES A RESTING ORDER TO `reconciling`.

   Not to `cancelled`, and not to `conflict`. `reconciling` is what an AUTHORED
   cancel already produces, because `cancelled` is a notification in this design
   rather than terminal evidence - settlement comes from `cancel_reconciled`
   with final cumulative quantity. Treating the unsolicited case identically
   means it reuses machinery that is already built and tested, and it SELF-HEALS:
   the reconciliation path queries, the answer settles the order, and no operator
   is involved.

   The alternative of `conflict` was rejected for a specific reason. Conflict is
   terminal-but-stuck and blocks a resume under G-059, so a routine post-only
   rejection would wedge trading until a human cleared it. A safety rule that
   fires constantly during normal operation is a safety rule that gets disabled.

2. RECONCILIATION ASKS ABOUT RESTING ORDERS. A new `ReconciliationNeed` covers
   `acknowledged` and `live` orders, so a cancellation whose event never arrived
   - a dropped websocket, a reconnect gap - is still discovered.

   THE CADENCE IS DELIBERATELY SLOWER AND UNCAPPED, and this is the part that
   differs from every other reconciliation need. The existing needs are about
   something already known to be wrong, so they are urgent and give up after
   `max_query_attempts` to avoid hammering a stuck venue. Confirming a resting
   order is a HEARTBEAT: not urgent, and there is no "give up", because giving
   up returns the system to exactly the silent state this ADR exists to end. The
   bound is therefore the RATE, not the count.

3. `order_reconciled` BECOMES APPLICABLE TO `acknowledged` AND `live`. Without
   this the query in (2) would be asked and its answer refused.

## What is deliberately NOT changed

THE ASYMMETRIC DISAGREEMENT RULE STANDS UNCHANGED (architecture 10.3.4). A venue
reporting MORE filled than the cluster booked is missing evidence - wait, do not
resolve. A venue reporting LESS is a genuine conflict - terminal, reservation
retained, that slot blocked and every other slot still trading. This ADR adds a
new way to reach that comparison; it does not touch the comparison.

Cluster-wide escalation was considered and rejected again, as architecture 10.1.5
already rejected it: one unreconcilable order must not halt everything.

## Consequences

An order can now leave `live` without the cluster having authorized anything,
which is new. It cannot leave with its risk released on the venue's word alone -
that still requires cumulative evidence through the existing path.

Reconciliation traffic gains a steady-state floor proportional to resting orders,
where before it was zero when nothing was wrong. That is the price of not
assuming, and the rate bound is what keeps it bounded.

G-060's visibility half closes with this. The safety half was already closed.
