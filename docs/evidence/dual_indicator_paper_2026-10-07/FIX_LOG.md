# Fix log: findings from INDEPENDENT_REVIEW.md

Started 2026-10-07. One entry per finding: what was done, how it was checked, and what is still
open. Times are UTC. "Workspace" is a clean copy of commit `30f8ce8` at
`~/.cache/velocita-work/fix-20261007`, used so that other sessions' uncommitted work in the
repository is neither built nor committed by accident.

## H2. Gateway counters across a restart — RETRACTED, no change

- Re-read the committed `GatewayLoop` constructor before editing: it already seeds the fee and
  volume totals and the last-fill time from the venue's saved ledger, and a test covers it.
- Re-read the live series with the correct fill times (16:05, 16:15, 16:25, 16:35, 16:40, 16:45):
  the gateway restarted at about 16:08 with two fills saved, which is the $28.08 I misread as a
  jump.
- The review file now carries the retraction.
- Remaining, minor: range panels cannot see fills before 16:20, when the aliased series began.

## M2. The monitoring process wrote to the trading ledger — fixed in code

- `indicator_observer.py` now opens the ledger with a read-only connection, checks it belongs to
  this run, and computes equity in memory. It no longer ingests fills or writes `equities`.
- The edge is the only writer. It now reconciles and records fills on every one-second loop
  (`Book.refresh`, called from `Source.tick`), so the ledger is at most about a second behind
  the venue instead of up to five minutes.
- `Book.gate` treats a SQLite error as "unreconciled" instead of letting it end the process
  after the bar's attempt was recorded.
- When the venue report does not reconcile, the observer still exports the edge and gateway
  files and the ledger-based figures; only the account figures are withheld.
- The generator mounts the edge directory read-only into the observer.
- Checked: a read-only connection on a read-only mount reads the ledger while a writer runs
  and after a writer is killed; after a clean close it cannot open it, and the observer then
  reports `ledger_readable 0` (tested with two containers sharing a directory).
- Tests: 16 in `model/tests/test_indicator_paper.py` (5 new). 11 of 11 mutations killed.

## M7. Bad market data crashed the edge; a late bar was skipped silently — fixed in code

- A response that does not decode, or has a gap, is counted as `invalid` and skipped; the
  edge keeps running.
- The price fetch repeats each second until the bar that just closed is in the response, for
  up to 20 seconds, instead of caching the first response for the whole five minutes.
- A response that still lacks the bar is counted as `bar_missing`, separately from the normal
  `stale` re-presentations.
- Neither case uses up the bar's one attempt.

## H1. Cost and price model against ADR 0018 — labelled, measured alongside, decision recorded

- Not changed: the run still simulates 30 bp per side on Binance closes. Changing the fee would
  need a fresh paper account, and which cost model to use is the owner's decision.
- Added: the observer re-prices the same fills at the measured 100 / 98 bp and exports net P&L,
  fees and volume per dollar lost for both cost models. Hand-checked on one fill: $12.00 buy,
  measured fee $0.12, exit reserve $0.1176, net −$0.2376.
- Dashboard: account panels are titled "(30 bp scenario)"; new panels show net P&L at measured
  costs and volume per $1 lost under each model.
- ADR 0018 has an amendment recording the deviation and the rules while it stands.
- **Open, owner:** accept the 30 bp scenario with its evidence, or return to measured costs.

## M8. Latency panels — gated, and the missing delay added

- The gateway stage p50 and p99 panels now show a value only with at least 100 orders in the
  selected range, and a new panel shows how many orders each stage has.
- New metric and panels: bar close to fill (last, mean, max), computed from the ledger.

## M1 and M9. No alert for the strategy's failures — nine rules added

`VelocitaGatewayHeartbeatStale`, `VelocitaPaperAccountUnreconciled`,
`VelocitaPaperObserverDown`, `VelocitaPaperLedgerUnreadable`, `VelocitaPaperLedgerBehind`,
`VelocitaPaperEdgeStalled`, `VelocitaPaperBadMarketData`, `VelocitaPaperBuyStop`,
`VelocitaPaperOrderCapacity`.
- Each has a firing and a quiet case in the rule fixture; 9 of 9 threshold mutations killed.
- The heartbeat rule closes the old gap where a crashed gateway still read as up.
- The order-capacity rule (80% of 240) and the buy-stop rule cover M9; M9 itself is a
  projection, not a defect.

## M5. Dead dashboard areas — labelled, and replaced where it matters

- The Strategy dashboard and the weekly volume-strategy row now open with a notice that those
  strategies are not running. Their panels were kept: they belong to strategies that may run
  again, and the layout tests pin them.
- The stochastic section gained the figures that row used to give: volume per $1 lost and the
  share of the drawdown stop used.

## M6. One cluster was not enforced — start guard added

