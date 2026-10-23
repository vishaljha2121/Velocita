# Phase 2 (paper trading) handoff — 2026-09-29 21:20 EDT

> **UPDATE 2026-09-30:** resumed.
> - All five streams are finished, reviewed and **merged into this tree**. The merged tree passes
>   122/122 ctest and the model tests.
> - The per-agent copies are now history only.
> - Remaining: deploy (§4.5), which is a fresh cluster and needs the owner's keys 12 and 13.
> - Before arming the gateway, read the operational note in ADR 0017: it never re-arms itself
>   after an election.

Paused because of a usage limit. This file is the single entry point for whoever
continues (Codex or a later Claude session). Read `docs/CURRENT_HANDOFF.md` for
the state of the live cluster and everything before Phase 2.

## 1. Goal and owner decisions (all approved by the owner)

Paper trade on the live three-voter cluster: the kernel authorizes real orders,
a paper TrueMarkets venue fills them, the fills flow back as committed evidence.

- **Gateway:** a separate process `velocita-v2-gateway` on nuc1 (book ch45: one
  gateway per venue, outside the cluster; it never updates positions, it submits
  observed venue events).
- **Node protocol:** new node-protocol bytes approved outright.
- **Fills:** fill-or-kill (FOK) at the measured costs (about 115 bp buy, 120 bp sell).
- **Limits:**
  - $25 per order and in total, one position, long-only.
  - Halt on an unknown outcome.
  - Total trading budget is **$25**.
  - This budget is NOT an approval to trade live.
- **Operator adjustable, book-aligned (Deterministic Speed, `~/Desktop/Trading/velocita-book`):**
  - Limits are operator-only commands.
  - The strategy runs as a non-operator caller.
  - Book deviations are recorded in `docs/V2_ARCHITECTURE_CONTRACTS.md` §8.1: no limit expiry, four-eyes lives in operator tooling (ch18), pending orders count on both sides.
- **Deferred by the owner to the next phase:** pre-vote.

Standing constraints:
- **Security and credentials**
  - No venue trading and no venue credentials.
  - Only read-only GET calls with no keys; order sends are mocked.
  - The second TM account is for quotes only.
  - Raw markup capture files are private.
  - Never repeat the owner's passwords.
  - The nuc2 sudo password is passed on stdin only, never written down, and the owner should change it.
- **Git and file handling**
  - Never `git add .`/`-A`, reset, clean or stash.
  - Protected paths: docs/README.md, docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md, docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md, docs/adr/0002-*.
- **Actions:** confirm destructive or irreversible actions with the owner.
- **Engineering bar:** see `~/.claude/projects/-Users-vishaljha/memory/velocita-cpp-quality-standards.md`:
  - tests first;
  - the test-oracle rule;
  - mutation-check every rule with a named failing test;
  - sparse comments.

### Decision records (read these for the reasoning)

| ADR | Decision | State |
|---|---|---|
| [0015](../docs/adr/0015-operator-only-position-bounded-risk-limits.md) | Risk limits are operator-only and bound position. Includes the book mapping and the four deviations, each with its reason. | Implemented in the `kernel` copy |
| [0016](../docs/adr/0016-strategy-and-operator-callers-are-separate.md) | Strategy (21) and operator (9) are separate callers. Paper mode is level-triggered and re-syncs from the refusal. | Implemented in the `edge` copy |
| [0017](../docs/adr/0017-the-gateway-is-a-separate-process-reading-committed-state.md) | The gateway is a separate process, reads the canonical snapshot and receives authenticated proofs. | Design accepted, implementation in progress |
| [0018](../docs/adr/0018-paper-venue-fills-fill-or-kill-at-measured-costs.md) | The paper venue fills FOK at the limit price with measured costs, has fault scenarios, and surfaced five kernel findings. | Implemented in the `paper` copy |

### Owner decisions, in the owner's words (2026-09-29)

