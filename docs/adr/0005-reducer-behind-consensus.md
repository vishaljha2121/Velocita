# ADR 0005: The deterministic reducer sits behind consensus, in a separate node

Date: 2026-09-15
Status: Accepted
Implements: Phase 6, guarantees G-035 through G-041.

## Context

ADR 0001 puts the deterministic reducer at the centre and everything untestable
outside it. Phase 6 places it behind Raft without coupling domain logic to the
transport or the Raft library.

V1 already had a working `RaftNode`: willemt/raft, an Aeron transport, a
write-ahead log and metadata, driving a `CounterFsm`. That node is accepted and
gated. Its storage, peer-wire and checksum layers are already payload-generic -
`WalRecord::payload` and `PeerMessage::payload` are byte vectors - and only
`Command`, `Response` and `CounterFsm` are V1-specific.

## Decision

Build `ReplicatedKernel` as a SEPARATE class rather than generalising V1's
`RaftNode`. It reuses the payload-generic storage, peer wire and checksum layers
unchanged; what is duplicated is the Raft callback table and nothing economic.

The reducer does not change. It gains nothing, loses nothing, and does not know
it is replicated.

`ReplicatedKernel` has EXACTLY ONE call to `apply()`, inside the Raft apply
callback. G-035 is therefore the shape of the code rather than a rule it follows:
there is no other path from a command to the reducer, so a command that is
proposed and never commits cannot have an economic effect.

Restore is by REPLAY of the committed prefix, not by trusting a stored state
blob. Snapshot install covers a prefix that has been reclaimed and is trusted
only because the canonical decoder revalidates every invariant before accepting
it.

A retry is NOT short-circuited from the retained receipt. It is proposed like
anything else, and the reducer answers it with the original result when it
commits.

The convergence oracle is the canonical snapshot digest.

## Consequences

V1's accepted gates stay valid, at the cost of about three hundred lines of
duplicated callback boilerplate. That is the trade: an accepted artifact is not
put at risk to save boilerplate.

Because restore replays, a determinism divergence SURFACES instead of hiding
behind a state file.

Because a retry is proposed rather than served locally, a duplicate log entry is
written. The cost is bounded by the caller's own retry policy; the alternative
costs a read-index discipline that Phase 6 does not otherwise need.

`CommittedOutboxView` pairs committed outbox bytes with the authority they were
read under, since `TradingState` alone cannot say whether it is current and a
bare reference invites acting on a deposed leader's copy. It proves only that
this node believed itself leader at the read; a read-index round trip is the
stricter form and is deferred.

G-041 is closed as the STATEMENT it always was: three voters in one process prove
process-fault and partition behaviour and nothing about host failure, which is
what ADR 0002's two-machine topology is for.

## Alternatives rejected

GENERALISE V1's `RaftNode` over its state machine. Five touch points, so it looked
cheap. It would have put an accepted, gated artifact at risk for a saving
measured in boilerplate, and V1's `Command`/`Response` wire is V1-shaped anyway.

MODEL RAFT RATHER THAN USE IT. A faithful model would have proved the seam
against a model, and the gate asks for one and three voters - which the real
library already provides.
