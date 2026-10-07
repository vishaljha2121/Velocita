# Stochastic paper checkpoint — 2026-10-07

Checkpoint tag: `v2-paper-stochastic-2026-10-07`. The owner approved checkpointing
the running system before beginning documentation. This is a source/deployment
checkpoint, not a profitability claim or approval to trade live.

## Exact running version

One three-voter cluster, incarnation `c8a6a0a78fbf49a099b222124a5d557f`.
Each host runs one stochastic voter and its matching exporter. nuc1 also runs
the strategy edge, paper gateway, observer and monitoring services.

| Component | Image tag | Source commit |
|---|---|---|
| Voters/exporters on nuc1, nuc2, Pi | `velocita-v2:m3-cp081` | `30f8ce8e221698f7f1e409f0563ff49d1cd8ab46` |
| Paper gateway on nuc1 | `velocita-v2:paper-7297bd5` | `7297bd56f95ce33a4d3677d6d215592048d78789` |
| Edge and observer on nuc1 | `velocita-v2-shadow:paper-cash-9a71762` | `9a717627b3e1d3971cb889a6517252a45fc45179` |

Full per-host image IDs, source labels, non-secret mount paths, unit hashes and
runtime observations are in [checkpoint/](evidence/stochastic_cash_2026-10-07/checkpoint/).
All three source commits are ancestors of this checkpoint. The checkpoint commit
adds records; it is not the source commit of every running binary. Rebuilding
every component from the checkpoint tip would be a new deployment.

The five mounted dashboard JSON files match the committed files in
`docs/evidence/stochastic_cash_2026-10-07/observability/` by SHA256. The paper
dashboard SHA256 is `a2591305136f54a787c7e78197424c75c61a597183c6524c3b1cece6211310d9`.
Monitoring selects `paper_run="c8a6a0a78fbf49a099b222124a5d557f-cash-v1"`.

## What the strategy does

This is long-only SOL spot simulation, with fixed 12-lot orders (0.12 SOL).
The Python edge reads completed one-second SOLUSDT candles from Binance's public
feed. Those candles are a market-data proxy, not True Markets executable quotes.

1. Compute stochastic K from the last 900 seconds' high/low range; smooth it
   over 10 seconds. D is a 30-second average of K.
2. Compute fast EMA300 and slow EMA1200, seeded with their initial simple average.
3. Enter when K crosses above D, K is at least 60, and EMA300 exceeds EMA1200;
   require three consecutive qualifying seconds after the crossover.
4. Exit when K is below D and at most 40 for three consecutive seconds.
5. Apply a 60-second cooldown after an exit. Use the previous candle's signal
   on the next candle; reject stale data rather than replaying missed trades.

Policy/schema IDs are **129/139**, variant `stochastic_1s_cash_v1`.
Features are 0 `exit_flag`, 1 `entry_flag`, 2 price in cents (an integer execution
input, not a generic indicator in micros). True flags are 1,000,000.
The ordered rule program checks exit first, entry second, and otherwise holds.
The edge gates buys while holding a position and gates sells while flat.
One-second decisions do not imply one-second trades.

## Execution and ownership

The edge calculates indicators and submits decision frames. Raft replicates the
deterministic rule decision and risk/account state. The gateway obtains execution
authority and applies fill-or-kill simulated fills through `--venue paper`.
Paper fees are **20 bp per side**, rounded up to USDC micros. The operator's
kernel fee reserve remains **30 bp**; a reserve ceiling is not a charged fee.

Only the edge writes `ledger-cash-v1.db`. The observer mounts ledger and venue
files read-only and exports Grafana/Prometheus metrics. Completed sessions use
a validated standalone SQLite copy so monitoring does not need writable WAL
sidecars. Account reconciliation is required before taking a new action.

The strategy has no scheduled stop. When flat, it stops if available cash cannot
fund another fixed-size purchase plus fees. Low cash while holding SOL does not
disable exits. Existing $10 drawdown, 240 lifetime-order and correctness/storage
guards can stop it earlier. No automatic top-up or account reset is enabled.