- `deploy/v2/one-cluster-guard.sh` refuses, with exit 64, to start a voter while another
  cluster's voter container runs on the same host. It is an `ExecStartPre` in the generated
  paper node unit and in `velocita-v2-node@.service`.
- Tests: `tests/test_one_cluster_guard.py` (4), plus generator assertions.
- Limit: it is per host. Nothing stops a different cluster being started on a different host.

## M3 and M4. Edge reads the venue report; operator commands from a host — recorded

- ADR 0016 has an amendment describing both, how the first is bounded, and the decision the
  second needs.
- **Open, owner:** may set-up automation on a voter host act as the operator?

## M10. Red test and patched host tree

- `tests/test_alert_delivery.py` now finds nuc1's address by its host label, not by the old
  exporter port. The tool suite passes (129 tests).
- The host tree is put right by shipping the fix commit in the documented way (see the
  deployment entry below).

## H3. Images built from uncommitted code — rebuilt from a commit and deployed

**Commit.** `7297bd5` holds the strategy, the observer, the gateway's `--paper-report-file`
option, the generator, dashboards, rules, tests and these fixes. Its tree is byte-for-byte the
workspace that was tested.
- Left out on purpose: the other sessions' gateway-loop change, `--mode` selector, resting-order
  venue and True Markets REST parser. The gateway unit therefore uses `--venue paper` again.
- Included as they stood, because the strategy imports them: `research/indicators.py`,
  `fast_trend_data.py`, `fast_trend_features.py`, `ten_indicator_data.py`,
  `ten_indicator_features.py` (634 lines). They were checked only for this: with the committed
  `rules.py` they produce the same signals as the working tree on 480 synthetic frames. Their
  research content was not reviewed.

**Tests before shipping.** Model suite 358, tool suite 129, clean C++ build 122 of 123 in the
parallel run; `v2-node-process-tests` failed there and passed alone (it has done this before
under parallel load).

**Build.** On nuc1 with `deploy/v2/build-image.sh`, from the shipped tree (hash `51bbf5ab0857100b`
on the Mac and all three hosts), 140 of 140 tests inside the build:

| Image | Tag | ID |
|---|---|---|
| Gateway | `localhost/velocita-v2:paper-7297bd5` | `8bec1a44fdce…` |
| Edge and observer | `localhost/velocita-v2-shadow:paper-7297bd5` | `67471d5ff5e3…` |

Voters and exporters were not rebuilt or restarted; they stay on `m3-cp081`.

**Deployment, 17:37–17:40 UTC, with the account flat.**
1. Unit files backed up on each host under
   `~/velocita-indicator-paper/deployment/fix-7297bd5/units-before/`.
2. nuc1: gateway unit to the new image and `--venue paper`; edge and observer units to the new
   image; observer mounts reduced to the stochastic edge and gateway directories, both read-only.
3. All three hosts: start guard added to the stochastic node unit, and the updated
   `velocita-v2-node@.service` installed. No voter was restarted.
4. nuc1, 155 seconds into a bar: edge stopped, gateway restarted, edge started, observer
   restarted, then Prometheus and Grafana restarted to mount the new source tree.

**Checked after.**
- Paper state identical before and after the gateway restart: 0 lots, $24.635620 cash,
  $0.336780 fees, 8 orders. The gateway re-armed at once.
- The new edge evaluated the 17:40 bar (one attempt, done). No unit restarted by itself.
- The observer cannot write: `touch` in its ledger mount fails with "Read-only file system";
  `ledger_readable 1`, `ledger_fills_behind 0`.
- Prometheus loads 47 rules; all ten scrape targets are up; the only active alert is the
  recorder database size, which predates this work.
- Grafana serves the dashboard built from this commit (hash matches the shipped file).
- The guard returns 0 for the running cluster's voter and 64 for the original cluster's voter on
  every host.

**First figures from the new metrics** (8 fills, $112.26 volume):

| | 30 bp scenario | Measured 100 / 98 bp |
|---|---|---|
| Fees | $0.337 | $1.111 |
| Net P&L | −$0.364 | −$1.139 |
| Volume per $1 lost | $308 | $99 |

Bar close to fill: mean 3.8 s, max 4.6 s.

**Rollback.** The previous image tags are still on nuc1
(`velocita-v2:paper-indicators-20261007`, `velocita-v2-shadow:paper-stochastic-20261007`); restore
the unit files from the backup directory and restart the three services.

## Still open

- **Owner decisions:** the cost model (H1) and operator commands from a voter host (M4).
- **M6 limit:** the guard is per host.
- **Not done, low severity:** L1–L8 in the review, including the leftover stopped container on
  nuc1 and the observer being scraped by two jobs.
- **Other sessions' uncommitted work** in the repository is untouched and still uncommitted.
