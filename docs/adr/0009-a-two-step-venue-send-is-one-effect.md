# 0009. A two-step venue send is one effect

Date: 2026-09-17
Status: Accepted

No byte contract change. No change to the reducer. This is about what the
adapter owes the gateway.

## Context

The DeFi order path is not one request. Creating an order is:

  1. `POST /orders` - the venue returns UNSIGNED PAYLOADS for client signing, and
     the order exists at the venue in `initialized`, documented as "awaiting
     client signature";
  2. the client signs;
  3. a second call submits the signatures, and the order becomes `active` -
     "limit order resting in order book".

Cancellation has the same shape: `POST /orders/{id}/cancel` prepares, and a
separate call executes the signed cancellation.

Phase 5's model assumes a send is ONE act with three possible answers: it
arrived, it did not, or we cannot tell. Two steps give TWO ambiguity points and
an intermediate state that is neither "not sent" nor "working".

## Decision

A TWO-STEP SEND IS ONE `send()`. The `VenueTransport` boundary already says so -
"a send is one call from this side however many round trips it costs on the
other" - and this ADR makes that a rule the adapter must satisfy rather than a
comment. The gateway never learns that the venue has two steps, because nothing
in the cluster's model could use that knowledge and everything in it would be
complicated by it.

THE OUTCOME MAPPING, which is the whole safety content:

| What happened | Outcome | Why |
|---|---|---|
| Step 1 refused cleanly | `pre_send_failure` | nothing at the venue |
| Step 1 ambiguous | `pre_send_failure` | see below |
| Signing failed | `pre_send_failure` | nothing was submitted |
| Step 2 refused cleanly | `business_rejected` or `pre_send_failure` | nothing became active |
| Step 2 ambiguous | `ambiguous` | it may be resting; this is the dangerous one |
| Step 2 accepted | `accepted` | |

STEP ONE'S AMBIGUITY IS REPORTED AS `pre_send_failure`, AND THAT IS THE ONE
CLAIM HERE THAT COULD BE WRONG. An `initialized` order cannot fill - it is
awaiting a signature that was never sent - so it holds no exposure, and telling
the gateway "nothing is at the venue" is true in the only sense the gateway
cares about: nothing can move a position.

THIS RESTS ON A DOCUMENTED FACT AND FAILS WITH IT. If the venue could ever
activate an order without the client signature, then step 1 creates live risk and
the mapping above is unsafe. The documentation is explicit that `initialized`
means "awaiting client signature", and that is the load-bearing sentence. It is
recorded in `TRUEMARKETS_DOC_DISCREPANCIES.md` as a dependency, because the
documentation has already changed under us once.

ABANDONMENT IS THE ADAPTER'S JOB, NOT THE GATEWAY'S. A step-1 success followed by
a signing or step-2 failure leaves an `initialized` order at the venue. The
adapter attempts to abandon it, and a failure to abandon is NOT escalated: the
order cannot fill, so a litter of unsigned orders is an operational untidiness
rather than an economic risk. Escalating it would turn a harmless outcome into a
gateway-visible failure and, worse, teach the gateway about steps.

## Alternatives rejected

EXPOSE THE INTERMEDIATE STATE TO THE CLUSTER, as a new `ClusterOrderState` or a
new observation kind. This is a material byte contract change to represent
something with no economic content - an order that cannot fill and holds no
reservation. It would also make every venue with a one-step API carry a state it
never enters.

RETRY STEP 2 INSIDE THE ADAPTER after an ambiguous submission. Tempting, because
the signatures are already in hand. Rejected: a second submission of the same
signed payload is exactly the duplicate that `EffectId` idempotency exists to
make safe, and deciding it is safe is the VENUE's call to make, not ours. The
gateway's ambiguous-send reconciliation already handles this correctly by asking
what is actually resting, and it does so with the cluster's full picture rather
than the adapter's partial one.

## Consequences

The adapter needs somewhere to keep an in-flight order id between steps. That is
adapter-local and non-durable: a crash mid-send is indistinguishable, from the
gateway's side, from an ambiguous send, and reconciliation already covers it.

`SendOutcome::rate_limited` becomes reachable at either step and means the same
thing at both - nothing is at the venue, pace yourself.
