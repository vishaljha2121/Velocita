# Fresh verification and dormant account adoption

**Completed dormant adoption at 2026-10-13 21:41:13 UTC.** No transaction was
signed or submitted. Both live units remain inactive/dead/static. Root-owned
account documents and both isolated real credentials are installed and verified.
Installed manifest: ca4bb2fbec356c6ecdd4ba405d322b954337cd76ae76d181bc42439d586d4442.
Profile: 17370a3cbc1063fd7c8c8c2d2ebd57ea7d471b68e2d40b0e8e894cb4305912f0.
`installed.json` records exact metadata and cross-role access checks.

The initial account refresh used 20 requests and saw the same four finalized
transactions, one USDC token account, no TM orders and no blocking orders. Stable
balances: 40.006989 USDC and 0.040764166 SOL. Closed historical token accounts and
independent provider archival completeness remain outside the collector's claim.

The one unsigned buy returned HTTP201: $1 input, $0.002 quoted explicit fee and
0.009325186 quoted SOL output. Independent finalized six-account lookup passed:
source USDC belongs to the reviewed owner, initialized/unfrozen/no delegate;
fee recipient unchanged; WSOL destination absent; active warmed lookup resolves
20 distinct message accounts. Historical instruction/account positions match;
only the quoted route output, blockhash and envelope timestamp changed.

The same production C++ verifier source as 13cc98b accepted the captured payload
in an offline diagnostic with a synthetic pending intent and capture-time clock:

    ceil(9,325,186 * 9500 / 10000) = 8,858,927 lamports gross floor
    8,858,927 - 2,039,280 + 0 sponsor credit = 6,819,647 net floor
    998,000 principal + 2,000 fee = 1,000,000 USDC atoms debit

This remains conditional on the reviewed Jupiter/Kipseli program contract; account
snapshots are not a promise of future account state. No current Raft authorization
was manufactured by the synthetic diagnostic. The expired quote is never executed.

Fresh authenticated reads under both UID29001/caller22/key13 and UID29002/caller23/
key14 passed: term5, leader 2, applied index 4, state version 3 and the same reviewed
halted/catalog-only state digest. Existing recovery directories contain no files.
The owner explicitly authorized use of interactive sudo; no password was written
to a file, argv, environment or command output.

## Correctness findings before credential transfer

1. The exporter incorrectly compared the API-key UUID with profile signer_id.
   C++ interprets signer_id as the envelope's Solana signWith wallet. Export now
   validates that wallet against owner_wallet; API-key format and exact P-256
   private/public-key validation remain enforced. The source private key is not
   converted into a Solana key and is never a wallet key.
2. The Python launcher compared raw 16-byte cluster ID with the swap profile's
   32-byte incarnation digest. C++ requires SHA256(raw incarnation bytes). Python
   now follows that same mapping. Synthetic launcher fixtures previously repeated
   the incorrect assumption. A regression covers both acceptance and rejection.
3. Initial account adoption needs a bounded installer. adopt_account.py validates
   all four document hashes, principal bindings and freshness; accepts only a
   dormant manifest whose account pins were null; preserves prior manifest/units;
   creates root-owned documents, replaces disabled unit pins and reloads systemd.
   It cannot activate, fund, initialize journals or install credentials. Partial
   failure is retained and cannot be silently retried/overwritten.

42 service tests, six adoption tests and eight credential-export tests pass on Mac and NUC1; 11 credential-handoff tests also pass locally.
Six targeted mutations fail named assertions, without test/compilation errors.
The offline capture reader initially used a wrong route-byte offset; preparation
refused before networking. Correct offset25/length36 was checked against the
production decoder before the single metadata read. No network retry occurred.

## Final installation and remaining launch boundary

Checkpoint 5cdb5e1 contains the identity corrections and dormant adopter. Its NUC
release reuses the exact qualified 13cc98b gateway binary SHA256
09f7c3ff0ea68db58cf55c0045e8fbf35c0dcffe1f01632a21cf77b2107b9d7c:
no production C++ changed and no gateway rebuild is claimed. The Python changes
were tested on NUC before installation. All 27 staged release files were hash
checked, the prior bundle matched before replacement, and old releases remain.
No source tree swap, voter/exporter restart, second cluster or monitoring restart.

The final 0.040764166 SOL position quote returned 4.351947 USDC net quoted output
and 0.008721 USDC explicit fee. This is the opening liquidation **quote mark**,
not a realized sale or guaranteed future receipt. The subsequent 20-request history
cut again found unchanged balances, four transactions and no blocking orders.
The production offline builder encoded the complete opening record within 30 seconds
of the quote and checked profile/account/transaction identities and exact $1 limits.
The opening record exists privately; it has **not** been submitted to Raft.

One initial gateway handoff invocation failed locally with AttributeError because
the CLI used receiver_sha instead of receiver_sha256. It did not invoke SSH or
create a remote credential file. A named CLI-entry regression reproduced this;
checkpoint bb16daf fixes the argument, after which one corrected handoff per role
completed. This is a local export-tool change; no second NUC runtime update needed.
No blind retry or venue request occurred. See handoff-attempts.json.

Root adoption created the four public documents, archived the prior dormant
manifest/units, updated the disabled manifest hashes and reloaded systemd. Real
bearer and P-256 PEM were separately installed into role-owned 0700 tmpfs directories,
with 0600 single-link files under UID/GID 29001 and 29002. Hashes/public-key bindings
match the profile. Running a read-access check as the opposite service user failed
for each credential. Both journals are empty, launch_authorized remains false, and
both units match the rendered manifest and remain inactive/dead/static. Only the
two exact temporary owner staging copies were deleted after successful verification.
Sudo cache was invalidated and the interactive administrative session closed.

The account review expires at **2026-10-13 21:52:03.602 UTC (17:52 EDT)**. This
snapshot is not evergreen launch authority. If launch occurs later, refresh the
account/mark and explicitly replace the dormant review; do not extend timestamps
or overwrite initialized journals. The replicated opening and gateway role,
first-use journal/start ceremony, and a freshly bounded one-buy intent remain
separate launch work. No trade intent or live service activation was performed.

The earlier owner checkpoint/install authorization covers this required scoped fix.
A redundant new approval question was withdrawn in chat before proceeding.
