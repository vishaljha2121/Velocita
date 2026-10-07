# Independent review of the stochastic paper deployment

Reviewed 2026-10-07, about 16:50–17:15 UTC, against `REVIEW_HANDOFF.md`. Review only: nothing on
the hosts or in the working tree was changed, apart from adding this file.

**Origin tags used below**
- **[this deployment]**: code or configuration the handoff attributes to this work.
- **[pre-existing, shipped here]**: uncommitted work from other sessions that this deployment
  compiled into the running gateway image or depends on.
- **[pre-existing]**: already true before this deployment.

## What I checked, and how

| Area | Method | Result |
|---|---|---|
| Single cluster | `podman ps -a`, unit states and listeners on all three hosts | One stochastic voter and exporter per host; original and SMA units stopped and disabled |
| Image identity | Image IDs and labels on each host against the handoff | All match; voters and exporters run the committed `30f8ce8` image |
| Source manifest | Re-hashed all 1,572 entries of `scratch/source_manifest.json` against `scratch/source.tar.gz` | 0 mismatches; the archive has 2 extra files (`SOURCE_COMMIT`, `SOURCE_MANIFEST.json`) |
| What the bundle adds to HEAD | `diff -r` of the bundle against `git archive 30f8ce8` | Listed under H3 |
| Running Python | sha256 of the four `shadow/*.py` files inside the edge container against the working tree | Identical |
| Accounting | Recomputed cash, fees and volume by hand from the six fills in `venue.json`; compared with `ledger.db` and the observer's metrics | Exact: cash $24.740243, fees $0.252557, volume $84.1848 |
| Alert rules | `promtool test rules` on the working tree; every metric named in a rule queried on live Prometheus | Fixture passes; every metric exists except the heartbeat's (not configured) |
| Dashboards | All 194 dashboard queries run against live Prometheus | 151 return data, 43 return nothing (see M5) |
| Loaded config | sha256 of `prometheus.yml`, the rules file and the paper dashboard inside the running containers against the working tree | Identical |
| Venue credentials | File-name search on all three hosts | None; the only token is the alert publisher's |
| Live refusal | Source of the running gateway | `--mode live` still ends at "no live venue transport is compiled" |
| Tests | `test_indicator_paper.py` (11 pass), `test_indicator_deploy.py` and `test_observability_layout.py` (pass), `test_alert_delivery.py` (1 failure, see M10) | — |

Not checked: the research that selected the stochastic rule, the C++ suite on the hosts (I relied on
the recorded 160/160), and the Grafana rendering itself (queries only; no browser access).

## Verified correct

- Exactly one cluster runs. After a reboot only the stochastic node units are enabled.
- Voters are unchanged and built from a commit. Keys are mounted per ADR 0016: the edge sees only
  `caller-12`, the gateway only `caller-13`.
- Catalog root, catalog row, risk limits (12 lots, $25, long-only) and paper funding ($25) are as
  stated, and the limits were applied through an operator caller.
- The fill ledger equals the venue report, fill for fill and in order. The attempt journal shows
  one attempt per bar, with the one interrupted bar left unreplayed, as documented.
- Paper state survived the gateway restart: balances continue across it.
- The uint64 order-id migration kept the original table and the third (full-width) id reconciles.

## Findings

### High

**H1. The cost and price model contradicts ADR 0018, and the dashboard's economics are about
three times kinder than the measured ones.** [this deployment]
- *Evidence.* The gateway runs `--buy-fee-bps 30 --sell-fee-bps 30`
  (`deploy/v2/indicator-paper/prepare.py`). ADR 0018 says paper fills use the **measured**
  per-side cost; the measurement on record for SOL is 100 / 98 bp
  (`docs/evidence/truemarkets_markup_2026-09-28/`). The limit price is a Binance SOLUSDT bar
  close, not the True Markets reference ADR 0018 names. The handoff says only the sell-side
  service fee has live evidence; the 10 bp execution part is assumed.
- *Scenario.* The six fills lost $0.2598 on $84.18 of volume: $324 of volume per $1 lost. At the
  measured 100 / 98 bp the same fills would have lost about $0.84: about $100 per $1, the same as
  plain churn. A reader of "Net P&L since funding" would conclude the strategy is three times
  better than anything measured.