- "lets tackle C, ledger and phase 2" → then "per defer pre-vote to next phase; lets lackle paper trading".
- Phase 2 choices (AskUserQuestion):
  - separate gateway process on nuc1;
  - new node-protocol bytes "Approve outright";
  - fills FOK at measured costs;
  - limits $25/order, 1 position, halt on unknown.
- "record that the trading budget is 25dollars rn … work with that".
- "shouldnt these parameters be operator adjustable like long only or position sizing and all? model should adjust them?"
  - Answer given: the model **proposes**, the operator **sets** (ch49: one worker must not both propose and approve).
- "lgtm; lets try and be as close to the book as well".
- "use parelle agents for implementaiton; you be the judge for correctness; lets paper trade today".
- "the dashboard needs to be divided into diff dashes".
- "lets pause here and handoff to codex".

### How the work was run, and why

The repo has a large uncommitted tree (CP-064..074 plus Phase 2), and the rules forbid committing
without the owner. So git worktrees, which start from HEAD, would have lost that work. Instead:
- each agent got an rsync copy with a declared file ownership;
- a `base` copy records the common starting point;
- the reviewer merges by file.

Before launch, I added the shared struct fields in `kernel.hpp` by hand, so every agent coded
against the same `ConfigureRiskLimits` and `RiskAccount` layout.

### What the reviewer checked independently versus took from reports

**Checked by me:**
- **Dashboards:** reran `tests/test_observability_layout.py` in the copy and after merging (OK); read the alert and CMake diffs.
- **Kernel reducer:** read the whole `src/v2_kernel.cpp` diff:
  - the gate comes before the receipt;
  - the position rule is P ± (R+q) against the cap or floor, with checked arithmetic;
  - a cap is stored without refusal below the position;
  - the stale detail is attached.
- **Snapshot budget:** measured the 65,483 baseline with `v2-layout-report`.
- **Arithmetic:** the headroom numbers (528 → 464 free) and the clip at the live BTC 83,430 ($20.86; $21.17 with the fee hold, under $25).
- **Shadow edge:** confirmed it healthy after a false alarm (its journal logs only when a run ends).

**Taken from agent reports, not rerun by me:**
- Full ctest counts:
  - kernel 119/119;
  - edge 118/119 plus 223 model tests;
  - paper 22 tests, and 119/120 overall in that copy.
- Mutation results.
- The golden regeneration.

Rerun them after the merge. The mutation claims are the ones to spot-check first, per the quality
standards; each report names the killing assertion.

## 2. Where the work is

The work was split across parallel agents. Each one has its own copy of the repo, made
from one baseline:

| Path | What |
|---|---|
| `~/.cache/velocita-work/base` | Baseline snapshot of the repo when the agents started. Diff every copy against it. |
| `~/.cache/velocita-work/kernel` | P2.1 kernel risk limits — **done**, one follow-up open (§4.1) |
| `~/.cache/velocita-work/paper` | Paper TrueMarkets venue — **done** |
| `~/.cache/velocita-work/edge` | Submitter paper mode, operator verbs, deploy units, runbook — **done** |
| `~/.cache/velocita-work/gateway` | Node↔gateway protocol + gateway process — **IN PROGRESS** (cut off while running its node test suite) |
| `~/.cache/velocita-work/dash` | Dashboard split + gateway alerts — **done and already merged into this repo** |
| `~/.cache/velocita-work/tmp-*` | Each agent's scratch space: mutation harnesses, probes, logs |

Build dirs are `~/.cache/velocita-build/agent-<name>`. Configure a copy with:

```
cmake -S <copy> -B ~/.cache/velocita-build/agent-<name> -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_AERON=/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src \
  -DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT=/Users/vishaljha/.cache/velocita-deps/willemt-raft-cp073 \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64
```

Never modify deploy/patches or the willemt clone. A patch change needs a fresh
pristine clone.

## 3. What each finished stream delivered (reviewed)

### Kernel (`kernel` copy): 119/119 ctest

The report is summarised here; §8.1 of the contracts doc in that copy has the detail.
- **Operator gate:** `ConfigureRiskLimits` is operator-only. Other callers get `unauthorized_control`, with no trace and no receipt.
- **New limit fields** `max_position_quantity`, `allow_short` and `reason_code`:
  - `max_position_quantity` must be ≥ 0, and 0 denies;
  - `reason_code` must be non-zero.
