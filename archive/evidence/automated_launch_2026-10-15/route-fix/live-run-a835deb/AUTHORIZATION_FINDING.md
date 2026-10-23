# Strategy caller rejected before venue access — 2026-10-15 20:40 UTC

## Outcome

The owner explicitly approved 240 attempted orders, $25 managed capital, 0.12 SOL target buys and the configured $10 realized-loss stop through the current review window. Following fresh prechecks, the existing driver started once at 20:39:13.898 UTC, PID 493419. The named driver-silent alert silence was expired. No driver state, funding, credentials or journals were reset.

The first live signal created two sequential requests for effect 1: caller 21 sequences 1 and2, committed at indices 13 and14, both `code=71 unauthorized_command`. Amount 12,760,000 USDC atoms, minimum 110,400,000 SOL lamports, fee cap 25,520 USDC atoms. The existing driver continues after a definite refusal, so it emitted the second request before the first supervision read exposed the rejection. Codex stopped the driver immediately after reading the refusal logs; it is now inactive. Fresh producer state is index 14, flat $25, last_effect 0, pending 0, fees 0 and no settlements. There was no admitted intent, venue execution or wallet reconciliation to report. [Exact filtered logs](REFUSALS.log).

The explicit 240-order approval resolves the earlier automatic approval rejection. The present blocker is actual kernel authorization, not missing owner approval for that trading budget. The alert silence remains expired; the driver-silent alert can correctly fire while the driver is stopped.

## Root cause and missed coverage

`src/v2_kernel.cpp`, `roles_allowed`, puts `swap::Intent` in the same operator-only branch as `Initialize` and `AmendBuyPrice`. This exists in deployed voter source `54a5fcb` and current HEAD. Driver uses caller 21/key12 with a strategy role, as intended by ADR0016 and its deployed config. Reassigning its existing strategy role would not fix the command whitelist.

`tests/v2_swap_integration_tests.cpp:47` explicitly expects a strategy swap intent to be unauthorized (the assertion describes preventing bypass of rule evaluation). The six-effect fixture in `tests/v2_swap_continuous_drill.cpp:433` submits the first intent with `c.commit(op, ...)`, even though the inner record role says strategy. Thus green qualification proves the gateway path with operator-originated intents, not admission by the real strategy caller. The previous review did not catch that mismatch. Do not claim end-to-end strategy authorization is qualified.

## Correctness constraint: do not just widen the whitelist

Rejected requests 13 and14 are now committed history. A global change from operator-only to operator-or-strategy could change their replay result when recovering from a state preceding those entries. An image upgrade must not reinterpret old committed history. No live caller-key workaround, operator identity in the strategy, ledger reset, snapshot trick or reissued sequences is acceptable.

## Proposed bounded correction, not yet implemented or approved for deployment

Introduce an explicit operator-committed authorization for future direct spot-swap intents by the designated strategy caller, bound to the existing account/incarnation and represented in replicated state. Default is absent/disabled, preserving old replay refusals. Intent admission requires both the strategy role and the new grant; existing operator intent capability remains as before. Only an operator can grant authority. Funding, price amendments, limits, mode and venue evidence permissions remain unchanged. No new transaction-verifier trust or economic limit expansion.

Review the smallest compatible command/state representation before coding. Update codec/snapshot validation and authority documentation as actually required; this is a security-boundary change, not a request to add unrelated features. The alternative is integration through the kernel rule-program path, which is a larger change because the current live Python driver emits direct spot-swap intents.

Required tests: replay the same pre-grant history including rejected caller 21 sequences 1/2 and prove unchanged state/result; operator grant then caller 21 buy/sell succeed; unassigned callers, gateway callers and strategy control/evidence forgery still refuse; grants cannot bypass existing funding/epochs/halt/bounds; snapshots/restarts preserve the grant. Convert/add the production-composition continuous drill to originate intents from an assigned strategy caller and exercise the new grant, rather than operator submissions alone. Add mutation checks, then qualify the committed voter/runtime build on NUC/Pi before a coordinated single-cluster upgrade. All voters must agree on command semantics before activation.

No kernel code was changed in this diagnosis. No new commit or deployment was performed. Strategy sequence 2 is preserved; continue from 3 after resolving authorization, never reset. Operator next sequence 10 remains subject to verifying no intervening owner command. Mode stays normal; driver stopped and account flat. Gateway/signer remain active with protected start guards. Review drains 2026-10-16 13:41:34.459 UTC and expires 13:48:34.459 UTC; refresh through the owner procedure if work exceeds the window.
