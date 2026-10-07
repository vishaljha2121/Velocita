# One-second stochastic paper deployment — October 7, 2026

Status: continuation deployment was superseded by the owner-requested fresh $25 reset.
The new sole cluster is healthy; owner ceremony completed and paper trading is running.
See ACTIVATION_RESULT.md for the startup reconciliation and verified first fill.
See RESET_COMMANDS.md for the current state and exact executed/pending commands.
The sections below document the historical continuation attempt.

## Source, hosts and images

The Mac prepares commits/archives, runs Python validation, invokes SSH, and stores
remote build stdout/stderr. Native production compilation and image assembly run
on nuc1 with `BUILD_JOBS=2 deploy/v2/build-image.sh shadow TAG`. No production
image is built on the Mac. Pi/nuc2 do not need new voter binaries for this update.

- Original implementation: `978b4c46d02e9513c2f42bffb1c9f72d2b24aed1`.
- Legacy mutation-test maintenance only: `381faa7`.
- Startup repair: `0680b08a5bbfbcaba32a77fbdf5cbe5d0162f40c`.
- Pinned amd64 toolchain image ID:
  `2a79f82cce5de80ab5afc832af64acba549c963e89df509acfaeb5565cb232ee`.
- Initial image `paper-1s-978b4c4` ID:
  `29f203ed7b9a10454718114f6485db4676babfe5836a8286a526965e821b943d`.
- Target edge/observer: `localhost/velocita-v2-shadow:paper-1s-0680b08`.
- Gateway remains `localhost/velocita-v2:paper-7297bd5`, `--venue paper`.
- All voters/exporters remain `m3-cp081`; no voter restart, new cluster, operator
  ceremony, credential push, account refund/reset or live venue operation.

The fixed committed archive SHA256 is
`83c76ca431a97ef4e64b756d217d552f7cf24e7f331c1843f12e99ac2f6b7469`.
It is extracted to `~/src/stochastic-1s-0680b08` for the nuc1 build. The active
source tree on all three hosts is replaced from this same archive; previous
source is retained at `~/src/aeron-raft-pi.before-stochastic-1s-0680b08`.
Prometheus/Grafana must restart to remount updated provisioning files. The market
data recorder files are unchanged; its running source mount is preserved.

## Account and activation

Existing incarnation: `19137567d0c14889b29f7674e26a8fd8`.
Catalog root: `3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe`.
Account/instrument 2, venue 20, source 41, policy/schema 128/138.
The old edge stopped flat after its normal exit, before cutover. Baseline:
$24.520467 cash, zero lots, 10 fills, $0.420733 historical fees,
$70.150800 buys and $70.092000 sells. Original funding remains $25.

The setup-only caller 11 published the new program. Its existing session/state
were retained (generation 2, first sequence 10001, kill epoch 1, executor
generation 1). The gateway restarted with 20 bp/side and every cash, inventory,
fee and historical order field was checked equal across restart. Prior fills
retain 30 bp; buy-side 20 bp symmetry is assumed from sell-side evidence. This
is a fee-only model, not verified all-in execution cost.

Exact activation transcript: `scratch/cutover.log`.
Exact one-time scripts: `ACTIVATE_RECORDED.py`, `REPAIR_RECORDED.py`. These are
review evidence, not idempotent installers; do not rerun them. Backups of units,
old observer config, venue report, durable venue state and a SQLite journal backup
are at `~/velocita-indicator-paper/deployment/one-second-978b4c4` on nuc1.

## Startup failure and correction

First activation failed before submitter launch: the runner omitted the existing
required `shadow_recorded_code` argument to `run_once`. Unit restart was stopped;
no strategy decision or new fill occurred. A regression test executes `main`
against an autospecced actual runner signature. It reproduced the TypeError
before the fix. The fixed 369-test model suite passed. This revealed a gap in the
original unit coverage; the first ten indicator/guard tests had passed inside
the initial image but did not exercise startup wiring.

The committed fix explicitly passes code 17. It is rebuilt through the same
host-native script with the full C++ test suite. No runtime source overlay is
used. The immutable epoch and original pilot deadlines remain unchanged; the
repair shortens available observation time and does not renew the trading budget.

## Bounds and remaining evidence

The original epoch starts 2026-10-07 18:31:53.718876 UTC; entries stop at
19:01:53.718876 UTC (15:01:53 EDT), all forwarding ends by the bounded shutdown
path after 19:06:53.718876 UTC (15:06:53 EDT). Source attempts max 2400 across
restarts, max 40 additional fills plus one exit reserve. Stale/missing data can
prevent liquidation; do not claim guaranteed flat exit. No indefinite 1s run.

Preflight: all three voters up, ordinary retained entries <=1000, flat reconciled
account; prior disk/WAL checks recorded in validation. Capacity reserves 4800
frame entries plus 1312 conservative order entries under the 8128 ordinary pool.
Observer mounts ledger and venue directories read-only. Edge is the sole ledger
writer. Pilot-only metrics subtract the immutable baseline; lifetime metrics keep
previous five-minute trading.

The continuation epoch was retired before any new decisions or orders. Its
proposed deadlines are historical and no longer schedule a running strategy. Engineering success will not establish strategy profitability,
True Markets fill realism, maker-order eligibility or leaderboard credit.

## Fresh reset result

New cluster c8a6a0a78fbf49a099b222124a5d557f: three ready voters, term 1,
one leader, empty retained history at bootstrap. Old data archived on all hosts.
New image paper-1s-0680b08 passed 140 C++ and 11 in-image Python tests. Its ID is
33d6c413a396fddbefd908d86c15f706b4cc4b8b31345bd4cb4b1addc2c7f09f.
Owner ceremony, new venue funding and edge activation completed. A startup
reconciliation interruption is documented in ACTIVATION_RESULT.md.
Prometheus configuration is valid, 47 rules loaded, Grafana healthy, and new-run
voter count three. Monitoring is isolated with a paper_run label.
