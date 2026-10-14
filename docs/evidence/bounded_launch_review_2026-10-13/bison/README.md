# Bison compatibility and the first-buy price amendment

The retained Bison transaction now passes offline verification under its exact
reviewed accounts and the trusted Jupiter/Bison program contract. A separate,
owner-approved one-time price amendment is implemented and prepared, not applied.
This does not qualify a fresh quote or start live trading.

| Item | Result |
|---|---|
| Total USDC debit / explicit fee | 1,000,000 / 2,000 atoms ($1 / $0.002) |
| Conservative net SOL floor | 6,777,951 lamports |
| Original Raft minimum for $1 | 6,819,647 lamports |
| Proposed maximum buy price | 146,635,156 → 147,537,213 USDC atoms per SOL |
| Funding, balances and prior journals | Preserved; original opening unchanged |
| Host changes / orders / signing | None in this implementation batch |

Schema4 accepts only the observed 20-account, 12-static-key Bison177 layout,
including its false direction flag, exact account ordering and permissions,
canonical programs, six outer instructions, fee-inclusive input and full repayment
deduction. Native and Python policy/journal digests agree. Tests reject changed
recipients, permissions, route, fee, repayment and a one-lamport receipt shortfall.
Raw transaction/account data and candidate profiles remain private. See
[actual capture](ACTUAL_CAPTURE.md) and [account evidence](CAPTURE_REVIEW.json).

The price amendment is a new operator-only Raft record. It requires the original
opening digest, expected original price, revision zero, current fencing generations,
halted mode and no prior effect, settlement, holds or balance change. It stores
only a separate effective price and review evidence. It cannot fund the account,
raise purchase/fee caps or run twice. The reducer uses the amended price for both
new reservations and deterministic settlement reconstruction. The original opening
bytes and account-review identity remain unchanged. See
[approved scope](PRICE_AMENDMENT_APPROVAL.md) and [design review](PRICE_BOUND_REVIEW.md).

Existing unamended snapshots retain their exact 3,752-byte representation; a
representative snapshot matches the old checkpoint byte-for-byte. Amended swap
snapshots add a tagged 65-byte extension. Every voter and executable using swap
state must be built from the new checkpoint before amendment. After kind9 commits,
rollback to an old executable that cannot decode it is not supported. Preserve
WALs, snapshots, balances and journals during upgrade.

The offline builder prepares an amendment record from the original opening and
explicit parameters. Intent preparation can include that record, but marks it as
an uncommitted proposal. Only authenticated committed Raft state can authorize the
runtime. The real proposal is retained privately; [its hashes](price-proposal.json)
prove the original opening was preserved.

## Local validation

Eight focused CTests pass, including kernel authority, snapshot/codec and model
replay. The verifier passes eight groups, transaction-review seven, offline builder
five, CLI five, and the Python review/renewal/apply suites pass 63 tests. Seventy
mutations fail their designated assertions (27 verifier, 10 review, 17 amendment,
3 kernel, 4 Python transition, 4 builder and 5 CLI). Old snapshot bytes and schema
goldens remain unchanged. These are local checks, not native qualification.

The operator command for the amendment requires both records:

```sh
velocita-v2-client --dry-run --incarnation CLUSTER_HEX \
  spot-swap-record AMENDMENT.record ORIGINAL_OPENING.record
```

The client verifies canonical bytes, operator roles, original-opening hash,
expected prior price and cluster incarnation, and displays the old/new prices and
review digest. Existing funding/intent command syntax remains supported.

## Remaining launch sequence

1. Review and commit only this scoped change; run exact-commit qualification on
   NUC and Pi. The native runner includes the four additional kernel/codec/replay
   tests as well as the existing swap tests, TLS and eight process drills.
2. Install the qualified voter/exporter pairs and gateway/signer/client artifacts,
   retaining one cluster and all state. Verify authenticated reads after restart.
3. Complete locked route/release/account renewal and credential freshness before
   applying the price amendment. Verify a fresh quote matches the reviewed route
   and bound. The retained capture is historical, not a current quote.
4. Apply the reviewed amendment, verify it in authenticated committed state, then
   enable the single-$1 intent. Reconcile submission, finalized settlement and
   actual balances; the current runtime stops after that effect.

The locked renewal helper deliberately requires the original bootstrap state
before amendment. If expiry, route drift or another failure occurs after amendment,
stop for reviewed recovery. Do not reset journals, replace the opening, reverse the
amendment automatically or submit another amendment. The successful sequence needs
no additional startup helper: startup checks the current authenticated state and
unchanged opening, not equality to the historical bootstrap state digest.

Scope remains one bounded buy. Continuous strategy, sell support, multi-effect
rollover and warning cleanup are deferred. The $0.50 setting is the existing
realized-loss control, not a guarantee against all unrealized mark-to-market loss.
