# 0017. The gateway is a separate process that reads committed state and receives authority proofs

Date: 2026-09-29
Status: Accepted in design; the owner chose "separate gateway process on nuc1" and approved new
node-protocol bytes outright. The implementation is **in progress** in
`~/.cache/velocita-work/gateway` and was not finished at the pause. See
`docs/PHASE2_PAPER_HANDOFF.md` §4.2.

Byte contract change: new node-protocol request and response kinds.

## Context

The gateway libraries already existed and had been reviewed (CP-012 closed A01 at library level),
but no process used them:
- `plan_delivery`, `plan_reconciliation` and evidence planning;
- `GatewayAuthority` (one-shot send permission);
- `dispatch_gateway_send`;
- the `VenueTransport` seam.

Two facts shaped the design:

- **`plan_delivery` takes a whole `const TradingState&`.** The gateway needs the committed kernel
  state, not a summary of it.
- **`AuthorityProof` could not leave the process.** It has a private constructor and only
  `ReplicatedKernel` is a friend, so a proof could be held only inside a voter.

Book guidance followed here:
- ch45: one gateway process per venue, separate from the cluster; the gateway never updates
  positions, it submits observed venue events.
- ch46: an ambiguous submission becomes `UnknownSubmission`; stop duplicate risk, then query,
  adopt or retry.
- ch18: every hop an administrative or trading action crosses needs its own answer to "who is
  allowed to do this".

## Decision

1. **State read.** The node answers an authenticated request, only from the caller configured as
   `gateway.caller` (22), with:
   - the **canonical state snapshot** of its applied state (`encode_state_snapshot`, at most
     65,519 bytes);
   - term, applied index, leader id and responder id.

   Reasons:
   - The snapshot is the existing, versioned, digest-checked, fully validated encoding.
   - The gateway decodes it with the same `decode_state_snapshot` that recovery uses, then runs
     the unmodified planners on it.
   - No second, hand-written projection of kernel state is created, so nothing can drift.

   If one Aeron message cannot carry it, it is chunked with (request, i of n, total digest) and
   reassembly is bounded.
2. **Authority request.**
   - The gateway's `GatewayAuthority.begin_request` produces an `AuthorityBarrier`.
   - The leader proposes it (`propose_authority_barrier`). When it commits, the leader answers with
     the encoded `AuthorityProofView`.
   - A voter that is not the leader refuses with `not_leader` and a leader hint.
   - On timeout or leader loss the request is abandoned.
3. **The proof crosses the process boundary through one narrow decoder**, a friend of
   `AuthorityProof`. It builds a proof only from a response that:
   - verified under that voter's response key (the envelope MAC);
   - passed session fencing;
   - matches the pending request's incarnation, authority domain and barrier.

   Forged, replayed and mismatched proofs are refused, and each case is tested. The trust argument
   is written as one sentence in the contracts doc, as ch18 asks.
4. **Loop.** A non-blocking step with an injected clock:
   1. read state → plan → barrier → proof → consume → send through `VenueTransport`;
   2. venue observations → `ObserveFill` / `ObserveOrderEvent` as CallerId 22 →
      `AcknowledgeGatewayResults`.

   The gateway never mutates positions.
5. **Halt on unknown** (owner decision). Any ambiguous send or reconciliation conflict calls
   `stop_new_risk()` and exports `velocita_gateway_new_risk_stopped = 1`, a critical alert. A
   risk-increasing effect is never resent without a fresh proof. Cancels keep the library's
   committed-cancel exception.
6. **Metrics** go through the node-exporter textfile directory, with fixed names (listed in the
   handoff):
   - `velocita_gateway_heartbeat_timestamp_seconds`, because a textfile's `up` goes stale at 1
     after a crash;
   - counters published at 0 from start, so `increase()` alerts see the first event.

## Alternatives rejected

- **A gateway thread inside the voter process.** Simpler: no protocol, and the proof never leaves
  the process. But:
  - the book puts the venue boundary outside the cluster;
  - a venue-side hang or crash would take a voter with it;
  - the voter would need venue reachability and, later, venue credentials.

  The owner chose the separate process.
- **A new outbox-only read message** (just pending effects plus their bindings). Smaller on the
  wire, but a second hand-written projection of kernel state. `plan_delivery` and reconciliation
  need orders, reservations, receipts and catalog context, which is most of the state anyway.
- **Trusting the network instead of authenticating the proof.** Rejected under ch18. Reachability
  of a port is a materially weaker boundary than a MAC under a per-voter key.

## Consequences

- A 64 KiB snapshot per read limits the read rate. Paper cadence is 30 s ticks and a single-digit
  number of orders a day, so this is irrelevant now. If it ever matters, change the read, not the
  proof path.
- Adding a gateway requires a node-config change (`gateway.caller`) on every voter.
