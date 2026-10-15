# Strategy-grant native qualification — ba299ff

**Qualified on NUC1/x86_64 and Pi/aarch64. Not staged, installed or activated.**
Exact executable source: `ba299ff6c89392177cfd4702fda11dabb7ee1719`.
This checkpoint contains the explicit strategy grant from `b490d68` and a test-only correction
matching production read-recovery settings. No production C++ behavior changed after `b490d68`.

| Check | NUC1 | Pi |
|---|---:|---:|
| Selected CTest targets passed | 59 | 59 |
| Python/CLI tests passed (actual log totals) | 407 | 407 |
| Process-failure cases validated | 8 | 8 |
| Continuous cases (three fixed repetitions) | 9 | 9 |
| Synthetic settlements | 54 | 54 |
| Controlled read outages recovered | 3 | 3 |
| Collected artifact SHA-256 checks | 154 | 154 |

Both native runs completed without retries. Incremental compilation took under a minute on each
host using preserved caches. The three continuous scenarios include normal operation with an
injected read outage, crashes around journal advancement, and accepted-but-unconfirmed execution
followed by recovery-only reconciliation. Intents use strategy caller21 after an operator grant.
For every effect, the retained event logs independently show one create, signature and execution;
each case finishes flat at $25 with its expected synthetic fees. In each injected outage, ordinary
reads recover before signature four. Fixture prices are an accounting oracle, not profitability evidence.

The exact NUC node/gateway/signer/client hashes are unchanged from the earlier `b490d68` qualification.
All architecture-specific executable hashes are in [summary.json](summary.json). No deployment
images, installed binaries, runtime documents, accounts, funding, credentials, client sequences or
journals were changed. Source remained clean and exact in each dedicated build cache. The build
containers had network disabled, one CPU/core3 affinity, 2GiB RAM and no production credentials.

## Limitations retained

The earlier Pi `b490d68` qualification failed after three settlements. Its logs did not identify
the exact failed Raft operation. The bounded diagnostic passes and subsequent qualification do
not prove its root cause. Original failure evidence remains in [the investigation](../RESULTS.md).
The verified test mismatch is corrected and now has RED/GREEN evidence plus two freshly compiled
mutation kills. Failed commands still stop the production runtime; signer checks are unchanged.

Each incremental build retains six unique compiler-warning lines. The prior full-build warning
inventories remain preserved separately; this is not a warning-free build. The runner's historic
`focused_python_tests:104` field is stale: the actual unittest/CLI log total is 407 per host.
Original raw logs are preserved byte-for-byte, including CTest's whitespace.

## Production observation during qualification

At22:31:28UTC the driver was inactive, gateway and signer active with manual-start guards.
Producer index15 reported $25 managed USDC, zero SOL/effects/pending/fees/P&L. This is producer
telemetry, not a fresh independent TM-wallet read. At22:32:45–47UTC all three voter exporters
reported up/ready, no failure, term5, leader2 and commit/applied15. The exporter binds its Tailscale
address, so the initial localhost HTTP read was refused; reads using inspected bind addresses passed.
One existing production voter per host remains on `qualified-54a5fcb`; gateway/signer remain on
`a835deb`. Native qualification did not install this new grant-capable code.

## Next deployment step — not performed

Prepare a coordinated upgrade of the existing three voters, gateway, signer and operator client.
Reverify account/no-pending state, review window and sequences. Owner halt must precede the grant.
Preserve incarnation `aae119b95ddcea5a206f03ab5ca12c15`, funding, WAL, journals and driver sequence2
(next3). Expected operator sequence10 must be verified before use. Restart each voter with its
exporter; keep one cluster. The gateway-only upgrade helper is insufficient for this kernel change.

Only after every reader understands the new state extension may the owner commit the reviewed
one-time strategy grant (caller21, floor3, opening/evidence/epoch pins), then restore normal mode
and resume the existing driver. No old-reader rollback after granting. No grant or ceremony
record has been sent. Entry drain is October16 13:41:34UTC, review expiry13:48:34UTC; renewal is
owner-run if needed. Qualification does not itself authorize activation or fund movement.

See [commands](COMMANDS.md), [raw producer observation](production-readback.txt) and
[voter metrics](voter-readback.json). Prior 240-order trading approval is preserved separately;
this completed step was the scoped test/evidence checkpoint and offline qualification.
