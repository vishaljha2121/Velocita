# Retained Bison candidate versus the replicated buy-price bound

Read-only design review, 2026-10-14, source checkpoint `8dde28e`.
No host changes, quotes, signatures, orders, funding changes or code implementation.
The candidate net floor below is supplied retained evidence, not a transaction
verification result. This note does not approve a changed economic limit.

## Finding and exact arithmetic

The opening contains a maximum all-in buy price of 146,635,156 USDC micros/SOL.
For exactly 1,000,000 USDC atoms, `swap.cpp:73-78` requires:

    ceil(1,000,000 * 1,000,000,000 / 146,635,156) = 6,819,647 lamports

The retained Bison candidate's proposed conservative net receipt is 6,777,951
lamports, 41,696 lamports below that threshold. If independent transaction
verification confirms that floor, the smallest integer price bound permitting it
is:

    ceil(1,000,000,000,000,000 / 6,777,951) = 147,537,213 micros/SOL
    ceil(1,000,000,000,000,000 / 147,537,213) = 6,777,951 lamports
    ceil(1,000,000,000,000,000 / 147,537,212) = 6,777,952 lamports

This raises the allowed all-in price from $146.635156 to $147.537213/SOL,
approximately 0.615171%. The total purchase remains $1 including its $0.002 fee;
the separate maximum SOL repayment remains 2,039,280 lamports with zero guaranteed
sponsor credit. This is an economic relaxation, not a rounding fix.

The old bound was deliberately derived from the earlier verified quote's exact
6,819,647-lamport net floor (`fresh-adoption-20261013/prepare_documents.py:65-67`).
It is a quote-specific conservative limit that may no longer fit current prices.
Calling it stale does not authorize weakening it. It still correctly rejects a
transaction with a worse guarantee than the owner-approved opening.

## Existing mechanisms cannot change it

- `swap::Limits` exists only in `Initialize`; a second Initialize refuses
  (`src/v2_swap.cpp:42`).
- Swap Control changes mode and fencing generations, not prices (`:191`). Direct
  swap Control is itself prohibited by kernel integration; existing outer control
  commands synchronize those fields (`src/v2_swap_kernel.cpp:62-71`).
- `ConfigureRiskLimits` belongs to the separate limit-order ledger and is blocked
  once the swap ledger is initialized (`src/v2_kernel.cpp:3219-3225`).
- Profile/route renewal cannot change the replicated bound. Its helpers explicitly
  preserve opening/account pins (`idle_policy_renewal.py:115`).

Setting an intent minimum to 6,819,647 cannot repair a transaction that guarantees
only 6,777,951: the transaction verifier compares the guaranteed net receipt to
the intent (`src/v2_swap_transaction_verifier.cpp`, `bound_buy_net_cost` call).
Lowering the intent minimum alone is refused by the kernel. Reducing order size
would violate the reviewed exactly-$1 scope and established $1 venue minimum.
No local file edit or inferred sponsor credit is a valid workaround.

A newly returned transaction with a genuinely sufficient guarantee, or a tighter
venue-supported on-chain slippage threshold, could satisfy the existing limit
without changing Raft. Those are alternatives, not established TM capabilities
or predictions about another quote. Do not edit TM's returned transaction bytes.

## Smallest amendment if the owner approves the weaker bound

Keep the original `Initialize` and its funding/limits bytes immutable. Add a
separate, one-time, pre-first-effect buy-price amendment in replicated swap state
and an operator-only command. This is narrower than general risk reconfiguration:

1. Command binds the existing allocation and opening digest, expected amendment
   revision zero, expected current buy-price bound, current kill epoch/executor
   generation, replacement bound and nonzero review evidence digest.
2. Accept only while halted, initialized, unconflicted and pristine: no pending
   intent, last effect zero, no releases/settlements, purchases/fees/P&L/holds zero,
   and current balances/cost equal the original opening. All existing $1 purchase,
   fee, loss, repayment and single-effect restrictions remain in force.
3. Store revision one, replacement price and review evidence separately. Reject a
   second amendment. Use the effective price for subsequent intent validation and
   deterministic state reconstruction. Keep it after settlement so snapshot
   validation does not reinterpret the historical trade against the old bound.
4. Require an authenticated committed read confirming the exact amendment before
   constructing the intent or permitting signing. Generic caller request
   deduplication handles exact retries; a new request with stale revision refuses.
   Raft ordering makes simultaneous unhalt/intent/amend attempts deterministic:
   amendment after unhalt or any intent refuses, and an intent before amendment
   still observes the original limit.

The immutable opening digest and allocation remain valid, preserving funding,
account-review identity and journal domains. Existing journal bytes remain
unchanged. Fresh review and normal watermark checks still apply; do not rewrite
old archives to make an amendment fit.

Changing `opening.limits` directly would change the opening digest used by
account review, rewrite historical opening meaning, and force funding/review
adoption changes. That is a larger and less clear change than the separate
pre-effect amendment.

## Implementation and qualification implications

This requires voter/kernel work, not only a gateway configuration change. Likely
affected files: `swap.hpp`, `swap.cpp`, `swap_codec.hpp/.cpp`,
`v2_swap_kernel.cpp`, operator-role dispatch in `v2_kernel.cpp`, state snapshot
capacity/codec integration, and the operator command builder/client interface.
Review schema/replication compatibility explicitly: existing swap snapshots have
an exact fixed byte length. All voters must understand the amendment before it
is submitted. Preserve and test recovery of the existing unamended state; do not
reset the cluster or journals. A new native checkpoint is required for changed
C++ bytes.

Required focused tests: exact integer boundary above; unauthorized/stale/fenced
and non-pristine refusal; only one accepted amendment; unchanged opening/funds/
journal domains; fixed $1/fee/repayment scope; snapshot and replay round trips
before/after amendment and settlement; duplicate command handling; halt/intent
ordering; stale-read signer refusal and existing lost-response recovery drills.
Do not expand this change into continuous strategy, sell or multi-effect work.

Recommendation: present the exact weaker economic bound and the necessary narrow
operator amendment to the owner before implementation. If no amendment is wanted,
retain the original bound and execute only a newly verified transaction that
actually meets it.
