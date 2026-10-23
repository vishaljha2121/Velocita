# Narrow Obsidian buy support

Owner requested support, tests and native qualification, followed by refreshed
authorization, exactly one $1 SOL purchase through Velocita, and reconciliation.
No continuous strategy, sell support, resizing or automatic trade retry is added.

The new schema3 pins 17 resolved accounts, 11 static keys, two signers and the
observed writable permissions. Jupiter must have one Obsidian109 step and the
exact 18-account role list. Canonical program/WSOL/sysvar keys are checked. The
only optional instruction is compute-price, between compute-limit and ATA-create;
its absence means zero price. ATA payer stays sponsor; destination close goes to
owner; the final owner-to-sponsor payment is fully deducted with no sponsor credit.
Old schema1/2 domains and policy serialization remain unchanged. No Raft state,
wire format, strategy or one-effect limit changes.

The synthetic oracle computes ceil(9,298,883 * 95 / 100) = 8,833,939 lamports,
less 2,039,280 = 6,794,659 net. Total USDC debit remains 1,000,000 atoms including
2,000 fee atoms and 998,000 principal. Exactly the floor passes; floor+1 fails.
Both retained real envelopes pass offline against their own account/lookup review;
the wrong capture review is refused. These diagnostics use each historical
envelope timestamp, not today's clock. They are not current launch approval.

Capture2 account checks used exactly two read-only Solana RPC POSTs. Its pool,
vaults and lookup table differ from capture1. Raw payloads/accounts and runtime
profiles stay private. CAPTURE_REVIEW.json retains sanitized observations/hashes.
The USDC source and fee account identities are preserved across the locked route
transition; USDC is not falsely added as a separate account in Obsidian's list.
Instruction-sysvar RPC null is normal runtime behavior, not an absent data account.

Checks: seven C++ verifier groups and six review groups under ASan/UBSan; 60 Python
tests; 21 verifier +9 review +3 transition named assertion mutation kills. Both
actual documents match native/Python signing, protected-signer and runtime digests.
Preparation errors are retained in validation.json and are not mutation kills.

Trust boundary: verification checks the full outer transaction under the existing
Jupiter exact-input/output-guard contract and pinned program identities. It does
not audit Obsidian program implementation or guarantee behavior of malicious
program upgrades. No new simulation or independent rounding proof is claimed.
Pool-byte vault occurrences corroborate identity, not a separate layout contract.

Next: owner-scoped checkpoint, exact-commit offline NUC/Pi qualification using
../native/launch.py and native-focused.py (54+ CTests, Python checks, TLS, eight
process drills). Then stage qualified executable and locked Byreal-to-Obsidian
review transition with both services inhibited, refresh account/session/quote,
verify fresh net minimum and loss/debit limits, execute authorized single purchase
and reconcile. Route/account drift or failure stops the attempt; no requote loop.
