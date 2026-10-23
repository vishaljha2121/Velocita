# Byreal route: local qualification, not live admission

This supersedes the route-identification uncertainty in the original launch
review. The installed release still does not support this route. No new trade,
signature, broadcast, host write, service start, commit or deployment occurred.

## Problem and implementation

TM selected Jupiter direct-route enum156, identified as `ByrealDynamicV3` by the
program-owned IDL. It has no enum payload and uses 16 static / 27 resolved accounts.
The previous qualified Kipseli route used 12 / 20. A common displayed fee did not
make their instruction layouts interchangeable.

The C++ decoder, transaction verifier and review parser now implement a separate
schema2 Byreal policy. The verifier requires all 27 identities and their exact
signer/write privileges, six outer instructions, exact Jupiter account ordering,
the sole Byreal route, classic USDC/WSOL identities, fee-inclusive input, repayment
cap and positive conservative net receipt. There is no route fallback. The parser
owns its fixed-size arrays: seven additional keys (224 bytes), no new allocation
or borrowed lifetime. The existing schema1 digest remains byte-compatible.
Policy checking is bounded by 27 accounts and six instructions; it runs on the
transaction/signing path, not on every Raft decision tick. All policy consumers
must be rebuilt together because the in-memory C++ policy layout has changed.
No Raft wire/state schema, trading strategy, funding or budget was changed.

Python journal binding now mirrors both native policy domains. A separate,
explicitly pinned schema1-to-schema2 transition retains the 13 owner-facing
account/program roles under their new indices. It preserves execution caps,
financial limits, caller identities, opening balances and launch flags. The
existing five-lock, authenticated unchanged-state and crash-recovery procedure
remains mandatory. Only the pristine protected journal ordinal/domain advances;
the four gateway journals stay byte-identical. A `route-review.json` witness pins
before/after documents and contract evidence; it is an owner-review record, not
an independent economic proof. Expiry-only renewal still rejects route changes
without that witness. Nothing applies or starts automatically.

## Fixed capture results

All amounts below are integers from the retained unsigned October13 capture.

| Item | Result |
|---|---:|
| Maximum USDC debit | 1,000,000 atoms ($1) |
| Explicit fee inside that debit | 2,000 atoms ($0.002) |
| Input reaching the pool | 998,000 atoms |
| Original quoted SOL output | 9,345,460 lamports |
| Slippage guard | 500 bp |
| Gross guard floor | 8,878,187 lamports |
| Maximum owner-to-sponsor repayment | 2,039,280 lamports |
| Guaranteed sponsor credit assumed | 0 |
| Conservative net floor | **6,838,907 lamports (0.006838907 SOL)** |
| Baseline simulated gross output | 9,345,942 lamports |
| Baseline simulated sponsor funding | 1,488,440 lamports |
| Baseline simulated owner net receipt | 8,795,102 lamports |
| Simulated network fee paid by sponsor | 110,000 lamports |

The successful simulation's full native and USDC token deltas reconcile, including
account creation, close and final repayment. USDC source is classic-token,
owner-controlled, undelegated, with sponsor close authority; destination was
absent. Fee denomination is USDC. Pool/vault account ownership, mint and authority
were checked. All 13 owner-facing role mappings match the previous reviewed route.

The first adjacent rounding pair failed on both sides after market movement:
it is inconclusive and retained. The second pair had the same actual gross output
9,343,679 at the same slot456633849. Quote9,835,451 succeeded (unrounded
9,343,678.45); quote9,835,452 failed Jupiter error6001 (unrounded9,343,679.4).
This distinguishes ceiling from floor at that boundary. Failed-transaction inner
transfers are attempted effects, not settlement. Only the quote guard bytes were
changed; both transactions remained unsigned and were never submitted.

The positive floor is conditional on the reviewed program contract, account
configuration and successful atomic transaction. A simulation alone does not
prove arbitrary future state. Primary Byreal source is pinned at
`6863b33fd0f7874f722e58fe8191009b6061de5d`; source identity does not establish
reproducible equality with the deployed binary. See `byreal-sources.json` and the
program-owned IDL metadata retained privately.

## Verification

- Nine decoder, six verifier and five review-parser test groups pass locally.
- 52 Python transition/renewal/application tests pass.
- 29 named assertion mutation kills: decoder3, verifier15, review8, transition3.
  No test errors are counted as kills. The decoder harness initially expected a
  later assertion; it was corrected to the earlier spurious-boolean rejection.
- Sanitizers run for the decoder/verifier mutations and review executable; the
  review executable links unchanged dependency objects from the existing local
  archive. This is focused local qualification, not a full release build.
- C++ and Python agree on schema2 signing, protected-journal and runtime identity
  digests. Policy consumers were recompiled for that parity diagnostic.
- The actual retained envelope passes the modified production verifier with the
  exact bounds above. Its intent and clock are synthetic offline inputs; it has
  **no actual Raft authority** and cannot be used as a fresh signing approval.
- Five corrupted evidence cases fail at their expected checks, plus three
  independently calculated integer-floor examples pass.

## Remaining path to the bounded live test

1. Owner-approved scoped checkpoint; qualify that exact source on NUC/Pi. Include
   all policy consumers and existing gateway/process recovery drills.
2. Upgrade the stopped, inhibited executable; validate and publish the locked
   route-transition bundle without resetting funding or journals.
3. Refresh account/session and current-route evidence near launch. The venue may
   choose another route or different accounts: unsupported bytes must still refuse.
4. Review the fresh receipt floor and exact start/unhalt/one-intent sequence, run
   only the authorized one-$1 test, reconcile finalized settlement and halt/stop.

Continuous strategy operation, an automatic retry or sell are not authorized by
this implementation. The existing $0.50 realized-loss control is not a guaranteed
market-loss stop on an unsold SOL position.
