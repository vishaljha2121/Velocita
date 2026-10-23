# Byreal route verification protocol

Scope: retained unsigned response SHA256
`1601a54d32f2f9825e614f59b776c4bef2fc8e19179a8ff623373bb694f113ee`.
No new venue order, credentials, signature, broadcast, startup or funding change.

1. Reuse program-owned Jupiter IDL evidence to identify enum156. Read current
   program/IDL metadata once and compare the variant definition.
2. At most two finalized `getMultipleAccounts` calls: lookup tables first, then
   the resolved transaction accounts and required metadata. Validate identities,
   account permissions, classic-token USDC/WSOL source/destination/fee accounts,
   sponsor and program executability. Retain raw responses privately.
3. One unsigned `simulateTransaction` of the original bytes with
   `sigVerify=false`, `replaceRecentBlockhash=true`, `innerInstructions=true`.
   Inspect token/native deltas, instruction effects and fees. A simulation is not
   settlement or unconditional proof of future state.
4. Only after a successful baseline and understood encoding, at most two HTTP
   batches of two unsigned diagnostic simulations may vary the quoted-output
   guard across an adjacent rounding boundary. Require identical actual swap
   outputs within a pair; otherwise record inconclusive. Modified bytes remain
   unsigned and must never be executed. Stop on any HTTP/RPC error; no retries.
5. Public keyless GETs of the protocol owner's source/IDL are permitted; pin
   commits and hashes. Do not infer deployed binary equality from a GitHub name.

Acceptance requires a bounded full-transaction debit and conservative receipt
floor, preserving $1 total debit, $0.002 explicit fee, repayment at most2039280
lamports and zero guaranteed sponsor credit. Parsing support alone cannot enable
signing. Any deployed integration needs the existing commit/native-qualification
and explicit bounded-start review; keep services inhibited during this work.
