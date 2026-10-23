# Owner-approved conservative single-buy treatment

The owner approved: “lets use this conservative treatment for a single bounded MVP buy”.
The concrete preparation is one SOL buy with exactly **1,000,000 USDC atoms total
wallet debit**, including the explicit **2,000-atom fee** and **998,000-atom swap
principal**. Maximum encoded native repayment is **2,039,280 lamports**; sponsor
funding receives **zero guaranteed credit**. This supersedes the 550,840-lamport
observed-cost cap for this one proposed buy only. It does not assert a permanent
venue fee or enlarge the strategy's general trading allowance.

A fresh supported transaction must produce a positive conservative net floor and
meet the committed minimum. For the retained diagnostic only, 8,940,092 gross
floor minus 2,039,280 repayment equals 6,900,812 net lamports. That old payload is
not executable authorization and its output amount must not be reused blindly
for a fresh market price. Fresh transaction bounds and an executable opening mark
must keep the proposed trade within the owner's $0.50 loss allowance. Market loss
after acquisition is not guaranteed by a transaction receipt floor.

The session-wide purchase ceiling remains $5 including the already executed $1
CLI buy, leaving $4. The MVP's own replicated purchase budget should be $1 so that
this test cannot consume the other $3. No automatic retries, additional purchases,
size increases, sale or buyback are included. A lost response enters reconciliation;
it never triggers a fresh order. The signer remains one-use with a durable fence.

## Implementation and trust boundary

Keep the existing cluster and Raft protocol. Raft commits integer spend/minimum
receipt limits; the signer independently checks all transaction instructions,
accounts, exact payload, policy identity and fresh committed authority. Reconciliation
credits only actual finalized wallet changes. Do not substitute observed sponsor
funding for an encoded guarantee.

This MVP uses canonical Jupiter/Kipseli under their reviewed route contract. It
does not formally prove arbitrary program behavior. Review actual account owner,
source/fee mint and destination lifecycle before preparing the pinned transaction
review; unsupported accounts/routes refuse. Explicitly check the classic SPL USDC
source belongs to the owner, initialized/unfrozen/no unexpected delegate, the fee
account is USDC with its reviewed recipient, and destination is the supported owner
wrapped-SOL account. Owner and sponsor identities must match independently reviewed
configuration. An allowlisted program identity is not a proof of all future upgrades.

Implementation, fresh account pins, exact-build qualification and bounded launch
remain required before execution. No live gate has been opened, and no money has
moved through Velocita during this preparation.
