# Approved preparation: one-time buy-price amendment

On 2026-10-14 the owner selected “Approve the narrow amendment” for implementing
and preparing a one-time, pre-trade Raft buy-price amendment. This approval does
not apply the amendment, deploy the build, or start trading.

The proposal preserves the original funding record, account allocation, balances,
request history and journals. It changes only the effective maximum buy price
from 146,635,156 to 147,537,213 USDC atoms per SOL. The exact $1 maximum debit still
includes the 2,000-atom (20 bp) fee. The single-purchase limit, $0.50 loss setting,
and separately accepted 2,039,280-lamport maximum output deduction are unchanged.
No sponsor credit is assumed.

The retained Bison transaction passes offline verification under its pinned
account/program contract at a historical clock, with net floor 6,777,951 lamports.
The old cap requires 6,819,647; the proposed cap requires 6,777,951. Raising the
cap is an economic relaxation of approximately 0.615171%, not a rounding fix.
The exact on-chain output threshold uses the existing reviewed Jupiter contract;
this is not an independent audit of the Bison implementation or future upgrades.

The amendment is operator-only, requires a halted, pristine account and matching
opening digest, prior price, revision and fencing generations, and can apply only
once. A proposed record is not evidence of a committed amendment. A fresh
transaction must still pass the verifier and the committed price bound before
signing. No repeated quote loop is authorized by this preparation.
