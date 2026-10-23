# Fresh account, route and dormant adoption

Owner authorized fresh account/route verification followed by reviewed profile and
real credential adoption. This does not authorize service activation or trading.
Both services must remain inactive, with launch_authorized=false.

Use the existing account-history collector once (at most 110 reads, 40 historical
transactions, three pages per history, one session refresh, 300-second deadline).
Pin the established setup response to
fbf9602f339af07a4ad77e5002f688a8b2b57e604d2d788b77be94b6a46cd9c9.
Then use the bounded MVP quote helper for one unsigned $1 total-input SOL buy.
Independently resolve and validate its actual route/accounts; retain raw evidence
privately. No retry, signing, execution or order-size increase. A changed route
must not be forced to match historical assumptions.

If verification passes, prepare the dormant document-adoption path and fresh
whole-position unsigned SOL sell valuation, then a final stable history cut after
quote creation. This later cut is a separately identified finalization operation,
not a retry of a failed capture. No sell is executed. Account/profile documents
must reflect actual current identity, all actual inventory, existing catalog and
authenticated cluster controls. Profile adoption must not initialize/fund Raft,
start services or set launch authorization. Credential bytes travel only over
SSH stdin to role-isolated private tmpfs, never logs/argv/environment/repository.
Privileged final installation is owner-run. Preserve partial evidence on failure.

Financial scope remains one effect/$1 total USDC debit, 2,000-atom explicit fee,
2,039,280-lamport maximum repayment and zero guaranteed sponsor credit. Verify the
new transaction's positive conservative net receipt and exact rounding; the venue
$1 minimum size is a separate fact. $0.50 loss allowance is not a guaranteed market
stop for an unsold position. Prior paper balances are excluded.

## Recorded timing deviation, before finalization

The identity fixes and dormant replacement took longer than the initial account
cut's 15-minute lifetime. Take one new pre-valuation history cut, then the single
whole-position unsigned sell quote, followed by the already-planned final history
cut. Each collector retains the same 110-read/300-second bound and stops on error.
This is refresh of successfully captured but expired evidence, not retry of an
error. The quote and final opening mark must be at most 30 seconds old when the
offline builder runs; no timestamp extension or fabricated observation is allowed.