- *Fix.* Either amend ADR 0018 with the evidence for 20 bp + 10 bp, or run at measured costs. In
  both cases put "30 bp scenario" in the panel titles and add a second P&L series recomputed from
  the same fills at 100 / 98 bp (the fills are in `ledger.db`; it is arithmetic).

**H2. RETRACTED (2026-10-07, during the fix work).** I claimed the gateway's volume and fee
counters fall to zero on a restart and jump to the lifetime total on the next fill. That is wrong.
- `GatewayLoop`'s constructor already seeds `fee_micros_total`, `volume_micros_buy/sell` and the
  last-fill time from the venue's saved ledger (committed code, `src/v2_gateway_loop.cpp`), and
  `a_restart_with_the_persisted_venue_reconciles_and_keeps_its_fill_record` tests it.
- The live figures I read as a jump were the seeded totals: the gateway restarted with two fills
  already saved ($28.08), and one more fill had been counted since.
- What remains true is smaller: the aliased `velocita_gateway_*` series only began at 16:20 UTC,
  so range panels cannot see the two fills before that (4 of 6 fills, $56.31 of $84.18). That gap
  is one-off and does not recur on restarts. No code change is needed.

**H3. The running gateway is built from uncommitted code that this deployment did not write or
review.** [pre-existing, shipped here]
- *Evidence.* Against HEAD the bundle adds or changes: `src/v2_gateway_loop.cpp` (+22/−8),
  `src/v2_gateway_main.cpp` (+120/−4, including a `--mode live` selector and a resting-order
  "engineering" profile), `src/v2_resting_paper_*.cpp`, `src/v2_truemarkets_rest.cpp`,
  `include/velocita/v2/paper_report.hpp`, and `CMakeLists.txt` (+66/−3). Of these, only the
  report writer and its `--paper-report-file` option belong to this deployment.
- *What is fine.* The label says so honestly (`30f8ce8…+worktree.a04b3033e9fd1d1d`), the bundle is
  retained and matches its manifest, and the pinned toolchain was used.
- *What is not.* The image cannot be rebuilt from git, which `docs/TOOLCHAIN.md` requires of a
  release. (H2, which I first cited here as an example, was retracted.)
- *Fix.* Review and commit the hunks the paper gateway needs (the report writer and the loop
  change, corrected per H2), leave the rest uncommitted, and rebuild the gateway from that commit.

### Medium

**M1. The strategy's own failure modes have no alert.** [this deployment]
- No rule uses `velocita_indicator_reconciled`, `velocita_indicator_buy_stop`, or
  `up{job="indicator-paper"}`. There is no rule for a stopped edge. [the edge gap is pre-existing]
- *Scenario.* The venue report goes stale or fails validation while the account is long. The edge
  then sends hold on every bar (`indicator_paper.py`, `Source.tick`: `unreconciled` → features
  `(0, 0)`), so the position cannot exit and the drawdown stop cannot act. Nothing notifies.
- `VelocitaGatewayDown` cannot see a crashed gateway directly: `velocita_gateway_up` is written
  as a literal 1 (`src/v2_gateway_loop.cpp`) and the file outlives the process. [pre-existing]
  It fires only indirectly, about 5 minutes later, because the observer stops re-exporting when
  reconciliation fails.
- The edge's file is re-exported for as long as it exists. `velocita_shadow_last_decision_age_seconds`
  is computed when the file is written, so it reads 0.001 forever.
- *Fix.* Add rules: `velocita_indicator_reconciled == 0 for 2m`; any
  `velocita_indicator_buy_stop == 1`; `up{job="indicator-paper"} == 0`;
  `time() - velocita_gateway_heartbeat_timestamp_seconds > 30`;
  `velocita_indicator_price_age_seconds > 700` (the edge has stopped marking bars).

**M2. The monitoring process writes to the trading ledger.** [this deployment]
- *Evidence.* `indicator_observer.py` opens `Journal` read-write on every scrape, runs `ingest`,
  and inserts into `equities`. `Book.gate` in the edge reads `max(equity)` from that table for
  the drawdown stop. The unit mounts the edge directory read-write. The module docstring says
  "Read-only".
