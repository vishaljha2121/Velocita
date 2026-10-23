# 0026. Stable execution policy and per-attempt route evidence

Date: 2026-10-14.
Status: **ACCEPTED (narrowed) 2026-10-14 — implemented locally, not committed, qualified or deployed.**
Owner approvals, 2026-10-14: the narrowed design below; lookup tables read over a public RPC by the
gateway; at most three fresh quotes at launch, each refused before signing if unsupported. No new
DEX is trusted by this change; the five already reviewed DEX programs are the allowlist.

## Problem

TM selects the DEX route and may change it between quotes. The current bounded verifier
pins one transaction topology and its account arrangement. In
`src/v2_swap_transaction_verifier.cpp`, `direct_buy_policy_digest` binds route/account,
lookup and expiry data. `src/v2_swap_signer_session.cpp` includes that policy in protected
journal binding. `deploy/v2/swap-runtime/idle_policy_renewal.py` also includes transaction
review in runtime identity. Transient evidence has become persistent authority.

Consequently, moving from a captured Byreal transaction to Obsidian, Bison or Invariant
required code/review transitions; the next fresh quotes selected Meteora and Deriverse
and were refused. A new venue-selected pool should not itself require reopening a funded
account. However, an unfamiliar program's authority cannot be accepted merely to avoid
this inconvenience. There are two separate problems: identity churn and semantic support.

## Proposed separation

| Layer | Stable or per attempt | Responsibility |
|---|---|---|
| Account authority | Stable, explicitly versioned | Owner, wallet, chain, canonical asset, account/caller roles, original funding and journal lineage |
| Execution policy | Stable until reviewed change | Allowed operation, total debit, fee/deduction bounds, minimum net receipt, supported program/contracts, limits and policy version |
| Attempt evidence | Fresh and immutable for one intent | Exact payload digest, blockhash/expiry, resolved accounts and lookup provenance, selected route, quote and verified economic result |
| Settlement evidence | Bound to original attempt | Venue operation identity, transaction signature, finalized receipt and wallet deltas, committed result |

Moving expiry into attempt evidence does not eliminate authority expiry, credential
renewal, fresh account reconciliation or quote freshness. Limits retain their own
reviewed validity. Exact bytes must still be authorized; the proposal changes which
record owns evidence, not whether it is checked.

The first implementation should remove adoption churn for already-supported routes,
using a bounded, versioned registry of native verifier contracts. Resolve account keys
per attempt and validate their semantic roles and authenticated provenance instead of
turning an arbitrary captured pool list into permanent account identity. Unknown
programs/layouts continue to refuse. Adding support remains a reviewed policy/code change.
Do not build a generic plugin framework for this MVP.

A second, conditional option is a verified aggregator-level contract covering several
routes. It is viable only if inspection can establish total wallet debit, guaranteed
net receipt after all fees/repayments, destination ownership and permitted privileges
across the delegated calls. Program ID alone is not proof of deployed code semantics.
Define program upgrade/version trust and exact deployed minimum-output rounding first.
Do not equate decoding a Jupiter route tag with validating its DEX's behavior.

## Independent authorization and recovery

The gateway may collect evidence but cannot manufacture a trusted verification result
for the signer. The signer must independently validate exact bytes and authenticated
account/lookup evidence. Its restricted networking means evidence provenance must have
an explicit, reviewable mechanism; arbitrary gateway JSON is insufficient. Do not remove
isolation or pretend a simulation is an on-chain execution guarantee to solve this.

Persist attempt ID, policy version/digest, exact payload/evidence binding and submission
identity before the corresponding external side effect. Recovery retains the original
policy and attempt until terminal reconciliation; a newer route or policy cannot replace
an ambiguous operation. Specify bounded retention/refusal when storage is full. No
unbounded policy history and no automatic replay under today's configuration.

Existing protected journals embed the old policy identity. Removing a digest field in
place would weaken replay protection. Implement an explicit, locked migration with
before/after witness and rollback/recovery rules. Keep old-policy recovery supported
when an operation is pending, or refuse migration until terminal settlement. Never
reset journals, re-fund the account or disguise a pending operation as pristine.

## C++ and system-design requirements

Use small, bounded, owning evidence/attempt records; decoding spans may not outlive their
source buffers. Successful validation should construct an immutable private result that
binds payload, intent, policy and calculated bounds. Avoid a public boolean flag that a
caller can set to bypass validation. Check integer overflow, units and conservative
rounding before converting to fixed-width economic fields.

Keep external I/O and route discovery outside deterministic Raft apply. Shared kernel
changes are needed only if durable command semantics lack a required binding; do not
assume either a kernel rewrite or zero schema changes before reviewing the contracts.
Preserve the single writer and lock ordering for each journal. No new asynchronous buffer
sharing without explicit ownership and failure behavior.

Parsing is bounded by maximum bytes, instructions and accounts. Linear scans are suitable;
small pairwise alias checks may be quadratic in the fixed account cap. State the actual
caps and worst-case work. More semantic coverage improves availability but adds trusted
code and inspection cost; reducing identity churn should not weaken validation to save
latency. No performance benefit is claimed before measurement.

## Alternatives and tradeoffs

- Continue one-capture/one-layout patches: smallest immediate local changes, repeated
  rebuild/adoption work and no guarantee the next quote uses that layout.
- Ask TM for documented route selection: potentially simpler, but no such capability
  is established in the current recorded contract. Do not invent request fields.
