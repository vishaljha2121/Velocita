# Invariant layout and route-selection verification — 2026-10-14

Review existing unsigned payload SHA-256 `15bfdf29bcd880167ff8715a58ad7ccda512a312a8120960f828e6e855951760`; no new order, signing, simulation, submission, retry or service start.

1. Read current TM create-order/CLI/Trading OpenAPI documentation and primary Invariant source with keyless GETs. Establish which routing controls are documented; do not send undocumented parameters.
2. Exactly two keyless Solana getMultipleAccounts POSTs: the captured lookup table, then the 19 resolved transaction accounts. Finalized commitment; second read at or after first context slot; stop on error, no retries. Keep raw account data private.
3. Check instruction bytes, signer/writable flags, resolved program IDs, pool/vault/mint/authority roles and outer debit/receipt accounting against source. Separate source-layout compatibility from deployed-bytecode equivalence and guaranteed admission.
4. Record a support decision and necessary implementation scope. Do not adopt a different route schema or broaden economics merely to make this capture pass.

Existing single-cluster state, journals, service inhibitors and bounded-$1 authorization remain intact. Any later fresh quote must pass the exact qualified verifier and renewed account/route review.