- *Scenarios.* (a) Edge and observer ingest the same new fill at once; the loser gets
  `sqlite3.IntegrityError`. In the edge that is not in the caught tuple
  (`OSError, ValueError, KeyError, TypeError`), the bar's attempt is already recorded, and the
  bar is lost. Narrow window, real. (b) Any future bug in the observer's equity arithmetic
  changes the peak the stop is measured from.
- Also: when reconciliation fails, the observer returns before re-exporting the edge and gateway
  files, so every edge and gateway series disappears together.
- *Fix.* Observer opens the database with `mode=ro` and computes equity in memory; only the edge
  ingests; catch `sqlite3.Error` in `Book.gate`; mount the edge directory `:ro` in the observer.

**M3. The edge now decides from a file the gateway writes, outside the committed-command path.**
[this deployment]
- *Evidence.* `Book.gate` masks entry and exit by `lots` from `venue.json` and stops on its cash.
  ADR 0016 chose "the submitter talks to the cluster only through committed commands".
- *Assessment.* The checks are strict (identity, age ≤ 5 s, balances must equal the order list,
  no pending reports) and failure is hold. It is still a second source of truth for position
  and a new coupling. It needs an ADR note, and it disappears if ADR 0021 lands.

**M4. Operator commands were issued by a script on nuc1.** [this deployment]
- *Evidence.* `deploy/v2/indicator-paper/control.py`, action `ceremony`: mounts the voter's copy
  of `caller-9.key` and sends kill-epoch, executor-generation, install-catalog and risk-limits as
  CallerId 7, with the response endpoint rewritten from the Mac to nuc1.
- *Why it matters.* ADR 0015 makes limits operator-only. Until now "operator" meant a person at
  the Mac with the Keychain. The kernel cannot tell the difference, so this is a policy change,
  not a bug: anything running as `velocitas` on a voter host can now act as operator by precedent.
- *Fix.* Record it as an owner decision, or run ceremonies from the Mac as before.

**M5. Two dashboard areas are dead or mislabelled for this strategy.** [this deployment]
- The paper dashboard row "Volume strategies: this ISO week against the loss budget": 17 queries
  on `velocita_paper_week_*` and `velocita_paper_buys_*`, which nothing produces now. That
  includes "Volume per $ lost" and "Loss budget used".
- The Strategy dashboard: 18 queries return nothing (`outcome="tick"`, `decision_code="17"`,
  `velocita_shadow_book_*`, the trend features), and titles still say BTC and SMA 125.
- *Fix.* Remove or rebuild those panels from the `velocita_indicator_*` metrics.

**M6. The one-cluster rule is not enforced by anything.** [this deployment; acknowledged in the
handoff]
- The original and SMA units are still installed, and their ports do not collide with the
  stochastic ones, so `systemctl --user start velocita-v2-node@1` would succeed.
- *Fix.* Add `Conflicts=` between the unit families, or an `ExecStartPre` that refuses when
  another `velocita-*-node` container is running.

**M7. Bad market data crashes the edge instead of being skipped.** [this deployment]
- `decode_bars` raises `ValueError` on a gap or malformed row; `edge.py` catches only
  `(ProtocolError, OSError, UnicodeDecodeError)`. The process exits and systemd restarts it every
  10 seconds for as long as the response stays bad.
- `CandleSocket.recv_text` fetches once per 5-minute bucket, about 1 second after it opens, and
  caches the payload for the whole bucket. If Binance has not yet published the bar that just
  closed, that bar is never evaluated; it is counted under `stale`, which also counts 280 normal
  re-presentations per bucket, so the miss is invisible.
- *Fix.* Treat `ValueError` as a market-data error; refetch until the expected bar is present or
  20 seconds pass; count a missed bar separately.

