# ADR 0003: Native and synthetic observations are different identity domains

Date: 2026-09-15
Status: Accepted
Supersedes: nothing. Refines the `VenueObservation` contract frozen in Phase 5.

## Context

Every `VenueObservation` carries a `(VenueId, VenueSessionId, VenueSequence)`
triple, and the reducer deduplicated all of them on it. Only some of them own
one.

`order_acknowledged`, `order_rejected`, `fill` and `cancelled` are the venue's
own events. The sequence is the venue's, monotonic because the venue says so,
and the triple is a real identity a cluster may refuse a repeat on.

`effect_unknown`, `order_reconciled` and `cancel_reconciled` are authored by the
GATEWAY about an order. A declared timeout is not a venue event. A query answer
is a snapshot rather than an event and has no venue sequence at all. Whatever
number reached the wire was chosen by the shell, and nothing coordinates that
choice with the venue's counter.

The shared domain costs real evidence in both directions, and both are reachable:

- a gateway-authored `effect_unknown` takes sequence N; the venue later answers
  the same cancel at its own sequence N; the native cancellation is refused
  `venue_event_identity_conflict`. Real venue evidence discarded over a number
  nobody owned.
- the venue used N earlier; a reconciliation answer carrying N is refused the
  same way. That answer is the only thing that can resolve an unknown effect, so
  the order is stranded with nothing able to reach it.

## Decision

Derive a two-valued origin from the observation kind by a total switch, and
retain it in venue-event result metadata word 1 - the word left free when
proof-carrying reclamation put the owning `EffectId` in word 0.

Sequence deduplication searches the NATIVE domain only. Synthetic replay is
protected by two mechanisms that were already present:

1. the retained request receipt, which answers an identical command with the
   ORIGINAL RESULT rather than a bare acknowledgement - a gateway that lost a
   result needs to learn what the result WAS; and
2. state-based applicability. While an effect is unresolved, a reconciliation
   verdict is a function of the final cumulative total and the evidence already
   booked, not of message history. Once an answer resolves the effect, the same
   observation under a fresh identity is refused
   `venue_observation_not_applicable` and never enters an economic delta path.

Fills are unchanged: their authoritative economic identity is
`(VenueId, ExecutionId)`.

## Consequences

Zero bytes added. `ApplyResult` stays 112 bytes, `TradingState` 81,360 with 560
bytes of headroom, maximum canonical snapshot 65,451. Retained result VALUES
change, so kernel-codec and state-snapshot schemas move V6 to V7 and the golden
corpora were regenerated as a reviewed migration.

Command identity becomes load-bearing for RECONCILIATION and not only for
delivery. A gateway that ever retried an evidence obligation under a fresh
identity would double-adjudicate, so that rule is pinned by a test rather than
left as an inference.

Behaviour change, recorded because a test had pinned the old outcome: a synthetic
answer re-presented under a fresh identity after its effect has resolved is now
refused `venue_observation_not_applicable` rather than `already_applied`. The old
refusal asserted that an identity was taken; the new one states a fact about the
order.

The decoder validates the origin word as an exact enum value, because the runtime
accessor resolves anything unrecognised to `native` - correct at runtime, and
exactly the wrong thing to accept from a decoder, where a forged word would move
a gateway-authored receipt into the venue's identity domain.

## Alternatives rejected

RESERVE A NUMERIC BAND for gateway-authored sequences. Prevents a collision only
for as long as the assumption about the band holds, and a real venue's sequence
is whatever that venue emits - the gateway cannot know it in advance. An
assumption that fails once and fails silently is worse than no assumption.

ADD A WIRE FIELD for the origin. The kind already says it. A second field can
disagree with the first, and a byte contract that can contradict itself is worse
than one that cannot.