- Trust opaque TM payloads: fewer checks, but discards the independent signer guarantee;
  rejected under the current authorization boundary.
- On-chain guard or independently constrained wallet: may enforce broader limits, but
  requires supported custody/transaction changes and is outside the present MVP unless
  review establishes necessity and owner authorization.

## Acceptance evidence before adoption

1. Two already-supported routes under identical account/economic policy verify without
   account adoption or funding changes; each has distinct exact attempt evidence.
2. The original attempt recovers unchanged across route/policy renewal and crashes before
   and after journal writes. Unknown submission retains reservation and cannot send again.
3. Named negative tests reject changed wallet/mint/destination, extra debit or instruction,
   low net receipt, excess fee/repayment, writable/signer escalation, account aliases,
   stale/forged lookup evidence and payload substitution after validation.
4. Old/new identity migration has deterministic digest parity, retained replay protection,
   fail-closed partial-transition recovery and bounded policy retention.
5. Native NUC/Pi qualification and existing combined drills pass from the scoped commit.
   A broad aggregator path additionally needs verified deployed semantics/rounding and
   evidence provenance; historical simulation alone does not meet this criterion.

These are acceptance conditions for this proposal, not extra unrelated live features.
The next agent should explain its minimal design and trust changes before implementation.

## Source basis and limits

The following local book chapters were read for this recommendation:

- `~/Desktop/Trading/velocita-book/chapters/46-exchange-connectivity.tex`, lines37–49,
  103–165 and653–664: isolate venue I/O, retain capabilities/raw evidence, keep the adapter small.
- `19-cig-custody-follower.tex`, lines1219–1295: observe/decide/act/confirm and durable outbox.
- `34-security-and-secrets-celeritas.tex`, lines16–37: bytes becoming authority require
  authentication, authorization and audit.
- `50-position-pnl-ledger-recon.tex`, lines98–122: authority/evidence versus read models.

The stable-policy/per-attempt split is our inference from these principles; the book does
not supply a TM/Jupiter verifier or prove arbitrary routes safe. Solana's
[CPI documentation](https://solana.com/docs/core/cpi) describes propagated signer/writable
privileges; [simulation documentation](https://solana.com/docs/rpc/http/simulatetransaction)
describes a simulation, not a transaction broadcast or future-state guarantee.

See [ADR0025](0025-bounded-swap-verification-and-policy-bound-recovery.md) for implemented
behavior and the [handoff](../../archive/handoffs/LIVE_SWAP_REVIEW_2026-10-14.md) for source/evidence pins.

## As built (2026-10-14, local, uncommitted)

**Stable execution policy.** Transaction review schema 6 holds only the sponsor, the owner's USDC and
wSOL accounts and the TM fee account, plus the existing caps; owner, binding and deduction cap still
come from the runtime profile. Its digest (plus the compiled Jupiter constants and the five
`variant -> program` contracts: Invariant 18, Obsidian 109, Byreal 156, Bison 177, Kipseli 181)
replaces the old route policy in the signer verification digest, so the protected journal identity
no longer changes with pool, lookup table or expiry. Expiry now lives only in the account review.

**Role-based verifier.** `verify_swap_transaction` checks by role, not by a captured account list:
two signers (sponsor, owner); owner accounts and fee account as static writable keys; exact outer
shape (compute limit, optional price, wSOL account create funded by the sponsor, Jupiter `route`,
close to the owner, repayment owner to sponsor); the nine fixed Jupiter accounts by key; a single
100% step of an allowlisted variant whose program is present among the route accounts. Pool accounts
are unconstrained except that they cannot alias owner-side keys (resolver alias check). Economics are
unchanged except that the guaranteed output now rounds **down**, which is conservative under any
on-chain rounding and closes the minimum-output rounding deferral for this bound.

**Per-attempt lookup evidence.** The gateway reads the payload's one lookup table with a finalized
`getAccountInfo` on the existing mainnet RPC client (one more exact request template), requires the
lookup-table program owner, the Jupiter table authority and an active table, and sends the signer only
the entries up to the highest index the payload uses. Tables are append-only and their addresses
cannot be reused, so this prefix is deterministic: it is part of the gateway's durable request
identity and a restart re-reads the same bytes. The signer resolves accounts from it; reconciliation
uses the same read and still cross-checks the chain's `loadedAddresses`.

**Trust change accepted.** Table contents now come from the gateway's RPC read rather than a
human-reviewed copy of the same kind of read. A wrong table could at most mislabel which DEX program
the transaction invokes; owner-side keys never come from a table.

**Migration.** One locked pristine transition (`byreal-v1-to-stable-execution-v1`) carries sponsor,
owner accounts, fee account and every limit over exactly; only the protected journal ordinal and
binding change. Old schemas still compute their old binding so the installed state can be validated.

**Evidence.** Production configuration: 205/205 CTests; Python renewal/transition/launcher suites
pass. Offline check on the eight retained TM captures under one policy: six verify (Invariant x2,
Bison x2, Obsidian x2 on two different pools and tables), Meteora 38 and Deriverse 161 refuse as
`unsupported_route`. 87 mutants (C++ and Python): all killed except four analysed equivalents
(extra-signer check shadowed by the owner-account roles; the session's redundant policy re-check; the
RPC null-account check shadowed by the owner check; the review size cap unreachable by any parseable
document). The optional price amendment (147,537,213) was sized to the old rounded-up Bison floor; with
rounding down that capture's floor is 6,777,950, one lamport below what the amendment admits.
