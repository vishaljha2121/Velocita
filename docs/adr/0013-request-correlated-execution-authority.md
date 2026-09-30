# ADR 0013: Request-correlated execution authority

- Date: 2026-09-22
- Status: **APPROVED; host-library composition implemented in CP-012; deployment acceptance pending**
- Scope: roadmap F4 / audit A01; ADR 0001 execution boundary
- Detailed review: `../F4_AUTHORITY_PROTOCOL_REVIEW.md`
- Approved byte/admission contract: `../F4_AUTHORITY_BARRIER_BYTE_REVIEW.md`
- Evidence: `../evidence/f4_authority_2026-09-22/README.md`

## Context

An isolated old leader can renew the gateway from its own role bit after a
majority commits kill. A delayed reply can also acquire a fresh timestamp on
arrival. Both fail on the current catalog-bound schema-8 tree through public
interfaces. Catalog correctness and committed origin do not prove current send
authority. A new material protocol needs owner approval, not an incidental fix.

## Approved semantic decision

For MVP, use one freshly committed, current-term, replication-only barrier for
each new-risk attempt. It changes no reducer state/receipt or order economics.
After commit AND apply, the same owner captures the exact bound effect and
controls. Return permission only for the matching live request while still
leader in that term. Replay/restart never issues permission.

The gateway records a suspend-aware local start time for its unique process
incarnation/request sequence, accepts only the matching unused response and
expires permission from that original start. It consumes permission once at
the final transport-owner check, after asynchronous preparation. Default live
bound is zero/disabled; deployment must separately justify a nonzero bound.

Preserve known committed cancel availability under quorum loss, bounded retry
and emergency quota. Separate stop-new-risk from stop-all-transport. Neither a
delayed reply nor a leadership change clears local stop latches or promotes a
gateway. External fencing and reconciliation remain mandatory before replacement.

The owner separately approved durable type 101, the 186-byte schema-1 V2AB frame,
explicit domain equality and retained-marker drain admission, with its old-term
availability cost. V1 stays unchanged; no TradingState/snapshot budget increase.
Do not request these approvals again. Production static-domain derivation,
authenticated IPC and deployment freshness remain separate open boundaries.

## Alternatives and costs

True ReadIndex is valid and cheaper per read, but this pinned library lacks
request-correlated heartbeat/read support and a current-term no-op bootstrap.
Adding that correctness surface is deferred until justified by measurements.
The committed barrier costs a durable quorum round trip and WAL record per
attempt, making F5 bounded retention and F6 I/O behavior deployment dependencies.
It must not consume the emergency WAL reserve. Idle gateways append no markers.

Reusing a business control command was rejected because it churns business
receipts/StateVersion and can exhaust them. Reusable timed grants broaden the
stale-permission window across multiple sends; one-shot is the smaller MVP
contract. A local role bit or uncorrelated heartbeat cannot prove the request.

## Limits and acceptance

Quorum evidence proves an ordered point, not a lock against subsequent kill or
an external fence. An unseen remote change can occur inside the freshness
window; a request already in flight cannot be recalled. Arbitrary pause between
clock check and transport handoff prevents an unconditional wall-clock guarantee
without enforcement beyond that check. Deployment must state the timing model;
do not close G-055 on an application timer alone.

The detailed review lists partition, delay, duplicate, restart, clock, saturation,
kill, cancel and transport cut tests. Scope is the library composition first;
authenticated process integration, persisted membership and target gates remain
necessary. F4 host composition is complete; no live sender is authorized and
deployment guarantees remain open.

## CP-012 implementation and review boundary

The producer alone constructs `AuthorityProof`, copying exact historical effect
and catalog binding after live current-term marker commit/apply. A fixed result
slot is revalidated at dequeue, never replayed or reconstructed as authority.
The gateway's noncopyable owner accepts exact correlation once and consumes its
private permission before final synchronous handoff. Copying proof bytes does
not copy owner permission. This is accidental-misuse protection, not protection
from malicious code in the same address space.

`dispatch_gateway_send` runs after asynchronous preparation, on one owner with
stable inputs and a non-reentrant clock callback that cannot mutate admission
state. It checks eligibility, exact prepared value, known fences and final age,
records delivery uncertainty and quota, then calls transport with an owned value.
The callback must be the handoff boundary, not another unchecked work queue.
Exceptions after entry remain ambiguous. Boolean role renewal always refuses;
candidate filtering is explicitly not send authority.

Availability cost is deliberate: malformed/late/wrong replies drop their pending
request, refused final attempts drop permission, and lost/abandoned producer
results require a new attempt. The retained marker drain still blocks fresh
issuance until apply/overwrite, never normal/control work. No automatic promotion,
new no-op or emergency-reserve borrowing is introduced to hide this cost.

Library tests inject identity and integer clocks. Production incarnation entropy,
membership-bound domain, authenticated IPC/activation, suspend-aware clock,
measured nonzero duration and real encoding/transport remain separate integration
gates. Evidence: `../evidence/f4_proof_2026-09-22/README.md`. The explicit unseen
remote-change/in-flight limitations above remain part of acceptance, not bugs
purportedly fixed by this implementation.
