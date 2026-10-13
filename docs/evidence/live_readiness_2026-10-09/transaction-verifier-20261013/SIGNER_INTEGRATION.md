# Conservative real-transaction verifier: signer/startup integration

The local working-tree implementation passes **52 selected CTest tests** and
**36 startup-launcher/profile Python tests**. The production gateway executable builds.
The independent verifier/decoder passed sanitizer and mutation checks; signer
integration kills five named mutations and the review loader kills seven.
This code is **not committed, native-qualified or deployed**. The separately
qualified b1c6650 checkpoint predates this implementation.

## What changed

- Real-format payloads can pass `SwapSignerSession` only with an owned immutable
  reviewed context. Missing context retains the production refusal. Synthetic
  text fixtures remain confined to their existing test-only macro.
- The verifier runs against authenticated prepared state before a fresh Raft
  signing-authority barrier. The session compares payload, complete intent and
  policy identities, then enforces the earliest policy/envelope/intent deadline
  across durable fencing, signing and signature persistence.
- Context identity includes organization, wallet signer, account/program policy,
  cost cap, lookup image and provenance. It changes the signing journal domain.
  A changed reviewed policy cannot reuse an old journal or recover a signature.
- Lost-response recovery verifies the exact durable signature and original request
  identity without signing again. It additionally enforces current policy/envelope
  freshness. Recovery does not fabricate a new Raft intent or semantic proof.
- Startup accepts an optional nonzero `transaction_review_digest` in the public
  runtime profile. That raw SHA-256 extends runtime identity, invalidating any
  old account review. The real runtime requires adjacent `transaction.review`:
  strict 32-KiB/12-field schema, 20 account pins and at most 256 lookup addresses.
  Runtime allocation, owner, cost cap and TM identities cannot be overridden by
  the document. Sponsor key must match the failure-fee-payer pin.
- The launcher checks this new public file before credential opening. Both signer
  and gateway load the same digest-pinned data; settlement receives an owned copy
  of the lookup image. Source/fee mint facts remain independently reviewed inputs,
  not claims manufactured by the file parser.

The authenticated opening must limit total purchases and a single buy debit to
1,000,000 USDC atoms, fees to 2,000 atoms and the realized-loss allowance to at
most 500,000 atoms. Repayment authority cannot exceed 2,039,280 lamports. The
only admitted real-context intent is buy effect1 with exactly 998,000 principal
plus 2,000 fee; a second effect refuses even if a previous unsent intent left
budget unspent. This scope is checked at runtime startup and independently in
the signer. A named RED preceded this guard, and its removal fails the named
broader-budget assertion. Separate tests cover a second effect, excess loss
allowance and a one-lamport excess repayment allowance. Existing general paper
and synthetic-fixture paths retain their prior behavior.

All memory is bounded. The session owns context/payload across asynchronous reads;
there are no borrowed pointers into a file buffer. These checks run at order
preparation/signing, not in the kernel's per-tick indicator path. No Raft wire,
replicated state schema or consensus algorithm changed.

## Tests and qualifications

A named RED preceded signer admission (`verified_payload_signs_after_fresh_authority`),
profile wiring (`transaction_review_pin_reaches_both_processes`) and launcher
pinning (`transaction_review_checked_before_secret`). Initial compilation/test
fixture errors were corrected and are not counted as mutation kills. The
conservative test fixture initially had an incoherent old $125/SOL kernel price
limit; it now explicitly uses $145/SOL for its constructed net-floor oracle.
That test fixture is not a production price setting.

The constructed conservative case signs with a generated test key at a net
minimum of 6,900,812 lamports and refuses 6,900,813. Account-context ownership,
authenticated authority, changed journal policy, and expired proof/recovery are
covered. Envelope freshness expires at 1020 while intent expiry is 1100, so the
new expiry check cannot hide behind the old intent deadline.

[Current account evidence and historical actual-payload result](../conservative-mvp-account-check-20261013/README.md)
show that the named route/account snapshot and retained bytes support the
conservative treatment. This relies on the documented Jupiter/Kipseli contract;
it is not formal proof against arbitrary malicious CPI programs. The expired
retained envelope is never executed. Only a fresh transaction can be considered
for the owner's approved single $1 buy.

## Remaining activation work

`swap_runtime_live_contract_admitted()` still returns false. Before changing live
admission, review/checkpoint this implementation and qualify the exact native
build. The one-buy startup/intent scope is implemented. Prepare
fresh account/history/transaction pins, an executable opening valuation and fresh
net minimum. Then use the protected gateway path for the one bounded test, with
no retry or resize. Do not treat broader account balance, the larger cost cap,
this local test result, or a fixture signature as permission for ongoing trading.

[Owner-approved limits](../CONSERVATIVE_MVP_BUY_2026-10-13.md) remain the controlling
proposal. No real key was loaded, no real payload was signed, and no money moved
while implementing/testing this change. Build warnings are retained separately;
no warning suppression or unrelated cleanup was included.