- **Position rule** at authorization, where R counts open orders on both sides:
  - buy: P + R + q ≤ cap;
  - sell: P − R − q ≥ (allow_short ? −cap : 0);
  - otherwise `ApplyCode::position_limit_exceeded = 67`.
- **Tightening:** a cap below the current position is accepted and makes the account reduce-only.
- **Re-sync:** a `stale_risk_state` authorization refusal carries `ApplyResultDetailKind::risk_state = 7` with words {risk_version, position_version, position_quantity atoms}.
  - An exact retry returns the retained (older) versions.
  - So a strategy must send a fresh request to learn the current ones.
- **Byte changes:**
  - Codec schema 8→9: the ConfigureRiskLimits body goes from 88 to 101 bytes.
  - Snapshot schema 8→9: the risk-account record goes from 112 to 121 bytes, and the maximum snapshot is 65,519 (17 bytes under 64 KiB).
  - Static identity schemas 8→9, so V8 and V9 binaries cannot form one cluster.
  - `TradingState` is 81,456 of 81,920 bytes, and the layout tripwire went from 512 to 448.
- **Goldens:** regenerated by `tools/v2_schema_goldens.py`. `--check` reproduces the V8 values from their fields.
- **Pre-existing test bug fixed:** `test_snapshot_rejects_unbacked_risk_aggregates` had wrong offsets.

### Paper venue (`paper` copy): 22 tests, 31/31 mutants killed

Files: `include/velocita/v2/paper_venue.hpp`, `src/v2_paper_venue.cpp` and `tests/v2_paper_venue_tests.cpp`. They are also copied into the `gateway` copy, which may have been adapted since; diff before merging.

Behaviour:
- **Structure:** standalone, bounded and deterministic; built on the `sim::VenueTransport` seam.
- **Fills:** FOK at the limit price. The fee is `ceil(notional*bps/1e4)`, reported in `VenueObservation.fee`.
- **Long-only backstop:** a sell with no inventory is killed.
- **Faults** (ch46): delay, duplicate, reorder, drop request, drop response, reject, split, query unavailable.
- **Killed orders** are not recorded, so a query answers not_found. The gateway must then submit `order_reconciled` with venue order id 0.

CMake lines are in its report; they are also in `gateway/CMakeLists.txt`.

### Edge / submitter / deploy (`edge` copy): 118/119 ctest + 223 model tests

The one failure was the layout tripwire, which the kernel copy fixes.

**Authority split:**
- `velocita-v2-shadow-submitter --setup-only` is an operator one-shot (key 11 / CallerId 9).
- The tick loop runs as CallerId 21 (key 12) and refuses operator IDs with exit 64.

**Paper mode:**
- `--mode paper` evaluates authoritative frames bound to the current risk view.
- It sends `AuthorizeDecision` when the decision is an action with a proposed resolution.
- It re-syncs from the stale-refusal detail.

**Client verbs:** `velocita-v2-client strategy-mode ...` and `velocita-v2-client [--dry-run] risk-limits key=value ...`.

**Deploy:**
- Gateway unit and setup unit.
- Templates get callers 12 and 13 (CallerId 22) plus `gateway.caller = 22`.
- The edge unit mounts only key 12.
- The book defaults to $25.

**Runbook:** `docs/PAPER_TRADING_RUNBOOK.md`, with exact risk-limit values:
- 25-lot clip, which is $20.86 at BTC 83,430;
- $25 order and exposure caps;
- 1.5% fee hold;
- max position 25, `allow_short=0`.

The file `tests/v2_node_process_tests.cpp` was edited in this copy (outside its ownership) because it spawns the submitter.

### Dashboards: merged here

Five dashboards: Overview (the old uid), Raft, Strategy, Paper and Hosts. There are three gateway alerts, and `tests/test_observability_layout.py` passes. The promtool rule tests passed in the `prom/prometheus:v3.13.2` image. An optional CTest runs them when promtool is on PATH.

