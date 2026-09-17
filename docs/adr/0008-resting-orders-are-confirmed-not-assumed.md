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

## Implementation status, 2026-09-17

ALL THREE RULES BUILT AND MUTATION-CONFIRMED, five mutants, each dying on the
assertion that names its property.

A FIRST ATTEMPT AT RULES 2 AND 3 WAS BACKED OUT AND THE REASON IS WORTH KEEPING.
The refusal being chased was blamed on the applicability gates, and two of them
were patched on that theory. The actual cause was the TEST FIXTURE: `validate`
requires a venue sequence on every observation, including a synthetic one, and a
zero there produced `invalid_identity` - which reads exactly like an
applicability refusal from the outside. The instrumentation used to find it was
also misattributing single-line returns, and the `[NA]` lines being read came
from other tests in the same suite run.

Re-done as a focused pass, the function's twelve refusal sites were enumerated
first and the two that could fire for this case identified BEFORE any change.
Both were the ones patched originally. The work was never wrong; the diagnosis
was, and the lesson is that a refusal code says which check failed only if you
confirm which check ran.

TWO ACCEPTED TESTS CHANGED THEIR EXPECTATIONS, which this ADR authorises and
which should not pass unremarked:

  * `test_replaying_the_same_answer_changes_nothing` required a repeat answer to
    be refused because the effect was already resolved. Under rule 2 a resting
    order is re-confirmed on an uncapped heartbeat, so refusing repeats is the
    "assume" behaviour being removed. The old comment already conceded that
    admitting it was harmless by value - adjudicating a final cumulative total
    against what the cluster holds computes a delta of zero - so the refusal was
    the tidier statement, not the safer one.

  * `test_a_reconciliation_answer_is_refused_where_it_cannot_apply` required a
    live order to reject a reconciliation answer because it "needs none". That
    is the clearest statement of the assumption this ADR overturns.

Both now require the answer to be ADMITTED AND TO MOVE NOTHING, which is what a
confirmation should look like.

ONE LABEL CHANGED. An absent answer for an order the venue never acknowledged
still resolves to `rejected` - the venue refused it. An order the venue
ACKNOWLEDGED and later stopped holding resolves to `cancelled`, because it was
not refused, it is gone. Economically identical - terminal, zero fills,
reservation released - so this is about not telling an operator something false.

## Consequences

An order can now leave `live` without the cluster having authorized anything,
which is new. It cannot leave with its risk released on the venue's word alone -
that still requires cumulative evidence through the existing path.

Reconciliation traffic gains a steady-state floor proportional to resting orders,
where before it was zero when nothing was wrong. That is the price of not
assuming, and the rate bound is what keeps it bounded.

G-060's visibility half closes with this. The safety half was already closed.
