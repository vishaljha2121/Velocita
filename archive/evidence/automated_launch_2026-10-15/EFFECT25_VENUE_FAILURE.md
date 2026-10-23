# Effect 25: failed TM order without a recorded chain signature

## Verified evidence

Owner-run authenticated GET for order `00cd85c6-80b1-4a70-ba2f-7216e69cc8e4` returned `status=failed`, `failure_reason=Venue temporarily unavailable`, buy quantity12.69USDC, executed_qty0, leaves_qty0, fee0 and no tx_hash. Created2026-10-16T03:06:31.018973Z. SHA256 of captured response:2ac53ae7e12952a872f002fcf2296c11946fca87c0f54244ca0e91adb9e19d8f. Raw response remains in owner-private storage; no credentials copied here. The first summary omitted executed_qty; that field was subsequently read from the saved response with hash and order identity verified.

Owner's read of the checksummed submission journal confirms effect25, execute_intent phase2 and an all-zero transaction-signature field. This is not standalone proof of non-broadcast. The authenticated TM response independently reports zero execution/fee. No fresh wallet/history comparison has yet established absence of every economic effect.

## Why the gateway stays blocked

Installed commit ba299ff: SwapProductionPorts::reconcile reads the order, then calls admit_retained_swap_settlement. collect_retained_swap_settlement in src/v2_swap_settlement_connection.cpp refuses an empty retained transaction signature before any finalized-chain read. Thus this order cannot resolve through that path even if its TM status is terminal failed. SwapGatewayRuntime waits up to180000ms, then blocks with settlement_unproven; main returns2. Initial stop was confirmed by status4/cause6. The owner's one restart again exited2 after approximately184seconds; pending effect25 and driver sequence27 stayed unchanged. See GATEWAY_RECOVERY_2026-10-16.json.

This distinguishes the venue-reported operational cause (temporarily unavailable) from the recovery-contract gap (failed order without on-chain evidence). No evidence that the strategy signal itself caused this failure.

## Proposed fix, not implemented

Define and qualify an explicit terminal venue-failure resolution separate from finalized-chain settlement. Require exact authenticated order/custody identity, zero execution and charges, reconciliation of account/history, and a justified no-late-execution boundary. Commit the appropriate failure/release through Raft and durably resolve gateway/signer recovery records before permitting a later strategy intent. Do not invent a transaction hash, mutate journals directly, reset funding, or automatically retry effect25. An absent signature or UI status alone is insufficient authority to release the pending reservation.

Gateway remains stopped pending this resolution. Strategy capture updates remain deferred; no driver, kernel or gateway code was modified by this diagnostic work. The local read-only diagnostic and its tests remain uncommitted.