## 4. Open work, in order

### 4.1 Mass-cancel result detail (DONE, merged 2026-09-30)

Fixed in the kernel copy and merged into this tree. `mass_cancel` (6) is now admitted by the
result codec and by the snapshot's retained-receipt check, with these constraints:
- the code is `applied`;
- `cancelled <= in_scope <= 16`;
- `skipped == in_scope - cancelled`.

Tests: `test_mass_cancel_result_survives_codec_snapshot_and_retry`,
`test_mass_cancel_detail_is_validated_by_both_codecs`, and
`mass-cancel-commit` in `v2_replication_tests` (3 voters, restart, digest). All five mutants were
killed in each codec.

**What the LIVE cluster (built before this fix) does on an operator mass-cancel:**
1. The command commits and applies on every voter.
2. The operator's node throws "committed result did not encode" and exits 2.
3. Within about 100 ms every voter's health publish throws "replica state is not encodable" and
   exits 2.
4. Recovery replays the entry, so the cluster likely crash-loops.

**Never send mass-cancel to the pre-Phase-2 cluster.** The fresh-cluster deploy carries the fix.

**Pre-existing, not fixed: a seed-dependent liveness failure.**
`test_a_voter_behind_the_leaders_snapshot_does_not_stop_the_cluster` fails for 6 of 30
`srand` seeds (3, 10, 19, 20, 27, 30) on the original code too.
- Cause: willemt draws election timeouts from the unseeded process `rand()`
  (`raft_server.c:65`). So each test's timing depends on how many `rand()` calls earlier tests
  made.
- It is real leader churn or a stranded voter after compaction.
- It probably explains the earlier Pi test flake.
- Fix: seed per test and investigate the property. Do not pin a lucky seed.

### 4.2 Finish the gateway stream (`gateway` copy)

It was cut off at "Now the full gateway node suite". New files so far:
- `gateway_link.{hpp,cpp}`, `gateway_loop.{hpp,cpp}`, `v2_gateway_main.cpp`
- `tests/v2_gateway_node_tests.cpp`, `tests/v2_gateway_protocol_tests.cpp`

Edited: `node.hpp`, `v2_node.cpp`, `node_config.*`, `node_protocol.*` and `authority_proof.hpp`. Requirements (the original brief plus follow-ups sent to it):
- **State read:** the node answers the configured gateway caller only, with the canonical state snapshot of applied state plus term, applied index and leader. Chunk it if Aeron cannot carry about 65.6 KB.
- **Authority request:** the leader proposes the barrier and answers with the encoded proof. A narrow friend decoder builds an `AuthorityProof` only from a response verified under the voter's response key, session-fenced and matching the pending request. Write the one-sentence trust argument into the contracts doc.
- **Gateway loop:**
  1. snapshot → `plan_delivery` → `GatewayAuthority` begin/accept/consume → `dispatch_gateway_send`;
  2. PaperVenue → observations → `ObserveFill`/`ObserveOrderEvent` as CallerId 22 → `AcknowledgeGatewayResults`.
  - An unknown outcome or a conflict → `stop_new_risk`, and the gateway exports `new_risk_stopped=1`.
- **PaperVenue wiring:** `--venue paper` wired to PaperVenue: `--buy-fee-bps 115`, `--sell-fee-bps 120`, `--paper-quote-usd 25`, `--paper-base-lots 0`. Spec and profile come from the verified catalog row.
- **CLI must match the deploy unit:** `--caller-key-id 13 --caller-id 22 --key-file --incarnation --catalog --session-file --state-file --response-endpoint --node ... --aeron-dir --venue paper --metrics-textfile /textfile/velocita-gateway.prom`.
- **Metrics (exact names), all counters published at 0 from start:**
  - `velocita_gateway_up`
  - `velocita_gateway_heartbeat_timestamp_seconds` (refreshed every loop)
  - `velocita_gateway_new_risk_stopped`
  - `velocita_gateway_orders_sent_total{side}`, `velocita_gateway_fills_total{side}`, `velocita_gateway_rejects_total{reason}`
  - `velocita_gateway_unknown_outcomes_total`, `velocita_gateway_authority_requests_total{result}`
  - `velocita_gateway_stage_ms` histogram, stage ∈ {decision_to_authorized, authorized_to_proof, proof_to_sent, sent_to_fill, fill_to_committed}
  - `velocita_gateway_position_lots{account,instrument}`, `velocita_gateway_fees_usd_total`, `velocita_gateway_last_fill_timestamp_seconds`