## Observed state at checkpoint

At 2026-10-07 22:38:51 UTC (18:38:51 EDT):

- 3,049 decisions; zero edge errors and zero Python/kernel rule disagreements.
- Ledger readable, account reconciled, fills behind = 0, gateway armed.
- Three new fills in this continuation: buy, sell, buy. Seven account fills total,
  including four preserved from the earlier pilot.
- Cash $10.886238, position 0.12 SOL, equity after estimated exit fee $24.778398.
- Continuation volume $41.6652; fees $0.083332; net mark-to-market P&L -$0.090772.
- All ten scrape targets up. A pending reconciliation alert seen during the
  sequential snapshot cleared on the subsequent check; do not treat this
  multi-request observation as an atomic distributed-state snapshot.
- The only remaining firing alert was recorder database size (about 20.79 GB).
  Notification delivery had recovered; phone display remains owner-confirmed.

Session baseline was $24.869170, not a new $25 top-up. Original funding remains
$25. Runtime balances and counters continue changing after this checkpoint.

## Verification and operations

The released edge build passed 140/140 native CTest targets and 25 focused tests
against the packaged Python code. Earlier checks include 383 model tests,
131 tooling tests and five named mutation kills. See
[VALIDATION.md](evidence/stochastic_cash_2026-10-07/VALIDATION.md).
These are engineering checks; neither this short run nor proxy fills establish
profitable trading, real venue execution quality or leaderboard eligibility.

- [Deployment commands and incidents](evidence/stochastic_cash_2026-10-07/DEPLOYED.md)
- [Alert mount/ownership repair and delivery evidence](evidence/stochastic_cash_2026-10-07/ALERT_DELIVERY_REPAIR.md)
- [Approved ongoing-session scope](evidence/stochastic_cash_2026-10-07/PROTOCOL.md)
- [Earlier fresh-ledger reset commands](evidence/stochastic_1s_2026-10-07/RESET_COMMANDS.md)
- [Paper trading runbook](PAPER_TRADING_RUNBOOK.md)

This checkpoint makes no host changes. Preserve the current ledger, gateway
state, voter logs and operator sessions. A Git checkout alone cannot restore
those mutually dependent runtime states. No hot Raft/database backup was taken;
this record is not a crash-consistent cluster recovery image. Do not rerun old
activation, reset or owner-ceremony scripts against this running session.

For an exact binary rebuild, use each component's source commit above and
`deploy/v2/build-image.sh` in an isolated checkout, preserving the native target
architecture. A replacement voter and its exporter restart together. A second
cluster must never run alongside this one. Source-tree swaps require refreshing
affected bind mounts, including ntfy templates. Alertmanager's volume must be
writable by its configured container UID/GID 0:0 in the rootless user namespace.

## Cleanup and documentation starting point

No destructive cleanup is needed to checkpoint this version. The shared working
tree contains unrelated uncommitted REST/resting-order, research and test work.
It is deliberately excluded, as are scratch output, Python caches and credentials.
Do not broad-stage shared CMake/gateway files or prune images/state/worktrees.

The next documentation pass can build on this baseline in this order:

1. End-to-end architecture: candle, features, Raft decision, risk authority, fill,
   reconciliation, ledger and dashboard; distinguish latency boundaries.
2. Strategy contract: exact indicator math, timing, warmup, crossover state and
   position/cash guards; include HOLD examples and the price-proxy limitation.
3. Operations: committed native builds, one-cluster deployment, operator ceremony,
   persistent state ownership, monitoring and recovery without duplicate orders.
4. Evidence: engineering tests and mutations separately from research results,
   paper outcomes and the still-unmet requirements for live trading.

Recorder archive/compaction and a broader cleanup of old research branches remain
separate maintenance tasks. They are not silently included in this checkpoint.
