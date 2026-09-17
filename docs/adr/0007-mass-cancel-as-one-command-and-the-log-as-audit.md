# 0007. Mass-cancel is one command, and the log is the audit record

Status: accepted, 2026-09-17. Approved by the project owner before encoding, per
the standing rule that a new material byte contract is reviewed first.

Supersedes nothing. Constrains Phase 8's remaining work.

## Context

Phase 8 gave the emergency path two mechanisms and no cargo. A reserved send
lane (`SendQuotaView::emergency_lane_remaining`) and a reserved propose lane
(`ProposeLane::control`) both exist, are enforced, and are mutation-checked. What
does not exist is the operation an operator actually reaches for: cancel
everything, now.

G-057 says the emergency lane may "stop, cancel, mass-cancel, or reconcile".
Stop exists - `SetStrategyMode` to `halted`, and `AdvanceKillEpoch` halts as a
side effect. Single cancel exists. Mass-cancel does not.

## Decision 1: mass-cancel is ONE command, not N

`RequestMassCancel` is added as a new `KernelCommandKind`, carrying a scope
(all, account, or account-and-instrument) and the usual expected kill epoch and
executor generation.

THE ALTERNATIVE WAS FREE IN BYTES AND WRONG IN BEHAVIOUR. The gateway could
issue N individual `RequestCancel` commands on the control lane, needing no new
encoding at all. Two reasons that fails:

  * IT CAN DRAIN THE RESERVE IT SHARES. `kControlLaneReservedBlocks` is 4.
    Sixteen cancels are sixteen log entries, and a mass-cancel that consumes the
    reserved capacity has starved the kill path it was meant to sit beside -
    the precise failure the reservation exists to prevent, arriving from inside
    the lane rather than from a strategy flood.

  * IT IS NOT ATOMIC. A mass-cancel that stops half way leaves some orders live
    and no record of the intent to cancel them. The operator believes the book
    is flat. Under a single command the reducer either cancels the whole scope
    or refuses it, and the refusal is itself a durable, attributable fact.

Bounded by construction: the scope can name at most `kMaxClusterOrders` orders,
which is 16, and the outbox that must carry the resulting effects is the same
size by static assertion. So a mass-cancel cannot ask for more than the state can
represent - it either fits or is refused, and neither outcome is partial.

Appending a `KernelCommandKind` is ADDITIVE. The codec tags kinds by explicit
enum value rather than by variant index, so every existing kind keeps its number
and no already-encoded command changes. The new variant is small against the
1789-byte canonical command budget.

## Decision 2: G-060 means the emergency path cannot bypass the log

"Every emergency external action is recorded and reconciled into authoritative
cluster state" reads two ways, and they cost very differently.

THE READING ADOPTED: the guarantee is that no emergency action reaches a venue
except through a committed command. There is no side door - not an operator at
the venue's own console, not a gateway acting on its own initiative. This is
already true by construction, because the only path to a venue is a materialized
`OrderEffect` derived from committed state, and it needs a test and an explicit
statement rather than new bytes.

THE READING REJECTED: that replicated state must additionally record WHICH
cancels were emergency ones, by stamping each `ClusterOrder` with the kill epoch
it was authorized under. Rejected for two reasons, in order of weight:

  * THE LOG ALREADY IS THAT RECORD. The write-ahead log holds the
    `AdvanceKillEpoch` and the cancels that followed it, in commit order, with
    the caller identity of each. An auditor asking "which cancels were the
    emergency" is asking a question about a sequence, and the sequence is
    durable. Copying a derivable fact into state is not more evidence, it is a
    second place for it to disagree.

  * IT SPENDS SCARCE BUDGET ON A DERIVED VALUE. Sixteen orders at eight bytes is
    128 of roughly 560 bytes remaining under the 81,920-byte `TradingState`
    budget, and it moves the canonical snapshot and every snapshot golden.

WHAT THIS OBLIGES US TO SAY PLAINLY, rather than leave implied: an action taken
at the venue outside the cluster is NOT SUPPORTED and is not made safe by this
decision. See the amendment below for what actually happens if one occurs; the
first version of this ADR got it wrong.

## Amendment, 2026-09-17: an out-of-band venue action is not representable

The paragraph above originally claimed that an action taken at the venue outside
the cluster would reconcile back through `ObserveOrderEvent`, leaving the order
in `conflict` or `reconciling` and blocking a resume by G-059. WRITING THE TEST
FOR THAT CLAIM SHOWED IT IS FALSE, and the correction matters more than the
original claim did.

There is no evidence kind that says "the venue cancelled an order you never
asked to cancel". `cancelled` is only applicable against a cancel the cluster
authored. `order_reconciled` answers "the venue STILL HOLDS this order, with
this cumulative fill" - it cannot express absence. `cancel_reconciled` says a
cancel took effect, and is likewise tied to an effect the cluster authored.

So an unrequested `cancelled` for a live order is refused with
`venue_observation_not_applicable`, and the cluster goes on believing the order
is live.

THIS IS THE SAME SHAPE AS ADR 0004. V2 cannot represent a position change it did
not authorize; that is why perpetuals are out of MVP V1, and an out-of-band
cancellation is a member of the same class. The honest containment is therefore
NOT "we reconcile it" but the stronger and simpler statement:

  An out-of-band venue action cannot be represented, which is precisely why the
  emergency path must go through the cluster. The guarantee is not that we
  recover from a side door; it is that there must not be one.

WHAT IS ACTUALLY GUARANTEED, and is now tested: the cluster refuses to ACT on
such evidence rather than absorbing it. A venue cannot unilaterally free reserved
risk by announcing a cancellation the cluster never authored - the order stays
live, the reservation stays held, and economics do not move. That is the safety
half and it holds.

WHAT IS OPEN, recorded rather than closed by redefinition: the VISIBILITY half.
The cluster's book and the venue's book can diverge silently, and nothing today
raises that. Closing it needs an evidence kind for "the venue does not hold this
order", which is a material byte contract and a change to accepted Phase 4
economics. It is not folded into this ADR.

## Consequences

Two pinned corpus digests will move again if the generated corpora learn the new
command. That is a reviewed fixture change and must be recorded as one.

The control lane's admission check must learn `RequestMassCancel`, or the
operation this ADR adds will be refused by the lane built to carry it.