**M8. The latency panels are valid for what they measure, and leave out the part that matters.**
[this deployment]
- The boundaries in `LATENCY_AUDIT.md` are accurate, and the p99 gate on tick-to-decision is right.
- The four gateway stage p50/p99 panels have no sample gate and about five samples.
- No panel shows bar close to fill. From the six fill stamps it is 2.1, 4.0, 3.7, 4.6, 3.7 and
  4.3 seconds. The causes are the 1-second poll, the HTTP fetch, and the fact that every trade is
  first refused as `stale_risk_state` and retried (3 applied, 3 stale so far).
- *Fix.* Gate the stage quantiles the same way; export bar-close-to-fill from the ledger.

**M9. At the opening pace the run ends in about a day, not fourteen.** [this deployment]
- Six fills in the first 40 minutes. The venue keeps 256 orders for life and the edge stops at 240.
- Each round trip costs about 8.4¢ in simulated fees, so 120 round trips is about $10: the
  drawdown stop and the order cap arrive together.
- There were no fills in the following 38 minutes, so the pace is not steady. Treat this as a
  projection to watch, not a forecast.

**M10. The repository's tool tests are red, and nuc1's source no longer matches its commit.**
[this deployment]
- `tests/test_alert_delivery.py::test_the_webhook_reaches_ntfy_on_nuc1s_tailscale_address_and_port`
  fails: it finds nuc1's address through the `:9410` target, which is now `:11410`. The recorded
  validation ran only the deployment and layout tests.
- Monitoring files were patched in place under `~/src/aeron-raft-pi` on nuc1 while `SOURCE_COMMIT`
  still says `30f8ce8`. The next documented ship replaces the tree and silently reverts them
  unless they are committed first.

### Low

- **L1.** `velocita_indicator_paper_position_lots` reads 12 while the account is flat: it is the
  position from the last refusal, not the current one. No panel uses it. [this deployment]
- **L2.** Leftovers on nuc1: the stopped container
  `velocita-paper-stochastic-edge-stopped-drill-20261007`, and `velocita-v2-shadow-edge.service`
  in the failed state. [this deployment]
- **L3.** The observer is scraped twice every 5 seconds by two jobs, and each scrape reopens the
  journal, runs the schema script and takes a write lock. `equities` grows about 20,000 rows a
  day. [this deployment]
- **L4.** The constants 25,000,000, 12 and 30 are repeated in the journal identity, `snapshot`,
  `Book.gate`, `program_text` and the observer. A change to one is a silent mismatch. [this
  deployment]
- **L5.** Order ids are written as JSON numbers above 2^53. Python reads them exactly; a browser
  would not. [this deployment]
- **L6.** The cluster dashboard's "Role" panel returns nothing: the three terms carry different
  `role` labels and never match. [pre-existing]
- **L7.** `VelocitaQuorumAtRisk` has no `or vector(0)`, so it is silent when no voter is scraped at
  all; `VelocitaQuorumLost` covers that case. [pre-existing]
- **L8.** Something on the Pi listens on port 3000 and is not a Velocita container. [pre-existing]

## ADR conformance

| ADR | Guarantee | Status |
|---|---|---|
| 0001, 0017 | One execution gateway, acting on committed state with authority proofs | Met |
| 0006, 0014 | Voters on reviewed, committed code | Met |
| 0010 | No venue credential on any host | Met |
| 0012 | Immutable catalog, same root | Met |
| 0015 | Operator-only, position-bounded risk limits | Met mechanically; see M4 for who the operator was |
| 0016 | Strategy and operator are separate callers; the submitter learns only from committed commands | Callers met; the second part is weakened by M3 |
| 0018 | Fill-or-kill at the True Markets reference with measured costs | Fill-or-kill met; price source and cost **not met** (H1) |
| 0019 | No live transport compiled | Met; a `--mode live` selector now exists and still refuses |
| Toolchain doc | An image names the commit that built it | Not met for the gateway and edge images (H3) |
| Owner rule | Only one cluster | Met at runtime; not enforced (M6) |

## Suggested order

1. M1: it decides whether the alerts can be trusted. (H2 was retracted.)
2. H1: decide the cost model and label it.
3. M2 and M7: small code changes that remove the two ways a bar is silently lost.
4. H3 and M10: commit what is reviewed, rebuild from a commit, get the tests green.
5. The rest.