- **Tests:**
  - codec round trips and garbage input;
  - wrong-caller and not-leader refusals;
  - end to end: delivered exactly once, fill committed, position moved;
  - leader change during an authority request;
  - lost and duplicated responses;
  - a stale proof after a kill-epoch advance;
  - unknown outcome → new risk stopped;
  - mutation checks.

### 4.3 Merge into this repo (dependency order: kernel → paper → gateway → edge)

For each copy:
1. Copy the files it changed relative to `base`.
2. Before overwriting, check that this repo's version still equals `base`. Only the dashboard files have changed here since.
3. Apply CMake hunks by hand. Kernel and edge need none; paper and gateway add targets and tests.

Conflict points:
- **Stale-refusal placeholder:** in `edge/include/velocita/v2/shadow_program.hpp`, replace the placeholder `kRiskStateDetailKind = static_cast<ApplyResultDetailKind>(7)` with `ApplyResultDetailKind::risk_state`. The value 7 matches.
- **ApplyCode names:** the edge's `apply_code_name()` is a full switch built with `-Werror=switch`, so add `position_limit_exceeded`.
- **Template test:** the edge's `tests/v2_deploy_template_tests.cpp` strips `gateway.caller` before parsing (`without_gateway_caller`). Remove the strip once the gateway's node-config key is merged.
- **Node process test:** `tests/v2_node_process_tests.cpp` is changed by the edge; check whether the gateway changed it too.
- **Paper venue files:** the `paper` and `gateway` copies may differ.
- **Kernel doc:** the kernel's `docs/V2_ARCHITECTURE_CONTRACTS.md` must be merged with the gateway's gateway-section edits.

Then:
1. Run the full ctest and the model tests on the merged tree.
2. Build on the Pi and both NUCs.
3. Before building on the Pi, prune the old `:m3-pre-c` images: its root is 78% used, 5.9 GB free.

### 4.4 Docs to update after the merge

- CHANGELOG entries: dashboard split + 3 alerts, P2.1, paper venue, gateway, edge.
- `docs/CURRENT_HANDOFF.md`.
- `V2_MESSAGE_MEMORY_ATLAS.md` and other figures still saying 65,483 / schema 8.
- `docs/M3_DEPLOYMENT_LOG.md` for the deploy.
- Wire `VelocitaGatewayDown` to the heartbeat's staleness. With a textfile exporter, `up` alone goes stale at 1 after a crash.

### 4.5 Deploy (fresh cluster; the schema bump makes it mandatory)

Follow `docs/PAPER_TRADING_RUNBOOK.md` from the edge copy.
1. **Owner, by hand on the Mac:** mint caller keys 12 and 13 (`tools/velocita-secrets init caller-12`, `init caller-13`) and push them to all three voter hosts.
2. **Fresh cluster:** start it and run the ceremony.
3. **Setup:** `systemctl --user start velocita-v2-shadow-setup`.
4. **Risk limits:** send `risk-limits` with the runbook values.
5. **Gateway:** enable it.
6. **Edge:** set `VELOCITA_MODE_ARGS=--mode paper` and restart it.
7. **Ledger:** reset the shadow ledger.db to the $25 book.

## 5. Known gaps (recorded, not blocking paper)

