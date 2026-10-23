# Native checkpoint qualification result — October 13

**Passed on x86_64 NUC1 and aarch64 Pi.** Final source checkpoint:
`86f440806c6adc6f4c1dc9dc7a271e3d95293d76`. No new release was deployed.

| Check | NUC1 | Pi |
|---|---|---|
| Full baseline suite at `1d2c820` | 201/201 passed, 524.01 s | 199/201 passed, 1,170.90 s |
| Two corrected native fixtures at `86f4408` | Each passed 3/3 repetitions | Each passed 3/3 repetitions |
| Other 191 top-level executable hashes | Unchanged | Unchanged |
| Supplementary account checks | 44/44 passed | 44/44 passed |
| Independent process artifact checks | 24/24 passed | 24/24 passed |
| Independent schema vectors/literal audit | Passed | Passed |
| Full-suite memory peak, 2.75 GiB cap | 2,745,487,360 bytes | 2,744,483,840 bytes |
| Full-suite OOM events / kills | 0 / 0 | 0 / 0 |

This is selective qualification: 199 unchanged registered tests retain their
full-suite passes, and the two modified tests each pass three fresh repetitions.
The entire source delta from the full-suite baseline is three test files: Python
scratch setup, the crash-drill shutdown loop, and the busy-frame counter check.
All 193 top-level executable hashes were compared; only the two modified test
executables changed. Production code and production binaries did not change.
The final candidate has coverage for all 201 registered tests; we do not relabel
Pi's earlier 199/201 run as a clean full-suite pass.

## Failures retained and resolved

The 1-GiB and 2-GiB runners OOM-killed synthetic voters. The owner-authorized paper
pause made room for a 2.75-GiB test limit, already shown necessary by older
qualification evidence. The initial runner had not carried that budget forward.

Earlier qualification also caught missing C++ headers, stale fixture expectations,
missing offline-reader dependencies, root/private-directory runner problems, and
a too-short signer request budget. Those corrections and their exact commits are
in [the review](../CHECKPOINT_REVIEW_2026-10-13.md).

The final follow-ups corrected test scratch placement, kept voter turns running
while the separate signer exits, and required a new busy-frame event after the
worker parks. The failed process scenario is retained alongside the three new
passing runs. No order-count, balance, held-frame, SIGKILL, or recovery assertion
was relaxed. Production request deadlines were unchanged.

## Runtime restored

All three original voter/exporter pairs recovered on unchanged `roles-884816d`
images and incarnation `348e0b2cd4f639ff404df4eeadee0ee5`. At verification, each
reported ready=1, failed=0, term=17, leader=2, and current/commit/applied/durable
index 17822. One production cluster is running. Paper gateway and observer were
restored; gateway reports zero kernel/venue positions and new-risk-stopped=0.
No ledger, WAL, virtual balance, catalog, configuration, image, or credentials were
reset. Monitoring and recording remained running throughout maintenance.

The strategy edge was already inactive before maintenance: it exited October 12
at 18:15:23 EDT after market-data timeouts (`market data ended`, 8180 decisions).
It remains inactive; restored infrastructure does not mean strategy decisions
have resumed. This is a pre-existing condition, not a new reset or deployment.

Commands: [MAINTENANCE_COMMANDS.md](MAINTENANCE_COMMANDS.md).
Health/image IDs: [RESTORED_HEALTH.txt](RESTORED_HEALTH.txt).
Machine-readable summary: [results.json](results.json).

## Evidence and limits

The `nuc1/` and `pi/` folders retain current and failed-attempt logs, command
arguments, CTest XML, source identities, hashes, resource counters and synthetic
process transcripts. Each artifact manifest was independently hash-checked after
copying. No real keys or venue credentials were mounted in qualification; the
collectors omit generated fixture keys. The original `checkpoint.json` remains
the original a890307 integration manifest.

[WARNINGS.md](WARNINGS.md) records 113 diagnostics deduplicated by location/message
across retained builds, including compiler/dependency messages. Identical log
copies are counted once. Warning cleanup remains the owner's next-iteration work.

Native tests do not qualify distinct production UIDs, the complete deployed Aeron
gateway/signer route, held-position operational drills, or real TM transaction
economics. Separate paper deployment and owner-reviewed live gates remain open;
no signing, funds movement or live activation was authorized or performed.
