# Focused automated-launch correction and offline qualification

Authorized by the owner on 2026-10-15: fix the five review findings in one batch and qualify six consecutive production-path fake-venue trades with crash recovery.

Scope: F1 repeated-effect lifecycle; F2 caller-key preservation/verification; F3 candle freshness; F4 exit capacity; F5 bounded pre-expiry drain. Main owns C++ lifecycle; two agents own bounded driver/tooling and drill/test work. No new trading strategy, funding, risk limits or economic-verifier relaxations.

## Required qualification

1. Six consecutive effects through preparation, independent protected signing, durable submission and authenticated settlement. Production-format Solana envelopes must pass the real verifier; finalized receipts must pass the real settlement parser.
2. Six alternating buy/sell effects through the production gateway adapters against a fake HTTPS leaf, with real authenticated Raft reads and actual journal persistence. Exact create/execute counts and final managed balances are checked. No real venue calls or funds.
3. Restart between effects and across the settled-but-not-fully-retired journal boundary. Old effects cannot be submitted or signed again; mixed journal stages either finish the proven transition or refuse without releasing unknown exposure.
4. Unknown submission resumes reconciliation without re-execution. A failed or unsupported transaction never becomes a successful fill marker.
5. Independent tests reject missing/incorrect predecessor settlement, stale reads, repeated effects, stale/future candles, insufficient exit capacity and expired authority. Mutation checks must rebuild changed C++ sources before executing; compilation failures do not count as killed mutations.

The fake HTTPS leaf is linked only into the drill executable. It replaces the transport boundary, not production codecs, verifier or settlement admission. This drill does not qualify external TLS connectivity or Linux service-UID separation. Native NUC/Pi qualification and deployment require a scoped committed checkpoint. Source tests do not authorize live starts.

## Preserved deployment state

No host or cluster changes in this batch. Last owner readback: caller keys remain at the retained tmpfs paths; active keys absent; signer failed64, gateway inactive, recovery directories empty. One funded/halted cluster, incarnation aae119b9. Existing review expires 2026-10-16 13:48:34 UTC; it is not renewed by this work.

## Deliberate recovery limits

A crash with a current prepared/attempted effect can still require supervised recovery. Unknown execution is reconciled, never retried. Automatic continuation after settlement requires exact retained predecessor proof and fresh current-account balance matching. Drain deadlines inhibit new entries and request an eligible exit; feed failure, venue refusal or missing finality can leave exposure and force a visible stop. No claim of guaranteed flatness.