From the paper-venue review:
1. **The kernel never checks a fill's fee or price.** It accepts a fee far over the hold, a negative fee, and a buy filled above its limit. So promotion-gate criterion 5 (fill costs inside the measured range) must be checked from gateway evidence until the kernel checks fills.
2. **Order size cap:** one venue receipt is reserved per lot, so orders over about 59 lots are refused with `venue_event_capacity_exceeded`. The clip is 25 lots.
3. **Order known but killed:** the kernel has no "known but killed" query answer. A nonzero venue id with 0 filled moves the order to `live` and keeps its hold. A real adapter must map a killed order to venue id 0.
4. **Fill adoption:** fills can only be adopted from identified venue reports, never from a totals-only snapshot.
5. **Re-sequenced fill:** the same execution id under a new sequence gets `fill_identity_conflict`, but the order is not marked `conflict`, so a conflicting report is dropped silently.

Carried over:
- **Pre-vote:** deferred; elections come from wifi drops plus a term-inflated rejoin (finding 43).
- **Watchdog and panic reboot** on nuc1 and the Pi: still to do.
- **CMake digest pins** for raft_log.c and raft_server_properties.c: still to add.
- **First live compaction pause:** yet to be observed. Then relax the 30 s edge throttle.

## 6. Live cluster at pause (21:10 EDT)

- **Nodes:** all three voters active; nuc1 also runs md-stream, exporter, Prometheus, Grafana and the shadow edge.
- **Incarnation:** 453345d0ac2193a09958e96cf367da07.
- **Shadow edge:**
  - healthy: 650 ticks, 0 errors, last decision about 0 s old;
  - it restarted itself at UTC midnight as designed; the journal only logs when a run ends;
  - BTC reference price 83,430.
- **Disk:** Pi root 78% used.

## 7. Volume strategies and the live path — in progress (2026-09-30 afternoon)

Paper trading is LIVE on the fresh cluster: incarnation `5ebc6359…`, trend strategy,
one 25-lot position held (M3_DEPLOYMENT_LOG finding 44).

**Owner decisions** (2026-09-30):
- Objective: TrueMarkets leaderboard VOLUME.
- Loss budget: $10–12 per week at most. Losing is acceptable if it buys a
  leaderboard placing.
- Rapid round trips are acceptable: "tm is chill". This overrides the 9/28
  "every trade needs a stated reason" rule.
- Plan: backtest every candidate on the same battery as the trend strategy,
  paper-test today, and consider live in a couple of days only if there is
  an edge.

**Three agents in git worktrees** (base commit `f1ef5ce`). Each keeps
`AGENT_PROGRESS.md` at its worktree root. Resume an agent by messaging it,
or read its checkpoint and continue by hand.

| Worktree (under `.claude/worktrees/`) | Work |
|---|---|
| `agent-a8868440527afb9cb` | Research. Candidates: A pure churn, B trend-gated churn, C hold-and-rotate, D dip scalp, E trend / buy-and-hold baselines. Battery: in-sample permutation, walk-forward, walk-forward permutation and random-walk tests at the live execution model (30 s ticks, one lost re-sync tick after each fill, FOK at the measured 110/123 bp, $25 capital). Output: `docs/evidence/volume_battery_2026-09-30/`. |
| `agent-a559cc81a7b1dd453` | Paper enablement. `--strategy {trend,churn,trend_churn,rotate,dip}` edge features, volume metrics (gateway and edge), the weekly loss-cap stop, the dashboard row, and a runbook section. |
| `agent-ab5ee37fcf8763fe2` | Live path. `docs/LIVE_TRADING_JOURNEY.md`, a new ADR for live execution and key custody, and a mock-only `TrueMarketsVenue` with a test signer and fault tests. No network and no keys. |

**Book guidance sent to the agents:**
- ch52: the honesty rule (label every result's evidence class); fees charged
  at each fill; capital limits; a research ledger with a held-back period;
  forward testing as an information purchase.
- ch57: an isolated signer that inspects each operation, rejects replays,
  keeps an audit log and never exposes the key.
- ch19: rate limits enforced before sending; deduplication.
- ch25: the definition of wash trading.

**Merge rule:** review each worktree's diff against `f1ef5ce`, then merge by
file into the main tree. Run the full ctest and the model tests before any
deploy.
