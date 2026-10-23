# 0025. Bounded swap verification and policy-bound recovery

Date: 2026-10-14.
Status: retrospective record of owner-approved, implemented bounded-MVP work through
`2e5642a30da3c9cea7e094afc1cbd6a20b2d6e33`. This document grants no new authority.
The latest executable is qualified and staged, not adopted; no cluster live buy has executed.

## Problem and decision

TM's supported DeFi flow supplies an unsigned quote-input swap transaction. It is not
our paper fixed-base limit order. A successful HTTP quote or a displayed fee cannot
prove what a wallet signature authorizes. A lost execution response cannot prove that
nothing happened. Reusing the paper balance would also invent live purchasing power.

The implemented path therefore uses a dedicated bounded swap intent and reducer,
authenticated Raft authority, whole-transaction inspection by a separate signer,
durable submission/recovery journals, and settlement from authenticated venue lookup
plus finalized chain evidence. It does not silently reinterpret a paper order as a swap.
This extends the original ADR0019 mock-adapter plan; production integration required
explicit kernel/reducer contracts rather than only plugging a new venue into its seam.

For this checkpoint the executable supports one supervised SOL purchase and then stops.
Maximum cumulative total USDC debit is 1,000,000 atoms; the quoted explicit fee is
2,000 atoms within that dollar. Conservative maximum additional output deduction is
2,039,280 lamports, with no assumed sponsor credit. The owner accepted this conservative
bound after the earlier settled CLI buy showed a smaller 550,840-lamport deduction.
These are different observations and limits, not interchangeable fee measurements.
The configured realized-loss limit is 500,000 USDC atoms; it is not an automatic
mark-to-market liquidation guarantee. Original price cap is 146,635,156 USDC atoms/SOL.
An optional amendment to 147,537,213 is implemented and qualified but has not been applied.

## Mechanics and invariants

- The kernel applies deterministic commands and records funding, reservations and effects.
  Venue calls, route parsing, TLS and signing remain outside Raft apply.
- Actual reviewed TM balances initialized the account once. Fresh review checks the same
  funding lineage; it never reimports cash or erases activity. Paper cash is unrelated.
- Supported transaction layouts are inspected for account roles, privileges, instructions,
  owner debit, destination and conservative net receipt. Only successful inspection can
  construct the private verified result consumed by signing. Unknown layouts refuse.
- The signer has an independent service UID, authenticated authority reads, protected
  credential handoff and a durable journal. Gateway assertions alone are not signing proof.
- Exact payload, intent and policy identity bind authorization and recovery. Persisted
  submission uncertainty retains exposure; recovery queries the original operation.
  Recovery-only startup cannot create, sign or execute another order.
- Account/release renewal uses locked, hash-checked transitions and retained before/after
  evidence. The existing idle helper only supports the pristine/effect0 state.
- One cluster and its WAL, opening record and journals remain intact. Changing a release
  is not permission to reset funding, reuse operator sequences or delete replay state.

Related decisions: ADR0009 two-step send, ADR0011 swap model, ADR0013 authority,
ADR0017 gateway, ADR0019 inspecting signer, ADR0020 caller roles and ADR0021 accounting.
The [detailed handoff](../../archive/handoffs/LIVE_SWAP_REVIEW_2026-10-14.md) maps implementations,
commits, tests, operational ordering and explicit limitations to retained evidence.

## Qualification and current limit

At `2e5642a`, both NUC and Pi passed 58 selected CTest targets, 241 packaged Python/CLI
checks and eight combined process-drill cases. Local route changes also have oracle,
mutation and sanitizer evidence. These are scoped qualification results, not a claim
that all project tests or a real-money integrated trade passed. See
[qualification](../evidence/bounded_launch_review_2026-10-13/invariant/qualification/).

The captured Invariant layout passed the native verifier with a conditional net floor
of 6,910,173 lamports. Later fresh TM quotes selected Meteora38 and then Deriverse161;
the direct verifier refused both. The installed manifest remains the earlier Byreal
release, while the newer executable is staged. No signed transaction resulted.

Exact deployed Jupiter minimum-output rounding remains a recorded deferral, not a
verified fact or waiver. General route-independent economic guarantees, continuous
strategy execution, sells and multi-effect rearming are not delivered by this checkpoint.
External heartbeat and deployed held-position drills remain open for broader operation.

## Consequence and follow-up

The narrow policy makes authority auditable but also pins transient account arrangements,
lookup data and expiry into persistent identity. Route changes cause repeated adoption
work. That is a limitation of this bounded implementation, not an architectural goal.
[ADR0026](0026-stable-execution-policy-and-per-attempt-route-evidence.md) proposes a
separation; it is not yet approved or implemented. Existing economic and replay bounds
must survive any migration.
