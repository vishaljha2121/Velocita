# Velocita Raft changelog

This changelog records release intent, validated behavior, measured failures,
and the evidence required to promote the next release. It is deliberately more
than a commit log: implementation claims and experimental conclusions are kept
separate, and a throughput number is not published until a repeatable benchmark
meets its acceptance gates.

The project currently uses milestone names (`V1`, `V1.1`, `V2`) and the corresponding
container tag `localhost/velocita-raft:v1`. `CMakeLists.txt` still declares the
project version as `0.1.0`; artifact and semantic-version unification remains
release-engineering work.

## [V2] - In development

### Outside heartbeat; pinned-receipt metrics — 2026-10-05 (CP-081)

- **Heartbeat.** nuc1 pings a monitor outside the house every minute, and reports a failure at
  once when Prometheus, Alertmanager or ntfy stops answering
  (`docs/ALERT_DELIVERY_RUNBOOK.md`). New alert: `VelocitaHeartbeatNotSent`.
- **Receipts.** Each voter now exports how many request receipts it holds and how many are
  pinned (cannot be reclaimed yet), with the peak since the process started. This is the input
  ADR 0022 needs to size the receipt table; the held count alone is no guide, because the table
  fills by design before it is swept. New alert: `VelocitaPinnedReceiptsHigh`. The health page
  format changes (version 5), so a voter and its exporter restart together.

### The gateway stops acknowledging its own acknowledgements — 2026-10-05 (CP-080)

- An idle gateway proposed one `AcknowledgeGatewayResults` a second, because each
  acknowledgement's own sequence looked unacknowledged: about 90 % of the Raft log (M3 finding
  54). It now acknowledges only while an evidence command is unacknowledged. Gateway only; no
  wire or storage change.

### Alert delivery; compaction no longer rereads the whole archive — 2026-10-03 (CP-079)

- **Alert delivery.** Prometheus → Alertmanager → self-hosted ntfy on nuc1 → phone
  (`docs/ALERT_DELIVERY_RUNBOOK.md`). Two rules watch the delivery path.
- **Compaction (M3 finding 53).** Each compaction reread and fsynced every archived segment,
  twice, so its pause grew with the cluster's age: 0.8 s → 4.8 s on nuc1 in two days, and 64
  elections. The compaction path now checks the archive's layout and rereads two segments per
  compaction in rotation. The full reread remains at startup and in history recovery (ADR 0014
  amendment). No wire or storage format change.
- **New alert:** `VelocitaElectionsRecurring`.

### Edge resilience, source re-sync, persisted paper venue, automatic re-arm — 2026-10-01 (CP-077, CP-078)

These fix M3_DEPLOYMENT_LOG finding 48 (paper data corrupted). They were built by parallel
agents in worktrees; the reviewer finished their last verification runs and merged them.
Merged main passes the full ctest, the model tests and the repo Python tests. Pre-vote
(CP-076) is listed separately. **Not yet deployed:** it needs the fresh-cluster reset.

**CP-077: edge and source re-sync**
- **Kernel:** a `source_generation_gap` refusal now carries `ApplyResultDetailKind::source_state`
  (8) = {source_id, cursor generation, cursor sequence}, with both cursor values 0 when there is
  no cursor. Both codecs admit it only for that code. The rule itself is unchanged: the
  generation must be cursor + 1.
- **Submitter:** on that refusal it persists cursor.generation + 1 and retries the tick once.
  Regressions and sequence gaps still fail closed.
- **Edge:**
  - Market-data errors (handshake 5xx, connection, timeout, protocol) reconnect with jittered
    exponential backoff, 1–60 s, keeping the same submitter. They never exit the process.
  - It checks the submitter is alive before every write.
  - Submitter ERROR replies are logged verbatim and counted
    (`velocita_shadow_submitter_errors_total{reason,code}`).
  - New metrics: `velocita_shadow_market_data_connected`,
    `velocita_shadow_market_data_errors_total{kind}`.
- **Proof:** a real-process test reproduces the incident (`ERROR … reason=source code=14`) and
  recovers from it.

**CP-078: gateway**
- **Persistence:** the paper venue state lives in `/state/paper.state`: magic, version, length,
  payload, SHA-256. It is written atomically before every answer, and a corrupt or foreign
  file is refused.
- **Reconciliation:** at every committed read, the venue's base is compared with the kernel's
  position at zero-lot tolerance. A mismatch latches new risk, and both values are exported.
- **Automatic re-arm** happens only after fence stops (term or leader change, state-read gap),
  after a quiet period (default 30 s) and a matching reconciliation.
  - Kill epoch, generation or mode changes, unknown outcomes, conflicts, refused evidence and
    mismatches latch until an operator restart.
  - Stops and re-arms are counted by cause.
- **First start:** a one-shot `--paper-state-init` (flat), or `--paper-adopt-position` when a
  position already exists.
- **Persisted counters:** last fill time, fees and volume.
- **Alerts:**
  - New: `VelocitaGatewayReconciliationMismatch` (critical) and `VelocitaGatewayAwaitingRearm`
    (warning, after 10 m).
  - `VelocitaGatewayNewRiskStopped` no longer pages for a stop that is only awaiting re-arm.
  - Rule tests pass under promtool v3.13.2 (26 rules).

### Raft pre-vote — 2026-10-01 (CP-076)

Fixes the election bursts of M3 log findings 43 and 47 (term 49 -> 191 in 6 h on
wifi). Patch 0001 adds optional pre-vote to the pinned willemt/raft (off by
default; V2 enables it): a timed-out voter asks for term + 1 without raising its
own, and peers that still hear a leader, or hold a more up-to-date log, refuse.
An isolated voter no longer deposes a healthy leader when it returns. Pinned
digests: patch 2212859a..., raft.h d349e211..., raft_server.c 447eec99...;
raft_private.h and raft_server_properties.c are now pinned too. **The V2 peer body
version moves to 3**: deploy as a fresh cluster (or stop all voters, upgrade, start
all). Fresh pristine clone: `~/.cache/velocita-deps/willemt-raft-cp076`. ADR 0006
amendment.

### Phase 2 paper trading: limits, gateway, paper venue, dashboards — 2026-09-30 (CP-075)

Built by parallel agents in per-owner repo copies, reviewed and merged by hand.
Merged tree: 122/122 ctest, model tests OK. **Not yet deployed.** It needs a fresh
cluster, because the kernel codec, the snapshot and the static identity move to schema 9.
Decisions and their reasoning: ADRs 0015–0018. Continuation plan: `docs/PHASE2_PAPER_HANDOFF.md`.

**Kernel (ADR 0015)**
- `ConfigureRiskLimits` is operator-only.
- New limit fields: `max_position_quantity` (0 denies), `allow_short` (0 means long-only) and
  `reason_code` (non-zero).
- Authorization refuses `position_limit_exceeded` (67):
  - buys: P + R + q > cap;
  - sells: P − R − q < floor.
- A `stale_risk_state` refusal carries the current versions (detail `risk_state` = 7) so that a
  strategy can re-sync.
- Sizes: the ConfigureRiskLimits body goes from 88 to 101 bytes, a risk account from 112 to 121,
  and the maximum snapshot to 65,519 (17 bytes under 64 KiB). TradingState is 81,456 of 81,920.
- Goldens were regenerated by `tools/v2_schema_goldens.py`, which reproduces the V8 values first.

**Mass-cancel fix (ADR 0007 amendment)**
- The `mass_cancel` result detail was admitted by neither codec. On the pre-CP-075 build, an
  operator mass cancel fail-stops every voter, and the cluster likely crash-loops on restart.
- Both codecs now admit it with derived constraints. A three-voter commit + restart test covers it.
- **Do not send mass-cancel to the pre-CP-075 cluster.**

**Gateway (ADR 0017)**
- `velocita-v2-gateway` is a separate process. It reads applied state as the canonical snapshot
  in 24 KiB chunks, authenticated, and served only to `gateway.caller` by the leader.
- It asks the leader for an authority barrier and receives a MAC-verified, fenced, correlated
  proof. Only that path can construct an `AuthorityProof` outside a voter.
- It sends once, returns venue evidence as CallerId 22, and stops new risk on any unknown outcome
  or fence. It does not re-arm itself.
- New protocol kinds:
  - requests 0x10 (state read) and 0x11 (authority);
  - responses 4 (gateway_refused), 5 (state_chunk) and 6 (authority_granted).
- Metrics go to a textfile, with a heartbeat and counters starting at 0.

**Paper venue (ADR 0018)**
- `PaperVenue` fills fill-or-kill at the limit price. Fees are ceil(notional × bps), 115/120 bp.
- The quote balance is $25 and the base balance 0, which makes the venue a long-only backstop.
- The ch46 faults are tested against the real reducer.
- Five kernel findings are recorded in the ADR. The most important: the kernel never checks a
  fill's fee or price.

**Strategy / operator split (ADR 0016)**
- The tick loop runs as CallerId 21 and refuses operator ids.
- Setup is an operator one-shot: `--setup-only`, run by `velocita-v2-shadow-setup.service`.
- `--mode paper` evaluates authoritatively and sends AuthorizeDecision, level-triggered, re-syncing
  from the refusal.
- New client verbs: `strategy-mode` and `risk-limits` (prints a summary; `--dry-run`).
- The ledger defaults to a $25 book.
- Templates gain callers 12 and 13 and `gateway.caller = 22`.
- New gateway unit, which is armed only by `VELOCITA_GATEWAY_RISK_ARGS`.

**Dashboards**
- The single dashboard is split into Overview (old uid), Raft & storage, Strategy (shadow),
  Paper trading and Hosts, with all 66 old panels kept.
- New alerts: VelocitaGatewayDown, VelocitaGatewayNewRiskStopped and VelocitaGatewayUnknownOutcome.

**Pre-existing test failure found**
- `test_a_voter_behind_the_leaders_snapshot_does_not_stop_the_cluster` fails for 6 of 30 `rand()`
  seeds, on the original code too.
- willemt's election timeouts come from the process-wide unseeded `rand()`.
- Still open.

### Shadow book, promotion gate, compaction alerts — 2026-09-29 (CP-074)

- `model/.../shadow/ledger.py`: a $1,000 virtual book filled at the True
  Markets reference price with the MEASURED quote costs (`--costs-evidence`,
  cbBTC buy +109.9 / sell -123.4 bp; the image carries the evidence file; no
  guessed cost). Two books: `tick` (every committed decision) and `daily`
  (the last decision before each UTC midnight, like the backtest; the one
  that counts). Liquidation-value equity, realized/unrealized P&L, fees,
  round trips (break-even is not a win), buy-and-hold benchmark from the
  first price, SQLite persistence (`/state/ledger.db`), bounded order history
  as metrics. The edge feeds it only decisions the cluster recorded AND
  resolved as proposed (`PROPOSED` parity-tested with action_resolver.hpp).
  11 ledger tests with hand-worked dollar oracles + an end-to-end edge test;
  11/11 mutants killed after two gaps got tests.
- `docs/STRATEGY_PROMOTION_GATE.md`: pre-registered shadow -> paper -> canary
  criteria; shadow P&L reported, not gated (the strategy trades ~9.5x/year).
- Alerts `VelocitaVoterStranded`, `VelocitaCompactionPauseLong`,
  `VelocitaCompactionBlocked`; log-filling alerts reworded for compaction.
  promtool 21 rules; 3/3 rule mutants killed. Dashboard rows "Raft log &
  compaction" and "Shadow book" (fills table).
- Test fix: two replication tests looked up the new leader with
  `cluster.leader()`, which can return the partitioned old leader (it still
  believes it leads); failed once on nuc2's build. Both now search the other
  voters.

### Compaction step C4: automatic compaction in the node — 2026-09-29 (CP-073)

- `history.compact_at_entries` (default 2,048; 16..8,000). The node attempts
  a floor-gated compaction at most once a second, only past the threshold;
  compaction time is measured in owner-loop ticks (Raft pauses meanwhile).
- FOUND (M3 log finding 41) and FIXED in the pinned patch: a candidate whose
  log is compacted to its end advertised last-log term 0 and could never be
  elected; `raft_get_last_log_term` now falls back to the snapshot term.
- Visibility: `ReplicaStatus::snapshot_index`; `NodeCounters::compactions`,
  `last_compaction_ms`; health page version 4 (30 cells); metrics
  `velocita_v2_compaction_floor`, `_snapshot_index`, `_stranded_voters`,
  `_compactions_total`, `_last_compaction_seconds`; status tool.
- Tests: node level - nothing compacts below the threshold; every voter
  compacts past it; a stopped voter pins every cut (without the gate a voter
  reclaimed to 80 while the stopped one held 40); it returns unstranded and
  compaction resumes; failover after compaction. Kernel level - voters
  compacted to their end still elect. Process level - the shadow scenario
  compacts on all three voters, then survives a leader SIGKILL and a
  submitter restart; retained + snapshot index == current index. 4/4 trigger
  mutants killed; page round trip and metric text cover the new fields.
  Release 119/119.

### Compaction step C3: compaction gated by the floor — 2026-09-29 (CP-072)

- `ReplicatedKernel::compact_within_floor()`: starts a compaction only while
  the applied index is at or below `compaction_floor()`, so no voter is ever
  left needing an entry another reclaimed; continues one already in flight.
  The ungated `compact()` primitive stays for fixtures that must simulate a
  lost-storage voter (the stranded-voter test). The node will use only the
  gated call.
- `compaction_floor()` is one function for every role: min(own durable
  index, each peer's match as this node knows it), never below what leaders
  have told it. Stale match counts from a past leadership are still true.
- Tests: a partitioned voter blocks compaction on the leader AND the healthy
  follower; after healing all three compact; a voter that then falls behind
  catches up by ordinary appends (0 stranded). New: the floor waits for the
  leader's own disk even when two followers commit without it (a leader that
  crashed would return without those entries). 5/5 gate/floor mutants killed
  after one real gap (floor ignoring own durability) got its test; two
  equivalent mutants removed by unifying the floor function.
- Known operating cost for C4: while a compaction is in flight the kernel
  pauses Raft progress (tick returns early), including a leader's heartbeats.

### Compaction step C2: the compaction floor on the wire — 2026-09-29 (CP-071)

- V2 peer bodies are version 2: the V1 layout plus, for AppendEntries, an
  8-byte trailer carrying the leader's compaction floor (the lowest index
  every voter holds durably). The V1 codec is untouched; a V1 body is
  refused, as is a missing trailer or a negative floor.
  `encode_bound_peer_body` builds V2 bodies (fixtures use it too).
- The leader computes the floor as min(own durable, every voter's match) and
  stamps it on every append; a follower keeps the highest floor it has
  learned. The floor only rises: entries every voter holds can never be
  truncated, so a new leader (whose match counts start at zero) keeps it.
  `ReplicaStatus::compaction_floor`.
- Test harness: the per-node "commit <= durable" check was a proxy that is
  wrong for followers (they may hold a commit learned from the leader before
  their own copy is durable; see the zero-durable-follower test) and, with
  three voters, for the leader. Replaced by the real guarantee: whatever any
  voter believes committed is durable on a majority. Measured: neither form
  can see a staged-vs-durable window in the harness (each round flushes
  before routing); the clamp and the leader gate are killed by their
  dedicated tests (follower-ack, commit-before-durable), verified by mutants.
- Tests: codec floor round trip (owning and allocation-free encoders), V1 /
  trailer-less / negative refusals; the floor reaches every voter, is pinned
  by a silent voter, survives a leader change, and never drops on a
  follower. 9/9 C2 mutants killed. Release suites green.

### Compaction step C1: a stranded voter no longer disrupts the cluster — 2026-09-29 (CP-070)

- FOUND (M3 log finding 40) while preparing compaction: a voter behind the
  leader's snapshot received no heartbeats (RAFT_ERR_NEEDS_SNAPSHOT) and
  starved every peer after it in `raft_send_appendentries_all`; it then
  campaigned and deposed a healthy leader repeatedly.
- FIXED in `deploy/patches/0001-raft-durable-quorum.patch` (regenerated from
  pristine e428eeb; pinned raft_server.c digest now 2d4c9c13...): the send-all
  loop serves every peer, and a stranded peer gets an empty append at the
  snapshot point from the periodic heartbeat only (so a refusal cannot cause a
  resend storm). `ReplicaStatus::stranded_voters` (leader view, willemt's own
  needs-snapshot test).
- Test: a three-voter cluster with the lowest-numbered follower partitioned,
  5 commits, forced leader compaction, heal, 4 simulated seconds: every voter
  stays in its term, leadership does not move, the leader reports 1 stranded
  voter, and the other follower keeps committing. RED before (voter 2 a
  candidate in term 4); 2/2 patch mutants killed. Release 119/119.

### Raft log capacity made visible; edge tick budget — 2026-09-28 (CP-069)

- FOUND (M3 log finding 39): the shadow edge stopped after ~2 h 10 min; the
  retained Raft log (8,128 ordinary + 64 operator-control entries) was full
  and nothing compacts it yet, so every normal command was refused as
  retry-later. The dashboard showed only "submitter not ready".
- Retained-log use is now reported per pool: `ReplicaStatus` ->
  health page (version 3, 25 cells) -> `velocita_v2_retained_log_entries` /
  `_capacity{pool=ordinary|protected}` -> status tool. Alerts
  `VelocitaRaftLogFilling` (>75%), `VelocitaRaftLogFull`,
  `VelocitaKillReserveLow` (>50% of 64); dashboard row "Raft log capacity"
  with time-until-full. Tests: page round trip with distinct values, metric
  text, replication status by pool (ordinary vs operator kill-path), and two
  process-level checks (kill-epoch scenario fills only the reserve; the
  shadow scenario's 23 ticks fill >= 46 ordinary). 7/7 code mutants and 3/3
  rule mutants killed; promtool 18 rules.
- Shadow edge `--min-tick-interval-s` (unit: 30 s): at most one forwarded
  price per interval; a throttled price is not marked seen, so the freshest
  one still goes out when the interval ends. 4/4 mutants killed. At 30 s the
  log lasts ~34 h per cluster until compaction (next).
- Open flake: v2-node-process-tests failed 3 times with a node startup
  "(-11) generic error", only on the first run after relinking the node
  binary during mutation loops; 9 plain back-to-back runs passed. The node's
  full log was truncated by the mutation script; capture it next time.

### Early durable follower acks: tick-to-decision 374 ms -> 22 ms — 2026-09-28 (CP-068)

- FOUND (CP-063, finding 31): a follower's reply may only claim durable
  entries (the clamp), and it then stayed silent until the leader's next
  200 ms heartbeat, so every commit waited for news the follower already had.
  Twice per tick (source event, then decision frame).
- FIXED: `FollowerAckTracker` (`include/velocita/v2/follower_ack.hpp`). After
  each durability flush a follower sends `min(verified, durable)` to its
  leader, where `verified` is the highest index the CURRENT leader checked in
  the CURRENT term. It never covers entries from an older term, never speaks
  for a term already left, never while not a follower, never repeats, and
  stays silent after a failed append (its reply may still sit in the
  one-per-kind outbound slot). The wire message and the leader's handling are
  unchanged (a normal AppendEntriesResponse), so mixed versions interoperate.
- Tests: 10 tracker cases; replication: durable follower acks unasked,
  never covers unverified durable entries (term-2 leader checked index 1 of 3),
  never speaks for a term already left, a commit does not wait for a
  heartbeat (3 voters, time frozen; RED before), and a writer thread parked
  before fsync (new `WalSyncFence`) gets no ack for staged bytes. The
  suffix-replacement test now checks its property directly: any ack claiming
  the replacement must find the replacement on disk. 15/15 mutants killed;
  one equivalent mutant (durable-term check, implied by the tracker) removed.
  Release 119/119 x3.
- Measured, same live 2-voter Mac rehearsal as before the fix: mean
  tick-to-decision 374 ms -> 21.7 ms (source commit 140 -> 11.2 ms, decision
  commit ~215 -> 10.4 ms; 27/27 ticks under 50 ms; 27/27 agreement).

### Three voters live; host crash recovery — 2026-09-28 (CP-067)

- nuc2 joined: fresh three-voter cluster 8d9d7c95... on nuc1 + Pi + nuc2
  (M3 log finding 37). First election at term 1; shadow edge on all three
  voters (`VELOCITA_NODES` in shadow.env; systemd splitting verified on
  systemd 249); 3/3 targets up, no alerts.
- nuc2 outage diagnosed (finding 35): a hard freeze, not wifi. Host recovery
  files in `deploy/v2/host/`: `velocita-watchdog@.service` (Ubuntu
  blacklists watchdog drivers; load by name at boot), `velocita-watchdog.conf`
  (RuntimeWatchdogSec=30s), `90-velocita-panic.conf`. nuc2: panic reboot
  proven by a sysrq crash; watchdog arms at boot, proven by a reboot
  (finding 36). nuc1 and the Pi still to do.

### Secrets-missing alert; Prometheus rule-directory fix — 2026-09-28 (CP-066)

- FOUND by the nuc1 power loss (M3 log finding 32): the promtool test file
  lived in `rules/`, which Prometheus globs as rule files, so Prometheus
  crash-looped after the reboot and Grafana showed no data. Moved to
  `rule-tests/`; `tests/test_observability_layout.py` (ctest
  `v2-observability-layout-tests`) fails if any loaded file is not a
  rule-group file (RED on the old layout).
- `deploy/v2/secrets-presence.sh` + `velocita-v2-secrets-check.{service,timer}`
  on every voter host: reads the node config for the key files it needs and
  publishes whether each is present in the RAM-only directory (existence and
  non-empty only; an unreadable key still counts as present, proving it never
  opens a secret). Alerts `VelocitaSecretsMissing` (names the host, the secret
  and the exact push + restart commands) and `VelocitaSecretsCheckBroken`.
  5 script tests, 6 mutants killed; promtool: 15 rules, alert tests pass,
  2 rule mutants killed. Dashboard row "Secrets".

### Shadow journey funnel and model view on the dashboard — 2026-09-28 (CP-065)

- Edge metrics: every True Markets message gets an outcome (`tick`, `resend`,
  `other`, `no_history`); the kernel's proposed and effective directives are
  counted by name (`Directive` parity-tested against contracts.hpp; unknown
  codes shown as `code_N`); the model's last features and BTC price are
  exported. 5 new tests (oracles: the C++ enum, `rules.features`); 7 mutants
  killed. All 15 new PromQL queries validated on nuc1's Prometheus.
- Dashboard rows "Journey" (funnel: TM messages -> new prices -> decisions
  recorded -> trades proposed -> trades authorized, with executor/TM marked
  not built) and "Model: BTC trend, SMA 125" (price vs SMA, signal, features).

### Phase 1 shadow loop, step 5 (code): True Markets shadow catalog row; deploy packaging — 2026-09-28 (CP-064)

- `velocita-v2-shadow-catalog --truemarkets-cbbtc`: a SHADOW-ONLY catalog row
  for cbBTC/USDC on Base with the venue's published units (GET
  /v1/defi/core/assets: cbBTC 8 decimals, USDC 6): $1 ticks, 0.00001 BTC lots,
  10 USDC micros per tick-lot; place_limit + FOK only, no post-only (RFQ). The
  fixture row (prices 1..100 ticks) could not price real BTC. 4 tests through
  the real reducer with dollar oracles ($83,000 x 0.0001 BTC = $8.30; cap binds
  at $8.29, not $8.30; GTC refused); 9 mutants killed. Not a venue contract.
- Node templates (two- and three-host): caller 11 = CallerId 9 for the shadow
  submitter on nuc1 (own key, fence, response port 42411). The template test
  now covers both topologies and pins the full caller table; 5 mutants killed.
- `Containerfile --target shadow`: the Python edge (stdlib only) + submitter
  from the same tested build. `velocita-v2-shadow-edge.service` (nuc1).
  Dashboard row "Shadow edge" (readiness, last decision, agreement, errors,
  tick-to-decision p50/p99, time per hop, resolutions, rules, data age).
- FOUND and FIXED: True Markets re-sends the last reference price every second
  but refreshes it only every few seconds (one stretch: ~12 s); the edge
  forwarded every resend as a new tick, so ~1/3 exceeded the 5 s age cap
  (`market_data_stale`). The edge now forwards only a newer observation
  (resends and late stamps dropped); test + 3 mutants killed. Live two-voter
  Mac rehearsal with the TM row: 50 ticks -> 50 decisions, all `proposed`
  (a buy of 0.0001 BTC at the live price, under the $25 cap), 50/50 agreement, 0 errors, mean data age 0.30 s
  (was 4.7 s), mean tick-to-decision 374 ms (follower durable-ack, CP-063).
- FIXED test flake (Pi image build): the writer-thread commit test waited
  4000 loop rounds (~3 ms of real time), less than one fsync on the Pi's SD
  card while its live voter was also syncing. It now waits on a 10 s
  deadline like the other writer tests; a no-durability mutant still fails.
- Release 116/116; model 199 OK; safety audit 0 findings.
  Source fingerprint ade92b7098c343c5edf4a1fe988e6526b5ea6bd82413d9d797cb1d49db993712.

### Phase 1 shadow loop, step 4: Python shadow edge; election-jitter fix — 2026-09-28 (CP-063)

- `model` `shadow.edge` + `cli shadow-edge`: True Markets `reference_prices`
  (BTC|USD) -> `rules.features` on Binance daily closes + the live price as
  today's provisional close (stale history refused) -> TICK lines to the C++
  submitter; evidence = SHA-256 of the raw message; program file generated from
  the same Python rule program; metrics for tick-to-decision latency (per hop),
  decision and resolution codes, market-data age, and Python-vs-cluster rule
  AGREEMENT. 15 tests; 8 mutants killed; a cross-language parity test pins
  `SHADOW_RECORDED` to kernel.hpp.
- Live Mac smoke (3 loopback voters, operator ceremony, fixture catalog with
  meaningless units): 71 live ticks -> 71 replicated decisions, 71/71
  agreement, 0 errors; ticks whose reference price was > 5 s old were refused
  by the template's market-data-age cap (`market_data_stale`), as designed.
- FOUND: tick-to-decision ~460 ms on loopback. Cause (code comment in the
  follower's durability clamp): a follower's durable ack waits for the leader's
  next heartbeat (200 ms). Fix identified (ack when the writer catches up),
  not yet made: it touches the replication safety path.
- FIXED: willemt/raft randomizes election timeouts with an UNSEEDED rand(), so
  every voter process drew the same timeouts; two survivors of a leader kill
  split votes for 10 terms (process test flake). `seed_election_jitter()` seeds
  from OS entropy in the node binary; fork-based test pins defect and fix.
  Suspected cause of the 12-14-term election bursts on NUC+Pi (to verify).
- FIXED test flake: the partition test submitted request 2 once; under load it
  was never admitted. It now retries like a client. Release 115/115 x3.

### Phase 1 shadow loop, steps 2-3: shadow builder and submitter — 2026-09-28 (CP-062)

- `shadow_program` (`velocita_v2_node`): rule program -> Publish, Validate,
  Stage, ActivateExecutable, SetStrategyMode; per tick RecordSourceEvent +
  EvaluateDecisionFrame(SHADOW). Units only from the execution-catalog row.
  10 reducer tests incl. the real operator ceremony and a 5000-tick stream;
  11 mutants killed.
- `shadow_submitter` library: strict program-file parser (catalog-root
  verified) and a durable state file (incarnation, generation, reserved request
  sequences; torn file refused). 5 tests, 9 mutants killed.
- `velocita-v2-shadow-submitter`: line protocol TICK in, DECISION out with
  per-hop wall-clock times; exact-command retry on timeout; decision id =
  generation<<32 | source sequence. Process test over real Aeron with three
  voters: ceremony via the operator client, 20 shadow decisions on hand-worked
  rules across a leader SIGKILL, restart on generation 2 past the reservation.
  Binary mutants (decision id without generation, policy on the control lane)
  killed end to end. Release 114/114.
- Found by the tests: permanent propose refusals (`refused`,
  `not_a_control_command`) were retried until timeout; now final. Catalog
  installation is an operator ceremony (kill epoch and executor generation
  nonzero, cluster halted) and goes on the normal lane; the client gained
  `executor-generation` and `install-catalog` verbs. The submitter needs its
  own caller route: a route is one session, and sharing one with the operator
  client meets the replay fence.

### Phase 1 shadow loop, step 1: client session library — 2026-09-28 (CP-061)

- `NodeClientSession` (`velocita_v2_node`): the authenticated caller side of the
  node protocol as a non-blocking, clock-injected state machine. Leader kept
  across requests, pre-admission resends rotate across voters, post-admission
  resends paced (no duplicate log entries), per-voter response fences,
  correlation-checked settlement, per-hop timestamps.
- RED-first against real in-process voters (node-test harness moved to
  `tests/v2_node_test_support.hpp`): 9 tests; 10 mutants killed by their
  intended assertions (an authentication-bypass mutant is unreachable because
  the envelope API yields no body before the MAC verifies).
- `AeronClientTransport`; node and client transports share one bounded inbox.
  `velocita-v2-client` now runs on the session; the process suite (redirect,
  outage, partition, restart, rotation, three voters) passes. Release 112/112.
- Research: rule backtester with cluster-identical first-match semantics,
  measured True Markets costs (SOL/cbBTC buy and sell), three demo programs
  through the battery; only BTC trend (SMA 125) survives in-sample nulls
  (`evidence/demo_strategies_2026-09-28/`).
- Build hygiene: dependencies and long-lived builds moved out of /private/tmp
  (macOS cleaned the pinned raft source).

### M4 market data and strategy research — 2026-09-28 (CP-060)

- Read-only stdlib RFC 6455 client and `cli stream` recorder (raw evidence,
  bounded candle storage, silent-topic metrics); live on nuc1. True Markets
  order-book topics are accepted but deliver nothing (D-7).
- Research lab: checksum-verified Binance archives, True Markets history (one-day
  stamp offset found and corrected), Minara momentum rotation, five-test
  permutation battery, point-in-time survivorship-free universe, band
  rebalancer. All RED-first with mutation checks; model 169 tests.
- Minara REJECTED: significant only on its hand-picked universe (p 0.025);
  on a point-in-time universe p = 0.93 / 0.56 / 0.97 and it trails BTC.
- Volume: a band rebalancer yields 3-24x stake per year with no edge
  (p 0.28-0.71); leaderboard volume must be bought at the markup, not yet measured.

### Three-voter topology: one dead voter no longer stalls the quorum — 2026-09-27 (CP-059)

- Owner decisions: three voters (nuc1, Pi, nuc2); M4 may make GET-only,
  credential-free calls to True Markets, with order sends mocked.
- RED first: with three voters, one unreachable follower stopped the leader
  sending to the live follower (`send_peers` held back every peer), so two of
  three voters committed nothing. Reproduced over real Aeron with SIGKILL.
- Fix: a refused offer holds back only that peer; its frames wait, coalescing,
  in the bounded mailbox. Five mutants killed by their intended assertions.
- Three-host templates with a topology test, Prometheus targets and quorum
  alerts (promtool-tested) for nuc2, soak bots use all three voters and require
  the cluster incarnation. Release 111/111; node suites clean under ASan+UBSan.

### Secrets from the Mac and zero-outage rotation — 2026-09-24 (CP-058)

- Owner decisions: defer asymmetric caller keys, WireGuard, key tooling beyond
  this and persistent receiver watermarks to the next iteration; do rotation and
  Mac-sourced secrets now.
- Node accepts current/.next/.previous secrets and sends with current;
  SIGHUP reloads all-or-nothing without touching sessions. Client `--key-stdin`.
  `tools/velocita-secrets` keeps secrets in the macOS Keychain and pushes them
  over SSH into private tmpfs only (never argv, never host disk).
- Live two-process rotation with no failed commit and no auth refusals; torn
  push refused safely. 10 mutants killed after strengthening four tests.
  Release 108/108.

### M2 steps 5–6: startable node and two-process exit — 2026-09-24 (CP-057)

- `velocita-v2-node`, `velocita-v2-client`, `velocita-v2-status` over Aeron UDP,
  with strict secret/catalog loaders, a seqlock health page and a CMake venue
  interlock. Config gains required `caller.<id>.response_endpoint`.
- Two real node processes start, redirect, fail closed when a voter dies or the
  link partitions, recover from SIGKILL and full restart, and stop cleanly on
  SIGTERM. Release 107/107.

### M2 step 4: V2Node owner loop — 2026-09-24 (CP-056)

- `V2Node` drives one kernel from a single owner thread with the WAL worker on
  by default: authenticated/fenced peer and caller ingress, CallerId binding,
  not-leader redirect, committed results to configured callers, fail-stop.
- Tests found and fixed a liveness defect: dropping peer frames while the
  kernel was busy (hard state in flight) prevented elections. Busy frames are
  now held and retried, proven by a parked-worker scenario and a frame
  conservation law. Backpressure redelivers the exact stashed bytes.
- RED-first; 5 mutants killed after strengthening two tests.
  Release/ASan+UBSan 104/104.

### M2 ingress/response codec — 2026-09-24 (CP-055)

- Exact decision-D bodies (admitted 57 / refused 46 / committed 185 bytes; lane +
  command ingress), each response echoing request sequence and payload digest.
  RED-first, Python goldens, 5 mutants killed. Release/ASan+UBSan 103/103.

### M2 step 3: node configuration — 2026-09-24 (CP-054)

- Strict config parser per approved decision E, plus required identity keys
  (`cluster.incarnation`, `catalog.root`, `catalog.file`). RED-first; test found
  a misleading-error ordering defect; 10 mutants killed after strengthening one
  accidentally covered test. Release/ASan+UBSan 102/102.

### M2 steps 1–2: authenticated envelope and session fence — 2026-09-24 (CP-053)

- Owner approved M2 decisions C, D, E. New `velocita_v2_node` library.
- Authenticated envelope (VLA1, HMAC-SHA256, per-route derived keys): RED-first,
  RFC 4231 + Python goldens, every bit flip/truncation refused, 6 mutants killed.
- Session fence + 16-byte session-epoch file: RED-first, Python goldens, crash
  model over every persistence cut proves no epoch is reissued; 8 mutants killed
  including all three fsync-ordering mutants.
- Release 101/101; ASan+UBSan 101/101 (run alone). No kernel source change.

### E sign-off plan and M2 design review — 2026-09-24 (CP-052, approved)

- Added `docs/V2_E_EXIT_SIGNOFF_PLAN.md`: the frozen-candidate rule and every
  NUC/Pi process, fault-injection and power-cut run needed to sign off E1/E2/E3.
- Added `docs/M2_NODE_PROCESS_REVIEW.md`: startable-node design reusing Aeron,
  the owner-loop scheduler and the metrics page; authenticated envelope,
  ingress/results and config grammar await owner approval. Nothing encoded.

### Two-voter E1 scope — 2026-09-24 (CP-051)

- Owner decision: MVP stays one voter per host (NUC + Pi). The online snapshot
  installer moves to the three-voter release; its approvals stand.
- Added `v2-two-voter-catchup-tests`: a partitioned peer and a peer that loses
  power after an unsynced acknowledgement both catch up through the log; the
  leader never commits or compacts past the peer's durable copy. It pins
  existing behavior; a mutant removing the follower durable-ack clamp fails the
  intended assertion. Asserts the approved no-automatic-leader-no-op cost.
- Three-voter lagger RED stays open for V2.1. No production source, byte,
  venue, device, staging or commit change.

### Claude continuation handoff — 2026-09-24 (CP-050)

- Added current takeover section with exact remaining E1/E3 integration, approved
  contracts, ownership/result/selector tripwires and reproducible evidence limits.
  Reason: HEAD predates the extensive uncommitted remediation and helper GREEN
  must not be mistaken for completed online installation.
- Updated ADR0014 status and added a superseding ADR0006 implementation amendment:
  worker-backed hard state and native backup verification already exist; actual
  installation/authenticated deployment/target acceptance still do not. Historical
  decisions/evidence retained. No new byte/economic choice or code change.
- Read-only verification plus documentation diff check only; no fresh runtime
  gates, staging, commit, tag, venue or device actions. Protected paths untouched.

### Serial writer generation handoff — 2026-09-24 (CP-049)

- Added a bounded native target-switch request/completion to the existing serial
  writer, so an eventual installer can drain old writes and change targets without
  owner-thread join or I/O. Pauses new blocks/maintenance, retains old completion
  accounting, and echoes exact generation/sequence. No wire/durable bytes changed.
- Fixed a draft empty-queue/publication race found in main review: recheck queued
  old writes after acquiring the switch request, before rebinding borrowed targets.
  Require advancing generations and successful switch sequences. Storage owners
  must retain both targets and apply dequeued completions before retirement.
- Added dedicated normal/adversarial tests and independent gate inventory entries.
  Initial implementation preceded tests; subsequent draft/mutation RED is not
  represented as chronological test-first evidence. No safety rule was removed.
- This enables E1/E3; it does not install a snapshot, switch the active kernel,
  reconcile the live ledger or close an E exit. Host Release97/97 and C++
  ASan+UBSan97/97 pass; final test-strengthening reruns and exact instrumentation
  limits in `docs/evidence/f5_writer_handoff_2026-09-24/verification.txt`.
  Two deterministic queue-guard mutants fail. An earlier scheduling-sensitive
  mutation claim was withdrawn, then replaced with an explicitly parked witness.

### Fresh destination verification and native E2 integration — 2026-09-24 (CP-048)

- Recorded owner approval: reverify the independent destination at reclamation
  and relevant recovery; defer persistent receipts. Reason: preserve required
  backup safety without another durable authority/format for MVP.
- Added private verifier and move-only exact-request evidence; reuse existing
  immutable archive/catalog/dependency bytes and file/directory durability checks.
  No public raw authentication bypass; real transport/physical identity remains
  unimplemented and must be verified separately.
- Changed native two-host compaction from unconditional refusal to waiting for
  exact independent evidence when a destination is pinned. No-pin/local-only
  refusal remains. Reclaimed-history startup stays gated until fresh evidence.
  Owner request/completion operations do no disk/network work.
- Fixed reproduced draft restart replay: counter reuse alone is insufficient;
  add a fresh cold entropy nonce and derived per-attempt challenge. Entropy error
  refuses, never silently falls back. No order-path or replicated randomness.
- Added two permanent suites and independent gate entries: actual WAL reclaim,
  full backup payload reconstruction/restart, missing/corrupt backup gating,
  stale/duplicate/pin/dependency checks, file/directory sync EIO and entropy EIO.
  Three direct mutants fail intended assertions. Removed no protected work,
  original missing-backup refusal or safety requirement. Corrected corruption
  fixtures that initially targeted headers while claiming payload coverage.
- Final rebuilt Release95/95 and C++ ASan+UBSan95/95 (Raft C uninstrumented,
  leaks off); inventory95host/92V2. Native replica124904 (+464), request184/evidence192 bytes,
  not total RSS. Verification and limitations in
  `docs/evidence/f5_destination_verification_2026-09-24/`. Original E1 lagger still
  RED; authenticated service/target gates/E1–E3 acceptance remain open.

### Existing E1–E3 scope and parallel integration review — 2026-09-24 (CP-047)

- Recorded owner direction: finish existing E1/E2/E3 then startable node; defer
  nonessential additions. Two-agent limit and main integration review retained.
- Read-only parallel reviews confirmed shared E1/E3 generation/worker ownership
  and E2's existing independent-destination contract gap. No code or tests changed.
- Proposed fresh authenticated backup verification for MVP instead of new durable
  receipt bytes, explicitly awaiting approval because restart/reclamation would
  depend on destination availability. Existing laboratory-only reclamation guards
  remain intact. This is not new scope, implemented backup or target acceptance.
- Updated handoff/audit/roadmap/deferrals; CP-046 remains latest runtime evidence.

### E1 selected-generation history recovery — 2026-09-24 (CP-046)

- Recorded explicit approval of the history bridge,1GiB lab candidate-WAL cap
  and separately accounted nonserving recovery. No new wire/durable bytes.
- Added serial-worker publication of snapshot-covered WAL history and acquired,
  identity-checked completion; gate consensus/admission until complete. Reason:
  historical distance must not consume the live8192-entry arena or bypass quotas.
- Added whole-WAL cap and whole-plan archive object/byte preflight, retaining
  live-suffix reservation; validate all WAL but charge only missing `(A,S]`.
  Fixed draft overlap accounting exposed by partial-progress restart witness.
- Replaced the former8193-history refusal expectation with quarantine/recovery;
  retained missing-history refusal. Added permanent full-payload, pause, failure,
  quota, restart and completion tests plus independent gate inventory registration.
  Updated cold-result fixture to pump recovery/async commit before its unchanged
  exact-result assertions. No protected files or existing evidence removed.
- Corrected linear total-I/O claim: each segment publication re-verifies growing
  archive coverage. Native job8920/replica124440 bytes are not process or target
  memory budgets. Existing WAL bytes are never reclaimed by this path.
- Three intended guard mutants fail. Release93/93 and C++ ASan+UBSan93/93;
  Raft C uninstrumented/leaks off. Inventory93host/90V2; final verification scope
  in `docs/evidence/f5_history_recovery_2026-09-24/`. Online lagger catch-up remains
  RED; no candidate selection, live ledger transfer or phase closure is claimed.

### E1 history/selection contract review — 2026-09-24 (CP-045)

- Rebuilt the actual lagger reproducer: still RED at applied0 after compaction12.
  No production installer or acceptance is claimed.
- Documented the archive-catalog/selector publication mismatch and proposed
  existing-WAL history bridge with bounded disk/accounted recovery work. Reason:
  preserve old recovery before selection and new recovery after selection without
  inventing a second transaction journal. Owner approval remains pending.
- Added three cold-layout witnesses and independent test inventory entry;
  missing-history and8192-backlog guard mutations fail intended assertions.
  Corrected expected WAL to independently authored tuples. No production code,
  durable encodings, archive refusals or retention guarantees removed/changed.
- Updated handoff, checkpoint audit and ADR with explicitly proposed status.
  Verification: `docs/evidence/f5_install_bridge_review_2026-09-24/`.

### Composed async persistence crash proof — 2026-09-24 (CP-044)

- Added one registered metadata-plus-WAL durable-image suite and independent
  gate inventory entry: reason, verify cross-file ordering through actual worker
  operations rather than infer it from isolated persistence components.
- Added75 reached cuts, seed recovery and real vote/ACK/apply/result baselines;
  exact pre-recovery tuple/payload checks and two intended failing mutations.
- Reused the compaction model with a test-only main guard; no production code,
  pinned fork, durable bytes or economic contract changed. Removed permissive
  test-oracle assumptions, not production behavior; corrections are documented.
- Updated handoff/audit/F6 review/ADR evidence boundary. Release91/91 and final
  sanitizer verification: `docs/evidence/f6_async_crash_model_2026-09-24/`.
  Actual E1 installation and target acceptance remain open.

### Nonblocking hard state and whole-suffix persistence — 2026-09-23 (CP-043)

- Implemented approved desired/durable metadata fence on the serial WAL worker;
  reason: keep owner responsive without releasing undurable votes/results/proofs.
- Added optional Raft whole-cut preparation and owned32-KiB peer continuation;
  removed runtime per-entry stop/join/rewrite. One durable cut precedes ownership
  release; no duplicated conflict algorithm. Runtime clear is cold-only.
- Added exact typed completion validation and unconsumed-grant failure; corrected
  ignored candidate self-vote errors. No economic/canonical encoding changes.
  Regenerated pinned patch and extended source verification to raft_log.c.
- Added four suites and independent inventory entries; extended real authority
  composition and adjusted fixtures to explicit async progress/whole-cut failure.
  Added actual syscall/pause/EIO,16 suffix cases+2 failures,9 completion cases,
  and five direct-source mutations. Recorded main/agent harness corrections.
- Updated handoff, audit, ADR and native memory atlas. Evidence and verification:
  `docs/evidence/f6_async_persistence_2026-09-23/README.md`. Host-only implementation;
  composed async crash/E1 activation, independent copy and target gates remain.

### Async persistence continuation review — 2026-09-23 (CP-042, proposed)

- Added concrete five-choice review and source-call matrix to the F6 review;
  reason: moving callbacks to a worker alone can release undurable promises.
- Recommended library-selected whole-suffix preparation and one owned retry
  packet rather than duplicating Raft conflict logic or borrowing caller memory.
  Kept live clear/install out of this increment because its callbacks cannot
  pause before all mutation today. Recorded explicit disk-stall availability cost.
- Corrected the historical claim that compaction is still synchronous; CP-039
  already moved its laboratory path to the worker. No source, tests or bytes
  changed; no new verification pass. Exact continuation awaits owner approval.

### Composed compaction crash-image validation — 2026-09-23 (CP-041)

- Added actual-kernel archive → snapshot → WAL durable-image sweep:243 cuts
  across first/replacement compaction and three namespace schedules. Reason:
  component successes do not establish ordering across the complete transaction.
- Added an independently persistent WAL rename schedule; a later sync in the
  shared parent directory can otherwise mask missing snapshot directory sync.
  Before any recovery, exact old WAL must survive or matching new snapshot,
  full archive and exact suffix must already be durable. Refusal alone is not
  an oracle for proper ordering.
- Added one CTest/gate-inventory entry and test-only hooked production objects.
  No production behavior, canonical bytes or hot memory layout changed.
  Three direct actual-source mutants fail at the intended ordering/data checks:
  missing snapshot directory sync, missing replacement WAL sync, dropped suffix.
- Corrected draft harness ownership/oracle/namespace flaws before counting
  evidence: no swallowed accepted-recovery assertions, atomic rename name delta,
  complete replacement history, explicit completed cut and pinned inode identity.
  Retained unsuccessful fixture-run explanations without inventing product REDs.
- Corrected prior CP-040 standalone mutations from mixed cache-line definitions
  to the linked libraries' actual64; both intended failures reproduce. Historical
  mixed-definition results are superseded, not silently rewritten.
- Rebuilt Release86/86, C++ ASan+UBSan86/86, focused243 cuts, tools40/40,
  independent inventory86host/83V2. UB halt/nonrecovering, leaks off, Raft C
  uninstrumented. Two bounded Sol workstreams, main review/corrections/mutations.
  Evidence: `docs/evidence/f5_composed_persistence_2026-09-23/README.md`.
  This finishes the bounded composed host-model step, not broad E2/live-copy/
  hardware obligations. E3 hard-state/truncation and E1 real installer remain.

### Archive persistence fault validation — 2026-09-23 (CP-040)

- Added actual-source archive syscall/retry tests:139 reached EIO sites and
  three successful retry cases. Replaced partial-ordinal/status-only oracles
  with exact restart record/payload checks and preserved fault images, because
  error labels alone cannot establish historical evidence preservation.
- Added124 archive durable-image cuts with separate inode-byte/directory-name
  persistence. Both file-sync and directory-sync omission mutants fail at the
  intended successful-publication recovery assertion. Process death alone
  cannot test loss of the host page cache.
- Added nine actual laboratory snapshot/WAL I/O failures with an uncommitted
  durable suffix, ensuring the nonempty WAL rewrite path executes and preserves
  every tuple on restart. Compaction failure must withhold service/results.
- Added three CTest/gate-inventory entries and test-only source instrumentation.
  Removed experimental global libc interposition and fixed-yield waiting from
  the final harness; hooks must reach actual operations without scheduler-race
  false defects. Existing fixture main now returns explicitly when reused.
- Corrected rather than promoted flawed intermediate test evidence. No new
  product defect, production behavior change or byte contract was established.
  ADR0014 now reflects already-approved install/local-archive decisions and
  explicitly distinguishes component crash evidence from composed proof.
- Rebuilt Release85/85 and C++ ASan+UBSan85/85; focused3/3, tools40/40,
  inventory85host/82V2. Sanitizer UB halt/nonrecovering, leaks off, Raft C
  uninstrumented. Evidence: CP-040 README. Complete archive/snapshot/WAL crash
  images, live independent-copy proof, E3 remaining async paths, E1 installer
  and target gates remain open; no E1/E2/E3/F5/F6/F7 closure claimed.

### Integrated local archive compaction — 2026-09-23 (CP-039)

- Implemented approved V2AC catalog and strict archive owner, fixed replay
  dependencies, greedy bounded multi-object publication and restart verification.
  Reason: a snapshot cannot replace exact historical command/result evidence.
- Replaced unsafe synchronous snapshot-only reclamation with a two-stage job on
  the existing WAL worker: archive+snapshot verification, owner capability/fence
  check and Raft release, then WAL rewrite last. No new competing storage worker,
  borrowed hot-state references or owner-thread join fallback. Tests use an
  explicitly test-only pump; production callers must poll asynchronous progress.
- Added byte/object escrow to new audit liabilities, including authority markers.
  Ordinary admission retains completion/control margins. Capacity-only early cuts
  defer with original history intact; full catalogs report capacity rather than
  stale generation. Integrity/I/O/uncertain publication faults still stop service.
- Added exact-byte compaction/restart, fence responsiveness, interruption,
  recovery-consistency and adversarial store regressions. Cached verification
  cannot authorize deletion after named evidence changes. Valid checksums alone
  cannot justify future-generation, ahead-of-history or term-inconsistent recovery.
- Moved bounded snapshot/archive codec scratch to owned heap workspaces after
  real worker stack overflow. Removed the old allocation assertion's assumption
  that all codec scratch lives on the stack; replaced it with total fixed-workspace
  accounting. The 65,588-byte input lookahead and persistent formats are unchanged.
- Added six CTest suites and independent gate inventory entries. Retained original
  REDs, failed verification runs, two consumer-bypass mutation kills and a
  file-proportional-allocation mutant killed by the revised workspace oracle.
- Final rebuilt Release82/82, C++ ASan+UBSan82/82, focused7/7, tools40/40 and
  inventory82 host/79 V2; focused WAL-worker TSan passed. Sanitizer scope remains
  UB halt/nonrecovering, leaks off, Raft C uninstrumented. Full results and caveats
  are recorded in the CP-039 evidence README. No independent
  destination receipt, live reclamation, exhaustive syscall/power-loss proof,
  target acceptance or E1/E2/E3/F5/F6/F7 closure is claimed.

### Concrete archive catalog review — 2026-09-23 (CP-038, approved)

- Owner approved all five concrete decisions; implementation starts from the
  reproduced actual archive-loss RED. Approval is not E1/E2/E3 acceptance.

- Added exact local archive catalog/publication/quota proposal tied to E2 actual
  compaction/restart and E1 history replay, with E3 shared-fence dependency. One
  bounded flat catalog replaces chain/page/head-selector candidates; archived
  command objects remain immutable. Fixed existing replay artifacts avoid a
  generic dependency framework.
- Two Sol reviews caught header-only provenance loss across storage generations
  and underfilled-file reservation undercount. Proposed per-row source generation
  and pre-cut reaccounting preserve both historical attribution and outstanding
  capacity obligations. All arithmetic independently checked.
- Local catalog never claims a durable independent copy; live reclamation remains
  blocked on real destination evidence. Lab starting quotas are not measured
  retention or deployment guarantees. No C++/byte encoding/build/test change;
  implementation was pending at this checkpoint. All three behavioral exits remain open.

### Approved install semantics and outcome-based exits — 2026-09-23 (CP-037)

- Recorded explicit CP-036 owner approval; no unspecified transport/archive
  encoding or quota approved. Replaced draft ledger-root wording with exact
  live obligation inventory; same owner/FIFO/free-slot generations remain intact.
- Added two contract witnesses enabling safe catch-up (E1): real pinned-Raft
  divergent suffix replacement with boundary-only ACK, and real cold-constructor
  replay/normal later result semantics. Overclaim fails with actual commit/apply20
  instead of12. These do not implement snapshot installation or close E1.
- Corrected reachability claims: healthy public owner calls apply through commit;
  combined with S>old applied, a behind-commit snapshot is not an established
  current live install boundary. Keep its response rule defensive, not a claimed
  reproduced live bug. Cold replay suppression remains intentionally unchanged.
- Main corrected test-oracle/lifetime/premature-commit and fixture path/caller
  mistakes, retained setup failures, and killed two direct recovery-source
  mutants at their intended assertions. Original lagger and archive-loss REDs
  freshly reproduced unchanged. Evidence: docs/evidence/f5_install_contract_2026-09-23/.
- Owner now measures remaining work only by E1 actual catch-up, E2 compaction
  preserving verified evidence, E3 nonblocking persistence/no early success, or
  separately named launch requirements. Roadmap/handoff updated. No additional
  helper-count milestones, no production source change and no F5/F6/F7 closure.
- Rebuilt Release76/76 and C++ ASan+UBSan76/76; tools40/40 and independent test
  inventory matched. Existing sanitizer limits remain (Raft C uninstrumented,
  leaks off); no target/model/TSan/latency/power-loss claim. Protected work kept.

### Integrated install contract review — 2026-09-23 (CP-036, proposed)

- Added consolidated owner review after two independent Sol reviews. Corrected
  draft ACK semantics: retained local tail is not verified leader replication.
  Corrected committed-suffix response handling: capture staged apply outcomes
  instead of leaving already-applied caller obligations permanently RESERVED.
- Proposed same-ledger activation without an extra durable receipt, canonical
  state transfer and four chunks, concrete wire deferral until transport review,
  async generation fencing and finite pinned historical replay instead of a
  total8192-record replay cutoff. Reasons/costs and remaining archive approvals
  recorded in docs/F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md.
- No implementation, byte freeze, new tests or acceptance claim. CP-035 remains
  the last verified test checkpoint; original integration REDs remain open.

### Read-only install preflight and result inventory — 2026-09-23 (CP-035)

- Two Sol workers implemented disjoint bounded components; main owned interface
  decisions, independent corrections, integration and guard mutations. Added a
  pure local-history classifier preserving the whole exact-matching suffix,
  including unknown-commit records, without granting install/deletion authority.
- Added value-only ResultLedger inspection: validate structure first, then READY
  in FIFO and RESERVED by slot. Original handles/full bindings/results preserved;
  no new owner state, allocation, serialized bytes, capacities or hot-path call.
  Main added occupied pool-tag validation after a permanent adversarial RED.
- Corrected a draft test that rejected normal beyond-tail snapshot catch-up, and
  pinned the old snapshot as committed despite a lagging metadata checkpoint.
  Removed superseded V2GS/broader-base instructions from the install draft: only
  approved V2LG/closed-base formats govern; pending receipts remain unapproved.
- Added two suites/gate entries and five intended direct guard mutations. Fresh
  Release74/74, C++ ASan+UBSan74/74 plus rebuilt final focused checks for final
  test-only additions, tooling40/40, inventory74 host/71 V2. Existing sanitizer
  limitations apply; no target/model/TSan/latency/hardware crash claim.
- Original end-to-end lagger RED independently remains RED. No transfer, staged
  activation, archive-backed result reconstruction or F5/F6/F7 closure claimed.
  Protected paths preserved. Evidence: docs/evidence/f5_install_preflight_2026-09-23/README.md.

### Initial generation publication and selected startup — 2026-09-23 (CP-034)

- Integrated approved generation layout into the private-constructor startup
  token: retain root/parent/runtime locks, route stores to the selected runtime,
  durably publish genesis dependencies before selector activation. Exact existing
  identity/metadata/seal/selector formats reused; no generic storage framework.
- Added selected cold recovery, snapshot-base floor and exact stale-token checks.
  Temporary selectors are diagnostic only and preserved; no fallback or promotion.
  Full history admission remains the kernel's responsibility before writable WAL
  open. Recovery resync is not a second history decoder or voting capability.
- Replaced transitional reject-all-generation behavior; removed legacy bootstrap
  retry/adoption and mixed-root acceptance because the approved layout requires
  closed ownership. Refusal preserves histories; migration and live replacement
  remain unimplemented. Corrected migrated fixtures rather than weakening checks.
- Added two suites and gate inventory entries, behavioral REDs, 26-cut/26-EIO
  production-source genesis persistence model, four output-close errors and four
  direct guard mutations. Fixed three test-only macOS temp prefixes for Linux.
- Release 72/72; full C++ ASan+UBSan 72/72 (UB halt/nonrecovering, leaks off,
  Raft C uninstrumented); tooling 40/40; inventory 72 host / 69 V2.
  No target/TSan/model/latency or hardware crash
  claim. Original integration REDs and F5/F6/F7 remain open; protected paths kept.
  Evidence: docs/evidence/f5_generation_startup_2026-09-23/README.md.

### Approved immutable generation base and file verification — 2026-09-23 (CP-033)

- Recorded owner approval of the consolidated base/layout/startup contract.
  Implemented exact 40-byte base seal using existing identity/snapshot formats;
  validate canonical state and catalog dependencies, not only checksums. Fixed
  caller scratch and temporary results preserve outputs on refusal.
- Added a read-only selector-derived base verifier with bounded non-following
  reads, owner-only single-link files, exact immutable namespace and full snapshot
  byte output. It grants no startup/publication/commit authority; mutable runtime
  checks and activation are still unimplemented. Existing startup guard stays.
- Added two suites and independent gate inventory entries. Retained component
  REDs, literal independently hashed golden, main corruption/rehashed-header and
  filesystem tests, six intended direct guard mutations. No formats removed,
  legacy files migrated or generic storage framework introduced.
- Release 70/70; full C++ sanitizer 70/70 plus rebuilt final focused 2/2 after
  test-only additions; tooling 40/40; inventory 70 host / 67 V2. Existing sanitizer
  limits remain; no target/model/TSan/power-cut claim. Protected paths preserved.
  Evidence: docs/evidence/f5_generation_base_2026-09-23/README.md.

### Unsupported generation layouts refuse before old recovery — 2026-09-23 (CP-032)

- Added a cold-start, descriptor-relative generation-artifact guard to recovery
  and ownership-token revalidation. Removed silent acceptance of a selector whose
  selected runtime cannot yet be interpreted; stop rather than use old root files.
  Existing bootstrap refusal and no-marker legacy startup remain unchanged.
- Preserved original production RED, added 3-name x 3-type tests and evidence
  preservation checks; direct recover/token/no-follow mutants fail as intended.
  Main corrected an opaque-WAL fixture's claim to identity-boundary scope only.
- Added a consolidated PROPOSED 40-byte base-seal/layout/bootstrap review, reusing
  existing storage formats and deferring automatic migration. No new bytes encoded,
  generic manifest framework, startup activation or requirement removal.
- Fresh Release 68/68, startup 3/3, C++ ASan+UBSan 68/68, tooling 40/40; independent
  inventory 68 host / 65 V2. Existing sanitizer limits remain; no new target claim.
  Protected paths preserved. F5/F6/F7 and original integration REDs remain open.
  Evidence: docs/evidence/f5_generation_guard_2026-09-23/README.md.

### Approved local storage-generation selector codec — 2026-09-23 (CP-031)

- Recorded approval of the exact160-byte selector and strict local selection
  direction. Added pure encoder/decoder and transition checks; no filesystem,
  startup, snapshot, compaction or voting behavior changed. Base digest remains
  opaque. No unspecified prepared-base/migration/repair contract was encoded.
- Fixed-byte LE staging avoids native padding/ABI serialization, preserves caller
  outputs on refusal and supports tested overlap. Counters at maximum recover,
  but cannot advance; abandoned generation gaps do not permit selection gaps.
- Added worker and independent adversarial suites plus gate inventory. Retained
  component-stub REDs, literal independently packed/hashed golden, corruption and
  resealed semantic tests,546 transition cells, allocation/overlap checks. Main
  direct owner/digest/sequence mutants fail their intended assertions.
- Fresh Release67/67, full C++ sanitizer67/67, tooling40/40; inventory67 host/
  64 V2. Existing sanitizer limits retained; no target/model/TSan/latency or
  filesystem power-loss claim. No requirement/protected path removed. F5/F6/F7
  and original integration REDs remain open; approval is not phase closure.
  Evidence: docs/evidence/f5_local_generation_2026-09-23/README.md.

### Approved closure directions and read-only archive verification — 2026-09-23 (CP-030)

- Recorded owner approval of four CP-029 directions, without treating it as
  approval of unspecified persistent formats, quotas or callback protocols.
- Added read-only bounded archive-file verification using unchanged CP-028 bytes.
  Caller-owned storage, safe relative open/type/size checks, interrupted/short
  reads, EOF/post-stat checks and codec validation precede metadata publication.
  No publication, coverage capability or deletion authority is introduced.
- Added two suites and independent gate inventory entries. Permanent new-component
  stub RED, main syscall fault tests/full-payload assertions and three intended
  guard mutants retained. Corrected stat error classification and a wrong-guard
  fixture; documented aliasing and fd-versus-path limits. No behavior removed.
- Proposed exact 160-byte local generation selector for owner review; not encoded.
  Main removed invalid directory link-count assumptions and recommended portable
  exclusive directory creation plus seal-last rather than extra platform-specific
  rename machinery. All existing startup/recovery behavior remains unchanged.
- Release65/65; full C++ ASan+UBSan65/65 plus rebuilt final focused2/2 after test
  diagnostic wording; tooling40/40, inventory65 host/62 V2. Existing sanitizer
  scope retained; no target/model/TSan/latency claim. Protected paths preserved;
  F5/F6/F7 and original integration REDs remain open. Evidence:
  docs/evidence/f5_archive_file_2026-09-23/README.md.

### Four-lane closure review and actual-owner-I/O REDs — 2026-09-23 (CP-029)

- Added coordinated archive publication, snapshot install, offline repair and
  async-fence reviews. All NEW contracts are PROPOSED, not silently approved by
  parallel-work permission. Shared generation ownership prevents stale writes/
  completions from granting authority after selection; exact bytes remain open.
- Added three permanent async diagnostics observing actual metadata fsync calls,
  not checkpoint labels: campaign/vote/commit perform4/4/2 syncs on Raft owner
  despite running WAL writer. Fresh intended REDs remain outside CTest.
- Added and independently reproduced lost-voter characterization: byte-identical
  donor histories can conceal different old quorum decisions. Preserving full
  uncertain suffix is mandatory; safe missing-hard-state refusal is not removed.
- Corrected draft designs before implementation: matching-prefix requirement for
  suffix reuse, no fabricated result for displaced commands, no checksum-only
  coverage permission, mutable WAL versus immutable generation base, no new
  durable result FIFO. Removed only unnecessary/unsafe review proposals, not
  shipped behavior. Recorded barrier-at-saturation as an open capacity decision.
- Fresh build/Release63/63; no new sanitizer/model/target claim. No production,
  CMake, protected path, commit or deployment changes. F5/F6/F7 remain open.
  Evidence: docs/evidence/f5_f6_parallel_contracts_2026-09-23/README.md.

### Approved portable archive component — 2026-09-23 (CP-028)

- Recorded owner approval of exact archive segment/record bytes, both hashes and
  4-MiB/8,192-record caps. Added separate archive_segment codec, keeping cold-object
  limits out of hot BoundFrame scratch. No existing command/result, peer, WAL or
  snapshot bytes changed; durable publication and deletion authority remain open.
- Added incremental SHA-256; replaced duplicate one-shot block/tail orchestration
  with delegation to the same bounded state, preserving literal known digest
  results. Fixed owner is112 bytes; no digest API or observation domain removed.
- Encoder pulls immutable record views without a per-record array; decoder
  validates the complete stable input before callbacks. Preserves failed/refused
  requests rather than applying current-state eligibility rules to historical
  evidence. Caller-buffer, nonaliasing, partial-source-failure and visitor-lifetime
  limits are explicit; this is not a filesystem or atomic replay transaction.
- Added three CTest suites and independent gate inventory entries: exact byte
  goldens, full payload comparisons, both real capacity boundaries, corruption,
  resealed semantic invalidity, borrowed-source/visitor failures and allocation
  checks. Main independently reproduced seven archive and two SHA guard mutants.
  Compile-only original RED provenance and discarded pre-stable mutation claims
  are recorded honestly in docs/evidence/f5_archive_codec_2026-09-23/README.md.
- Fresh final Release63/63, C++ ASan+UBSan63/63, tooling40/40; inventory63 host/
  60 V2. UBSan nonrecovering/halt, leaks off, Raft C uninstrumented. No model rerun,
  target, leak, TSan, throughput or latency claim. No requirement/protected path
  removed; no staging or commits. Fresh original catch-up/archive-loss diagnostics
  still RED. F5/F6/F7 remain open; codec success never permits working-log deletion.

### Approved recovery policy, bound-metadata proof and archive bytes review — 2026-09-23 (CP-027)

- Recorded owner approval of offline fenced lost-voter repair and immutable
  no-auto-prune archives with fail-closed fullness/independent live audit copies.
  Exact bytes, quota ledger and publication/admission protocols remain separate.
- Added actual BoundMetadataStore persistence model and CTest/gate inventory entry.
  The old generic model remains for its old implementation. New tests sweep cuts,
  failures and short writes with/without an existing temp; main added strict model
  error visibility and real load cross-check. No production sync or format changed.
- Main independently removed each sync in source copies: both mutants fail the
  intended recovered-value assertion. This closes the model-port coverage gap,
  not async hard-state, device power-loss or F6 acceptance. Evidence:
  docs/evidence/f6_bound_metadata_model_2026-09-23/README.md.
- Proposed exact portable archive codec in F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md,
  not implemented. Removed redundant identity fields from the draft layout and
  corrected semantic filtering: archive failed authority attempts as well as
  successful ones; gateway incarnation is not cluster incarnation. No acceptance
  requirement, economic byte, protected path or production behavior removed.
- Final fresh Release60/60, C++ ASan+UBSan60/60 and tooling40/40; independent
  inventory60 host/57 V2. UBSan nonrecovering/halt, leaks off, Raft C uninstrumented.
  No current target acceptance; CP-026's behavioral recovery REDs remain open.

### F5 completion plan and current-source REDs — 2026-09-23 (CP-026)

- Added independent intact-lagger catch-up and exact-command archive-loss REDs;
  main reproduced both against freshly built libraries. They remain outside CTest
  until fixed; a passing baseline is not evidence that these paths are correct.
- Added F5_COMPLETION_AND_NUC_PLAN.md with two-worker dependencies, owner review
  choices, archive/result ownership and transactional snapshot failure boundaries.
  Recommends offline fenced repair for lost voting memory, not automatic rejoin;
  recommendations and old quota candidates are not new approved byte contracts.
- Corrected roadmap's stale next-step status: outbound/log bounds are integrated;
  replacement/install/archive and F6/F7 remain open. NUC shadow/process/observability
  work is separated from live two-host acceptance; Pi deferral is not a waiver.
- Clarified SnapshotStore's comment: recovery durability alone does not permit
  last-copy audit deletion. No production behavior, guarantee, protocol or test
  requirement removed. Fresh build/Release59/59; other verification is historical.

### F5 approved outbound/log bounds integrated — 2026-09-23 (CP-025)

- Replaced unbounded V2 outbound deque with four owned same-kind mailboxes per
  remote peer; fair bounded copy-out, lower-term watermark and permanent encoding
  fault latch. Removed dynamic public V2 egress; tests adapt bounded bytes. This
  prevents stalled transport from accumulating retries, without dropping effects.
- Added borrowed-view allocation-free peer encoding, identical canonical bytes;
  main review found input/output alias corruption and added bounded staging/tests.
  Preserved approved16-entry suffix batching and unchanged V1/pinned Raft APIs.
- Replaced retained map/vector payloads with8192 fixed2048-byte cells and8128/64
  actual-pool admission tokens. Added local log refusal/counters, whole follower
  result/log planning, ordinary markers, stable index-checked payload ownership,
  and startup capacity before writable storage. No speculative suffix release.
- Removed production whole-history vectors from WAL validation/restoration and
  rewrite; bounded streaming preserves canonical bytes and durability ordering.
  Main review kept rewrite bound to its existing fd. Diagnostic readers retained.
- Registered five additional CTest scenarios (59 total), updated independent
  inventory, preserved original growth RED evidence, strengthened disk-failure
  ownership assertions and recorded nine sensitivity mutations. No acceptance
  requirement removed or quota enlarged to get GREEN. Corrected prototype vs
  actual mailbox accounting and4-byte small-frame arithmetic in the review.
- Fresh Release59/59, C++ ASan+UBSan59/59 (UB nonrecovering/halt, leaks off, Raft C
  uninstrumented), tooling40/40. Worker compile-only/stale-archive RED limitations
  explicitly recorded. Evidence: docs/evidence/f5_capacity_integration_2026-09-23/.
  F5 archive/install/replacement, F6/F7 and target/live acceptance remain OPEN.

### F5 outbound/log capacity review — 2026-09-23 (CP-024, pending approval)

- Added three permanent standalone diagnostic REDs: heartbeat replies accumulate,
  retries duplicate payloads, and draining results does not bound retained logs.
  They are outside CTest until quota/protocol approval; no production change.
- Added F5_OUTBOUND_LOG_CAPACITY_REVIEW.md with ownership, actual backing-RAM
  arithmetic, alternatives, saturation availability limits and ordered TDD work.
  Proposed four coalescing Raft mailboxes replace the old unapproved FIFO budget.
  Proposed8192-entry/2048-byte log cells replace misleading nonbinding byte quotas.
- Recommend retaining/budgeting existing Raft ring growth for MVP rather than
  expanding the fork for an allocation optimization. Require bounded recovery/
  rewrite scratch before claiming log memory closure; forbid unsafe reclamation.
- No guarantee or implementation removed. Updated handoff/ADR/checkpoint/candidate
  tables to distinguish proposals from CP-023 shipped result ownership. Fresh
  Release54/54; no new sanitizer/target/latency claim. Evidence:
  docs/evidence/f5_capacity_review_2026-09-23/README.md.

### F5 integrated result ownership — 2026-09-23 (CP-023)

- Owner separately approved follower control-only saturation §8. Added whole-batch
  result planning and typed local refusal; full followers learn newer terms without
  accepting unreserved entries or acknowledging omitted data. No new wire bytes.
- Replaced kernel result deque with the approved1024-slot ledger. Added per-entry
  handle ownership, exact pre-apply binding checks, startup uncommitted reservations
  and retained-ready FIFO delivery. This prevents post-apply capacity failure.
- Added planned actual-pool reuse only after durable suffix deletion; failed offers
  return their unaccepted slots/payloads. Added fixed diagnostic ownership counters.
  Result planning is bounded; outbound/retained-log memory remains a separate gap.
- Registered original four REDs and new edge cases as two permanent CTest suites
  plus independent gate inventory entries. Strengthened WAL reserve fixture with
  larger ordinary records so it still isolates WAL capacity rather than result
  capacity. No guarantee test removed or limit enlarged to make it pass.
- Five direct mutants catch batch partial append, missing recovery reservation,
  missing suffix release, premature release and failed-offer leakage. Evidence,
  verification results and compile-only worker-RED caveat:
  docs/evidence/f5_result_integration_2026-09-23/README.md.
- No full F5/F6/F7 closure, target gate, new venue scope, staging/commit or freeze.
  Fresh host Release54/54, ASan+UBSan54/54, tools40/40; inventory54/51. Model not
  rerun; sanitizer scope remains C++ only, leaks off and UB halt enabled.

### F5 result-ledger component and numeric peer guards — 2026-09-23 (CP-022)

- Added approved1024-slot ledger (960 ordinary/64 protected), exact reservation
  binding, generation checks, allocation-free component operations and ready FIFO.
  This preserves result obligations under slot reuse; kernel integration is NOT
  landed and its growable deque remains. Follower saturation §8 awaits approval.
- Moved CommittedResult verbatim to a shared header to avoid circular inclusion;
  no layout/byte change. Added one suite and independent gate inventory entry.
  Native ledger229400 bytes; not part of TradingState or a total node RAM bound.
- Added basic signed peer-field validation before Raft to prevent malformed
  higher-term input mutating state. No transport-authentication claim.
- Added four permanent diagnostic integration REDs outside CTest and retained a
  suffix-replacement baseline. Strengthened stale-handle tests to reuse identical
  bindings so identity checks cannot mask absent generation checks.
- Fresh Release52/52, ASan+UBSan52/52, tooling40/40; model not rerun. Mutants fail
  intended assertions. Worker stale-library initial checks excluded from original
  RED claims; fresh main verification and correction recorded in
  docs/evidence/f5_result_ledger_2026-09-23/README.md. No target/acceptance claim.
- Removed no feature or protected work; no staging, commit, tag or source freeze.

### Result-ledger contract approval — 2026-09-23

- Recorded owner approval of F5_ADMISSION_RESULT_REVIEW §7:1024 local result slots
  per voter,960 ordinary/64 protected, reserve-before-accept and retain-until-take
  with finite backpressure. This bounds accepted notification obligations without
  dropping results after apply. Owner requested C++ education before code work;
  no ledger implementation or new verification claimed in this documentation step.
  Other queue/log, replacement/install/archive and async contracts remain separate.

### F5 verified AppendEntries prefix and result-budget review — 2026-09-23 (CP-021)

- Added original synthetic receiver REDs and capped V2 commit advancement at
  min(leader_commit, prev+count), because the pinned library otherwise counts an
  unchecked local suffix. Added typed prefix-arithmetic refusal before signed
  overflow; V1, pinned fork and canonical bytes remain unchanged.
- Kept reachability honest: crafted ingress applies a divergent tail before the
  fix, but independent review did not establish an honest current-sender election
  trace. Backtracking can remove that tail first. Do not call this a reproduced
  production split-brain. Real40-entry cluster convergence remains a positive test.
- Added exact result/state/reply, repaired restart, lower-commit, failed-prev and
  empty-heartbeat assertions. Three isolated mutants fail at their intended guards.
  Corrected the test's omitted post-flush commit-checkpoint tick, not production
  durability behavior. Fresh Release/sanitized51/51; tools/model not rerun.
- Refined the result contract: markers emit no business result; full-uncommitted-
  suffix replacement needs a safe ownership-transfer plan, not speculative free.
  Proposed an explicit1024/64 local result budget and measured native layout for
  owner review; no quota or saturation/response protocol implemented. Remaining
  signed peer semantics and full F5/F6/F7 stay open. No removal of user work.

### F5 bound local recovery and metadata-first genesis — 2026-09-22 (CP-020)

- Added typed V2 metadata/WAL codecs and identity-required stores; integrated
  approved V2BF local bytes without changing economics, canonical state or V1.
  Removed V2's legacy flat-WAL fallback/member/reclamation calls because an
  alternate unbound source could hide incompatible history.
- Bootstrap now publishes zero hard state before immutable identity. Missing
  voting memory never defaults to term0; bounded nonblocking/no-follow readers
  and checked publication preserve ambiguous files. Complete matching metadata
  temps are validated without promotion; partial/foreign temps refuse.
- Recovery checks all retained history, even snapshot-covered/uncommitted
  records. Added typed foreign/owner/unsupported refusals and term/overlap/index
  consistency. Complete corrupt blocks refuse rather than becoming overwrite
  frontiers. Physical short-tail recovery remains constrained by commit coverage.
- Preserved original REDs; added process-cut retry matrix and independent WAL/
  metadata golden vectors. Migrated malformed-history fixtures without routing
  them through a semantic encoder that would prevent reaching their target guard.
  Corrected an integration regression where oversized future snapshot schemas
  could fall through as ordinary corruption. Six isolated mutants fail correctly.
- Host Release/sanitized51/51, tooling40, model109; no target or latency claim.
  The old metadata persistence model does NOT cover the newly introduced store;
  porting it is an explicit F6 prerequisite, not an inherited PASS. F5 bounds,
  replacement/install/archive and F6/F7 remain open. See CP-020 evidence.

### F5 approved framing and peer identity admission — 2026-09-22 (CP-019)

- Recorded owner approval of exact common/local/peer bytes and metadata-first /
  identity-last genesis. Local storage/bootstrap integration is still pending;
  do not interpret the common codec as verified recovery.
- Added bounded V2BF framing, independent literal/hash golden and input-borrowed
  decode views; staged encoding supports overlapping payload/output without
  corrupting input. No native layout dumps or economic/V1 byte changes.
- Replaced public raw peer receive with typed-error bound-byte admission. All four
  requests/replies validate before Raft mutation. Full inner shape is preflighted
  before entry allocation; canonical commands and authority domains then validate.
- Capped outgoing AppendEntries at sixteen entries; a forty-entry three-voter
  backlog converges completely. This is a message bound, not a queue/log ceiling.
- Migrated actual cluster traffic using the real sender identity. Corrected one
  catalog latch fixture that otherwise failed while synthesizing a non-member
  sender; it now tests exact dependency refusal before even malformed input parsing.
- Added two CTest suites and independent gate inventory entries; original codec
  REDs, all-kind mutation-free admission, allocation probe and isolated mutants
  are retained under `docs/evidence/f5_bound_peer_2026-09-22/`. Historical foreign-
  peer RED is GREEN; metadata/WAL/snapshot/missing-hard-state diagnostics remain
  RED. F5/F6/F7 remain OPEN; no target acceptance, commit/tag or source freeze.

### Parallel F5 preparation and F6 worker failure correction — 2026-09-22

- Added permanent background WAL failure capture and owner-boundary checks so a
  worker exception cannot escape its thread or leave a failed writer serving.
  stop always joins; error survives restart attempts. No durable/wire byte change.
- Main review found direct submit/poll bypassed the draft latch. Added original
  RED proving a post-failure durability completion, then refused submit/rethrew
  before poll. CV/join tests observe real threaded failure without sleep oracles.
- Added five standalone intended RED diagnostics for foreign metadata/WAL/snapshot,
  real foreign peer votes and missing voting metadata. They remain outside passing
  CTest and explicitly OPEN; foreign history can still change recovered state.
- Proposed exact bound-frame bytes, limits and metadata-first/identity-last genesis
  publication. Added parallel MVP closure/budget-review plan. Deferred optional
  compression, zero-copy, concurrent transfers and archive tiering—not original
  recovery, boundedness, audit or durability guarantees. No approval inferred.
- CP-018 retains full host47/47 Release/sanitized, tools40/40, model109/109 and
  focused shared-path TSan. Worker initial RED and isolated-build discipline were
  missing; these are recorded, not rewritten. Main rebuilt/verified and retained
  isolated guard mutants. No user-file deletion, targets, venue, commit/tag or
  source freeze; F5/F6/F7 remain open.

### F5 history/peer binding review — 2026-09-22 (PROPOSED)

Owner update after CP-017: all six semantic recommendations APPROVED, including
record-level local WAL binding and no legacy-flat fallback in fresh bound lineages.
Exact bytes, budgets and concrete recovery/install protocols remain pending. The
bullets below record the original review, not current approval status. Added the
remaining F5 checklist to the roadmap/handoff; no implementation or new test run.

- Added `F5_HISTORY_PEER_IDENTITY_REVIEW.md`: distinguishes directory identity
  from content lineage, local voting memory from portable history, and public
  digest equality from authenticated sender authority. Records source evidence,
  alternatives, field costs, failure rules and required RED/mutation coverage.
- Proposed record-level local WAL identity and one active block-WAL format in
  fresh bound deployments; legacy-flat live recovery removal is NOT approved or
  implemented. Explicit selection avoids ambiguous nonempty-file preference.
- Documented missing-hard-state, reused-voter replacement and same-lineage rollback
  boundaries: static identity cannot restore a forgotten vote or authorize safe
  participation after disk loss. No donor-metadata copy or auto-shrink shortcut.
- Corrected stale review-document claims that raw construction remained open;
  CP-016 closed startup drift, not the new history/peer work. CP-017 is review
  only: no code/byte changes, tests rerun, source freeze, target or venue action.

### F5 mandatory verified kernel startup — 2026-09-22

- Removed ReplicatedKernel's raw/default constructor: it bypassed the verified
  identity module and could reopen three-voter history as one-voter quorum.
  The sole constructor consumes the approved startup token, validates before
  stores, and retains it as first member/last destructor. All static identity and
  F4 authority-domain configuration derives from it; catalog references owned
  token storage rather than duplicating the catalog or borrowing caller memory.
- Added ownership revalidation and a dedicated kernel-startup suite for moved
  tokens, changed paths/files, live writer lock lifetime and constructor unwind.
  Added regular snapshot-backed membership-drift refusal/unchanged-tree/recovery
  regression. CMake and independent gate inventory now include 47 total/44 V2.
- Migrated fixtures to explicit bootstrap/recover and identity-before-corruption.
  Replaced unreachable late missing-catalog construction with typed early refusal;
  retained true before-apply foreign-catalog coverage and its mutation oracle.
  Corrected copied-history tests that initially failed at bootstrap instead of
  their intended committed-history guard. No production history/files removed.
- CP-016 retains API/A04 RED provenance, host47/47 Release and ASan+UBSan GREEN,
  tools40/40, model109/109 and isolated ownership/quorum/catalog guard mutations.
  Replacement and archive diagnostics remain RED. Trusted stable paths, unbound
  foreign history, old-binary admission and pending target/power-loss evidence
  remain explicit limits. No new wire/economic bytes, commit/tag or target action.

### F5 approved identity bytes and standalone startup ownership — 2026-09-22

- Recorded owner approval of V2ID exact bytes/domains, semantic modes/epochs and
  max3 static voters. Added allocation-free canonical codec, private verified
  identity, literal independent vectors and schema-change compile-time pins.
  One/two/three-voter frames are116/120/124 bytes; pilot still exactly two.
- Added explicit bootstrap/recover owner with move-only RAII directory lock,
  required catalog, bounded input, descriptor-relative startup I/O, atomic
  publication and strict no-adoption/no-ambiguous-cleanup behavior. A valid complete
  temp may resume; partial state is preserved. No lockfile or legacy backfill.
- Main review added ownership-permission refusal and corrected recovery's
  visible-after-rename gap: matching identity is synced before returning startup
  ownership. Preserved REDs, process-cut/retry tests and independent mutants;
  process interruption/checkpoints do not establish physical power-loss safety.
- Added two CTest suites and independent gate entries. CP-015 records host46/46
  Release/sanitized, tools40/40, model109/109. No existing kernel constructor was
  removed yet: rebuilt A04 remains RED and is the next integration task, not a
  hidden acceptance gap. No economic/wire schema or live sender changed.
- Documented cooperative-lock/trusted-parent assumptions, later pathname races,
  sidecar/history/old-executable limitations and non-vacuous fixture migration.
  No user/history files removed; temporary publication only consumes its own exact
  verified temp name. No target, venue, source-freeze, commit or tag action.
### F5/F6 approved direction and persistence-model prerequisite — 2026-09-22

- Recorded owner approval of ADR0014's direction, including provisional atomic
  metadata replacement. Exact bytes, capacities and asynchronous Raft fences
  remain separate review boundaries; the original guarantees are not weakened.
- Added a test-only metadata persistence model that exercises actual save/load
  code with independent recovered-tuple expectations and separate durable data/
  namespace state. Frozen-cut semantics prevent C++ cleanup from advancing the
  modeled crashed machine. Short-write retry and syscall-failure sweeps included.
- Independently killed isolated production mutants removing only file sync or
  only directory sync: invalid durable bytes versus acknowledged-old-tuple loss.
  These are retrospective sensitivity checks, not an invented production bug/fix.
  No production syscall, stored byte or V1 behavior was removed or changed.
- Added standalone CTest and independent inventory entry (44 total/41 V2).
  CP-014 records host44/44 Release and ASan+UBSan, tools40/40, model109/109 and
  raw evidence. Model limits and pending NUC/Pi/async proof stay explicit.
- Proposed exact F5 startup-binding bytes in F5_STATIC_IDENTITY_BYTE_REVIEW.md:
  one atomic local file, shared-domain/local-ID separation and max3 voter support.
  Independent review checked arithmetic; no codec encoded before byte approval.
  Explicitly excluded claims of foreign-history binding or old-binary rollback
  protection: a sidecar alone proves neither. No user files/evidence removed.

### F5/F6 parallel recovery bounds and storage-failure corrections — 2026-09-22

- Replaced SnapshotStore's whole-file string/vector allocations with a fixed
  canonical-limit buffer and lookahead. Added a permanent allocation-observing
  RED/GREEN suite; preserved future-schema refusal and existing corruption
  handling. No snapshot/economic bytes changed; removed redundant file copies,
  not stored evidence or user data.
- Latched V2 service closed after synchronous WAL flush and term/vote/commit
  metadata persistence errors. Publish the metadata cache only after save
  succeeds; exposed default-inert metadata checkpoints for public-path tests.
- Main review reproduced the pinned Raft path swallowing a self-vote persistence
  error. Added a post-periodic owner-latch check and permanent RED/GREEN; no fork
  change or asynchronous acknowledgement shortcut. Added post-rename ambiguity
  coverage and rejected unknown test selectors rather than passing zero tests.
- Added standalone F5 membership/catch-up/archive RED diagnostics, not passing
  CTest targets, and proposed ADR0014/combined review with alternatives, costs,
  explicit budget/byte/retention approvals and parallel sequencing.
- Updated CMake and independent inventory for the new bounds suite. CP-013
  records host43/43, meaningful isolated mutations and provenance limits. Full
  F5/F6, async writer error reporting, power-loss proof and target gates remain
  open. Protected dirty paths preserved; no commit/tag/source-freeze/venue action.

### F4 host proof issuance, one-shot consumption and final handoff — 2026-09-22

- Added owned producer proof with live-only commit/apply issuance and dequeue
  revalidation; stale/abandoned/replayed work cannot mint authority. Fixed slots
  cannot block control apply. No new bytes, economic schema or snapshot growth.
- Added noncopyable gateway authority with original-request expiry, exact
  correlation/binding, monotonic observations and explicit local stop latches.
  Added synchronous final transport dispatch: exact prepared intent, current
  eligibility/fences/final time, one-shot consumption, quota and uncertainty
  recording before callback. Exceptions do not restore permission.
- Disabled boolean renewal (compatibility function always refuses); renamed
  `evaluate_pre_send` to `evaluate_delivery_candidate` so pure filtering is not
  mistaken for send permission. Removed misleading role-read authority comments.
  Added fieldwise value comparison, not padding comparison or a new byte format.
- Added two suites and independent gate entries. Public-command positive and
  adversarial traces cover bytes, partition, delay, known fences, replay/restart,
  clock/sequence boundaries, unknown retry, attempts and emergency cancels. An
  explicit test preserves the approved unseen-change window and in-flight limit.
- Main reran direct isolated-source mutations and final full host regressions;
  CP-012 records missed original RED evidence and a corrected shared-source
  mutation/build race rather than claiming perfect TDD execution.
- Added required roadmap M3a: detailed Grafana execution-path diagnostics,
  subsystem and NUC/Pi health, freshness-aware alerts and runbooks before live.
  No dashboard deployment, target acceptance, credentials, source pin or trading
  enabled. No user files removed; protected and pre-existing paths preserved.

### F4 approved marker codec, replay and availability prerequisite — 2026-09-22

- Implemented approved type-101 / schema-1 V2AB frame, 186 bytes; independent
  golden codec tests and guard mutations pin exact framing and validation.
- Added typed WAL/peer/pre-apply dispatch, non-economic marker apply, configured
  domain and bounded pending admission. On-demand drain scan accepts healthy
  follower backlogs and avoids reading Raft's not-yet-removed entry in log_pop.
  Both ordinary/control current-term commands drain old-term obligations;
  repeated blocked requests append nothing. No implicit no-op was introduced.
- Fixed persisted commit clamping at zero follower durability after a permanent
  RED; volatile quorum commitment is not evidence of local disk coverage.
- Moved suffix/clear payload release after successful storage mutation and
  latched failures closed; a fault-injection RED reproduced continued operation
  after failed truncation. Preserved original durable marker bytes and recovery.
- Added two suites to CMake and the independent gate inventory. No economic
  schema/layout change, live permission, external IPC or target acceptance.
  Removed misleading persistence commentary and premature payload release,
  not user data; all protected paths and historical RED evidence preserved.

### F4 semantic approval and exact-byte proposal — 2026-09-22

- Recorded owner approval of ADR 0013's marker-per-attempt direction; removed
  stale current-status requests to approve the same semantics again. Historical
  CP-009 review and RED evidence remain unchanged.
- Added the separately proposed 186-byte/type-101 durable barrier review with
  field offsets, intent binding, domain dependency, recovery dispatch and
  compatibility limits. No serializer or canonical bytes changed.
- Corrected the slot invariant during review: follower history may legitimately
  contain multiple unapplied markers. Proposed a conservative drain watermark,
  documenting old-term availability cost and F5 retained-history responsibility.
  No code/file removal, production behavior change or new acceptance claim.

### F4 authority review and current-tree RED evidence — 2026-09-22

- Reproduced stale-leader renewal after majority kill with schema-8/catalog
  fixtures, and independently reproduced delayed-reply renewal. Preserved both
  intentional RED assertions outside the passing suite pending protocol approval;
  historical probes remain intact instead of being rewritten as current proof.
- Added proposed ADR 0013 and detailed F4 semantic review: non-economic committed
  barrier versus ReadIndex, one-shot request-start expiry, owned proof copies,
  cancel/kill boundaries and unavoidable in-flight/scheduling limitations.
- Updated the handoff/checkpoint/architecture routing. No F4 production behavior,
  canonical bytes, accepted guarantees or live timeout were changed. No deletion,
  target/venue contact, commit or tag operation; material review remains required.

### F3 immutable installation and dependency-safe recovery — 2026-09-22

- Added approved InstallExecutionCatalog kind 20, codes 62..66 and one immutable
  32-byte state root. Switched command/result/snapshot schemas coherently to 8;
  retained old storage without migration to avoid reinterpreting historical WAL.
- Added explicit owned catalog context, exact staging/authorization row checks,
  historical copied effect binding and bound gateway admission. Removed the
  gateway's raw-materializer bypass, not the diagnostic reconstruction utility.
  Operation capability and historical runtime version now gate sends; the two
  unbound body encoders remain quarantined and no real venue is approved.
- Added optional pinned Raft before_apply before index advancement. Missing
  required artifacts now stop/latch node service without an economic result;
  applylog-only checking was too late. NULL preserves the legacy callback path.
- Strengthened snapshot startup with semantic row checks, because root/hash
  equality alone accepted contradictory retained profiles. Added permanent RED
  and direct guard-removal mutation, preserving existing files on refusal.
- Corrected dependency precedence for rootless installs refused by bounded
  receipt/caller capacity or state-version exhaustion. Replaced impossible draft
  capacity fixtures with public-command traces and canonical snapshot checks.
- Added fail-closed bounds checks for gateway input array counts before indexing;
  capacity+1 and SIZE_MAX tests cover the public planner/admission boundary.
- Updated synthetic cancel capability facts and explicit fixture contexts. Kept
  legacy replay/model golden behavior via schema-7 projection and derived new
  framing independently. No order lifecycle removal, native amend or overlapping
  replacement was introduced. CP-008/evidence records host results and limits;
  F3 real encoder/venue admission, F4/F5 review and target gates remain open.
- Final rebuilt host Release 38/38 and ASan+UBSan 38/38 (runtime UB halt, leaks
  disabled), tooling 40/40, passive model 109/109; independent inventory 38/35.
  TradingState 81,392 bytes and maximum snapshot 65,483 remain within current
  ceilings. No target performance or acceptance claim, commit or tag change.

### F3 approved canonical catalog prerequisite — 2026-09-21

- Added catalog-v1 canonical encode/decode and an owning verified context under
  the owner's approved byte contract. Exact precision, account denomination and
  repeated-version checks prevent a shared numeric product/version from hiding
  different execution facts. No heap or external I/O enters the codec.
- Added an independent golden fixture, maximum-size and every-prefix/short-output
  tests, malformed-but-correctly-hashed inputs, destination preservation and
  aliased encoding. Main review corrected a draft row-size calculation and
  removed an unjustified ban on the existing logical Replace capability bit;
  neither native amend nor overlapping replacement is enabled.
- Strengthened a weak factor test after its guard-removal mutant survived;
  the exact-remainder regression now kills it. All six targeted mutants fail
  at intended assertions. These are scoped checks, not exhaustive mutation proof.
- Added layout reporting and the suite to CMake and the independent gate list.
  Rebuilt host Release 37/37 and ASan+UBSan 37/37; offline tools 40/40, passive
  model 109/109. Context 8,488 bytes outside TradingState; max artifact 8,076.
- Recorded CP-007 and detailed evidence. Catalog schema 1 is implemented;
  kernel/result/snapshot stay 7 until coherent root binding. Removed no lifecycle
  mechanisms, changed no persisted ApplyCode ordinals, enabled no sender and
  claimed no target acceptance. F3/F4/F5/F6/F7 remain open as documented.

### Parallel F6/F7 prerequisites and F3 review — 2026-09-21

- Added deterministic metadata process-crash checkpoints and six cut tests to
  exercise old/new named-file visibility, including a torn temporary. Production
  keeps the same persisted format and default one-write/fsync/rename path.
- Corrected a misleading durability claim during main review: fsync-only mutants
  survive these process cuts. Recorded the limitation and withdrawn callback-only
  proof in ADR 0006 and F6 evidence; no power-loss or asynchronous-I/O closure.
- Expanded source identity to shared code and deployment inputs, removed Phase 7
  self-carried expected-root defaults, and added independent required-suite
  validation. Host tooling tests are not container/NUC/Pi acceptance; re-freeze is
  still pending and historical tags are unchanged.
- Added the unapproved catalog-v1 byte proposal. Recorded Raft's pre-callback
  applied-index advance in ADR 0012 so future artifact checks cannot be deferred
  to apply-time errors. Corrected the in-process-authority source comment: local
  leader belief is insufficient even when gateway and voter share a process.
  No new wire contract or live capability is enabled by this batch.

### F3 recovery compatibility prerequisite — 2026-09-21

- Added ADR 0012 for the approved immutable catalog and fresh-lineage recovery
  boundary. Command/result/snapshot schema 8 is approved but intentionally not
  activated until catalog-root semantics can change with it.
- Added read-only startup preflight across both WAL layouts, including retained
  uncommitted commands and snapshot overlap; delayed writable WAL construction
  until compatibility/replay succeeds to preserve diagnostic storage on refusal.
- Removed unsupported snapshot versions from the ordinary corruption/replay
  fallback. Recognize state schema before its size bound, so larger future
  snapshots cannot masquerade as current-schema corruption. Retained supported-
  schema corruption recovery and all current persisted bytes.
- Added permanent compatibility/byte-preservation tests and CP-005 RED/GREEN
  evidence. Rebuilt host Release and ASan+UBSan pass 36/36 each; no target or live
  claim. Catalog binding remains unfinished and unbound encoders remain closed.

### F3 catalog direction approval — 2026-09-21

- Recorded owner approval of one immutable committed execution catalog per
  deployment lineage; no effect, command or snapshot bytes changed.
- Added the next installation/recovery contract proposal: local artifact loss
  must stop a node rather than change replicated economic results; legacy WAL
  must not replay under new prerequisites and produce different history.
  Proposed a fresh-lineage compatibility boundary with old data preserved.
- Kept F3 OPEN and encoders quarantined. CP-004 is documentation-only and does
  not claim new tests or approve the remaining canonical schema/migration choices.

### F3 immediate refusal and metadata review — 2026-09-21

- Quarantined the two unbound request-body APIs: current effects cannot authorize
  market/swap economics, and a caller-set schema flag cannot bind a limit to exact
  execution metadata/capabilities. Refusals return zero bytes without touching
  caller output; there were no live callers. No reducer/lifecycle was removed.
- Removed the now-unreachable JSON body assembler and tests that rewarded
  limit-to-market conversion or treated the verification flag as authority.
  Retained exact decimal/tick/lot arithmetic; documented product agreement as
  insufficient to establish economic identity. Added sentinel-output regressions
  covering factor aliases, wrong identity and unsupported order constraints.
- Added `docs/F3_EXECUTION_METADATA_REVIEW.md`: committed catalog identity is
  proposed, not encoded. Measured headroom is 560 resident bytes and 85 maximum
  snapshot bytes; retention/migration and any budget changes need review.
- F3 remains OPEN pending binding approval and implementation. CP-003 records
  validation for the refusal increment, not a claim that venue delivery works.

### F2 remediation — 2026-09-21

- A05: validate normal as well as control proposal bytes before Raft/WAL
  admission; retain existing refusal codes and strict replay. Permanent malformed
  input tests prove unchanged history and healthy valid-command restart progress.
- A10: replace repeated front peeks with a bounded snapshot of distinct sealed
  descriptors; sync before completing, roll back on write or sync failure, and
  refuse mixed writer ownership. Added batch, retry and optional pre-write fault
  coverage without changing persisted bytes or existing checkpoint ordinals.
- A11: replace truncating token output with exclusive private temp creation and
  descriptor-relative atomic rename. Validate responses before replacement;
  refuse unsafe paths/origins, disable curl config/redirects, and move bearer
  headers from argv to stdin. Removed raw auth-body error output and corrected
  stale token-capability wording. No real credentials or network calls used.
- Main review rejected missing-helper test errors as RED evidence, found further
  newline/invalid-expiry defects in the first tooling patch, and added observable
  regressions. Mutation checks kill original defects and weakened safety guards.
- CP-002 records host Release 36/36, ASan+UBSan 36/36 (leaks disabled, halt on UB),
  offline tools 24/24 and passive model 109/109. Targets remain open; no gate pin,
  canonical schema, protected path or accepted tag changed.

### F1 remediation — 2026-09-21

- Fixed A02: carry both bounded quota lanes through the delivery planner so
  exhausted normal traffic cannot strand a pre-authorized emergency cancel.
  Retained authorization order and normal-first spending; no new wire fields.
- Fixed A06: refuse submit authorization with either control identity zero,
  because the resulting canonical effect could not be materialized. Positive
  replication fixtures now establish authority through committed commands.
- Added permanent RED regressions, mixed quota/zero-control cases, exact cancel
  payload checks and a public bootstrap trace. Corrected a fixture comment that
  falsely claimed a submit acknowledgement; the fixture actually has a pending
  submit. Added a checkpoint audit log; no historical evidence was removed.
- Host validation and mutation evidence are recorded in
  `docs/evidence/V2_REMEDIATION_CHECKPOINTS.md`. Target acceptance remains open.

### Full Phase 6–9 audit and two-host roadmap — 2026-09-21

- Added a consolidated architecture/guarantee audit and ten intentional RED
  reproductions. Independently confirmed stale authority, missing emergency
  quota propagation, unsafe economic lowering, config drift, malformed-log
  poisoning, zero-control outbox materialization, unbounded retention, replacement
  catch-up failure, multi-block flush duplication and token mode preservation.
- Added an ordered fix plan and NUC/Pi trading-pilot roadmap. Two-voter quorum
  and separate gateway are recommendations pending approval, not frozen contracts.
- Reopened current Phase 6/7 target acceptance and G-049 membership completeness;
  kept historical tags and original guarantees. Corrected the prior claim that
  atomic replacement requires two slots: its actual cut proof is what is missing.
- Fresh host Release 36/36, rebuilt ASan+UBSan 36/36 (leaks disabled), tools 9/9,
  model 109/109. Ten audit REDs are separate from these passing suites. No target
  or venue run, production C++ fix, credential access or trading occurred.

### Review corrections — 2026-09-21

- Corrected proposed ADR 0011: limited MCP claims to captured evidence, withdrew
  expected-output-as-slippage-control and submitted-as-filled reasoning, fixed
  volume arithmetic and wire-size reasoning. No economic or byte contract frozen.
- Scoped ADR 0009 to verified inert prepare; no C++ behavior changed.
- Replaced speculative markup computation with raw capture because response
  units are unverified; fixed required login email and suppressed auth output.
- Added read-only allowlists, MCP error handling, full paginated schema capture,
  size validation and non-overwriting output. Removed the unverified HYPE mint.
- Added nine offline regression tests, observed RED then GREEN. No venue call.
- Phase 8/9 review records missing executable integration and target evidence;
  Phase 6/7 independent review was interrupted and is NOT a completed audit.
  These corrections supersede stronger claims in the historical entries below.

### Phase 9 in development - True Markets adapter

#### The venue is not an exchange — the limit-order premise is withdrawn — 2026-09-21

`docs.truemarkets.co/mcp/` publishes two MCP servers. Both answer `tools/list`
anonymously, and the reply carries a JSON Schema per tool. That is the venue's
own machine-readable contract, served by the system that enforces it - a
strictly better source than the REST prose that failed to render in D-3, and
better than the browser-DevTools route it replaced, which needed a logged-in
session and, to see a request body, a real order.

WHAT IT SETTLED, AND WHY IT COST SO LITTLE TO FIND. D-3 had been open since
2026-09-17 with the price field name unknown. `trade_prepare` takes
`{side, base_asset, qty, chain?, qty_unit?}` and nothing else. `qty_unit` does
accept a base-denominated quantity, which was the open half of D-3. The other
half is answered by absence.

THE VENUE HAS NO LIMIT ORDER. `trade_prepare` is described by True Markets as
"Create a market order". There is no price parameter, no order type, no
time-in-force, and across all seventeen tools no cancel, no open-orders query
and no order book. The trading vocabulary is three verbs: `get_quote`,
`trade_prepare`, `trade_execute`. This is a request-for-quote swap venue.

V2 is built to post and manage resting liquidity. On this venue there is nothing
to rest, nothing to cancel, and no price to post. D-1 had concluded from the
endpoint reference that DeFi limit orders existed; the MCP server is a third
source and it settles the question the other way. **D-1's conclusion was wrong.**

WHAT SAVED US was a refusal written before the answer was known.
`encode_limit_order_body` has refused with `schema_unverified` since it was
written, on the reasoning recorded in `venue_wire.hpp`: many APIs ignore fields
they do not recognise, so a guessed price field would have produced a LIMIT
order that silently executed as a MARKET order, crossing the spread and paying
taker fees on every quote while appearing to work. That is exactly what would
have happened. The refusal is now permanent rather than provisional.

Also recorded, all from the same source:

- D-24: `qty_unit`'s default DEPENDS ON THE SIDE - quote for buys, base for
  sells. The same body with `side` flipped denominates `qty` in a different
  currency. Mitigated by always sending the field.
- D-25: omitting `chain` does not mean unspecified, it routes the order to a
  CeFi exchange with different custody, settlement and fees, and the request
  stays valid. Mitigated structurally - see ADR 0011 section 4.
- D-26: "If no payloads are returned the order was submitted directly." One call,
  two behaviours. A crash after sending `trade_prepare` leaves it unknown whether
  an order is live, which is worse than `resolve_two_step_send` assumed.
- D-27: `list_assets` and `get_asset` are advertised on both servers and return
  404. They are the only tools yielding decimals and contract address, so the
  REST catalog (D-11) stays the instrument source of record. Separately, the
  servers report RATE LIMITING as JSON-RPC `-32602 invalid params`, which sent
  this session hunting a bug in its own arguments for several minutes.
- D-28: no slippage, limit-price or minimum-received parameter exists anywhere
  on the order path, and `get_quote` returns no quote id to bind. **A fill
  cannot be bounded through the API.**

Evidence: `docs/evidence/V2_TRUEMARKETS_MCP_SCHEMA_2026-09-18.md`, captured by
`tools/mcp-tools.py`, which speaks `initialize` and `tools/list` and refuses
`tools/call`. No order was placed and no account was authenticated.

#### ADR 0011 proposed: the swap execution model, and volume as the objective — 2026-09-21

STATUS: PROPOSED. The byte contract change in section 5 is NOT approved and NOT
encoded. Nothing in this entry has been implemented.

THE OBJECTIVE IS MAXIMUM VOLUME TRADED, for a leaderboard, and on a swap venue
that changes what the architecture is optimising. Capital RECYCLES - a round
trip returns your money minus costs - so capital is not the constraint. The loss
budget is:

    max_volume = loss_budget x 2 / c        (c = round-trip cost fraction)

Volume is bounded by `1/c` and nothing else, so every design question reduces to
what makes `c` small and what stops it silently becoming large. This is a
cost-minimisation system, not a market-making system with the quoting removed,
and cost minimisation has a different risk model.

MEASURED `c` on Solana through Jupiter's quote endpoint (route simulation; no
transaction built, signed or sent). SOL 0.1-0.7 bps, JUP 1.5-10.6, JTO 46-72,
DRIFT 71-772, W 58-357, across $25 to $2000.

Two conclusions, both load-bearing. Cost per unit volume spans roughly four
orders of magnitude across assets, so ASSET SELECTION IS THE STRATEGY - no
timing cleverness recovers a factor of 140 between SOL and DRIFT. And impact is
superlinear in size, so many small round trips beat few large ones, floored by
per-trade overhead and rate limits rather than by economics.

A DEFECT IN THIS ANALYSIS, CORRECTED. The volume table was committed 10x high
and fixed in `191a741`: at c = 0.5 bps a budget buys 40,000x its own size in
volume, so $100 buys $4M and not $40M. The SOL-to-DRIFT spread is 140x, not
7000x - 7720x is the ratio of their COSTS, which is a different quantity. The
conclusions did not change.

FOUR DECISIONS, and the options weighed for each:

1. A swap is `EffectOperation::swap_market`, not a parallel `SwapEffect` type.
   A second type would duplicate the gateway, risk reservation, kill epoch,
   idempotency and audit trail, and create a second path Phase 8's mechanisms
   would have to be taught about separately - which is how a safety mechanism
   comes to cover one path and not the other.

2. The chain becomes part of `VenueId` (`truemarkets-solana`, `-base`, `-cefi`)
   rather than a field on the effect. CONSIDERED AND REJECTED: a `chain` field
   validated on the way out. Folding it into the venue id makes D-25's
   dropped-field-changes-venue failure UNREPRESENTABLE rather than guarded -
   there is no field to omit - and it stops per-venue risk limits silently
   pooling on-chain and exchange exposure.
   CONSEQUENCE: `hypercore-spot`, `hypercore-perp`, `robinhood` and `arbitrum`
   are in the catalog but not addressable, because `trade_prepare` accepts
   solana and base only. **PURR is hypercore-spot, so the decision to trade PURR
   is withdrawn.** HYPE is reachable as a bridged Solana token, subject to
   D-21's warning about which HYPE.

3. Quantity is denominated in what the side SPENDS - quote micros for buys, base
   lots for sells. A buy spends USDC, so quote denomination makes the risk
   reservation exact rather than an estimate a price move invalidates; a sell
   spends tokens, so base denomination makes the inventory exact. Denominate
   either side the other way and the quantity that matters becomes a function of
   a price nobody has observed yet. This is why the venue defaults that way.
   CONSIDERED AND REJECTED: deriving the basis from `side` at encode time. That
   puts the rule in two places - reducer and encoder - and a divergence places a
   buy for 113 USDC instead of 113 SOL. Storing it costs one byte, makes
   divergence impossible, and puts the denomination in the replicated log where
   ADR 0007 says the audit lives.
   THE BYTE CONTRACT CHANGE: `OrderEffect` V2 -> V3 gains `QuantityBasis
   quantity_basis` and `std::int64_t quote_micros`; 166 B -> 176 B after
   padding. Exactly one of `quantity` / `quote_micros` is non-zero, enforced by
   a validator. `base_lots` remains the only legal basis for a limit order, so
   Hyperliquid is unaffected. NEEDS APPROVAL BEFORE ENCODING.

4. The price bound lives at the SIGNING step, because D-28 leaves nowhere else.
   `trade_prepare` takes no slippage parameter and `get_quote` binds nothing, so
   the only control is that an unsigned payload does not execute: prepare, read
   the expected output, sign only if within bound. REFUSING TO SIGN IS THE
   SLIPPAGE CONTROL.
   AND THIS IS WHY D-26 IS THE MOST DANGEROUS ROW IN THAT FILE. When
   `trade_prepare` submits directly there is no signing step, therefore no
   bound, therefore a fill at whatever the market gave - and the client does not
   choose which mode it gets. The two-step is not merely a crash-recovery
   concern; it is the only mechanism by which a price bound exists at all.

WHAT IS FORECLOSED. No market making on True Markets, no post-only, no queue
position, no maker rebate, no resting liquidity. Any claim that V2 "market-makes
on True Markets" would be false. The significant-figure pricing work (D-15) is
MOOT here - there is no price to render - and defers to the Hyperliquid adapter,
which cancels the byte-contract churn that work was about to cause.

WHAT IS WEAKENED, SAID PLAINLY. Phase 8's mass cancel has no venue counterpart
here. Market orders do not rest, so the safety action on this venue is "stop
sending", which the kill epoch already provides. `RequestMassCancel` stays
correct for Hyperliquid and meaningful for internal state, but its venue leg on
a swap adapter will be an explicit, documented no-op rather than something that
quietly succeeds.

#### The number that gates the economics is unmeasured — 2026-09-21

Every value of `c` above came from Jupiter, not from True Markets, and is
therefore a LOWER BOUND: any venue markup sits on top and dominates completely.
Per $100 of loss budget the achievable volume is $4,000,000 at zero markup,
$190,000 at 10 bps, $66,000 at 30 bps and $20,000 at 100 bps - a factor of 200
across a plausible range.

NO FURTHER DESIGN WORK ON THE STRATEGY IS WORTH DOING UNTIL THIS IS MEASURED,
and it is cheap: `get_quote` is read-only, needs a login and nothing else, and
places no order. `tools/measure-markup.py` performs the comparison.

That tool's safety is a RULE and not a property, which is a real difference from
its siblings and is recorded in its docstring. `tools/mcp-read.py` is pinned to
the market-data server, which has no trading tool to call, so it is safe BY
CONSTRUCTION. `get_quote` exists only on the trading server, so the markup tool
necessarily holds a session that CAN trade: it carries a four-name allowlist,
refuses `trade_prepare` and `trade_execute` by name as a second barrier, and
contains no signing code or any path that could produce a signature.

#### A two-step venue send is one effect — 2026-09-17

Implements ADR 0009. The DeFi order path is not one request: `POST /orders`
returns unsigned payloads and leaves the order awaiting a client signature, then
a second call submits the signatures and the order rests. Cancellation has the
same prepare-then-execute shape. The gateway never learns any of that - nothing
in the cluster's model could use it and everything in it would be complicated by
it.

THE SAFETY CONTENT IS THE OUTCOME MAPPING, and specifically that the two
ambiguity points are NOT the same answer. An ambiguous FIRST step reports
`pre_send_failure`, because an `initialized` order cannot fill - it awaits a
signature that was never sent - so "nothing is at the venue" is true in the only
sense the gateway cares about. An ambiguous SUBMISSION reports `ambiguous`,
because the order may be resting and only reconciliation can find out.

Collapsing them either way is expensive. Reporting step one as ambiguous sends
the gateway reconciling an order that cannot exist; reporting step two as
nothing-sent lets the effect be retried into a SECOND LIVE ORDER.

AN AMBIGUOUS SUBMISSION IS NEVER ABANDONED. Cancelling what may be a live order
is worse than leaving it, and the gateway's reconciliation asks what is actually
resting with the cluster's full picture rather than the adapter's partial one.
Every other post-step-one failure does abandon the unsigned order - it cannot
fill, so that cleanup is untidiness rather than risk.

The load-bearing documented fact - `initialized` means "awaiting client
signature" - is recorded as D-9 in the discrepancies file rather than left in a
comment, because a documented fact about this venue already changed under us once
inside two days.

Five mutants, each dying on its own named assertion.

A METHOD FIX THAT FINALLY STUCK. Mutation through cmake is unreliable here and
cost the third such incident today; a fresh build directory works but its 46s
configure blew a command timeout and left a mutation in the working tree.
Compiling the suite DIRECTLY with `c++ -Iinclude tests/<suite>.cpp <deps>` takes
seconds and has no objects to go stale. Recorded in the handoff.

#### The create-order body, and the refusal that protects the strategy — 2026-09-17

The documented market-order body encodes. The LIMIT body refuses until somebody
verifies the schema, and that refusal is the point of the commit.

WHY. As of 2026-09-17 the create-order request schema does not render on the
venue's docs - only the market-order example does - so the price field name is
unknown, as is whether `qty_unit` accepts a base-denominated quantity. Many APIs
ignore fields they do not recognise. A guessed price field that the venue drops
turns a LIMIT order into a MARKET order: it crosses the spread, pays taker fees
on every quote, inverts the maker economics this project exists to capture, and
reports success while doing it. Nothing on our side can detect that afterwards.
So `VenueOrderSchema` carries a `verified` flag and encoding refuses without it -
not knowing is a state the code can be in, rather than a gap somebody fills with
a plausible guess.

`qty_unit` is sent as BASE rather than the quickstart's "quote". The cluster
authorizes a quantity of an instrument, not a spend; nothing in V2 can express
"five dollars of BTC", and pretending otherwise would let a price move change the
size of an order the reducer already sized.

Unsafe metadata is REFUSED, NOT ESCAPED. An asset symbol is alphanumeric on every
venue, so anything else means the metadata is wrong - and escaping would carry a
corrupt symbol into a live order and let the venue decide what it meant. A body
too long for its buffer refuses with zero bytes written, because a partial
request is a request. A cancel is refused outright: it has its own endpoint and
its own two-step signing flow, and encoding one as a create-order body would
place a NEW order in order to cancel one.

A METHOD FAILURE WORTH RECORDING. Three mutants were first reported as surviving
when none of them had been compiled - and earlier in the day a fourth died on an
assertion belonging to a different property, which is a false pass wearing a
green coat. Mutation runs in the shared build directory are unreliable on this
host; deleting the object file and `touch` both fail against sub-second mtime
granularity. Re-run one fresh build directory per mutant, all three die on their
own named assertions. The handoff's method notes now say so.

36/36 host, 36/36 ASan+UBSan.

#### The venue wire: exact units, and two sources that must prove each other — 2026-09-17

A FINDING FIRST, because it shaped the whole slice. The replicated
`InstrumentExecutionProfile` carries `quote_micros_per_tick_lot` - the PRODUCT of
tick size and lot size. That is what risk needs, since it yields notional. It is
NOT enough to render a venue order, which needs price and quantity SEPARATELY,
and the factors cannot be recovered from their product. So the adapter must carry
tick and lot itself, from venue metadata that is not replicated and must not be.

That leaves two sources for one economic fact, and the only safe thing to do
with two sources is make them prove each other. `spec_agrees_with_profile`
requires tick x lot to reproduce the replicated product EXACTLY - not closely,
because a product that does not land on a whole number of quote micros cannot be
represented by the contract at all, and rounding it would encode an error as a
price. A tick size wrong by a factor of ten is the classic venue-metadata error
and the one that makes every quote invalid; it is refused, as is a spec for a
different instrument or venue, because agreeing about a number is not the same
as being about the same thing.

EVERY CONVERSION IS INTEGER. There is no floating point in the header or its
implementation. A value that round-trips through a double is a value nobody
chose, and the venue accepts it as readily as a correct one.

REFUSAL RATHER THAN TRUNCATION is the contract of the return value. A prefix of a
number is a valid-looking number: "1234" cut from "12345" is an order at a tenth
of the price, and it would be accepted.

WHAT MUTATION TESTING ACTUALLY SHOWED, recorded because the honest result is more
useful than the tidy one. Removing the exactness check and removing the buffer
refusal each die on their named assertion. But rewriting the sign handling as
`negative ? -atoms : atoms` - the classic int64-min bug - STILL RENDERS int64 min
correctly on this compiler, so every value assertion passes. It is caught by
UBSan instead: "negation of -9223372036854775808 cannot be represented". The
protection for that line is the Debug ASan+UBSan run that gates every phase, and
nowhere else. The comment on the function now says so rather than implying an
assertion covers it.

36/36 host, 36/36 ASan+UBSan.

#### The venue boundary, made explicit — 2026-09-17

Until now there was none. `SimulatedVenue` is a concrete class used only by
tests, and no production code talked to a venue at all - so "the gateway does not
know which venue it is talking to" was a design intention with nothing enforcing
it. `VenueTransport` is the seam the True Markets adapter will sit behind.

SHAPED AS FUNCTION POINTERS PLUS A CONTEXT, matching `StorageCheckpoints` rather
than introducing virtual dispatch. Trivially copyable, owns nothing, and can be
handed to a pure planner without dragging a class hierarchy behind it.

WHAT DELIBERATELY DOES NOT APPEAR IN IT: authentication, JWT refresh, rate-limit
pacing, retry policy, and the venue's two-step DeFi signing flow where
`POST /orders` returns unsigned payloads and a second call submits the
signatures. A `send` is ONE CALL from this side however many round trips it
costs on the other, because the gateway reasons about effects and not about
HTTP. The moment any of that leaks through the struct, the simulator and the
venue stop being substitutable and every test written against the simulator
stops meaning anything about production.

POLL WRITES INTO A CALLER-OWNED SPAN rather than returning a vector, and that is
the one place the seam deliberately differs from the simulator it wraps. An
adapter must not decide how much memory the gateway spends, so a venue that
floods us is TRUNCATED rather than obeyed. Truncation must then DEFER rather
than drop, because an observation is economic evidence and losing one loses a
fill - so the overflow is held and offered first on the next poll, while
acknowledgement still goes straight to the venue, which is what keeps
redelivery-on-crash working.

Held by three properties: the same venue driven directly and through the seam
produces an identical trace of outcomes, counts and sessions; a poll never
overruns its buffer; and draining one observation at a time yields exactly the
sequence a single wide poll delivers. Mutation-confirmed both ways - dropping
the overflow and ignoring the caller's buffer size each fail a named assertion.

### Phase 8 in development - execution authority, fencing, and emergency control

#### G-057a's fencing half, and the gateway loop deferred to Phase 9 — 2026-09-17

G-057a discharges a forward reference Phase 4 left open deliberately.
`settle_orphaned_effects` marks an effect `incomplete` when its order resolved
before that effect ever received evidence - the alternative being a group that
can never be reclaimed, which wedges the bounded arenas - and its own comment
says the late-send hazard "is answered by executor-generation and kill-epoch
fencing plus stable EffectId idempotency in Phase 8, not by retaining these
bytes forever."

That answer is now a test, stated as three claims, and the guarantee's whole
point is that the second and third protect us rather than the first: the cluster
does NOT keep the bytes (a settled-incomplete effect is not deliverable - the
availability trade Phase 4 made, not a protection); FENCING stops a gateway that
kept them anyway, and the `OrderEffect` carries the generation and kill epoch it
was authorized under, so the refusal does not depend on the cluster being able to
rebuild it; and `EffectId` is derived rather than allocated, so the same effect
rebuilds to the same identity and bytes.

WHAT THE TEST IS AND IS NOT. Its individual assertions overlap existing coverage
- zeroing the effect's generation is caught by the pre-send suite long before it
reaches here - so it is NOT claimed as unique mutation coverage. Its value is
assembling the exact scenario the guarantee names, a closed order's orphaned
effect, which no other test does.

THE BOX STAYS UNTICKED. The guarantee says a duplicate "is a deterministic no-op
rather than a second external effect". The cluster can supply a stable
idempotency key and does; whether a redelivery becomes one order or two is the
VENUE's decision, and depends on the adapter using `EffectId` as its idempotency
key. Recorded as a Phase 9 obligation rather than assumed.

THE GATEWAY LOOP IS DEFERRED TO PHASE 9. Every Phase 8 mechanism is enforced and
none is DRIVEN: nothing calls `renew_authority`, chooses a lease bound, reaches
for the emergency lane, or runs the resting-order heartbeat. That loop's shape is
decided by the adapter it talks to - the lease bound by real round-trip times,
the heartbeat cadence by venue rate limits, the authoritative read by whether the
gateway is in-process with a voter - so building it first would mean choosing
those numbers twice. No Phase 8 guarantee is ticked as a result, which is a
sequencing decision and not an omission.

#### A resting order is confirmed, not assumed — 2026-09-17

ADR 0008 rules 2 and 3, completing the visibility half of G-060. Reconciliation
now ASKS about resting orders, and `order_reconciled` is applicable to them, so a
cancellation whose event never arrived - a dropped socket, a reconnect gap - is
discovered rather than believed away.

THE HEARTBEAT IS SLOWER AND UNCAPPED, and both differ from every other
reconciliation need on purpose. The others chase something already known to be
wrong, so they are urgent and give up after `max_query_attempts` to avoid
hammering a stuck venue. Confirming a resting order is not urgent, and giving up
returns the cluster to assuming - the exact state this removes. The bound is the
RATE, not the count.

THE FIRST ATTEMPT WAS BACKED OUT AND THE REASON IS THE USEFUL PART. The refusal
was blamed on the applicability gates and two were patched on that theory. The
actual cause was the TEST FIXTURE: `validate` requires a venue sequence on every
observation, including a synthetic one, and a zero there produces
`invalid_identity`, which from outside reads exactly like an applicability
refusal. The instrumentation used to chase it was also misattributing
single-line returns, and the lines being read came from other tests in the same
run. Re-done as a focused pass - enumerate all twelve refusal sites, identify
the two that can fire, then change - the original patch turned out to be right.
The work was never wrong; the diagnosis was.

TWO ACCEPTED TESTS CHANGED THEIR EXPECTATIONS, authorised by the ADR and
recorded there. Both required a reconciliation answer for a healthy order to be
refused - one because the effect was "already resolved", one because a live
order "needs none". That is the assumption being overturned, and the first
one's own comment already conceded that admitting it was harmless by value.
Both now require the answer to be admitted and to move nothing.

ONE LABEL CHANGED. An absent answer for an order the venue never acknowledged
still resolves to `rejected`. One the venue ACKNOWLEDGED and later stopped
holding resolves to `cancelled` - it was not refused, it is gone. Economically
identical, so this is about not telling an operator something false.

Five mutants, each dying on the assertion that names its property.

#### An unsolicited venue cancellation is evidence — 2026-09-17

ADR 0008, first of three rules. A native `cancelled` for a resting order the
cluster never asked to cancel is now accepted and moves the order to
`reconciling`. Refusing it left the cluster believing a dead order was still
working - reservation held, slot occupied, book silently wrong - and
exchange-initiated cancellation is routine rather than exotic: a post-only order
that would cross, self-trade prevention, a market halt.

`reconciling` is exactly where an AUTHORED cancel puts an order, because
`cancelled` is a notification in this design and not terminal evidence.
Settlement still comes from cumulative evidence, so the venue cannot free
reserved risk by announcing anything. Deliberately not `conflict`: that is
terminal-but-stuck and blocks a resume under G-059, so a routine post-only
rejection would wedge trading until a human cleared it, and a safety rule that
fires constantly during normal operation is one that gets disabled.

The gate that had been swallowing it is effect-centric, and the distinction is
the fix: an unsolicited cancellation is evidence about the ORDER, not about the
submit effect that placed it - and that effect being resolved is the normal case
here, since the venue acknowledged the order before cancelling it.

Because `reconciling` already carries the `cancel_unconfirmed` need, the push
case self-heals through machinery that was already built.

RULES 2 AND 3 OF ADR 0008 ARE NOT IN THIS COMMIT. They were written and then
backed out: widening `order_reconciled` to resting orders sits inside Phase 4's
evidence-applicability gates, where a first attempt exempted the wrong predicate
and made a late `order_acknowledged` erase partial-fill knowledge. Querying
resting orders while their answers are refused is worse than not querying - the
attempt never discharges and the heartbeat is uncapped - so the two rules are
one increment and must land together. The ADR records this.

#### G-060, and a claim the test falsified — 2026-09-17

The gateway half is now a PROPERTY rather than a sample: for every identity
probed, permission implies the effect is in the committed outbox, and the bytes
handed over are the cluster's own reconstruction. Emptying the outbox leaves the
gateway with nothing to send. The probe covers every cluster order id, decision
id and generation the state holds, because a sweep over an arbitrary numeric
range is weaker than it looks - the ids a wrong lookup really returns are the
other ids in the same records.

WHAT TRYING TO BREAK IT TAUGHT US. Keying both the guard's scan and
`materialize_order_effect` on `cluster_order_id`, so the two agree with each
other, changes nothing: the canonical payload digest BINDS THE EFFECT ID, so an
id that is not the record's own cannot reconstruct. The lookup key is not the
defence - the defence is that the bytes only exist for the identity that
authored them.

AND ADR 0007'S OTHER CLAIM WAS FALSE. It said an out-of-band venue action would
reconcile back through `ObserveOrderEvent` into `conflict` or `reconciling`.
Writing the test showed it does not. No evidence kind says "the venue cancelled
an order you never asked to cancel": `cancelled` is only applicable against a
cancel the cluster authored, and `order_reconciled` can only say the venue STILL
HOLDS an order. An unrequested cancellation is refused and the cluster goes on
believing the order is live.

Same shape as ADR 0004 - V2 cannot represent a position change it did not
authorize - so the containment is not "we reconcile it" but the stronger
statement that an out-of-band action cannot be represented, which is why the
emergency path must go through the cluster. The ADR carries the amendment.

The safety half holds and is tested: a venue cannot unilaterally free reserved
risk by announcing a cancellation nobody authored. The visibility half is OPEN
and recorded as such rather than closed by redefinition.

#### Mass cancel, as one command — 2026-09-17

Implements ADR 0007. `RequestMassCancel` is one command and one log entry,
carrying a scope (all, account, or account-and-instrument) and the usual
expected kill epoch and executor generation.

WHAT "ATOMIC" MEANS, because the strict reading would be useless. An order that
is already filled, or already being cancelled, is not a failure of a mass cancel
- it is outside the cancellable set, the same way an out-of-scope order is.
There is nearly always a filled or conflicted order in the book when things have
gone wrong, so refusing on account of one would make the command unusable in the
situation it exists for. The cancellable set is decided up front, and either all
of that set is cancelled or none of it is.

ONE RULE, READ TWICE. The feasibility pass and the acting pass would drift if
each had its own idea of what a cancel would do, and a plan that disagrees with
the action is worse than no plan. `disposition_of_cancel` is the single place
that decides, extracted from `apply_request_cancel`, which now reads it too.
Only the aggregate constraint - outbox capacity - is checked across the whole
set; checking per order is precisely what produces a half-finished mass cancel.

THE AUTHORITY SPLIT IS DELIBERATE. The control lane admits mass cancel and
requires an operator caller to do so; the reducer does NOT restrict it to
operators. Mass cancel only reduces risk, and requiring operator authority to
reduce risk is the asymmetry error avoided everywhere else in this phase. Anyone
may flatten the book on the normal lane. What an operator caller buys is the
right to spend the kill reserve doing it.

Appending a `KernelCommandKind` is additive - kinds are tagged by explicit value
rather than by variant index - so no existing command's bytes changed and no
corpus digest moved. An unknown scope decodes as a refusal rather than as `all`,
and a scoped request naming no account is refused at encode time: a zero account
under a scope of `account` would reach every order whose proposal also carries
none, which is the broadest possible reading of the narrowest request.

Mutation-confirmed four ways. Also fixed, and found by a sanitizer-build
warning rather than a test: the model-replay visitor fell off the end of a
non-void lambda for the new variant, which is undefined behaviour and not a
style nit.

#### Resuming normal trading now requires reconciliation — 2026-09-16

G-059, and specifically the half that is actually dangerous. A kill is easy;
UNDOING one is where the damage gets done. The moment after an emergency is when
the cluster's picture of its own exposure is least trustworthy - usually that is
why the kill happened - and resuming normal trading then can double a position
nobody has confirmed.

A transition into `normal` is refused with `reconciliation_required` while any
order sits in `submission_unknown`, `reconciling` or `conflict`: the three states
that mean the cluster cannot say what its exposure is.

Two boundaries are what make this a safety rule rather than a deadlock. It is
ONE-WAY - reducing authority is never blocked, or an unreconciled cluster could
not be halted, and a rule that prevents halting has become the thing it exists to
prevent; a kill in particular must always work. And only the unknown states block
it: a healthy resting order is a known quantity, and treating it as a reason to
stay halted would make the rule mean "never resume while trading", which the
first operator to meet it would route around.

Mutation-confirmed three ways - removing the gate, applying it to every mode
rather than only to `normal`, and narrowing it to `conflict` alone.

#### The control lane checks its label, and leadership is not promotion — 2026-09-16

A HOLE OPENED BY THE PREVIOUS COMMIT, closed here. `ProposeLane` is chosen by
the caller, so naming the control lane was not the same as being entitled to it.
Two ways that made the reservation meaningless: label an ordinary command
"control" and spend the kill path's reserved blocks on strategy traffic; or
offer a real control command from an unauthenticated caller, which the reducer
refuses - but only after the entry has consumed reserved capacity, committed and
replicated. A refusal that costs the resource it was protecting is a denial of
service on the kill path.

`propose` now decodes and checks before the entry exists: the right kind of
command, from a caller in the operator band. Bytes that do not decode are not a
control command either, because the lane cannot take the caller's word for what
it is carrying. That costs one decode on a path taken once per operator action.

G-053: LEADERSHIP IS NOT PROMOTION. The dangerous reading of "the leader is the
gateway" is that whoever wins Raft has become the execution authority - dangerous
precisely because leadership changes on its own, from a timeout or a restart,
while the old gateway may still be alive and sending. `no-auto-promotion`
promotes once deliberately, then removes and restores leadership three times,
requiring the executor generation to be unchanged on every voter after each, and
across a restart. It also requires leadership to have actually moved, or
"leadership changed and nothing promoted" is a claim about an event that never
happened.

Mutation-confirmed against the REALISTIC unsafe design - promote once per
leadership term - after a cruder mutant was killed by the wrong assertion.

#### Lease renewal, operator authorization, and the control lane — 2026-09-16

THE RENEWAL PATH IS WHERE THE LEASE BINDS. A lease any read could renew would
expire only when the process stopped, which is the one case needing no
protection. `renew_authority` renews only on an AUTHORITATIVE read, so a gateway
that cannot reach a leader gets no renewal and lapses by itself. Two of its
refusals matter more than they look: a backwards tick does not renew, because a
clock that moved backwards is the worst thing to extend authority on; and
renewal adopts the authority it confirmed rather than only the time, since
refreshing the clock while keeping the old generation leaves a gateway holding
authority that looks fresh and is stale - worse than not renewing at all.

OPERATOR AUTHORIZATION, WITH NO NEW BYTES. The reducer cannot verify a
signature: G-066 keeps key material out of replicated state and `apply` performs
no I/O, so authentication is necessarily an edge property. The cluster's job is
to refuse a control action from a caller the edge never authenticated, and to
record which operator issued the one it accepted. `kMaxOperatorCallerId` reserves
a `CallerId` band; `CommandIdentity` already carries the caller and a digest
binding the payload. `ApplyCode::unauthorized_control` is appended at the END of
the enum, so every existing code keeps its value and no golden byte moves.

The refusal lands before the receipt lookup, so an unauthorized control command
leaves no trace. Retaining a receipt would make a replay of the same bytes return
a completed action rather than the refusal - the one confusion an audit of a kill
cannot afford.

THE DEPLOYMENT OBLIGATION: the edge must never issue a reserved CallerId to a
strategy. The reservation is one-directional by design.

THE CONTROL LANE. `propose` had no admission control at all, so a strategy flood
filled the staging arena and the command that would stop the flood was the one
that could not get in. `ProposeLane::control` may draw on
`kControlLaneReservedBlocks` that nothing else may touch. The check sits in
`propose`, not the arena: by the time `log_offer` stages, Raft has already
assigned the entry an index, and before the entry exists is the only point at
which turning it away is free. It is a MARGIN, NOT AN EXEMPTION - control is
refused too once the reserve is gone, because unbounded admission is exactly what
backpressure exists to prevent (G-045).

A VACUOUS ASSERTION FOUND BY MUTATION. The lane test first checked that one
control command got through after the normal lane closed. That proves almost
nothing: whatever block was open still had room, so one more record fits whether
or not anything was reserved - and shrinking the margin to zero left it green.
The test now MEASURES what the control lane absorbs and requires it to exceed a
single block's payload, which only real reserved blocks can supply.

Three pinned digests moved, all from one reviewed cause: the replay corpora
issue control commands, so their caller moved into the operator band and every
generated command's identity changed with it. The generators, distributions and
reducer are untouched, and each suite's coverage assertions still hold - which
is what makes that claim checkable rather than asserted. The codec corpus was
NOT re-pinned: its caller was already inside the band.

Also closed, and found by a compiler warning rather than a test: the new
`ApplyCode` was missing from the codec's and the snapshot's `known()` switches,
so a refusal could not survive a canonical round trip. Now covered by a
round-trip assertion that fails when either list is incomplete.

#### The authority lease and the reserved cancel lane — 2026-09-16

First slice. Both mechanisms are ENFORCED and neither is yet DRIVEN - the
refusal exists, the thing that would renew or trigger it does not - so no
Phase 8 guarantee is ticked. The checklist records what each one still needs.

THE LEASE IS WHAT GENERATION FENCING CANNOT DO. Comparing the gateway's cached
authority against cluster state only helps a gateway that can still READ the
cluster. A partitioned one keeps believing its last good answer indefinitely, so
the fence protected against a gateway that knows it was superseded and not
against one that cannot find out. `GatewayControlView` now carries
`confirmed_at`, `lease_bound` and `now`, and a risk-increasing send is refused
with `authority_lease_expired` once the cache is staler than the bound. Time is
a parameter, not a clock read, so the guard stays pure and a test can place the
gateway anywhere in the lease without waiting.

Fail-closed, and that is the reason it is a function rather than a comparison at
the call site: a zero bound is not "no limit" but "no authority", and a view
claiming confirmation in the future earns no credit. Making it fail-closed
silenced every send in the delivery suite until its fixture declared a lease,
which is how the enforcement was confirmed to reach every path rather than only
the new tests.

A CANCEL IS EXEMPT, deliberately. A lapsed lease means this gateway may no
longer be the authority, and the safe response is to stop opening exposure, not
to stop closing it. Refusing cancels would trap risk at exactly the moment an
operator most needs it gone.

THE RESERVED LANE. `SendQuotaView` gained `emergency_lane_remaining`, drawable
only by risk-REDUCING effects. The asymmetry is the reservation - a lane both
kinds can spend is one lane with two names. Admission is by the OPERATION'S
IDENTITY, cancel yes and submit/replace no, rather than by arithmetic on the
effect's fields: a `replace` can be economically reducing, but proving it means
comparing against the resting order, and a lane whose safety rests on a
comparison fails whenever the comparison is wrong. A reducing replace waits for
normal capacity, which is the right price.

Four properties, each mutation-confirmed against its own assertion: a lapsed
lease stops new risk and the boundary tick itself does not; an unconfigured
lease is expired rather than unlimited; a cancel survives both an exhausted
normal lane and a lapsed lease; a submit cannot draw on the reserved lane.

### Phase 7 in development - durability and bounded memory

#### The first Phase 6/7 NUC gate run, and what it caught — 2026-09-16

FAIL at `configure`, with a FATAL_ERROR whose body was EMPTY:

```
failed to apply the pinned raft durability patch against
e428eeb921a014192d1d703dd317f3f29f5916c5:
```

`patch` is not in the container image. The Raft fork is applied at configure
time rather than vendored, Ubuntu's minimal base plus `--no-install-recommends`
does not carry `patch`, and no gate had ever needed it because the fork is newer
than the image. Added to the Containerfile.

The blank message is the more interesting half. `execute_process` reports a
program it cannot find through `RESULT_VARIABLE` and writes nothing to the
output, so a MISSING TOOL and a REJECTED HUNK are indistinguishable - and the
obvious reading of a blank patch error is that the patch did not apply, which
sends you to the diff and the zero-context hunks instead of to the image. The
tool is now resolved with `find_program` and refused by name, and the apply
failure reports patch's exit status alongside its output.

Worth recording: the hunks were never the problem. GNU patch 2.7.6 applies them
cleanly against the pinned commit on the target host; the development host runs
GNU patch 2.5.8, and the two agree here.

This is the first time the fork has been exercised on Linux at all, which is
what the target gates are for.

#### Phase 7 guarantees closed, and G-051 with them — 2026-09-16

G-042 through G-050 closed against named tests, with two honesty notes rather
than clean claims: G-043 holds for the STEADY STATE only, because truncation and
compaction quiesce the writer and rewrite synchronously; and G-046's
valid-prefix policy is "refuse", reversing the earlier clamp.

G-051 WAS FOUND OPEN WHILE WRITING THAT SECTION. `ReplicatedKernel` reuses V1's
`MetadataStore` and `WriteAheadLog` - correct, they are payload-generic - but
both wrote `metadata.bin` and `raft.wal` with the same magic and storage version
constants. A V2 node pointed at a V1 directory therefore ACCEPTED V1's hard
state, because the values it validates were the values V1 wrote, and failed only
later and incidentally when a V1 payload reached the V2 codec. Nothing stopped a
V1 node reading V2 storage.

V2 now uses `v2-metadata.bin` and `v2-raft.wal`. Distinct names rather than a
magic discriminator: the guarantee is stronger - the other format is not
rejected, it is not consulted - and NO PERSISTED FRAME'S BYTES CHANGE, so this
is not a byte-contract change. The name is a defaulted constructor parameter
with V1's name as the default, so V1 is unchanged by construction.

Held by `v1-v2-isolation`, which starts a V2 node on a populated V1 directory,
requires it to begin empty, then lets it run and requires V1's files to be
byte-unchanged - isolation that works by clobbering the other milestone's data
is the destructive migration the guarantee names. Restoring the shared names
fails it.

MIGRATION: an existing V2 data directory is not read under the new names. There
is no V2 production data, so no tool is provided; a Pi or NUC holding a Phase
6/7 scratch directory should have it removed.

AND A GATE-COVERAGE NOTE this exposed. `include/velocita/storage.hpp` and
`src/storage.cpp` are outside the source-root manifest, yet V2 links against
them - as this change demonstrates. Changing them does not move the pinned root.
Defensible while V1 is separately gated, but the Phase 6/7 root does not cover
all the code Phase 6/7 runs, and that should be reviewed before the root is used
as an acceptance identity again.

#### First NUC PASS, and the two warnings it printed — 2026-09-16

`V2 Phase 6/7 replication and durability acceptance target gate: PASS`, 34 / 31
/ 31, `trading_state_size_bytes=81360` against the 81,920 budget and
`state_snapshot_max_canonical_bytes=65451` against 65,536 - so the x86-64 layout
still fits, as it did for Phase 5.

GCC 15 printed two warnings the development host's clang does not, and both are
now gone rather than tolerated:

- `-Wunused-result` on `ftruncate` in the flush rollback. `(void)` does not
  satisfy `warn_unused_result`, and the reason the result is ignored was
  nowhere. It is ignored because the frontier must be rolled back whether or not
  the truncation succeeds: those bytes were never synced so nothing may rely on
  them, and leaving the frontier where the failed write left it would guarantee
  duplicate records on a retry. Now stated where the decision is.
- `-Wfree-nonheap-object` in `SnapshotStore::save`. A false positive about the
  `std::vector` reallocation path - `kHeaderBytes` was exactly what the
  push_backs wrote, so no reallocation could ever occur, but nothing said so and
  GCC cannot prove it. The frame is now sized once and written BY OFFSET, with
  the header length derived from the field offsets rather than asserted as 34,
  so changing the frame fails the build instead of silently under-reserving.

The second change touches a persisted frame, so it was checked as a byte
comparison rather than by the suite passing: the same state produces a
byte-identical 143-byte frame before and after, and both load. A round-trip test
would have agreed with itself either way.

#### And the gate runs read-only, so the fork moves to image build — 2026-09-16

The second NUC run got past the missing tool and failed on the thing the tool
would have done:

```
File include/raft.h is read-only; trying to patch anyway
/usr/bin/patch: **** Can't create temporary file include/raft.h.ocsCbaq :
Read-only file system
```

The gate runs `podman run --read-only` with the source at `/src:ro`. Those are
deliberate - a run must not be able to modify what it is validating - and the
pinned dependency is baked into the image, so it is read-only too.
Configure-time patching worked on a development host and could never have worked
here.

The fork is now applied in the Containerfile, which also writes the stamp
CMakeLists.txt reads in place of patching. The reviewed digests stay in
CMakeLists.txt and nowhere else, and the stamp branch now checks against them:
it previously compared the stamp only with itself, so any image could have
declared its own content correct. A stale image built from an older patch fails
that check rather than being used.

Both branches - apply and stamp - are now verified by perturbing an expected
digest and confirming configure refuses.

#### The patch digest check was recording, not checking — 2026-09-16

The pinned Raft fork's stamp records the commit, the patch digest and the
SHA256 of both patched sources, and refuses to build on a mismatch. But only on
the branch that finds an EXISTING stamp. The branch that APPLIES the patch -
taken by every first configure on a clean machine, which is every container gate
and both physical targets - recorded the resulting digests without ever
comparing them to the reviewed values.

That matters more than it looks, because the patch carries zero context: every
hunk applies by line number alone. `patch` exiting zero says the hunks were
placed, not that they were placed where the review put them, and a source
differing from the pinned commit by one line can absorb them silently into code
that compiles and is wrong. The one check that would catch it was inert on the
one path where it was needed.

The apply branch now compares against the expected digests and fails FATAL,
naming both values and saying not to update them to make the build pass.
Verified by perturbing an expected digest and confirming configure refuses.

#### Phase 6/7 gates, pinned — 2026-09-16

`deploy/run-v2-phase7-container-gate.sh` (ARM64) and
`deploy/run-v2-phase7-nuc-gate.sh` (x86-64), both pinned to source root
`543eb468167ed51dd8c918d8de356c7028dec736a9f223fe1618f5e40b83c14d`.

Phase 5's gates asked a layout question. These ask a durability and scheduling
one, which is the part least trustworthy on the development host: `fdatasync`
rather than `fsync`, a real page cache, a real scheduler behind the writer
thread, and directory syncs that mean something. Expected suite counts are
34 / 31 / 31.

The pin travels inside the tree, so it proves the tree matches its own pin and
nothing more. The external comparison stays an explicit operator step; it has
already caught a PASS against a superseded candidate.

Not yet run. `docs/NUC_OPERATOR_RUNBOOK.md` section 7 carries the procedure.

#### Corrections from an independent review — 2026-09-16

An independent review of Phases 6 and 7 found five defects. The RED evidence is
in `docs/evidence/V2_PHASE6_7_CORRECTION_RED_GREEN.md`; each case failed against
`8f66dca` before the fix.

THE DURABLE INDEX ADVANCED BEFORE THE BYTES WERE SYNCED. `WalSegment::flush`
wrote each block, then called `arena.complete()` on it, and synced only after the
loop. `complete()` is what raises the durable index, and the durable index is
what this node reports to Raft for its own quorum vote - so a crash between the
write and the sync left this node having already counted entries that were not on
disk. The whole point of Phase 7's gate, defeated inside the function whose own
comment described the correct order. Flush now writes every block, syncs, and
only then completes; a sync failure rolls the batch back rather than leaving it
half-credited.

MEMBERSHIP WAS LOST ACROSS A SNAPSHOT LOAD. Loading a Raft snapshot clears the
node table, and nothing restored the configured peers, so a node that recovered
from a snapshot came back believing it was alone. Now restored from the
configured membership after load, covered by an independent suite
(`v2-snapshot-membership-tests`). No byte change.

TRUNCATION AND COMPACTION RACED THE WRITER. `log_pop`, `log_clear` and `compact`
rewrote or shortened the segment while the off-thread writer could still be
draining into it. Each now quiesces the writer, flushes, rewrites the segment
atomically (temp, sync, rename, sync the directory), lowers the durable
watermark to the surviving prefix, and restarts the writer if it was running.
This is a rare synchronous maintenance barrier and is stated as one: the claim is
an asynchronous STEADY-STATE write path, not a fully non-blocking owner loop.

A DURABILITY ADVANCE DID NOT RECONSIDER QUORUM. The pinned patch gated the
leader's own vote on its durable index, but nothing re-evaluated the commit index
when durability later caught up - so with peers already acked, an entry could sit
uncommitted until the next unrelated event. `raft_periodic` now re-walks the
window between the commit index and the safe point, keeping Raft's current-term
restriction and the NULL-callback path byte-identical. Held by
`v2-durable-quorum-tests`, which drives willemt directly rather than through
`ReplicatedKernel`; removing the re-evaluation leaves the replication suite green
and fails only this one.

A RECOVERED COMMIT INDEX PAST THE END OF THE LOG NOW REFUSES, reversing the
clamp introduced on 2026-09-15. The power-cut sweep that motivated clamping
truncates the segment EXTERNALLY, which cannot happen while the durability
contract holds - so clamping taught the node to tolerate a broken disk silently,
on the single-voter shape that has no peer to re-supply what was lost. Recovery
now names the condition and leaves the files untouched for diagnosis. The cost is
accepted and stated in ADR 0006's amendment: such a node does not start without a
human, and the revisit condition is peer snapshot transport, which would let a
FOLLOWER rejoin by re-fetching instead.

EVERY ONE OF THESE FIXES WAS THEN MUTATION-CHECKED, and three of the six had no
test that failed when the fix was reverted - including the flush-ordering defect,
which is the most serious of the set. Reverting a fix and finding the suite still
green is the only way to learn that; the table and the three new tests are in
`docs/evidence/V2_PHASE6_7_CORRECTION_RED_GREEN.md`. Briefly:

- the durable index is now read from inside the sync, through the
  `before_file_sync` checkpoint, because a crash cannot expose this bug at all -
  `_exit` does not drop the page cache, so the file looks identical either way.
- the power-cut sweep now asserts the REASON recovery refused, not just that it
  did. Without the explicit check the node still refuses, via an unrelated replay
  failure with an empty cause - a refusal that tells an operator nothing, which
  defeats the point of refusing.
- `ReplicatedKernel` gained an optional `StorageCheckpoints` seam, defaulted off
  and carrying no behaviour, so a test can PARK the writer mid-drain and deliver
  a replacing suffix while it sits there. The truncation race was previously
  "covered" by a test that ran both threads and hoped; the mutant survived forty
  consecutive runs of it and is now killed five times out of five.

THE PATCH STAMP NOW PINS WHAT IT PATCHED. The build records the Raft commit and
the SHA256 of the patch and of both patched sources, and fails with a
FATAL_ERROR on any mismatch, so a fork that drifts cannot be built against
silently.

#### Durable snapshots, compaction, and an asynchronous write path — 2026-09-15

Phase 7a: `SnapshotStore` frames and persists the canonical state snapshot the
reducer already defines, adding durability and no economics. Durable strictly
before reclaimed - fsync, rename, fsync the DIRECTORY, and only then poll entries
away - so a crash leaves a snapshot plus an overlapping log rather than neither.
A corrupt snapshot degrades to replay instead of refusing to start.

Phase 7b: `WalBlockArena` stages records in fixed blocks and `WalSegment` drains
them with `pwrite` plus one sync per drain. `log_offer` now stages and returns,
so the owner loop no longer pays an open/write/fsync/close per record. A record
never straddles a block; every slot reuse mints a generation so a late completion
cannot release a recycled slot; the durable index advances in order only; and a
stalled writer produces bounded refusal rather than overwriting bytes that are
not yet durable. Two guards were checked by mutation rather than assumed.

The persisted commit index is clamped to the durable index. willemt counts the
leader's own vote unconditionally, so its in-memory commit index can name an
entry this node has staged but not synced; persisting that would leave a restart
reading a commit index past the end of its own log, which the constructor
refuses. Clamping on the WRITE side is what makes that refusal meaningful: with
it, the only way to reach that shape is for the storage to have lost data it
acknowledged, so the refusal names a broken disk rather than a normal crash.

#### The pinned Raft fork — 2026-09-15

The property left open above is now enforced. `deploy/patches/0001-raft-durable-quorum.patch`
adds one OPTIONAL `durable_idx` callback to willemt/raft and uses it in the two
places that count the LOCAL node without consulting durability: the
`votes = 1` that starts the leader's quorum count, and the immediate commit for
a single voting node. Neither was reachable through the public API - there is no
setter for one's own match index. A peer's durability was never the problem,
since its append-entries response already reports it.

When the callback is NULL the behaviour is byte-for-byte as before, which keeps
V1's node unaffected on the shared target. The patch is applied at configure time
rather than vendored, so the upstream pin stays the source of truth and the delta
stays reviewable; failure to apply is FATAL, because silently building unpatched
Raft would silently restore the defect.

AND A VACUOUS TEST, WORTH RECORDING. The first version had `tick` flush and
asserted `commit <= durable` after every tick of every scenario. It passed - and
it also passed with the callback unwired entirely. The harness closed the gap on
every tick, so the window the invariant describes never existed at a moment
anything could observe it.

`flush_durability` is now separate from `tick` and public, so the owner loop
decides when the disk happens. The real test withholds it: fifty ticks with no
flush, asserting nothing commits, no result appears, and the entry really is in
the log so something is actually being withheld - then flushes and requires the
same entry to commit, so the gate is a gate rather than a deadlock. Both
mutations fail it now.

TWO LIBRARY BEHAVIOURS THAT COMPOSE INTO DATA LOSS, found and fixed in 7a.
willemt promotes a lone voter to leader without campaigning, leaving term zero,
which Raft has no leader for; entries then inherit term zero, and
`raft_begin_load_snapshot` rejects a zero term outright. A single-voter cluster
could therefore snapshot, reclaim the prefix, and silently fail to install the
snapshot on restart. A leader's term is now raised to one.

#### Off-thread sync, and preallocated segments — 2026-09-15

`WalWriter` moves the pwrite and the fsync to another thread. The owner owns the
arena - staging, sealing and applying completions all stay on its thread, so
every mutation is single-threaded and lockless; the writer only reads the bytes
of a SEALED block, which the generation guard keeps stable for exactly as long
as it holds the descriptor. `SpscRing` carries descriptors each way and refuses
when full, because an overwritten descriptor is a block whose bytes never reach
disk while the arena believes they have. One sync per pass, completions
published only after it.

The thread is opt-in, so every existing test stays deterministic and the threaded
path is demonstrated rather than assumed. Verified under ThreadSanitizer: the
ring alone across a 200,000-entry handoff, and the replication suite with the
writer running.

Segments preallocate ahead of the frontier, since extending a file is a metadata
operation the filesystem may journal. That broke the resume logic in a way worth
recording: resuming at `file_size / block` is correct only while file size means
"end of data", and reserving ahead ends that - a segment with one block written
and 2 MiB reserved would resume 2 MiB in, leaving a hole recovery reads as the
end of the log. The frontier is now found by walking, which also overwrites a
torn tail for free.

`WalMetrics` reports blocks submitted against blocks durable, backpressure
refusals, and staged index against durable index - the exposure asynchronous
durability buys, as a number.

#### Phase 7 complete: follower acks, a power-cut sweep, and a measurement — 2026-09-15

A FOLLOWER'S ACK WAS THE OTHER HALF OF THE LEADER'S DEFECT. The leader turns a
follower's reported `current_idx` straight into that follower's match index and
counts it toward quorum, so a follower acking entries it has only staged is a
leader counting an undurable copy one hop away - and the Raft patch cannot fix
it, because the number is produced on this side of the wire. `receive_peer` now
clamps it to the follower's own durable index. Pinned by a three-voter test in
which only the leader flushes; removing the clamp fails it.

THE POWER-CUT MATRIX IS NOW A SWEEP, truncating the segment at every
structurally distinct offset through the written frontier. It found two defects
on its first run, both of which three hand-picked durability points had been
green over:

- a recovered commit index past the end of the log. A cut that loses the tail
  leaves exactly that shape - hard state that was durable naming entries that
  were not. This was first "fixed" by clamping, then reversed: see the
  2026-09-16 entry and ADR 0006's amendment.
- ONE 64 KiB BLOCK PER RECORD. `flush_durability` seals on every call, correctly,
  so a quiet system writes a block per record and a 200-byte command cost 64 KiB.
  The block is now 4 KiB, page-sized, with a static assertion tying it to the
  largest canonical command frame so growing the command budget fails the BUILD
  rather than silently refusing records at runtime.

THE MEASUREMENT, now that correctness and power-cut evidence pass. Owner-loop
time over two thousand command-sized records on the development host:

| | synchronous | off-thread |
|---|---:|---:|
| median | 21.6 us | 0.042 us |
| p99 | 109 us | 208 us |
| max | 7136 us | 358 us |

The median is the objective; the p99 is the honest part. Off-thread is ~500x
faster at the median and 20x better at the worst case, and WORSE at p99 - under
sustained load faster than the disk the owner waits for a block instead of an
fsync. The design converts an unbounded per-record stall into a bounded
queue-full one; it does not make the disk faster. The benchmark's first run also
found a sizing error: shrinking the block sixteen-fold had shrunk the staging
budget with it, refusing four fifths of records. The arena is now 64 blocks.

These are a laptop's figures. No claim is made for the Pi or the NUC until the
gates run there.

Phase 7 is complete in implementation scope and in its test-first list. Four ADRs
record the material decisions taken since the last takeover: 0003 native and
synthetic observation identity, 0004 MVP V1 excludes perpetuals, 0005 the reducer
behind consensus, 0006 asynchronous durability and the pinned Raft fork.

Host Release 32/32, Debug ASan+UBSan 32/32, TSan clean, model 109/109.

### Phase 6 in development - Raft Decision/OMS integration

#### The reducer placed behind consensus — 2026-09-15

`ReplicatedKernel` puts the deterministic V2 reducer behind willemt/raft,
reusing V1's payload-generic storage, peer wire and checksum layers unchanged.
V1's `RaftNode` is untouched, so its accepted gates stay valid; what is
duplicated is the Raft callback table and nothing economic.

The reducer did not change. It gains nothing, loses nothing, and does not know
it is replicated - which is why the same deterministic suite passes on one voter
and on three. `ReplicatedKernel` has exactly one call to `apply()`, in the Raft
apply callback, so G-035 is the shape of the code rather than a rule it follows.

Nine properties proved in `tests/v2_replication_tests.cpp`, all driving the
economic fixture through PUBLIC COMMANDS ONLY - strategy mode, risk limits,
policy publish and validate, executable-policy stage and activate, source event,
decision evaluation and authorization - so reaching an outbox effect at all
proves the full economic path survives proposal, replication and commit:

- an admitted-but-unrouted command reaches no voter's reducer, then takes effect
  exactly once when it commits;
- a follower refuses a command and names the current leader, and the committed
  result carries the original command identity back;
- a leader crash before commit leaves no decision and no outbox effect;
- a leader crash after commit replays to a byte-identical state with the same
  effect identity;
- a retry of an identical command returns the retained result without advancing
  the state machine;
- quorum loss creates no new risk on any voter, with a non-vacuity retry after
  healing, since an uncommitted entry from a deposed leader may legitimately be
  truncated and a client retries;
- a restarted voter recovers state, outbox and dedup by replaying its committed
  prefix; and
- all voters converge on the canonical snapshot digest, commit index, applied
  index, outbox watermark and state version.

G-035 through G-040 are closed. G-041 is closed as the statement it always was:
three voters in one process prove process-fault and partition behaviour and
nothing about host failure, which is what ADR 0002's two-machine topology is for.

A LIFETIME BUG FOUND AND FIXED. `log_append_entry` in willemt/raft copies the
entry struct, so only the `data.buf` pointer - the caller's buffer would have to
outlive the log entry, and a proposal's buffer cannot. It surfaced immediately as
`committed entry did not decode`, because a V2 command is a canonical frame with
a length and a digest and corrupt bytes fail loudly rather than producing a
plausible wrong value. `ReplicatedKernel` now owns every payload the log can
reference, keyed by log index and bounded by the log. The same pattern appears in
V1's `RaftNode`; a probe did not reproduce a failure there and V1 passes under
AddressSanitizer, so it is recorded as worth a look rather than a confirmed
defect.

Host Release 30/30, Debug ASan+UBSan 30/30 non-recovering.

### Phase 5 corrected acceptance - venue simulator and execution gateway

#### MVP V1 excludes perpetuals; synthetic restart proof tightened — 2026-09-15

MVP V1 will not trade perpetuals. No liquidation, ADL, funding, leverage, or
maintenance-margin bytes were added to the reducer. The verified True Markets
DeFi surface is therefore not a V1 execution target. The ledger-first design for
separate external executions and funding settlements is recorded in
`V2_DEFERRED_FUNCTIONAL_SCOPE.md` with a hard trigger before any later perpetual
connection. A single generic position observation was rejected because it would
conflate position and cash mutations and overload an order/effect-centric byte
contract.

Corrected the synthetic-replay explanation: stable command identity returns the
original result while an evidence obligation is retried; a fresh command about
an already resolved effect is refused by state-based applicability rather than
applying a cumulative economic delta. Existing tests separately cover the
identity replay, fresh-identity refusal, startup scan, and reconciliation. A
single integrated restart characterization was considered but deferred: it
would change the frozen gate source root without correcting production behavior,
forcing all target gates to be repeated for an oracle-only improvement. No
production behavior, test source, or encoded byte contract changed.

#### Phase 5 accepted on three environments, Pi gate carried — 2026-09-15

Phase 5 is ACCEPTED at source root
`681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f` on the
development host, the ARM64 Linux container, and the x86-64 NUC. G-025 through
G-034 are closed. `docs/evidence/V2_PHASE5_CORRECTED_ACCEPTANCE.md` supersedes
the 2026-09-11 record, which is preserved along with its tag as the honest
history of a candidate a later review found defective.

| environment | result | Release | instrumented | sanitized |
|---|---|---:|---:|---:|
| host (macOS, arm64) | PASS | 29/29 | - | 29/29 |
| ARM64 Linux container `20260915T134221Z` | PASS | 29/29 | 26/26 | 26/26 |
| x86-64 NUC `20260915T134322Z` | PASS | 29/29 | 26/26 | 26/26 |
| x86-64 NUC `20260915T154328Z` | PASS | 29/29 | 26/26 | 26/26 |

The NUC ran twice, from two accounts and two independently synced trees on the
same machine, which shows the result does not depend on leftover state in the
tree that produced it. Each gate computed the pinned root itself. The bounded layout is byte-identical
across both architectures: `TradingState` 81,360 of 81,920, maximum canonical
snapshot 65,451 of 65,536, `ApplyResult` 112, `ClusterOrder` 48. UBSan compiles
non-recovering with `halt_on_error=1` on both targets, and the negative probe
diagnoses a signed overflow and exits nonzero. The model corpus is 109/109 and
untouched.

ACCEPTED WITH ONE CARRIED OBLIGATION, recorded rather than dissolved: the
physical Raspberry Pi 5 gate has not run against this source, because the machine
was offline. The ARM64 container gate runs a real aarch64 kernel, so the logic
and the layout are already proven on ARM64; what the Pi uniquely adds is physical
hardware and clean power evidence - `throttled=0x0` before and after, plus
temperature, since a throttled Pi measures a different machine than the one that
will trade. The root is frozen, so that run will test exactly this source. If it
fails, the acceptance is void and Phase 5 reopens.

The exception is scoped to the Pi's unavailability. It does not license accepting
a future phase on partial target evidence, and the MVP boundary is unchanged: no
real venue connectivity, no strategy logic, no netting, no WAL implementation,
and no full Raft integration.

A GATE-LEVEL FALSE GREEN, found and recorded on the way. An earlier NUC attempt
from a second account, run `20260915T152553Z`, reported PASS against the
SUPERSEDED candidate: that account's tree had never received the corrected
source. The gate was correct to pass it, and that is the point -
`VELOCITA_EXPECTED_SOURCE_ROOT` lives in the gate script, which travels inside
the tree, so a stale tree carries its own stale pin and the two agree. A PASS
proves this tree matches its own pin; it never proves this is the candidate you
meant to test. Only comparing the gate-computed root against a value held outside
the tree distinguishes them. The sharpest staleness signal turned out to be the
ABSENCE of the `ubsan_*` metadata keys, because a stale gate script does not
print them wrong - it does not print them at all.
`docs/NUC_OPERATOR_RUNBOOK.md` now makes the external comparison an explicit step
and tells the operator to check for those keys' presence.

#### Native and synthetic observation identity domains — 2026-09-15

Approved material contract, implemented RED/GREEN. Every `VenueObservation`
carried a `(VenueId, VenueSessionId, VenueSequence)` triple and the reducer
deduplicated all of them on it, but only the venue's own events own one.
`effect_unknown`, `order_reconciled` and `cancel_reconciled` are authored by the
gateway; whatever sequence reached the wire was chosen by the shell, and nothing
coordinates that choice with the venue's counter.

Two reachable REDs pinned, in both directions: a native cancellation refused
`venue_event_identity_conflict` because a gateway-authored `effect_unknown` had
taken its sequence, and a reconciliation answer refused because the venue had
once used the number the shell put on it. The second case strands the order,
because that answer is the only thing that can resolve an unknown effect.

`ObservationOrigin` is derived from the observation kind by a total switch and
retained in venue-event result metadata word 1, the word left free when
proof-carrying reclamation put the owning `EffectId` in word 0. Sequence
deduplication now searches the native domain only. A synthetic retry is protected
by retained command identity, while a fresh command is adjudicated by the
effect's current state and the reconciliation total. Fills are unchanged: their
authoritative economic identity is `(VenueId, ExecutionId)`.

The simulator acknowledges scheduled observations by canonical fingerprint rather
than by the triple, so a synthetic observation can no longer retire a native one
the cluster has not committed. The decoder validates word 1 as an exact enum
value, because the runtime accessor resolves an unrecognised word to `native` and
a forged word would otherwise move a gateway-authored receipt into the venue's
identity domain.

Measured rather than estimated: zero bytes added. `ApplyResult` stays 112 bytes,
`TradingState` stays 81,360 with 560 bytes of headroom, and the maximum canonical
snapshot stays 65,451. Retained result values change, so kernel-codec and
state-snapshot schemas move V6 to V7 and the golden corpora were regenerated as a
reviewed migration. Host Release 29/29.

Behaviour change worth recording because a test had pinned the old outcome: a
synthetic answer re-presented under a fresh identity after its effect has
resolved is now refused `venue_observation_not_applicable` rather than
`already_applied`. The old refusal asserted that an identity was taken; the new
one asserts a fact about the order.

#### Reopened acceptance correction review — 2026-09-14

Reviewed correction commits `65cadc2..5b9eaa2` from Git after the previous
author's session ended without a new handoff. Host Release 29/29, full Debug
ASan+UBSan 29/29, and model 109/109 reproduced on the inherited tree. These are
baseline results, not acceptance: permanent counterexamples added afterward
intentionally reopen the focused suite.

Added a public-command RED proving that the venue-event expiry watermark mixed
native venue sequences with gateway-authored reconciliation/control sequences.
The high synthetic values advanced the shared floor past a new native fill and
the kernel returned `venue_event_expired` despite a new `(VenueId, ExecutionId)`
inside the retained reservation. Removed that redundant fill-side expiry check;
fill deduplication and retention already use the economically authoritative
execution identity, so the session watermark added only an unsafe rejection
path. The focused gateway-delivery suite returned GREEN.

Added the corresponding fresh-cancel RED. It still returns
`venue_event_expired`, proving a fill exemption is containment rather than a
sound reclamation contract. The required replacement is proof-carrying
reclamation: ownership/economic liveness, effect settlement, and producer
acknowledgement of the committed result must all precede compaction. The proposed
canonical changes and rejected shortcuts are recorded at the top of
`docs/CURRENT_HANDOFF.md`; implementation is paused for the required material
contract review.

Approved and replaced the heuristic with proof-carrying reclamation. Schema V6
removes replicated venue cursors, records the exact observation `EffectId` in
the unchanged-size `ApplyResult`, adds monotonic command kind 18 for the stable
gateway to acknowledge committed result prefixes, and retains a venue-event
receipt until both consumer acknowledgement and effect settlement/supersession
are proven. Code 59 remains reserved for historical decoding and is no longer
emitted.

Replaced the receipt-count query barrier with
`ClusterOrder::last_evidence_state_version` and required an independent
`ordered_after_relevant_sends` venue-finality fact in addition to a complete
query window. This removes count ABA after reclamation and prevents an old but
complete query from releasing or conflicting current risk.

Made simulator delivery replayable until explicit post-commit acknowledgement.
Permanent crash-cut tests cover poll-before-obligation,
obligation-before-proposal, and apply-before-result; a replayed fill changes the
position exactly once. After 40 legal cancel/reconcile cycles the corrected
path retained 18/64 venue events instead of wedging at 64 after 31 cycles.
Host Release and full Debug ASan+UBSan pass 29/29. `TradingState` is 81,360 bytes with 560 bytes of
headroom; maximum canonical snapshot is 65,451 bytes with 85 bytes of headroom.

Repaired three remaining acceptance oracles. A resolved-and-swept order no longer
excuses a never-booked execution: the order must remain conflicted and its exact
venue/execution receipt must remain retained. Restart recovery no longer treats a
settled `incomplete` effect as an in-flight response and therefore cannot deadlock
its barrier on a non-deliverable effect. A replicated policy rotation proves a
new logical slot reaches authorization after eight conflicts and is refused by
`outbox_capacity_exceeded`; the bound is the sixteen-record submit-plus-reserved-
cancel budget, not the eight templates in one policy.

Made undefined-behavior diagnostics fatal in the shared target gate with both
`-fno-sanitize-recover=undefined` and `UBSAN_OPTIONS=halt_on_error=1`. A permanent
signed-overflow negative probe must emit the expected UBSan diagnostic and exit
nonzero before sanitized suites may run. Leak detection remains explicitly off
and is not conflated with UBSan termination.

Closed five further proof gaps found by adversarial review. Recovery completion
now follows current replicated outbox state when an effect becomes `incomplete`
after startup scanning, rather than allowing a stale process-local response flag
to deadlock the barrier. Gateway receipt reclamation pins every request above the
producer's acknowledged prefix, so a later acknowledgement command cannot expire
an earlier uncommitted gap; full-fill and conflicting-fill evidence retain their
exact execution and decision ownership across arena pressure.

Made the execution oracle independent of reducer result bytes. It samples the
risk-account position before and after every fill apply—even when the simulated
client result is lost—and attributes the signed economic delta to the execution.
The simulator separately records every execution when venue state changes, before
delivery scheduling. The matrix now rejects both a successful-looking fill with
no economic mutation and an actual venue execution that never entered replayable
delivery.

Disconnect no longer clears scheduled observations: reconnect changes the
identity of future events but is not proof that prior unacknowledged evidence was
consumed. Permanent tests cover query disconnect before poll, query disconnect
after poll/before acknowledgement, and a later send disconnect. Snapshot V6 now
validates proof-field relationships, rejecting cross-decision effect links and
evidence revisions older than retained applied observations. The native-versus-
synthetic event identity-domain correction remains open for explicit byte-contract
approval.

Added two bounded-liveness regressions around the producer acknowledgement. A
full 256-receipt arena can now admit the valid monotonic acknowledgement that
releases covered results; malformed or regressive acknowledgements have no
special lane. A live partially filled order also survives more than 256
acknowledged redeliveries of one execution without exhausting receipts. The
original applied receipt and one exact state-changing conflict receipt remain;
replay-only result receipts compact because their answers are reproducible.

Unified `fill_quantity_conflict`: every path now makes conflict sticky and
advances `last_evidence_state_version`. Snapshot validation consequently covers
both applied observations and state-changing fill-capacity/quantity refusals,
without inferring mutation from an ambiguous result code. Current-tree host
Release is 29/29 after a complete rebuild; target and sanitized reruns remain.

Phase 5 opened with a contract review rather than code, because sections 9
through 15 - the primary gateway contract - had never passed an approval gate
while sections 8.1 through 8.7 had. The review found thirteen contradictions or
gaps; five blocked implementation and are resolved in architecture section 9.1.

Resolved: "committed" collapses to "applied" until Raft arrives in Phase 6;
`claimed` and `attempted` are gateway-local rather than consensus state,
resolving a direct contradiction between sections 9 and 8.2; expiry is defined as
control-generation staleness, deleting a phantom field the contract required the
guard to check but `OrderEffect` never had; lowering is representation only and
never economics, closing the largest under-specified permission in the contract;
and the gateway is a separate process in deployment, tested in-process with the
transport interface as the seam, because credential isolation under G-066 is not
achievable inside the reducer's process.

Also recorded: an open obligation inherited from Phase 4, where section 3 demands
retention exceed horizons that section 8.4 says are unknown until Phase 5, so
Phase 4's retention policy cannot yet be shown to satisfy it; and a trap, where
`wire.hpp` carries an unwired Phase 1 message vocabulary that does not correspond
to `KernelCommandKind` and is not a transport.

First increment delivers the final pre-send guard of G-029 as a pure function
that reads no clock, mutates nothing, and returns a decision carrying the exact
canonical bytes when permitted and nothing at all when refused, so a refused
effect cannot be sent by construction. Release 26/26 and full host Debug
ASan+UBSan 26/26.

Second increment adds the deterministic adversarial venue simulator, test-only by
construction: it lives in its own CMake library that no production target links,
so the property is a build fact rather than a comment somebody has to honour. It
never reads a clock - every deadline is a caller-supplied tick, so a timeout test
is exact rather than slow, and a fault matrix replays identically on a loaded CI
box and an idle laptop. Every adversarial choice comes from a splitmix64 stream
keyed by the seed and the operation ordinal, and every roll consumes a draw even
at zero probability, so enabling one fault cannot reshuffle the others. All nine
G-033 fault classes are reachable, the five G-030 send outcomes stay distinct, and
a query's `not_found` stays distinct from `unavailable`. Release 27/27 and full
host Debug ASan+UBSan 27/27.

Third increment adds the delivery loop and the Phase 5 gate. `plan_delivery`
decides ambiguity BEFORE sends, so a tick can never both declare an outcome
unknown and blindly retry it. Two contract decisions were forced by writing the
tests and are recorded in architecture section 9.2. A resend now requires PROOF
that nothing reached the venue: `pre_send_failure` and `rate_limited` are the only
outcomes that permit one, asserted explicitly by `record_unsent`, and every other
transition leaves the effect unresendable because the venue may hold an order. The
first planner refused every resend, which was safe but wedged a logical slot on a
transient rate limit and would have sent reconciliation to query a venue that had
never heard of the order. Provably-unsent retries are bounded at three attempts;
on exhaustion the effect stays pending and visibly unsettled rather than being
called unknown, since nothing ever left the process.

`v2-gateway-delivery-tests` drives the real kernel, the real guard and planner, and
the adversarial venue through the fault matrix over thirty-two seeds, asserting the
gate's three absolutes: no duplicate venue order, no lost accepted fill, and no
unresolved state presented as healthy. Slot exclusivity is asserted directly - at
most one order on the fixture's single logical slot may ever hold unresolved risk.
The matrix asserts its own non-vacuity, so it cannot keep passing after it stops
reaching its hard paths.

Removed by that work: nothing. Added to the simulator: the `duplication` fault now
duplicates fills, not only acknowledgements. A duplicated fill is the economically
dangerous case, because booking it twice doubles the position and no downstream
reservation arithmetic can undo it; duplicating only the acknowledgement tested the
identity check on the case that does not matter. Twelve of thirty-two seeds now
exercise duplicate-fill suppression. Release 28/28 and full host Debug ASan+UBSan
28/28.

Fourth increment reviews section 10 - the reconciliation contract, in the same
never-approved block as section 9 - and builds the gateway-side half. Six
findings, recorded in architecture section 10.1.

Half the compare list does not exist: balances, settlement state, and currency
appear nowhere in state or in any contract, and are deferred rather than invented
so a comparison can be written against them. The gateway may not classify and the
reducer may not query, so the two are split along the line that already exists -
the gateway decides only whether it holds a trustworthy answer, the reducer
decides what a trustworthy answer means, and `QueryDisposition` has no way to
spell "adopt" so the split is enforced by the type system. `Incomplete` and
`Unavailable` are facts about the QUERY, not the order, so they are gateway-local
and never enter consensus. "Policy-approved Adopt" is derived from the existing
reservation fit rather than given new machinery, since two statements of the same
economics eventually disagree. Section 10 would let one bad order halt the whole
cluster, which contradicts Phase 4's accepted per-order conflict; the accepted
artifact wins and the contradiction is flagged.

Held for approval: a query answer has no venue event identity, and giving it one
is a material byte-contract choice. The recommendation is one new
`VenueObservationKind` carrying final cumulative quantity - zero new bytes, and
safety resting on cumulative-by-value idempotency rather than on identity, which
is the pattern `cancel_reconciled` already established. No code encodes it, so the
cluster-side half is not built.

Built and tested: `plan_reconciliation`, `classify_query`, and the query
bookkeeping. Three reconciliation needs, one of which - `delivery_exhausted` -
closes a hole the third increment's own retry bound opened, where bounding retries
would have traded an unbounded loop for a permanently wedged slot. A `live` order
with no fill is deliberately not a target: without a clock, "its fill was lost" and
"it has not traded yet" are the same observation. Reconciliation is fenced by
executor generation but not by a halt, because it originates no risk and a halted
cluster with a blocked slot is when resolving it matters most.

Sixteen of thirty-two matrix seeds end with a stranded slot, every one in
`submission_unknown`, and the planner engages on all sixteen. The matrix asserts
that bridge, so an order stranded in a shape the planner does not recognise fails
the build. Release 28/28 and full host Debug ASan+UBSan 28/28.

Fifth increment encodes the approved byte-contract decision and closes the
reconciliation loop. One new `VenueObservationKind`, `order_reconciled = 7`,
inside the existing `VenueObservation`: zero new bytes, no new command kind, no
new struct, no new lifecycle state, no new `ApplyCode`, and the frame stays 116
bytes. Purely additive - no existing golden changes and no existing kind is
touched. The model package needs no change and must not receive one: it
deliberately implements no `VenueObservation` and its safety interlock fails the
build if the name appears.

The safety argument is stronger than replay protection, and the documentation now
says so precisely. Because the answer is a TOTAL compared for equality against
what the cluster has booked, the verdict is a pure function of the answer and the
currently booked quantity - independent of how many answers preceded it, in what
order, or whether any were seen twice. The adjudication needs no history, which is
why it needs no reliable identity. An incremental encoding would have to know
whether this message had already been counted, which is unanswerable without
exactly the identity a snapshot cannot carry.

Implementing it settled a question the review had left open: there is no `Adopt`
in Phase 5, and that is correct rather than a shortfall. A venue reporting more
filled than the cluster booked would require creating a position movement, but a
snapshot carries no execution identity, price, or fee - the three fields a fill
requires and Phase 4's fill path checks. Adopting would mean inventing all three,
so divergence in either direction is a `Conflict`: below what is booked a fill
would have to be un-booked, above it the cluster refuses to manufacture a position
from a total. `Adopt` returns when reconciliation can obtain per-execution
evidence, which is a venue-integration question.

Also fixed in the simulator: a query now runs over the same session a send does
and is subject to the same disconnect. Without it `QueryOutcome::unavailable` was
unreachable, and the classifier's most safety-critical branch - the one that
refuses to read silence as absence - was never exercised by the matrix.

Measured before and after across thirty-two seeds. Before: sixteen ended with a
stranded slot, every one in `submission_unknown`. After: none do. Eleven end with
the venue confirming a resting unfilled order, one ends in a genuine conflict for
an operator, and the rest resolve completely. The matrix asserts the sharp form -
no order may remain in `submission_unknown` after reconciliation quiesces - plus
that every order still holding exposure sits in a state something can act on, and
that the matrix itself reaches the silence and conflict branches at least once.
Release 28/28, full host Debug ASan+UBSan 28/28, and the model corpus 109/109.

Sixth increment takes the cancel path through reconciliation. Four findings, all
inside existing contracts, no byte change. Recorded in architecture section 10.3.

The gateway refused to cancel in every mode that matters. `evaluate_pre_send`
gated every effect on "does control permit new risk", which is true only in
`normal`, so cancels were refused in `no_new_risk`, `cancel_only`, `reduce_only`,
and `halted` - making `cancel_only` a mode whose own guard refused the only thing
it exists to allow, and `halted` a mode that abandoned the orders already working.
The predicate now takes the effect; a cancel is permitted in every mode, while
executor generation and kill epoch remain the real fences.

`submission_unknown` names the order but describes its current effect, which for a
cancel-pending order is the cancel. The reducer overloads the state deliberately,
but the gateway's `need_of` could not tell a stuck cancel from a stuck submit by
order state alone, so it would reconcile a submission that was never in doubt and
burn its query budget while the cancel stayed stuck. It now reads the current
effect's operation.

Final cancel evidence was only reachable from `reconciling`, which requires a
`cancelled` observation. A cancel that simply went silent never gets one, so the
only path out of the state was closed. The gate now also accepts a cancel effect
in `unknown` with its order in `submission_unknown`.

The fourth was got wrong first and caught by an accepted Phase 4 test, and is the
most transferable lesson here. Making any quantity disagreement a conflict broke
the cancel/fill/final permutation test, which applies three pieces of evidence in
all six orders and requires every one to converge. The directions are not
symmetric: a venue reporting MORE than the cluster booked is missing evidence -
fills may still be in flight, so refuse and let it settle - while reporting LESS
is a genuine conflict, because no future evidence reduces a booked fill. That rule
now governs both the cancel and submit paths, correcting the previous increment's
claim that both directions of `order_reconciled` divergence were conflicts. The
cost is that an order whose fills were genuinely lost stays unresolved rather than
conflicted, which is the better trade: unresolved is recoverable and visible,
conflict is terminal.

Two test-integrity defects fixed, recorded in section 10.4 because both are the
kind that make a green suite worthless. The harness was cheating - it set the
gateway's `authoritative` flag from the simulator's own `inconsistent` ground
truth, so a corrupted answer never reached the reducer and the reducer's refusal
to guess was never exercised. And `inconsistent_query` corrupted upward, the
recoverable direction, while its comment claimed it tested the opposite; it now
denies a fill it has already made.

The matrix's gate is restated honestly. It previously required that no order
remain in `submission_unknown`; an order legitimately can, when the venue reports
fills the cluster never received. It now requires that every stuck order have a
NAMED cause on record - no trustworthy answer obtained, or an answer ahead of the
cluster's evidence - so "stuck for no reason" is what fails. One coverage boundary
is stated rather than papered over: the matrix cannot produce a reconciliation
conflict, because that needs an order which went unknown while already holding
booked fills and the fault model applies one fault per operation. Direct unit
tests cover the branch. Release 28/28 and full host Debug ASan+UBSan 28/28.

Seventh increment makes a cancel redeliverable without proof, amending section
9.2.1 on approval. That rule exists because a duplicate submit creates a second
order; a duplicate cancel creates nothing, so the rule was strictest exactly where
its justification does not apply and exactly when an operator is trying to get
flat. A cancel may now be redelivered after its response deadline, bounded by the
same attempt limit, and `unknown` becomes a deliverable state for a cancel and
only for a cancel. `reconciling` stays undeliverable for both, since it means
final evidence is owed and sending would race the answer.

Redelivery and re-authorization are kept distinct: redelivery is the same EffectId
at-least-once, deduplicated by the venue, and is what a timeout triggers;
re-authorization mints a fresh cancel effect and is the reducer's business.
Redelivery runs first and reconciliation second - the cheap idempotent action
before the round trip that would most likely just say to send it again.

The rider is not optional. `order_rejected` on a cancel returned the order to
`live`, but a venue refuses a cancel for "already cancelled", "already filled",
and "no such order" as readily as for "still working" - and a rejection of the
duplicate is now the common case. Under reordering that rejection can arrive
before the original's cancellation and flip a genuinely cancelled order back to
working, after which its real cancellation is refused. A rejected cancel now means
the lifecycle is unknown, so the order moves to `reconciling`. To stop that being
a dead end, `order_reconciled` was widened to speak about a cancel effect, where
it means the venue still holds the order and the cancel did not take: the cancel
becomes `rejected` with no economic result and the order returns to `live`, which
is also what lets the outbox record be reused. Both kinds already existed, so this
costs no bytes.

Accepted-behaviour change: Phase 4's outbox-reuse test rejected a cancel and
immediately re-requested one; it now reconciles in between. The arena-pressure
guarantee is unchanged - a reconciliation allocates no outbox slot - it just takes
one more step.

The matrix now cancels, and found a third test-integrity defect that unit tests
structurally could not. The simulator attributed a cancellation to the submit's
effect identity rather than the cancel's, so the gateway never saw a response to
its cancel and timed it out while the reducer refused the evidence as inapplicable
to a submit. Unit tests construct observations directly and so had the identity
right by construction; only the end-to-end path had it wrong. The matrix's
named-cause gate is now symmetric, covering a cancel awaiting its final word as
well as an unknown submission, and asserts that cancels actually happen. Release
28/28, full host Debug ASan+UBSan 28/28, model corpus 109/109.

Eighth increment discharges architecture 9.1.7, the obligation Phase 4 was
accepted with. Section 3 requires identity retention to exceed the maximum retry,
recovery and reconciliation horizon; Phase 4 could not show that because the
horizons did not exist until a gateway produced them. Measured now, recorded in
section 10.5, reportable with `VELOCITA_HORIZON_REPORT=1`.

Peak occupancy turned out to be the wrong measure, and asserting on it failed
immediately: the decision arena sits at 16 of 16 under nominal faults with zero
conflicts, because it sweeps ON ADMISSION WHEN FULL. Reaching the ceiling is how
reclamation starts, so an occupancy assertion condemns a sweep-on-full arena for
working as designed. The liveness property is one level down - no admission was
ever refused for want of room - and that is what is now measured, across every
command the harness issues.

Under nominal adversarial faults over 48 seeds and 40 cycles: zero capacity
refusals, zero orders reclaimed while holding unresolved exposure, 24 order groups
reclaimed so the run completed full create-resolve-sweep cycles, maximum order age
105 state versions, and both attempt bounds reached - 3 of 3 sends, 4 of 4 queries
- so neither is an untested guess. The accepted capacities exceed the measured
horizon and section 3 is satisfied for this load.

The conflict-inducing run exposes the operational bound. A conflict is
terminal-but-unresolved by design and so never reclaimed, which means every
unresolved conflict permanently consumes one slot in the decision, order,
reservation and outbox arenas at once. Sixteen simultaneous conflicts saturate the
cluster and it stops accepting new decisions. That is correct - refusing beats
freeing capacity against exposure that may still exist - but it makes the G-074a
operator escalation path load-bearing rather than a convenience, and the capacity
cannot be raised to soften it: TradingState is 81,168 bytes against an 81,920
ceiling and one more decision record costs 704 of the 752 remaining.

The safety property holds at any capacity and under both loads: zero orders left
the arena while still holding unresolved exposure. Saturation is a liveness bound
and acceptable; premature reclamation would be a correctness failure and is not.

Two defects found before the measurement could report anything. The gateway's own
bookkeeping never shrank - `GatewayDeliveryState` and `GatewayReconciliationState`
are sized like the reducer's arenas but only ever appended, so a gateway that ran
long enough exhausted its attempt arena and refused to record sends on a cluster
with room to spare; `reclaim_delivery_attempts` and `reclaim_reconciliation_
attempts` fix it, kept out of `plan_delivery` so that function stays pure. And a
fill that cannot be booked was refused silently: with `fill_after_cancel` enabled a
venue reports a fill after the cluster released its reservation on final cancel
evidence, and the refusal changed nothing, so the venue traded and the position
did not reflect it. Unreachable before Phase 5, because nothing produced
`cancel_reconciled` and a released reservation could never be contradicted - a
query answer is a snapshot, so the gateway asserts a finality it does not have.
The fill still cannot be booked without exceeding authorized risk, but the order
now goes to `conflict` so the group is retained and the divergence is visible. The
gate was tightened to match: a fill may be refused as unbookable only if a
conflicted order is left behind. Release 28/28, full host Debug ASan+UBSan 28/28.

Tenth increment reviews the Phase 5 lifecycle by enumeration rather than by
reading, and fixes the three defects that found. Recorded in architecture 10.8.

`v2-lifecycle-reachability-tests` drives the kernel breadth-first through every
command it accepts and records what actually happens, converging at 27 reachable
lifecycle positions from depth 6 onward. It asserts the inventory permanently, so
an edit that creates or removes a reachable position fails the build; it asserts
that `conflict` is a sink; and it asserts that no position with an unsettled
effect is a dead end. Its fixture is built independently of the delivery tests',
the same cross-check discipline Phase 4 used for its property corpus.

A FILL COULD ERASE A CONFLICT. `apply_observe_order_event` refuses everything from
`conflict`; `apply_observe_fill` did not. A conflicted order accepted a later fill,
moved to `filled`, released its reservation, and vanished from the conflict
projection - ADR 0001's "never overwrites conflicting evidence to make a dashboard
green", performed by the reducer. The fill path now refuses a conflicted order.

A PENDING CANCEL COULD BE RECONCILED AWAY. A pending cancel has not been delivered,
so its outcome is not in doubt, and answering "the order is still live" about it
marked the cancel rejected and returned the order to live - silently abandoning an
intent the operator asked for. Reconciliation about a cancel now requires its
delivery to be uncertain. Consequence recorded rather than patched over: a cancel
whose send attempts are exhausted cannot yet be reconciled, because the useful
answer there is "re-authorize", and nothing does that yet.

A CANCEL COULD BECOME PERMANENTLY UNSETTLEABLE. An order that fills while its
cancel is in flight left the cancel effect pending forever: redelivery runs out,
every rejection is refused because the order is terminal, and reconciliation never
targets a finished order. One slot in each of four bounded arenas pinned
permanently while the order read as healthily filled. A cancel rejection against a
terminal order now settles the effect and leaves the order untouched.

That third defect needed a fill, a cancel, a specific interleaving, and a venue
answering with a rejection rather than a cancellation. No unit test would have
been written for it and the fault matrix cannot produce it, because its faults do
not compose. Release 29/29 and full host Debug ASan+UBSan 29/29.

Phase 5 gate scripts and the acceptance evidence skeleton are in place, pinned to
source root 8d31c33e. Host evidence is recorded; neither target gate has run, so
Phase 5 is not accepted.

Deferred functional work is collected in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`,
separate from the V2.1 performance backlog because the distinction decides whether
an item is ever mandatory. An optimisation is optional forever; an availability
defect is optional only until the system runs long enough to reach it. Every item
carries a blast radius and a trigger.

The headline item: policies, sources and caller cursors are never reclaimed, so
they are lifetime ceilings - sixteen `PublishPolicy` commands per cluster
lifetime, ever, after which the model pipeline can no longer ship while trading
continues on the frozen active policy. No economics are wrong and the refusal is
typed and fail-closed, so it is not a correctness issue; but it is the same
availability defect section 8.6 fixed for receipts, left open for three arenas,
and it has no recovery path.

A capacity rebalance was investigated and rejected. The arenas look independent in
a size table and are not: venue event receipts live inside the request receipt
arena, so that sub-budget rather than `kMaxClusterOrders` is the real limit on
concurrent orders, and the policy arena that looks like free space is an
unreclaimed lifetime budget rather than dead weight.

Two Phase 5 Pi gate attempts failed in the gate rather than the code, and both
fixes are structural rather than a re-pin.

The first was a source root mismatch: the pinned value was computed on a
development host under a UTF-8 locale while the container computes under C.
Manifest order decides the root and collation decides manifest order, so identical
content hashed differently. `LC_ALL=C` is now pinned inside the gate recipe - a
source root that depends on ambient environment is not a source root.

The second reported four suites as "Not Run". The gate builds an explicit target
list that had drifted from `CMakeLists.txt`, so the Phase 5 suites were never
compiled. The gate now builds `velocita-all-tests` and `velocita-v2-tests`,
aggregate targets derived from `BUILDSYSTEM_TARGETS` rather than hand-maintained,
so the list cannot drift as suites are added.

That second failure is worth remembering for its shape: "Not Run" reads like a
behavioural failure and is not one, and the hypotheses it invites - char
signedness, alignment, endianness - are all wrong. A gate that builds a different
set of things than it runs will always mis-signal in this direction.

The Phase 5 physical Raspberry Pi 5 gate PASSED on 2026-09-11 at checkout
04329fad, source root b23bdea0: Release 29/29, benchmark-instrumented 26/26, Debug
ASan+UBSan 26/26, `throttled=0x0` before and after, 46.30 C to 50.70 C, governor
`ondemand`, GCC 15.2.0 on Ubuntu 26.04.1 ARM64.

The bounded layout reproduced byte-for-byte on ARM64: `TradingState` 81,168 of
81,920 with 752 bytes headroom, maximum canonical snapshot 65,259 of 65,536. That
turns the sixteen-conflict saturation bound into a hardware fact rather than a
host-only measurement, and with it the argument that the arena capacities cannot
be rebalanced within the current state ceiling.

The four Phase 5 suites cost under three seconds of the sanitizer phase on target,
against 52.53 s for the Phase 4 property corpus alone - so the enumeration and
fault-matrix work is close to free on real hardware.

Phase 5 is not yet accepted. The standalone ARM64 container gate has not been run
as a separate pass. The Pi gate executes the same container and additionally
verifies the physical host and records power and thermal evidence, so a container
pass would be parity rather than new information - but the Phase 4 promotion rule
asks for three and this candidate has two, so the deviation is recorded in the
acceptance document rather than assumed away.

### Phase 5 accepted on all four environments, and the Phase 4 capacity caveat discharged - 2026-09-11

The x86-64 NUC gate PASSED: Release 29/29, benchmark-instrumented 26/26, Debug
ASan+UBSan 26/26, at source root b23bdea0 matching the Pi and the container
exactly. GCC 15.2.0, 64-byte target and runtime cache line, 4 CPUs, 2 GiB.

The measurement the gate existed for: the bounded layout is BYTE-IDENTICAL to
ARM64. `TradingState` 81,168 of 81,920 with 752 bytes headroom, maximum canonical
snapshot 65,259 of 65,536, `DecisionRecordCold` 584, `RequestReceipt` 160. That is
the intended result rather than a lucky one - every hot struct is padded to
cache-line multiples and every field is fixed-width - but it is now measured on
both deployment architectures rather than assumed.

Two consequences. The 80-KiB state capacity is no longer provisional: Phase 4
recorded it as pending NUC validation and this discharges it. And the
sixteen-conflict saturation bound of architecture 10.5.3 is
architecture-independent, so neither machine in the ADR 0002 topology binds
tighter and operator escalation is sized the same on both.

Three further gate defects surfaced, none in the candidate, each invisible from
ARM64. Rootless podman on Ubuntu does not delegate the `cpu` cgroup controller, so
the gate's deliberate CPU constraint could not be applied. The container image was
pinned `FROM --platform=linux/arm64`, making an x86-64 host pull an ARM64 base and
run it emulated - the Phase 3D NUC gate had existed since that line was written
and records no evidence anywhere, consistent with never having passed. And GCC
enables LeakSanitizer by default on x86-64 but not aarch64, while LSan needs
ptrace that `--cap-drop ALL` denies: every suite died before executing a test,
while the identical ARM64 run passed because LSan was never active. That last one
means "Debug ASan+UBSan 26/26" had always meant different things on the two
targets, and the Phase 4 and Phase 5 records asserted it for both without
qualification.

Five gate-level defects across the whole acceptance campaign and none in the code.
`docs/NUC_OPERATOR_RUNBOOK.md` records the bring-up from what actually happened,
including the three host prerequisites that are not obvious.

### Phase 5 accepted - 2026-09-11

V2 Phase 5 is accepted for source root b23bdea0 at checkout 04329fad. All three
required environments - development host, ARM64 Linux container, and the physical
Raspberry Pi 5 - reproduced the same canonical source root and the same bounded
layout and passed every required suite. Host 29/29 Release and 29/29 ASan+UBSan;
container and Pi 29/29 Release, 26/26 instrumented, 26/26 ASan+UBSan. Pi power
`throttled=0x0` before and after, 46.30 C to 50.70 C. The annotated checkpoint is
`v2-phase5-accepted`.

G-025 and G-026 are closed: delivery is at least once by stable EffectId, and an
ambiguous outcome is explicitly unknown and reconciled before any resend that
could duplicate risk. G-074a remains open by design and is now measured rather
than assumed.

The final container pass required one fix, recorded because it was a defect in the
verification rather than the candidate: the container gate hardcoded docker while
the physical target gate hardcodes podman, so two gates attesting the same
candidate ran through different runtimes. It now prefers podman, which is what the
target gate already requires.

Three gate-level defects were found across the acceptance runs and none were in
the code: a source root that depended on ambient locale, a build target list that
had drifted from CMakeLists.txt, and the engine split above. All are recorded in
the acceptance document rather than only the eventual pass.

Status: Phases 1-4 are accepted on the required host, ARM64 Ubuntu, and physical
Raspberry Pi 5 gates. Deterministic policy evaluation, risk reservation, cluster
orders, accepted venue-evidence accounting, and the transactional outbox are
implemented. The execution gateway, generic V2 Raft integration, durable V2
storage, and live trading behavior are not implemented. No V2 release has been
tagged, and no V1/V1.1 wire, storage, deployment, or evidence artifact has been
deleted or superseded in place.

The accepted architecture direction is recorded in
[`ADR 0001`](docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md).
The authoritative domain, identity, message, order, risk, outbox, execution,
reconciliation, persistence, threading, failure, and metric contracts are in
[`docs/V2_ARCHITECTURE_CONTRACTS.md`](docs/V2_ARCHITECTURE_CONTRACTS.md).
The test-first build order and phase acceptance gates are in
[`docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md).

### Objective

Evolve the counter laboratory into a deterministic trading Decision/OMS
platform. A Raft cluster will own authoritative policy activation, decision
evaluation, risk reservations, order intent, accepted venue evidence,
positions, deduplication, and a transactional outbox. A logically single
active execution gateway will deliver committed effects to a venue and return
acknowledgement, fill, cancel, unknown-outcome, and reconciliation evidence
through the cluster.

### Accepted design decisions

- A model or calibration service produces an immutable, versioned decision
  policy artifact; publication does not silently activate it.
- A market-data/feature edge proposes bounded canonical decision frames. The
  Raft state machine performs the final authoritative evaluation.
- An accepted decision atomically records the order, risk reservation,
  deduplication receipt, and committed outbox effect.
- Raft apply performs no external I/O. The executor delivers committed effects
  at least once by stable identity and reconciles unknown outcomes before
  retry.
- The executor may refuse or defer an unsafe effect but may not originate or
  economically modify an order.
- One executor generation is active per venue/account. Automatic standby
  promotion remains prohibited until the old sender can be externally fenced.
- Normal new-risk traffic and emergency stop/cancel traffic use separate
  bounded lanes and authority rules.
- V2 retains the bounded asynchronous WAL direction: fixed-capacity staging
  may move and amortize persistence work, but protocol success stays behind an
  explicit quorum-durability fence.
- Strategy economics are the final implementation phase, after identity,
  deterministic replay, OMS/risk, outbox, simulation, storage, execution,
  reconciliation, kill, and operational recovery are proven.

### Development method

V2 uses test-driven development: observe the contract test fail, implement the
smallest bounded behavior that makes it pass, refactor with all tests green,
and retain evidence. Every phase defines tests, metrics, failure drills,
acceptance gates, and rollback boundaries before production code is added.

### Phase 1 candidate - domain types and versioned contracts

Commit `a6ee8df5870a` implements the macOS candidate:

- zero-cost strong identity wrappers prevent implicit cross-domain ID use;
- checked fixed-point price, quantity, money, fee, and feature types make scale,
  overflow, and rounding policy explicit;
- fixed-capacity policy, decision-frame, order-effect, and venue-observation
  values have typed structural validation;
- the V2 envelope uses explicit little-endian fields, a versioned initial
  message-kind registry, bounded lengths, fail-closed flags, and CRC-32;
- canonical payload codecs reject truncation, trailing bytes, excessive counts,
  unknown schema/enums, missing identity/digests, and invalid domain values;
- SHA-256 provides immutable artifact and corpus identity; and
- a Release microbenchmark reports codec timing, encoded-size ceilings, and
  code-audited allocation upper bounds without claiming system throughput.

The exact 144-byte policy corpus has SHA-256
`7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30`.
Debug, Release, legacy regression, AddressSanitizer, and UndefinedBehaviorSanitizer
tests pass on macOS ARM64. The exact Phase 1 content root also passes Release,
benchmark-instrumented, AddressSanitizer, and UndefinedBehaviorSanitizer tests
under native ARM64 Ubuntu 26.04 with GCC 15.2. Raw commands, measurements,
limitations, and the remaining target gate are recorded in
[`docs/evidence/V2_PHASE1_MACOS_CANDIDATE.md`](docs/evidence/V2_PHASE1_MACOS_CANDIDATE.md)
and
[`docs/evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md`](docs/evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md).

Phase 1 passed its physical Raspberry Pi ARM64 gate on 2026-09-03 as the
unchanged contract subset of source root
`0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8`.
The combined gate ran the unchanged Phase 1 suites in Release,
benchmark-instrumented Release, and ASan/UBSan Debug configurations. Detailed
target evidence is recorded in
[`docs/evidence/V2_PHASE1_RASPBERRY_PI_ACCEPTANCE.md`](docs/evidence/V2_PHASE1_RASPBERRY_PI_ACCEPTANCE.md).

### Phase 2 experimental increments - policy lifecycle and control authority

Experimental commits `7bc9e42` and `c783ab7` introduce the first pure reducer
increment and its native ARM64 Ubuntu development gate. The increment provides
a fixed-capacity policy catalog, retained request outcomes, exact/conflicting
duplicate handling, immutable and monotonic policy identity, and bounded
proposed/validated/active/superseded transitions. It performs no I/O, clock or
random read, locking, heap growth, order creation, risk change, or external
effect.

Seven Release suites, four benchmark-instrumented V2 suites, and four
ASan/UBSan V2 suites pass under ARM64 Ubuntu 26.04 with GCC 15.2 for source root
`627b38f488aad60d3f0a65874a4bb132e196400c31c2e406f4b615e1095221d3`.
Detailed RED/GREEN evidence, invariants, C++ design notes, and limitations are
recorded in
[`docs/evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md`](docs/evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md).

Commits `6de06ec` and `2849a52` add the next TDD increment: fail-safe strategy
mode, monotonic kill epochs, monotonic executor generations, generation-fenced
mode transitions, and the extended ARM64 gate. The latest committed source
root is
`8e4cba2743760eb71bb3ee7277d63183c2e706cc3bec5a6d449f0eec21291a72`;
8/8 Release suites, 5/5 benchmark-instrumented V2 suites, and 5/5 ASan/UBSan
V2 suites pass under ARM64 Ubuntu 26.04 with GCC 15.2.

This is not Phase 2 completion or acceptance. It remains on
`experiment/v2-phase2-kernel` while the remaining reducer increments are built. State
digest/snapshot, reconciliation-gated authority restoration, orders,
reservations, positions, outbox,
reconciliation, model-based replay, reducer measurement, Raft/storage
integration remain pending.

Experimental commit `33d27f6` adds strict normalized source admission. A
source is identified by source ID, generation, and sequence; the
accepted event bytes are represented by a canonical digest. Exact redelivery
is a retained no-op, conflicting redelivery fails closed, and unseen gaps or
regressions cannot advance authoritative source state. Fixed source and event
receipt capacities are checked before mutation. Venue-specific reconnect,
buffering, and snapshot recovery remain adapter responsibilities.

The committed source passed 9/9 Release suites, 6/6 benchmark-instrumented V2
suites, and 6/6 ASan/UBSan V2 suites under ARM64 Ubuntu 26.04 with GCC 15.2.
Its canonical source root is
`0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8`.

This increment is not an indefinite-stream implementation: the experimental
128-event receipt region has no replicated expiry yet and deliberately rejects
when full. Reconciliation commands, snapshot/state-digest coverage, and
randomized replay remain pending. The increment passed its physical Raspberry
Pi checkpoint on 2026-09-03 with clean power state before and after the run.
Detailed rationale, invariants, C++ mechanics, and limitations are recorded in
[`docs/evidence/V2_PHASE2_SOURCE_ADMISSION_EXPERIMENT.md`](docs/evidence/V2_PHASE2_SOURCE_ADMISSION_EXPERIMENT.md).

Experimental commit `865ec08` adds a bounded canonical `TradingState`
snapshot codec and SHA-256 state digest. The format uses explicit little-endian
logical fields and excludes C++ padding, inactive fixed-capacity slots,
metrics, and scratch state. Decode is transactional: it validates into
caller-owned candidate scratch and publishes to the destination only after the
frame, schema, digest, capacities, enums, contracts, and state invariants pass.

The exact commit passed 10/10 Release suites, 7/7 benchmark-instrumented V2
suites, and 7/7 ASan/UBSan V2 suites under ARM64 Ubuntu 26.04 with GCC 15.2.
Its canonical source root is
`17bf519d09f4b4e3f87fcf399d0fe18999b5358112736c38c2e7262be6afba75`.
The empty-state golden digest is
`0e737df191c265e7d2a4825c229ffe5220a4fc0e9a9a679731ac475ea5e44eb8`,
and the currently declared maximum snapshot is 45,595 bytes.

This is not durable snapshot persistence or complete replay: file publication,
durability fences, crash selection, Raft snapshot installation, randomized
suffix replay, retention/expiry, and performance measurement remain pending.
The exact source root passed its physical Raspberry Pi checkpoint on
2026-09-03: both wrapper and container gates passed, all three configurations
passed, firmware reported `throttled=0x0` before and after, and temperature
moved from 43.55 C to 48.50 C. Detailed evidence is recorded in
[`docs/evidence/V2_PHASE2_STATE_SNAPSHOT_EXPERIMENT.md`](docs/evidence/V2_PHASE2_STATE_SNAPSHOT_EXPERIMENT.md).

Experimental commits `a04094e` and `0a20e4f` add canonical versioned kernel
command/result frames and a byte-level replay boundary. All eight current
command alternatives use explicit stable kind values rather than
`std::variant` indexes. Frames use explicit little-endian fields and SHA-256
integrity; the command maximum is 608 bytes and the result frame is 110 bytes.
Malformed input and insufficient result capacity fail before reducer mutation.

A ten-record corpus now proves that continuous replay and midpoint snapshot
restore plus suffix replay return byte-identical suffix results and converge to
the same canonical snapshot. The reviewed final state digest is
`596ec54fb24f55b91579330e08cefa63b3af8d39c1e277cd835c5af4e1b566c8`.
The combined increment passed 12/12 Release suites, 9/9
benchmark-instrumented V2 suites, and 9/9 ASan/UBSan V2 suites under ARM64
Ubuntu 26.04/GCC 15.2. Its canonical source root is
`810f8c4db00d6ae37061694ab3be6f775b3312f0b837c09b00c589903d5e04aa`.

The exact source root passed its physical Raspberry Pi checkpoint on
2026-09-03. Both wrapper and inner ARM64 gates passed; firmware reported
`throttled=0x0` before and after, and temperature moved from 43.55 C to 48.50 C.
Generated/model-based replay, durable snapshot publication, Raft/WAL
integration, receipt expiry, and compaction remain pending. Detailed evidence
and C++ rationale are in
[`docs/evidence/V2_PHASE2_CANONICAL_REPLAY_EXPERIMENT.md`](docs/evidence/V2_PHASE2_CANONICAL_REPLAY_EXPERIMENT.md)
and
[`docs/V2_CANONICAL_CODEC_REPLAY_GUIDE.md`](docs/V2_CANONICAL_CODEC_REPLAY_GUIDE.md).

Experimental commit `26f0853` adds deterministic differential replay against
a structurally independent reference model. Thirty-two fixed seeds generate 96
commands each. The production byte path and model agree after every command on
typed results, canonical result bytes, and canonical state. Six snapshot cuts
per history reproduce every suffix result and final snapshot. Required coverage
includes policy lifecycle conflicts, control-fence rejection, source ordering,
exact retries, and request-identity conflicts. The aggregate corpus digest is
`1eadcbdef0f9a244b935afdb3dd3be9c6e6f42fffe913509cc3baaea303357a0`.

The increment passed 13/13 Release suites and 10/10 instrumented and
ASan/UBSan V2 suites under ARM64 Ubuntu 26.04/GCC 15.2. Its canonical source
root is
`d6eb59adecac3a486c2eee83151df02d66e7425865dd5076ad34ac0beeb96668`.
The exact root passed its physical Raspberry Pi checkpoint on 2026-09-04. The
wrapper and inner ARM64 gates passed all 10 sanitized V2 suites; firmware
reported `throttled=0x0` before and after, and temperature moved from 43.55 C
to 48.50 C. This expands semantic/replay evidence; it does not establish
real-Raft integration, durability, capacity, or trading authority. Detailed
evidence is in
[`docs/evidence/V2_PHASE2_MODEL_REPLAY_EXPERIMENT.md`](docs/evidence/V2_PHASE2_MODEL_REPLAY_EXPERIMENT.md).

### Phase 2 accepted - deterministic kernel foundation

Phase 2 is accepted at its corrected architectural boundary: bounded policy,
control, source-admission, receipt, canonical-codec, snapshot, digest, and
model-replay mechanics. Decision evaluation begins in Phase 3; risk, orders,
and the transactional outbox remain exclusively Phase 4 work. This removes the
earlier checklist overlap without pulling later behavior forward.

The closure record is
[`docs/evidence/V2_PHASE2_DETERMINISTIC_KERNEL_ACCEPTANCE.md`](docs/evidence/V2_PHASE2_DETERMINISTIC_KERNEL_ACCEPTANCE.md).

### Phase 3 planned - authoritative decision evaluation

Phase 3 has a staged design checklist but no accepted behavior. The plan first
freezes pure fixed-point predicate and ordered-rule semantics, then separately
reviews causal source/event-time schema, authoritative retained decisions, and
economic action templates. This keeps accepted schema changes and risk-bearing
order semantics out of an oversized commit. See
[`docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md`](docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md).

Experimental commit `6ee47b7` implements the Phase 3A pure ordered-rule
evaluator after an observed missing-interface RED build. It validates the full
bounded rule-to-frame feature-reference set before economic evaluation, then
uses exact fixed-point comparisons and first-match array order. The result owns
its directive, reason, matched index, and rules-visited count. The complete
Release suite passed 14/14 and the V2 ASan/UBSan suite passed 11/11 on the host.
It does not yet provide authoritative decisions, revised decision-frame bytes,
retained decision identity, risk, orders, outbox, ARM64, Pi, or trading proof.
Detailed evidence is in
[`docs/evidence/V2_PHASE3_RULE_EVALUATOR_EXPERIMENT.md`](docs/evidence/V2_PHASE3_RULE_EVALUATOR_EXPERIMENT.md).

Experimental commit `3f65187` implements the Phase 3B decision-frame schema
revision. Payload families now carry independent schema versions; only the
decision frame advances to V2. Its canonical input adds source ID, source
generation, source sequence, event time, and receive time. The authoritative
decoder rejects the frozen V1 corpus with typed `unsupported_schema` rather
than reinterpreting absent provenance or misclassifying the shorter record as
truncated V2.

The two-feature V2 golden frame is 181 bytes with SHA-256
`9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3`;
the bounded maximum decision frame is now 421 bytes. The complete Release suite
passed 14/14 and the V2 ASan/UBSan suite passed 11/11 on the host. Source-state
comparison, validity evaluation, retained decision identity, ARM64/Pi evidence,
risk, orders, and outbox remain pending. Detailed evidence is in
[`docs/evidence/V2_PHASE3_DECISION_FRAME_V2_EXPERIMENT.md`](docs/evidence/V2_PHASE3_DECISION_FRAME_V2_EXPERIMENT.md).

Experimental commit `e650f31` adds the required pre-3C memory-layout
guardrails. Canonical codec sizes, resident C++ object sizes, and cache-line
rounded arena strides now have distinct reviewed budgets. The target cache-line
size is an explicit compiled ABI parameter, and the ARM64 gate records a
machine-readable layout report in its evidence metadata. Compile-time
assertions stop payload or object growth that exceeds a reviewed ceiling.

For a declared 64-byte target line, `DecisionFrame` is currently 421 bytes
canonical, 432 bytes resident, and 448 bytes per rounded arena slot;
`TradingState` is 57,152 bytes against a 65,536-byte ceiling. The complete
Release suite passed 15/15 and the V2 ASan/UBSan suite passed 12/12 on the host.
The hot/cold retained-decision layout remains intentionally pending Phase 3C;
no cache-performance or capacity claim is made.

The exact source root
`1d9e44e20c495ee935b58b0723351cd5282642d0cdc098c4c35eba25d29bb577`
passed its physical Raspberry Pi 5 ARM64 gate on 2026-09-04. The target's
runtime L1 data-cache line matched the compiled 64-byte assumption, all 12 V2
sanitizer suites passed, firmware power remained `throttled=0x0`, and host
temperature moved from 44.10 C to 49.05 C. Detailed evidence is in
[`docs/evidence/V2_PHASE3_LAYOUT_GUARDRAILS_EXPERIMENT.md`](docs/evidence/V2_PHASE3_LAYOUT_GUARDRAILS_EXPERIMENT.md).

Experimental commit `c4283f6` defines the pre-reducer Phase 3C retained
decision layout. A 64-byte hot record holds lookup, outcome, and control fields;
a 472-byte cold record retains the command digest and complete decision frame.
Their arena strides are independently rounded to 64 and 512 bytes. Sixteen
records would consume 8,576 packed object bytes or 9,216 rounded arena bytes.
The records are deliberately not yet members of `TradingState`: snapshot,
digest, replay, and atomic capacity behavior must change in the same reducer
increment. Host Release passed 15/15 and V2 ASan/UBSan passed 12/12. Detailed
evidence is in
[`docs/evidence/V2_PHASE3_DECISION_RECORD_LAYOUT_EXPERIMENT.md`](docs/evidence/V2_PHASE3_DECISION_RECORD_LAYOUT_EXPERIMENT.md).

The Phase 3C host candidate integrates that layout into authoritative state.
`EvaluateDecisionFrame` has explicit wire kind 9; kernel command/result and
snapshot schemas advance to V2; and deterministic rejection, ordered
evaluation, control suppression, shadow authority, retained identity, typed
decision conflicts, and bounded capacity are covered. Hot lookup does not scan
the cold frame. Canonical replay and an independent generated reference model
retain decisions across every tested snapshot cut.

The current 64-byte-target report records a 69,832-byte `TradingState` under a
reviewed 73,728-byte object ceiling, a 57,997-byte maximum canonical snapshot,
a 608/640-byte maximum command, and a 126/128-byte result. Host functional,
Release V2, and ASan/UBSan V2 suites pass 16/16, 13/13, and 13/13 respectively.
The exact source root
`d3a45742b260562abb987c31092d04bc2abe8b162f71e0008af14f3be4942b75`
also passed all 13 V2 suites on the physical Raspberry Pi 5 on 2026-09-04.
The target cache-line assumption matched at 64 bytes, firmware power remained
`throttled=0x0`, and temperature moved from 45.75 C to 49.60 C.

Phase 3-wide measurement and Phase 3D economic action-template work remain
pending; no risk, order, effect, venue authority, snapshot-latency, or
throughput claim is made. Snapshot-validator and per-command receipt-lookup
performance concerns are explicitly deferred to the measured V2.1 scope in
[`docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md`](docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md).
Detailed Phase 3C evidence is in
[`docs/evidence/V2_PHASE3C_AUTHORITATIVE_DECISION_EXPERIMENT.md`](docs/evidence/V2_PHASE3C_AUTHORITATIVE_DECISION_EXPERIMENT.md).

Phase 3D direction review approves bounded constant-or-feature value sources,
logical order-slot targeting, explicit replace intent, immutable policy safety
caps, and versioned venue capabilities. The proposed authority boundary keeps
model output advisory, validates exact strategy intent against the deployed
policy envelope, and leaves dynamic risk checking plus atomic reservation to
Phase 4. Silent price/quantity clamping is not planned: a limit violation must
produce a typed no-effect rejection unless a future policy explicitly defines
adaptive sizing as strategy behavior.

The first Phase 3D TDD slice adds standalone typed action contracts without
changing policy, command, result, snapshot, or replay schemas. Separate
`PriceSource` and `QuantitySource` types, canonical inactive fields, strict
safety-cap validation, logical order slots, instrument/capability versions, and
exact economic proposals are host-green. New Phase 3D semantics separate
post-only from GTC/IOC/FOK and prohibit executor discretion. Full contract
tests pass 17/17; optimized Release and ASan/UBSan V2 suites pass 14/14. Pure
resolution, authoritative instrument/capability inputs, codecs, reducer
integration, ARM64, and Pi evidence remain pending. Detailed evidence is in
[`docs/evidence/V2_PHASE3D_ACTION_CONTRACT_EXPERIMENT.md`](docs/evidence/V2_PHASE3D_ACTION_CONTRACT_EXPERIMENT.md).

A follow-up contract slice adds authoritative `InstrumentExecutionProfile` and
`VenueCapabilities` inputs. They bind monetary tick-lot conversion, inclusive
instrument bounds, supported directives/TIFs, and post-only capability to
explicit versions. The 72-byte cold instrument profile uses a reviewed 80-byte
object ceiling; the venue capability record exactly meets its 32-byte ceiling.
No canonical payload or reducer state changes yet. Functional tests remain
17/17 green, with focused Release and ASan/UBSan action/layout gates passing.

The standalone Phase 3D pure resolver now converts fixed-or-feature templates
to exact economic proposals using explicit signed rounding and checked integer
arithmetic. It enforces scope/version binding, venue semantics, freshness,
instrument bounds, and immutable offset/deviation/quantity/notional caps with
typed reject-not-clamp outcomes. Cancellation remains available during stale
market data. Functional tests pass 18/18; focused Release and ASan/UBSan
resolver/layout gates pass 2/2. Policy-schema/reducer integration and target
evidence remain pending, so this is not yet an order-authoritative path.

A candidate bounded executable-policy companion now maps the existing 32
ordered rules to at most eight deduplicated templates; slot zero is canonical
no-action. A separate 32-entry feature-schema artifact owns price/quantity unit
conversions. Structural and cross-artifact tests are host-green, including
Release and sanitizers. The active-plus-staged resident projection exceeds the
old state ceiling by 24 bytes before lifecycle metadata, correctly blocking
silent integration. Canonical size and Pi/NUC evidence must determine the final
state/snapshot budget before these candidate bounds are accepted.

### Phase 4 B1 - replicated risk-limit configuration (2026-09-09)

- Added approved full-config `ConfigureRiskLimits` command kind 17 and risk-result
  detail tag 5. Justification: risk limits must be configured by deterministic
  commands, not only fixtures. No balance/reset fields or external effects.
- Added strict expected-version updates, kernel-selected next RiskVersion,
  epoch/executor checks, positive/range validation, prospective per-order caps,
  current aggregate hold coverage and quiescent rate/policy changes. Existing
  balances and PositionVersion are preserved. Added typed capacity, exhausted-
  version and blocked-reconfiguration refusals; no existing enum values removed.
- Advanced kernel command/result and snapshot schemas to approved V5; kind 17's
  body is 88 bytes and full command 180 bytes. Receipt detail reuses three words.
  No resident capacity increase or OrderEffect change. Older schemas are rejected,
  not migrated or reinterpreted.
- Added creation/update/codec RED-to-GREEN tests, command-configured authorization,
  version staleness, economic preservation, replay/restore, full canonical body,
  malformed/truncated input, capacity, and version/rate/control boundary coverage.
- Updated five schema-dependent golden digest expectations (source command,
  apply result, empty snapshot, replay state and generated corpus); retained V4
  source/empty-state payload projection checks. No tests removed. The existing
  Phase 1-3 reference model still excludes Phase 4; corrected its misleading
  claim that a complete Phase 4 model already exists.
- Added architecture contract section 8.1 as the reference for later operator
  documentation; corrected stale replacement comments to the approved serial-only
  scope. Full Phase 4 acceptance remains outstanding.
- Validation: Release 23/23 and focused host ASan+UBSan 5/5 (risk, snapshot,
  codec, replay and Phase 1-3 model). Layout unchanged at 81,024-byte state and
  65,121-byte maximum snapshot. No physical Pi or pinned ARM64 gate claimed.

### Phase 4 review corrections - reclamation identity and snapshot accounting

Working-tree increment, 2026-09-08. No Phase 4 acceptance claim.

- Approved B1 terminology refactor: renamed `RiskAccount::max_order_notional`
  to `max_order_gross_notional`, and `max_reserved_notional` to
  `max_reserved_exposure`. Added comments distinguishing one order's gross
  notional from the aggregate margin-plus-fee hold budget. Updated reducer,
  snapshot codec/validation, test references and handoff; removed misleading
  source identifiers. No behavior, member layout, canonical field order or
  schema change; no aggregate gross-notional cap or configuration command added.

- Added permanent RED regressions for authorization identity after 20 completed
  cycles and for checksummed snapshots with unbacked account aggregates. Both
  failed for the intended reason before implementation changes.
- Changed evaluation and authorization retries to consult retained receipts after
  payload reclamation. Justification: freeing arena slots must not allow reuse of
  a DecisionId, a second economic authorization, or aliased/unmaterializable effects.
  Submit receipts use the existing generation-1 EffectId derivation; cancel
  receipts cannot substitute for them. No new identity or canonical fields.
- Added snapshot ownership and checked per-account quantity/margin/fee sums.
  Justification: individually valid records are insufficient if aggregate risk
  disagrees with live reservations. Missing owners, orphan holds, mismatches and
  overflow fail closed; rejected restore preserves its destination.
- Added tests for fresh-request retries/conflicts across sweep and restore,
  distinct fresh effects, full effect/snapshot bytes, account attribution and
  accumulator overflow. Corrected the maximum-size fixture's previously
  inconsistent account totals without reducing its encoded size or capacities.
- Removed the stale claim that DecisionId conflict detection must be lost after
  sweeping, and superseded the handoff's 16-lifetime-order limitation. Retained
  receipts already provide identity tombstones; their eventual exhaustion and
  unauthorized-decision retention remain separate limitations. No tests removed.
- Validation: Release 23/23; focused Release 2/2; focused host Debug ASan+UBSan
  2/2 (`build-phase4-review-sanitized`). Full Phase 4 sanitizer acceptance,
  pinned ARM64, physical Pi and the Phase 4 evidence document remain outstanding.
- Layout unchanged: `TradingState` 81,024 bytes; maximum canonical snapshot
  65,121 bytes. Snapshot validation adds only 96 bytes of temporary totals.

### Phase 4 in development - risk reservation, orders, and transactional outbox

Commit `401c861` introduces the first Phase 4 development checkpoint. It is not
a Phase 4 acceptance claim: no sanitizer, pinned ARM64, or physical Raspberry Pi
gate has been run against it, and no Phase 4 evidence document exists.

#### Added

- `AuthorizeDecision`, `ObserveFill`, `RequestCancel`, `ObserveOrderEvent`, and
  `RequestReplace` as explicit kernel command kinds 12 through 16; kernel
  command/result and snapshot schemas advance to development V4 without
  reinterpreting accepted V3 bytes.
- An atomic authorization transition: one accepted authoritative `place_limit`
  decision creates its risk reservation, cluster order, retained receipt, and
  pending outbox effect, or creates none.
- Risk accounts, reservations, cluster orders, and normalized outbox records in
  `TradingState`, at 112, 64, 40, and 64 bytes respectively.
- Domain-separated SHA-256 derivation of `ClusterOrderId` and `EffectId`, with
  zero and collision cases failing closed.
- Partial-fill accounting that moves signed quantity into position and
  proportionally reduces margin, fee, and notional holds.
- `VenueObservation` schema V2 (166-byte `OrderEffect` with explicit
  `PostOnlyMode`; V1 rejected, not reinterpreted) and observation kind
  `cancel_reconciled`, whose quantity is the final cumulative filled quantity.
- `observation_fingerprint`: SHA-256 over an ASCII domain tag plus the canonical
  observation bytes, verified before receipt lookup or economic deduplication so
  a copied digest cannot hide changed evidence.
- Cross-kind venue event identity conflicts: a fill and an order event may not
  reuse one venue/session/sequence in either direction.
- `materialize_order_effect`, reconstructing an effect's exact historical
  outbound bytes from retained outbox, receipt, and decision state.
- `ApplyCode::replacement_not_supported` and `ApplyCode::order_slot_occupied`.
- Two tests proving the above two refusals leave economics byte-unchanged and
  hold across snapshot restore.

#### Removed

- The `max(old, new)` reservation hold in `apply_request_replace` (276 lines).
  Justification: `max(old, new)` is the correct worst-case hold only if the
  venue guarantees old and new orders can never both execute. Where they can
  coexist, true exposure is `old + new` and `max` under-reserves, violating
  G-011. No venue this system targets documents that exclusivity, and proving it
  requires the Phase 5 venue simulator, so the assumption could not be settled
  inside Phase 4.
- The replace acknowledgement/rejection branch in the venue-observation handler
  (140 lines). Justification: unreachable once no effect can carry
  `EffectOperation::replace`.
- `ClusterOrder::active_filled_baseline`, and its snapshot validation, encode,
  and decode. Justification: its only readers lived in the deleted branch. It
  existed to reconstruct an old order's residual after a rejected replacement
  without retaining a second reservation object; with replacement deferred there
  is no such reconstruction. `ClusterOrder` falls from 48 to 40 bytes and
  `kStateSnapshotClusterOrderBytes` from 43 to 35.
- Five replacement-economics tests. Justification: they asserted behaviour that
  no longer exists. The outbox materialization test was converted to a
  submit/cancel effect pair, preserving its actual subject - that every retained
  effect reconstructs exact historical bytes across lifecycle and snapshot
  restore. The replacement control-recheck assertion became an unconditional
  refusal assertion: no control state permits direct replacement. This proves a
  disabled capability, not a stronger implementation of safe serial replacement;
  the serial lifecycle still needs current-control recheck evidence.

#### Decided (revised user approval, 2026-09-08)

Native amend and overlapping replacement remain disabled. Safe SERIAL
replacement remains a Phase 4 acceptance requirement: cancel, final
reconciliation, fresh authoritative decision, fresh authorization under current
controls and risk. A typed refusal alone does not implement this capability.
Wire kind 16 remains allocated; this decision changes no canonical bytes or code.

- Added: an explicit serial-replacement acceptance matrix in CURRENT_HANDOFF and
  corrected G-018. Justification: prove late-fill accounting, risk retention,
  fresh authorization, identities and full effect bytes through evidence
  permutations, compaction, snapshot restore and replay before closing Phase 4.
- Removed: the blanket replacement deferral and the rationale that covering two
  executable orders inherently violates safe reservation. Justification: covering
  both exposures is legitimate; double-counting one exposure is the error.
  `max(old, new)` remains unsafe without proven mutual exclusion. The selected
  MVP avoids overlap, rather than asserting that all safe replacement is impossible.
- Removed: dependence on the earlier venue-endpoint availability claim for this
  scope decision. Justification: reducer safety must not assume unverified venue
  semantics. No gateway, venue adapter, or native-amend implementation is added.
- No implementation or test was added or removed in this documentation revision;
  the serial acceptance matrix remains unproven, and no new gate pass is claimed.

Deferring replacement was not sufficient on its own. `ClusterOrderId` derives
from the varying `DecisionId`, so cancel-then-authorize created a second order
and a second reservation on one logical slot, violating the selected no-overlap
policy. `AuthorizeDecision` now enforces at most one unresolved cluster order
per account, instrument, venue, and logical order slot, keyed on the reservation
not being released, because retained risk is what G-018 constrains. The RED for
`test_fresh_submit_cannot_bypass_unresolved_cancelled_slot` observed exactly
that defect before the fix.

#### Measured

```text
ClusterOrder                    48 -> 40 bytes
TradingState                81,144 -> 81,016 bytes (headroom 776 -> 904)
maximum canonical snapshot  65,241 -> 65,113 bytes (headroom 295 -> 423)
```

Host Release and all 23 CTest suites pass. The reclaimed snapshot headroom is
reserved for Phase 4 compaction metadata and must not be spent on new capacity.

#### Added - bounded arena reclamation (B2)

- `sweep_resolved_order_groups`: reclaims fully resolved order groups - order,
  reservation, its outbox effects, and its decision record - from the bounded
  16-slot arenas. Without it the kernel stops authorizing after 16 lifetime
  requote cycles on a slot, which slot exclusivity makes arrive one requote at a
  time. Deterministic: no clock, no random source, no allocation, fixed passes
  over arenas of 16.
- `TradingState::reclaimed_group_count`, a monotonic counter carried in the
  canonical snapshot after the control prefix. Justification: it gives the
  dashboard an auditable "how much has ever been swept" signal without growing
  the 128-byte canonical result or regenerating every existing golden result,
  and it turns an otherwise silent state-digest change into an observable event.
- Four tests covering continuous requoting past every arena bound, retention of
  records an unsettled effect still needs, refusal to reclaim a conflicted
  order, and preservation of order/reservation index pairing across compaction.

#### Reclamation contract

Reclaimable requires TERMINAL AND RESOLVED, which are different properties.
`filled`, `cancelled`, and `rejected` resolve their risk. `conflict` is terminal
but unresolved - internal state and venue evidence disagree, so the exposure is
of unknown size - and `reconciling` still awaits final venue evidence. Neither is
reclaimable: dropping one would free capacity against exposure that may still
exist, and would destroy the retained proposal and effect bytes an operator needs
to resolve the disagreement.

A group is also retained while any of its effects is unsettled, because
`materialize_order_effect` rebuilds an effect's exact historical bytes from the
retained decision and receipt. Reclaiming those inputs would strand a pending
external effect whose payload can never be reproduced, breaking G-020.

Reservations and orders are reclaimed as a PAIR under dense compaction with one
index-fixup pass. `ClusterOrder` addresses its reservation by array index, so
compacting reservations independently would silently repoint an order at another
order's risk with no compile error and no validation failure.

The horizon is reference-based rather than time-based: the arena being full is
the trigger and "nothing can reference this" is the safety condition, so no time
horizon is required. To revisit in Phase 5, when the gateway retry horizon and
the venue reconciliation window become measurable. One accepted consequence: once
a decision record is reclaimed, reusing its `DecisionId` from a different request
with different provenance is no longer detectable as a typed conflict. Exact
command replay is unaffected, because request-receipt deduplication runs before
decision lookup.

#### Fixed during implementation

A stale-reference defect found by the B2 REDs. `apply_authorize_decision`
borrowed `const auto& cold = state.decision_cold[decision_index]` before the
capacity check; running compaction there left the reference in bounds but naming
a different, cleared slot, so an order was reserved from a zeroed proposal with
zero quantity. The failure was silent - no crash and no sanitizer report. Both
sweep call sites now run before any index is resolved or any arena reference is
borrowed, which makes the hazard structurally impossible rather than a rule a
future author must remember.

#### Measured after B2

```text
TradingState                81,016 -> 81,024 bytes (headroom 904 -> 896)
maximum canonical snapshot  65,113 -> 65,121 bytes (headroom 423 -> 415)
```

Empty-state, replay, and generated-model golden digests were regenerated because
the canonical snapshot gained the counter. Development V4 is unaccepted, both
replay paths still converge byte-for-byte, and no accepted V3 bytes were
reinterpreted.

#### Added - serial replacement acceptance, first slice

`test_serial_replacement_across_a_risk_limit_update` proves the cancel,
reconcile, fresh-authorize lifecycle while replicated risk configuration changes
underneath it. Refusing native amend does not by itself satisfy G-018: the
guarantee constrains the amend/cancel-replace transition, and with native amend
disabled the serial path is that transition, so it must be proven rather than
assumed.

Eight legs: rate or policy change refused while the cancelled order still holds
risk; aggregate cap below the retained hold refused; prospective per-order cap
accepted mid-cancellation without rewriting balances, position version, order
state, or historical effect bytes; stale-version successor refused; venue
cancellation not releasing risk; final reconciliation releasing the residual;
fresh successor authorizing against the new configuration; and every effect
reconstructing byte-for-byte across snapshot restore and canonical replay, with
the replayed snapshot byte-identical to the direct one.

Legs 1-7 passed on first run and are recorded as acceptance coverage for
behaviour already implemented correctly, not as fixes.

#### Fixed - replay fidelity

Leg 8 failed genuinely with `canonical replay changed historical effect bytes`.
`materialize_order_effect` takes `committed_directive_index` from the retained
authorization receipt's `state_version`, and an admitted refusal also retains a
receipt and advances the global StateVersion. A replay omitting refused commands
therefore authorizes at a different StateVersion and produces different canonical
effect bytes. The defect was in the test harness rather than the kernel, but the
property is general: a faithful replay must re-drive every admitted command,
refusals included. `evaluate_new_decision` gained an optional command sink,
having previously applied two commands invisibly.

#### Fixed - orphaned effects wedged the bounded arenas

`test_reclamation_during_cancellation_preserves_the_recovery_path` found that an
order could be terminal and risk-resolved while one of its own effects had never
received evidence: order `cancelled`, reservation `released`, cancel effect
`acknowledged`, submit effect still `pending`. Once the order closed, no future
observation could be scoped to that submit effect, but reclamation required every
effect settled - so the group was permanently unreclaimable. With the decision
arena full, evaluation failed closed and could not recover even after the stuck
slot reconciled. Wedged rather than degraded.

Orphaned effects now settle to `OutboxState::incomplete`, an enum value that
already existed and was never set, and `incomplete` counts as settled for
reclamation. It is deliberately not `acknowledged`, which would claim evidence
the venue never gave. Settlement runs only on definitive evidence - final cancel
reconciliation or a rejected submit the venue never accepted - and specifically
NOT on a full fill, because a cancel in flight when the fill lands is still owed
a venue response and is awaiting evidence rather than orphaned.

Justification for auto-healing here but never for `conflict`: this group's RISK
is resolved, so there is no exposure left to under-reserve. `conflict` is
terminal but unresolved and must never be settled automatically. The distinction
is specified in architecture contracts section 8.2.

`effect_is_settled` and `order_is_risk_resolved` were factored into shared
helpers so reclamation and settlement cannot drift apart on what "resolved" means.

#### Added - orphan settlement accounting

`TradingState::settled_incomplete_effect_count`, a monotonic counter in the
snapshot header. A rising count means the gateway is losing effect outcomes even
while reclamation stays healthy. It grew the V5 empty payload by eight bytes,
which falsified the guard asserting V5's empty payload matched V4's; that guard
now pins the empty-payload size instead, which is what unreviewed header growth
actually moves. Empty-state, replay and generated-model goldens were re-pinned
for development V5.

`TradingState` is 81,032 bytes and the maximum canonical snapshot 65,129.

#### Added - serial replacement matrix completed

`test_risk_configuration_respects_every_unresolved_order_state` pins the
architecture section 8.1 rule against all four unresolved states: cancel_pending,
submission_unknown, reconciling and conflict. Rate changes and bare
policy-version bumps are refused and mutate nothing; prospective per-order caps
are accepted without revaluing the live hold; aggregate caps below the retained
hold are refused rather than clamped; and rates become configurable once the hold
is released, without disturbing the filled position. The behaviour was already
correct, but the implementing predicate keys on the reservation not being
released rather than on a lifecycle-state list, so the states section 8.1 names
were only covered transitively and a future refactor could silently drop one.

`test_two_sided_slots_requote_independently` adds the suite's first two-sided
fixture. A second ordered rule bound to a second template places a sell quote on
logical slot 2 while a buy rests on slot 1, so first-match selection steers each
side to its own slot. It proves the slots never contend, exclusivity is enforced
per slot, holds are summed rather than netted, reconciling one side leaves the
other's state, pairing and effect bytes untouched, the freed slot requotes while
the other still rests, and the interleaving survives snapshot restore. It also
separates same-identity retry, which returns the original result through receipt
deduplication, from a different request re-authorizing a consumed decision, which
returns already_applied with the retained identities.

Both are acceptance coverage; neither found a kernel defect. A fixture trap is
recorded in code: two_sided_state swaps the policy after ready_state has already
evaluated its fixture decision, so that decision targets slot 1 whatever the
two-sided bindings say, and authorizing it makes both quotes collide in a way
that mimics a slot-exclusivity defect.

#### Added - independent Phase 4 property corpus

`tests/v2_phase4_property_tests.cpp` drives 24 seeds of 220 randomized but
well-formed command steps and checks contract-derived properties after every
command: no outbox effect outliving its cluster order, reservation arithmetic
that cannot go negative or exceed the original, released reservations holding
nothing, byte-identical snapshot round trips, monotonic reclamation and orphan
counters, reclamation being economically inert, and byte-identical canonical
replay of the whole history including refusals.

The Phase 4 gate is stated as a property rather than as agreement with a second
implementation, so the corpus derives its checks from the contracts. That makes
it independent evidence in a different way from the Phase 1-3 mirror model, which
remains the reference for the mirror style; a Phase 4 mirror is still worth
adding and is not superseded.

Phase 4 state is created only through commands - the risk account via B1
creation, decisions via real source admission and evaluation - so the corpus
cannot hand-seed states the kernel would never produce. Coverage is asserted
rather than assumed: the run fails unless authorization, partial and full fills,
cancel, reconciliation, unknown submission, configuration, reclamation, orphan
settlement and refusals were all exercised, and a vacuity guard requires at least
a third of commands to be admitted.

No kernel defect was found. Three corpus-side defects were, each a hazard for any
future hand-built state: a zero source-cursor sequence is invalid; an
executable-policy bundle's staged and activated StateVersions must both be at or
below the current version; and asserting a specific replacement refusal code is
wrong once the receipt arena fills, since admission refuses on capacity before
dispatch reaches the handler.

Release 24/24 and focused host Debug ASan+UBSan 6/6.

#### Added - unauthorized-decision reclamation

A live maker evaluates continuously and most frames produce no_action, so the
decision arena filled with records no order would ever reference, and reclaiming
order groups did not help because there was no resolved group to free. Two rules
now make retained decisions reclaimable.

Structurally non-authorizable records are reclaimed with no horizon and no clock.
Every condition in the authorization predicate is an immutable property of the
retained record, so a record failing any of them can never become authorizable
however the world changes. This covers no_action, shadow, control-suppressed and
rejected evaluations, which is the dominant volume.

Superseded records are reclaimed when a newer actionable proposal exists for the
same account, instrument, venue and logical order slot. A maker only ever wants
the latest quote for a slot, and resurrecting an older proposal would rest a
price computed from older market data - the same objection that already justifies
refusing a stale RiskVersion. Ordering is by array position, never DecisionId,
because position is arrival order and is canonical under stable compaction.

Identity is preserved without new machinery: applied evaluation receipts outlive
the decision arena and retain the DecisionId, payload digest and original
outcome, so a reclaimed DecisionId re-presented with a different payload still
conflicts and an exact replay still returns its retained result. The concern
recorded in an earlier session was already resolved by the reclamation-identity
fix.

The sweep previously returned early when no order group was reclaimable, which is
precisely the case that matters here; decisions are now assessed independently.
No schema or layout change was required.

Recorded as a strategy assumption in architecture section 8.4: supersession means
a decision must be authorized promptly, before the edge produces a newer proposal
for that slot. That is correct for level-triggered market making and wrong for a
strategy that deliberately queues intents. A state-version horizon, which would
be strictly more general, is deferred to a later version because N cannot be
justified until Phase 5 supplies a venue simulator.

#### Changed - two test premises became unreachable

The decision arena can no longer be filled by unauthorized decisions, since at
most one survives per slot plus those referenced by live orders. The wedge
scenario in test_reclamation_during_cancellation_preserves_the_recovery_path is
now structurally unreachable, so that test asserts the stronger property that a
stuck order cannot wedge evaluation at all. test_conflicted_orders_are_never_swept
now takes sweep pressure from the order arena using the two-sided fixture.
observe_fill gained side and price parameters, because a fill whose side
disagrees with the resolved proposal is correctly refused as not applicable but
reads as a mysterious fixture failure on a two-sided book.

#### Added - Phase 4 metrics

`collect_metrics` is a pure function of `TradingState`, adding no field,
participating in no digest, and reading no clock. Section 15 asks for ages and
latencies, which are durations, and apply may not read a clock, so every age is a
STATE-VERSION distance instead. That is exact, deterministic, identical on every
replica, and arguably the better operational measure: it is load-relative, so a
stuck slot looks equally stuck in a fast market and a quiet one, and it cannot
drift or jump when a host's clock is corrected. It is not convertible to wall
clock from one sample; an exporter needs its own timestamped StateVersion
timeline outside the authority boundary.

Reported: arena occupancy against compile-time capacities; orders, effects and
reservations by state; counts and oldest ages for unresolved orders, unsettled
effects, reconciling orders and conflicted orders; maximum
decision-to-authorization distance; the retained reclamation and
orphan-settlement counters; control state; and per risk account the reserved
exposure, position, versions and headroom against aggregate ceilings. Venue-event
identity count/headroom and caller-cursor count/headroom now expose the two
retention pressures that can stop admission.

Unsettled-effect age now uses the authorization receipt for the exact `EffectId`,
so a later cancel is not aged from its parent order. Reconciling age is named
`oldest_reconciling_order_age_since_authorization` because Phase 4 does not retain
a transition timestamp; it is an upper bound, not duration in the state.

`unresolved` keys on retained risk rather than lifecycle state, matching the
reclamation rule: a metric counting "not terminal" would omit a conflicted order,
which is terminal and simultaneously the most important record on the dashboard.
An age for a record authorized at or after the current state version reports zero
rather than wrapping, so a corrupt input cannot masquerade as maximal staleness.

`OutcomeCounters` is separate and deliberately not in state. Duplicate and
refusal counts describe command outcomes rather than retained records - a
duplicate leaves no trace in state, which is what makes it a duplicate - so they
are accumulated by an observer. Placing them in `TradingState` would make an
observability counter part of the state digest and therefore of consensus.

G-074a now has its signal: a slot stuck in reconciling is no longer
indistinguishable from a quiet market.

#### Fixed - a stale-binary false pass

`ctest` does not build, so building only the target under test left untouched
suites running stale binaries. `v2-decision-kernel-tests` had been passing
against a binary predating unauthorized-decision reclamation; a full build
exposed it. The failure was a premise change rather than a defect: that test
filled the decision arena with sixteen same-slot proposals, which supersession
now sweeps down to one. The capacity-atomicity invariant is still worth testing,
so the fixture assigns each retained proposal a distinct logical slot, making the
arena genuinely full and unsweepable.

#### Fixed - Phase 4 acceptance review corrections (2026-09-10)

- Authorization now requires the decision to match the current active policy,
  executable bundle, instrument/venue versions and scope, with no newer same-slot
  actionable proposal. Old bytes retain provenance but no longer resurrect old
  authority after policy rotation or supersession.
- `order_acknowledged` is submit-only; a cancel mismatch is refused before it can
  resolve the effect and wedge the still-pending order. Repeated semantic
  `effect_unknown` observations return `already_applied` without another applied
  venue transition.
- Fill evidence is submit-only at the effect boundary. A fill cannot acknowledge
  a cancel effect; a partial fill resolves an unknown submit, while a fill against
  the historical submit books execution without erasing an independently unknown
  cancel. The mismatched-operation path refuses before economic mutation.
- Applied fill and order-event results populate the existing `DecisionId` field.
  This zero-byte owner link pins identity receipts while their order exists and
  lets resolved groups release them. A sustained 119-order test crosses the old
  64-event lifetime ceiling.
- Authorization reserves venue completion capacity using remaining quantity
  atoms plus bounded submit/cancel/reconciliation transitions; two events per
  order was rejected because Q one-atom fills are legal. Pressure now refuses new
  risk, not evidence for admitted exposure.
- Every unresolved order retains one cancel-effect slot in addition to its submit
  effect. A definitively rejected cancel can reuse its own slot at a strictly
  higher generation; unresolved effects are never overwritten.
- Caller cursors are registered at first admission with a zero expired prefix.
  Snapshot validation now bounds the arena and rejects zero/duplicate caller
  identities. The watermark is documented honestly as a conservative prefix,
  requiring monotonic sequences per stable caller.
- Successful observation results require complete owner/identity fields, while
  typed malformed-observation refusals may retain partial non-empty diagnostics.
  Thus every admitted refusal remains encodable and snapshotable.
- Phase 4 metrics now use exact effect-creation receipts, honest reconciliation
  naming, identity-arena headroom, and conflict classification for derived
  identity/fill-quantity collisions.
- The randomized Phase 4 corpus now generates correct sell fills, exact- and
  cross-request duplicates, conflicting observation identities, seed/step
  diagnostics, and an independent signed-position ledger.

These changes add no `TradingState` field and do not grow the canonical V5 result
or maximum snapshot. Old unaccepted development V5 snapshots with applied
observation results whose `DecisionId` is zero are intentionally unsupported.

#### Added - Phase 4 acceptance gate contract (2026-09-10)

- Added Phase 4 ARM64-container and physical-Pi wrappers pinned to source root
  `a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
- Extended the shared native ARM64 gate to build and execute the Phase 4 metrics,
  property, and risk/order/outbox suites in Release, instrumented Release, and
  Debug ASan+UBSan configurations. Without these targets, an older gate could
  report green while never executing the Phase 4 acceptance corpus.
- Added the Phase 4 evidence document with candidate identity, byte contracts,
  host results, target commands, layout, limitations, and an explicit pending
  status. ARM64 and Pi success will be recorded only after those gates run.
- The pinned ARM64 Ubuntu 26.04/GCC 15.2 gate passed on 2026-09-10 for the exact
  expected source root: Release 25/25, instrumented V2 22/22, and Debug
  ASan+UBSan V2 22/22. The subsequent physical Pi result is recorded below.

#### Accepted - Phase 4 risk, orders, and transactional outbox (2026-09-10)

The physical Raspberry Pi 5 reproduced source root
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`
and passed Release 25/25, instrumented V2 22/22, and Debug ASan+UBSan V2 22/22.
Power remained `throttled=0x0`; temperature moved from 44.10 C to 50.15 C.
Evidence is retained at
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`
and summarized in
`docs/evidence/V2_PHASE4_RISK_ORDERS_OUTBOX_ACCEPTANCE.md`.

G-010 through G-024 are closed. G-025/G-026 remain open for the Phase 5
transport implementation, and G-074a remains the documented fail-closed
operator-recovery gap. The annotated checkpoint is `v2-phase4-accepted`. This
acceptance starts no gateway and authorizes no venue or real-money connectivity.

#### Standing invariants for later phases

Two rules were established here whose violation fails silently. First, the sweep
compacts arenas, so any index or reference taken before it runs stays in bounds
while naming a different slot; both call sites sweep before any arena lookup and
new lookups must not be inserted above them. Second, reclamation tests whether a
record's risk is RESOLVED rather than whether its state machine has stopped, so
future terminal-but-unresolved states inherit the correct behaviour without being
added to a list.

#### Historical limitations at the initial Phase 4 increment

At the initial Phase 4 increment, direct replacement, canonical risk-limit
configuration commands, terminal tombstone compaction and replicated expiry,
complete reconciliation and ordered venue evidence, position/margin/fee
accounting beyond filled quantity and open holds, Phase 4 metrics, and a Phase 4
model/property/replay corpus were all unimplemented. The 16-slot arrays bounded
retained lifetime records rather than reclaimed live orders, so the kernel would
fail closed after 16 lifetime orders on a slot; slot exclusivity made that arrive
sooner because every requote cycle consumed a fresh slot.

A logical order slot held by an order stuck in `reconciling` still has no
timeout, escalation, or operator override. Phase 4 metrics now expose its age,
but alerting and recovery remain G-074a work owned by Phases 8 and 10. It must
not be "fixed" with a wall-clock timeout in the reducer: that breaks determinism,
and releasing a hold without venue evidence of final cumulative filled quantity
is the G-011 violation reconciliation exists to prevent.

### Model and market-data edge - initial contract layer

A passive market-data research and shadow-quoting package is added under
`model/`. It implements the market-data/feature edge and offline calibration
service described in ADR 0001, and it adds no C++ and changes no accepted
contract.

The plug-in point is a byte contract, not an API. `model/` reimplements the
canonical `DecisionFrame` (schema V2) and `PolicyArtifact` (schema V1) encoders
in Python and asserts byte-for-byte parity against the exact C++ golden corpora:
the 181-byte decision frame hashing to
`9bdc490425ee07a28b4a0fd624d25ea5b167180dc01bae2080d54c0e4c05dcc3` and the
144-byte policy corpus hashing to
`7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30`. A schema
revision on either side fails that test before anything else.

`FeatureSchemaId` is derived from the SHA-256 of the schema's canonical
description, so editing a feature's unit or index changes the identity and the
cluster rejects the scope mismatch rather than evaluating an old rule against a
new meaning.

`simulation/rule_evaluator.py` mirrors `src/v2_rule_evaluator.cpp`: ordered
first-match evaluation, the whole-set feature-availability pre-pass, visit
counting, and the section 3.4 control-mode dominance table including the
`halted` short-circuit. The parity tests pin the written contract; they do not
execute the C++ evaluator. A differential harness over one generated corpus,
equivalent to `v2_model_replay_tests.cpp`, remains the required gate before any
policy authored by this package is activated.

The package is passive structurally rather than by configuration: it implements
no `OrderEffect` encoder, its HTTP transport refuses any non-GET request and any
state-changing path before opening a socket, a static AST audit rejects
wallet/signing imports and order-management definitions across the tree, and the
config loader refuses credential-shaped keys. 109 host tests pass.

Venue verification on 2026-09-04 confirmed the True Markets API base, ES256 JWT
auth, `GET /v1/conductor/assets`, `POST /v1/conductor/quotes`, and
`POST /v1/conductor/orders` (market orders). The documented retail DeFi path is
RFQ-shaped rather than a resting-quote order book, while maker rebates and FIX
order management are advertised on the KYC'd CeFi firm path. If that reading
holds, a passive market-making strategy is not expressible on the venue surface
this account can reach, and the adapter therefore reports a venue-level NO-GO
rather than per-pair scorecards.

This is not a collection, data, strategy, or profitability claim. No data has
been collected, no pair has been studied, no policy rules have been fitted, and
live collection is blocked pending the confirmations listed in
[`docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md).
Detailed design is in
[`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`](docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md).

### Version preservation

- V1 remains recoverable at annotated tag `v1.0.0`.
- Existing V1.1 branches and phase tags remain unchanged.
- V2 begins on branch `v2/architecture-contracts` from the documented V1.1
  storage-design checkpoint.
- V2 will use new wire/storage versions and will never reinterpret V1/V1.1 WAL
  data in place.
- `v2.0.0` will be created only after the release-wide acceptance gates pass.

## [V1.1] - Unreleased

Status: in progress. Stages 1 and 2 are accepted on Raspberry Pi ARM64; Stages
3 and 4 remain pending. No planned item is an implementation claim until it is
moved under **Implemented changes** and supported by tests and target-hardware
evidence.

The authoritative execution specification for this release is
[`docs/V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](docs/V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md).
It defines the phase objectives, wire/routing design, invariants, canonical
retry-amplification metric, experiment scorecard, and pass/fail gates. This
changelog remains the concise release record; the plan is the implementation
contract.

### Objective

Eliminate the leader-routing retry storm exposed by V1 benchmarking, protect
Raft control-plane progress from client load, and establish a repeatable safe
operating envelope before assigning a throughput number.

### Why V1.1 is required

V1 clients receive `not_leader` without a leader identity, rotate blindly to
the next request channel, and retry immediately. Under unstable leadership,
this turns a small logical workload into a large internal workload. The node
owner loop also polls as many as ten client fragments before peer traffic, and
that count is not a time budget because a proposal may perform synchronous WAL
work.

Representative V1 benchmark evidence showed:

- at 20 requests/s, one run scheduled 1,400 total commands, completed 3, timed
  out 1,397, received 63,426 `not_leader` responses, and made 64,826 successful
  Aeron offers: 46.3 successful transport attempts per logical command;
- at 35 requests/s, one run scheduled 2,450 total commands, completed 1,548,
  timed out 902, received 371,289 `not_leader` responses, and made 373,739
  successful Aeron offers: 152.5 attempts per logical command; and
- every Aeron offer-failure and backpressure counter in those two runs was
  zero, so the dominant failure was routing/retry amplification rather than
  Aeron rejecting publication offers.

### Planned implementation stages

#### Stage 1 - Versioned leader-aware routing

- Add `leader_id` to `not_leader` responses, using `-1` when the node does not
  know a leader.
- Introduce a backward-compatible response wire version and tests for legacy,
  leader-aware, malformed, and truncated responses.
- Map configured Raft node IDs to Aeron request publications.
- Cache leader hints with their observed term and ignore stale or non-member
  hints.
- Retry the same request ID directly against the hinted leader rather than
  rotating blindly.

#### Stage 2 - Bounded recovery and admission control

- Add deadline-bounded exponential retry backoff with jitter when no leader is
  known.
- Add explicit `leader_known`, `leader_suspect`, and `leader_unknown` client
  states.
- Stop admitting unbounded new work during a leaderless interval; fail fast or
  use a small time-bounded queue while allowing bounded discovery probes.
- Preserve open-loop benchmark scheduling and record leaderless admission
  rejection instead of pausing the generator and hiding outages.

#### Stage 3 - Owner-loop control-plane priority

- Service overdue Raft ticks and bounded peer traffic before client ingress.
- Flush vote, heartbeat, replication, and acknowledgement work before admitting
  more proposals.
- Bound client processing by fragment count and time/tick slack rather than
  relying only on `poll(..., 10)`.
- Bound peer and response drains so priority cannot become starvation.
- Preserve the single-owner Raft execution model; do not introduce concurrent
  mutation of the Raft server.

#### Stage 4 - Measurement and release proof

- Export raw logical-command, transport-attempt, retry, `not_leader`,
  leaderless-admission, completion, and timeout counters.
- Calculate retry amplification outside the hot path:

  ```text
  retry_amplification = transport_attempts / logical_commands_started
  ```

- Add Grafana panels and alerts for amplification, leader knowledge, admission
  rejection, term/leader churn, owner-loop tick age, and completion rate.
- Repeat the same target-hardware load matrix with fresh state per repetition,
  exact sample merging, stable power, recorded temperature/governor, and no
  unrelated workload.

### Planned new metrics

The final names may change during schema review, but their meanings must not:

- `velocita_client_logical_commands_started_total`
- `velocita_client_transport_attempts_total`
- `velocita_client_retries_total`
- `velocita_client_not_leader_responses_total`
- `velocita_client_admission_rejected_total{reason="leader_unknown"}`
- `velocita_client_completed_commands_total`
- `velocita_client_timed_out_commands_total`
- `velocita_client_leader_known`
- `velocita_client_retry_amplification` as an off-path recording rule, not a
  division performed by the client owner loop

Request IDs, arbitrary error text, and suite IDs must not become metric labels.
They belong in bounded evidence files to avoid unbounded Prometheus cardinality.

### V1.1 release acceptance gates

- Stable-leader retry amplification is approximately 1.00 and no greater than
  1.05 over the declared healthy operating range.
- A request initially routed to a healthy follower normally reaches the leader
  in no more than two transport attempts.
- Leader loss produces bounded retries and bounded admission rejection, not a
  retry burst that grows with event-loop speed.
- A leaderless interval does not cause unbounded pending work or queue growth.
- No peer messages are dropped and no recurring term changes occur under the
  declared healthy load range.
- Every scheduled benchmark command is accounted for as completion, timeout,
  admission rejection, capacity rejection, or explicit protocol/application
  error.
- Same-request-ID retry still applies an increment at most once after an
  unknown outcome.
- Three voters converge after follower restart, leader failover, quorum
  restoration, and full supervised restart.
- Results are repeatable across at least three independent repetitions at each
  claimed rate before a capacity or latency statement is published.

### Implemented changes

#### Stage 1 accepted - leader-aware routing

- Added response wire version 3 with explicit extension flags, `leader_id`, and
  term-aware routing information. The decoder continues to accept V1 production
  and V2 benchmark responses and represents their absent leader as unknown.
- Added validated `RequestTarget {node_id, channel}` configuration. Duplicate
  IDs, duplicate channels, missing member mappings, and non-member mappings fail
  before Aeron publications are created.
- Added a pure C++ leader router that caches `(term, leader_id)`, ignores
  older-term hints, rejects non-member hints, invalidates knowledge on a newer
  unknown/invalid term, and redirects the same request ID to a valid leader.
- Changed `raft-client` and `raft-bench` from anonymous `--request-channel`
  lists to explicit `--members` plus repeatable `--request-target ID=URI`
  mappings.
- Changed the one-shot client from broadcasting every retry to all voters to
  offering to one target and following the returned leader hint.
- Added exact benchmark counters for admitted logical commands and all
  transport attempts. Summary schema 2 and Prometheus evidence now report
  retry amplification without adding work to the Raft node owner loop.

Phase 1 passed its Raspberry Pi ARM64 acceptance gate on 2026-08-27.

#### Stage 2 accepted - bounded recovery and admission control

- Added explicit client leader-knowledge states: `unknown`, `suspect`, and
  `known`. A valid redirect is suspect until an `ok` response confirms the
  leader for the observed term.
- Added a pure monotonic recovery policy with deterministic exponential jitter
  between a positive 5 ms floor and a 250 ms cap. A retry never runs at or
  after its command deadline.
- Added a 250 ms response-silence deadline because successful local Aeron
  publication does not prove remote receipt, quorum commit, or response. A
  silent route becomes unknown and discovery resumes with the same request ID.
- Added an unconfirmed-leadership admission limit of one command and a globally
  paced discovery probe. Further open-loop demand is recorded as explicit
  leader-unknown admission rejection rather than hidden by pausing the load
  generator.
- Extended pending-command state with last target and next retry time while
  retaining the original request ID for every transport attempt.
- Added benchmark summary schema 3 evidence for discovery, admission rejection,
  retry counts, pending high-watermark, leader state, and exactly-one terminal
  accounting. Failure rows now retain attempts, last target, and leader state.
- Added a one-command Pi recovery runner covering leader failover under demand,
  an explicit leaderless/quorum-loss window, bounded recovery, and same-ID
  exactly-once resolution.

The candidate passed production and benchmark-instrumented native tests and a
15-command three-node macOS smoke run at retry amplification `1.00`.

A local leader-removal diagnostic remained bounded—40 of 40 terminal outcomes,
pending high-watermark 2, 58 attempts for 12 admitted commands—but the two
remaining voters did not elect before the eight-second window ended. It
therefore produced 28 explicit admission rejections, 3 timeouts, amplification
`4.83`, and final leader state unknown. This is retained as failed recovery
evidence, not presented as a pass or throughput result.

Phase 2 passed its Raspberry Pi ARM64 acceptance gate on 2026-08-28. The
production image was `015581814255` with benchmark instrumentation disabled;
the diagnostic image was `6cab4df1978b` with instrumentation enabled. A
follower-first/leader-last rolling upgrade preserved quorum, moved leadership
from node 3 in term 89 to node 2 in term 90, and converged all three voters at
log, commit, and applied index 39 with counter value 20.

Recovery suite `20260828T180907Z` produced the following immutable evidence:

- leader failover under 5 commands/s scheduled 75 commands, completed 70,
  rejected 5 during unknown leadership, used 86 transport attempts, bounded
  pending depth at 2, recovered with zero retries in the stable tail, and
  measured retry amplification `1.229`;
- explicit quorum loss scheduled 100 commands, completed 67, rejected 30
  during unknown leadership, used 117 transport attempts and 43 discovery
  probes, bounded pending depth at 2, recovered with zero retries in the stable
  tail, and measured retry amplification `1.671`; and
- unknown-outcome recovery retried request ID `1787940625718131511` after
  quorum restoration and changed the counter exactly once from 20 to 21 while
  the commit index remained held at 208 during the no-quorum check.

Suite `20260828T180440Z` is retained as a test-harness incident, not a product
failure: Ubuntu AWK rejected the convergence observer's `log[...]` array name
because `log()` is built in. The observer therefore returned a false negative
before comparing voter state. Commit `b5d530d` renamed the array to
`current_index`, after which the complete target-hardware gate passed.

#### Stage 3 candidate - owner-loop control-plane priority

- Added an allocation-free `OwnerLoopScheduler` using
  `std::chrono::steady_clock` time points, a one-millisecond Raft tick interval,
  a 250-microsecond client safety margin, and named positive work/queue bounds.
- Changed Raft outbound and completion transfer from whole-deque temporary
  vectors to one-message `std::optional` moves so the owner loop, rather than
  queue size, controls work per iteration without an additional drain-vector
  allocation.
- Reordered the node loop to service overdue ticks, bounded peer ingress, Raft
  output, and peer publication before polling at most one client fragment when
  sufficient tick slack remains. A second bounded peer flush follows client
  admission before bounded response publication.
- Shared pre/post-client drain and peer-offer budgets across the complete
  iteration, preventing two service points from silently doubling the stated
  limits.
- Added a statically dispatched, template-based iteration coordinator and fake
  clock/poller tests that prove tick/peer/client ordering, tick-slack
  suppression, the one-client-fragment limit, and cumulative work budgets.
- Added fixed peer and response queue capacities and stopped client polling
  while the external response queue is full. The existing peer-drop behavior
  remains visible through its counter; committed responses are retained until
  bounded drain capacity is available.
- Advanced the fixed shared-memory metrics page to version 2 and added
  owner-loop iteration count, maximum tick gap, full client/peer poll-budget
  counts, and client-poll skip reasons. Plain owner-thread counters are copied
  every 100 ms; the external exporter performs text rendering and unit
  conversion.

Both normal and benchmark-instrumented builds pass all three native test
executables. A three-process macOS loopback smoke run elected node 1, completed
25 of 25 measured commands at 5 commands/s with 25 attempts, zero timeouts,
retry amplification `1.00`, and p99 `11,719,084 ns`. These are development
results only. A second 50 commands/s development run completed 250 of 250
measured commands with 250 attempts, zero timeouts, amplification `1.00`, p99
`11,921,000 ns`, stable term 1, and no response-backlog polling blocks. Maximum
observed tick gaps were `2,978,375 ns`, `4,744,666 ns`, and `3,598,917 ns` for
nodes 1--3. The processes were stopped immediately after measurement, so their
final one-entry follower lag is not convergence evidence. Deterministic
ordering and configured-budget tests now pass in both build modes. Phase 3
remains a candidate until sustained fairness, overload behavior, follower
catch-up, and the Raspberry Pi acceptance matrix pass.

Raspberry Pi run `20260829T120112Z` was retained as exploratory evidence only
because `vcgencmd get_throttled` was already `0x50000` before measurement. A
clean-boot repeat, `20260829T124828Z`, held `performance` governor and remained
at `throttled=0x0`, but failed the Phase 3 acceptance gate:

- at 5 commands/s, 75 measured schedules produced 50 completions and 25
  leader-unknown admission rejections, with amplification `2.68`, p50
  `395,937,951 ns`, and p99 `828,406,854 ns`;
- at 20 commands/s, 300 schedules produced 64 completions, 230 admission
  rejections, and 6 timeouts, with amplification `5.086`, p50
  `1,041,311,701 ns`, and p99 `1,973,153,543 ns`;
- the 5/s voters stayed converged in term 1, but the 20/s voters ended
  leaderless in terms 7--9, with maximum owner-loop gaps between 1.068 and
  1.471 seconds; and
- post-run Linux pressure-stall information reported CPU `some avg10=0.02`
  versus I/O `full avg10=75.14` and `full avg300=57.25`. Memory remained
  available, no swap was configured, storage integrity checks passed, and the
  kernel reported no current-boot undervoltage or filesystem I/O error.

The state machine remained fast (`commit_to_apply` p99 below 12 us); the delay
was before commit. Code inspection explains the physical amplification: every
log offer opens, appends, `fsync`s, and closes the WAL, while every commit-index
advance rewrites and `fsync`s a metadata temporary file, renames it, and
`fsync`s the parent directory. Three logical voters share one SD card, so one
logical command can impose approximately nine durability barriers. The
control-plane-first scheduler cannot preempt a blocking durability syscall.
Phase 3 therefore remains unaccepted pending an explicit ordered-persistence
design and storage-isolation experiments; increasing retry deadlines alone is
not an accepted fix.

The resulting design checkpoint evaluates a fixed-capacity per-voter block
arena based on the book's memory-mapped ring and double-buffered snapshot
patterns. The accepted direction is bounded memory staging plus asynchronous
ordered batch persistence, not memory-only acknowledgment. It requires an
explicit durable-index completion path because the pinned `willemt/raft`
callbacks require `log_offer`, term, and vote changes to be flushed before they
return. Snapshot and compaction may recycle a WAL prefix only after a
checksummed replacement snapshot is durable. The full proposal, rejected
shortcuts, experiments, and release gates are recorded in
`docs/V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md`.

### V1.1 validation results

Local development evidence on 2026-08-27:

- production build: all three native test executables passed;
- benchmark-instrumented build: all three native test executables passed;
- three-node macOS loopback: an initial request found leader 3 in two attempts;
- forced follower-first route: follower 1 redirected the same request ID to
  leader 3 and the increment completed in exactly two attempts; and
- five-second measured loopback benchmark: 25 logical commands started, 25
  transport attempts, 25 completions, zero timeouts, zero invalid hints, and
  retry amplification `1.00`. One discovery retry occurred during warm-up.

These results validate the development implementation and test harness. They do
not substitute for target-hardware validation.

Raspberry Pi ARM64 acceptance evidence on 2026-08-27:

- the production candidate image `e2b3cb3c5c10` and benchmark-instrumented
  candidate image `53913e4eeec0` each built with all three tests passing;
- a follower-first/leader-last rolling upgrade preserved quorum, caught both
  followers up before leader replacement, and restored three active services;
- the fully upgraded cluster elected node 3 in term 81 and all three voters
  ran the production candidate image;
- request ID `1787831360569930796` reached follower 1 and then leader 3 in two
  attempts, incrementing the counter from 19 to 20 at log index 35;
- repeating that exact request ID returned value 20 and index 35 in two
  attempts, demonstrating at-most-once application and no duplicate log
  admission; and
- benchmark suite `20260827T115818Z` completed 75 of 75 measured commands with
  75 transport attempts, zero timeouts, zero capacity rejections, zero invalid
  leader hints, retry amplification `1.00`, p50 `46,016,965 ns`, and p99
  `101,675,703 ns`.

This closes the Phase 1 gate. It validates routing correctness and efficiency
at the gate load; it is not a certified maximum-throughput claim.

## [V1] - 2026-08-25

Status: functionally accepted for a three-process educational cluster on one
Raspberry Pi 5. Capacity not certified.

### Implemented

#### Native Raft service

- Implemented the service, client, storage, wire protocol, dump tool,
  benchmark driver, and exporters in C++20 with no Java runtime in the final
  image.
- Integrated pinned native Aeron and `willemt/raft` revisions.
- Ran an embedded Aeron C media driver per process and used Aeron UDP for client
  requests, responses, `RequestVote`, and `AppendEntries` traffic.
- Supported one voter or three statically configured logical voters.
- Kept Raft mutation under a single owner thread.

#### Replicated state and correctness

- Added a deterministic replicated counter FSM supporting `increment` and
  replicated `get` commands.
- Replicated stable request IDs and retained FSM results for request
  deduplication.
- Preserved the same request ID across client retries so an unknown-outcome
  command could be resolved without applying it twice.
- Returned responses only after committed entries were applied to the FSM.

#### Persistence and recovery

- Added an append-only, versioned WAL with little-endian encoding, record
  lengths, and CRC32 integrity checks.
- Persisted term, vote, and commit metadata through atomic file replacement.
- Replayed committed state and deduplication results on restart.
- Added `raft-dump` for offline inspection of metadata and log records.
- Failed startup closed on corrupt or torn records rather than silently
  accepting ambiguous durable state.

#### Packaging and service management

- Built a multi-stage ARM64 Podman image that runs tests during the builder
  stage and uses an unprivileged runtime UID.
- Ran each voter in a separate rootless container with its own Aeron shared
  memory and durable named volume.
- Added rootless Quadlet units and a systemd user target for boot management.
- Enabled bounded `on-failure` restart policy and rate limiting so persistent
  faults become visible instead of producing infinite crash loops.
- Used `loginctl` linger so the rootless user service manager starts at boot and
  remains available without an interactive SSH session.

#### Observability

- Added a fixed-layout, versioned shared-memory metrics page per voter.
- Kept Prometheus formatting and HTTP serving in a separate native exporter.
- Exported Raft role, leader, term, log/commit/apply indexes, FSM value, owner
  tick age, Aeron fragments/bytes, publication outcomes, queue depths, and
  observed leadership/term changes.
- Added external host, process, filesystem, microSD, memory, CPU, and Raspberry
  Pi thermal metrics.
- Installed bounded local Prometheus and Grafana services with provisioned
  recording rules, alerts, cluster-health panels, and benchmark panels.
- Verified that stopping observability components does not stop Raft.

#### Operations and testing

- Added a Pi operator runbook, repeatable cluster lifecycle scripts, and a
  bounded all-in-one failure-drill runner with exit-trap restoration.
- Added native core, shared-metrics, and benchmark tests.
- Added an open-loop benchmark driver with intended send times to prevent
  coordinated omission.
- Recorded exact warmup and measured samples, explicit failures, fixed-bucket
  histograms, p50 through p99.99, maximum, counts, and benchmark-only ownership
  timings.
- Reset benchmark cluster state for every repetition and merged exact samples
  rather than averaging percentiles.
- Kept benchmark-only timestamp instrumentation out of the production image.

### What V1 did well

The bounded failure-drill suite on 2026-08-22 passed every automated scenario:

| Scenario | Result | Evidence |
|---|---|---|
| Baseline | PASS | Three voters converged; counter 17; node 2 leader |
| Supervised crash restart | PASS | Node 1 PID changed 1618 to 2610; restart count increased 0 to 1 |
| Leader failover | PASS | Leadership moved from node 2 to node 1 in 1,522 ms |
| Quorum loss | PASS | Commit remained 24 with one voter; same-ID retry resolved once; counter 18 to 19 |
| Observability isolation | PASS | Raft remained available while exporter, Grafana, and Prometheus were stopped independently |
| Exporter-down alert | PASS | Alert fired 18,584 ms after exporter shutdown |
| Final recovery | PASS | Three voters converged; counter 19; node 1 leader |

These results prove process-level restart, majority failover, quorum safety,
same-ID deduplication after an unknown outcome, observer isolation, and follower
catch-up. They do not prove physical high availability because all voters share
one Pi, kernel, power supply, network interface, and microSD card.

Low-rate smoke runs also proved that the complete client-to-quorum-to-apply path
could execute without timeouts. One 15-second run completed all scheduled work
at 1, 3, and 5 requests/s, with measured p99 end-to-end latency of 64.6 ms,
98.1 ms, and 147.2 ms respectively. These are experiment observations, not
release latency objectives.

For one successful 5 requests/s run, benchmark-only attribution reported:

| Stage | p99 |
|---|---:|
| Client offer to matching response | 147.2 ms |
| Local WAL append | 45.7 ms |
| Quorum append | 56.1 ms |
| Admission to commit | 143.1 ms |
| Commit to apply | 6.0 us |

The spans overlap and must not be added. They showed that FSM application was
cheap while persistence, quorum progress, and scheduling dominated the
observed latency.

### What failed or remained unproven

#### Retry amplification and unstable progress

The load driver exposed a liveness failure mode rather than a clean throughput
knee. A short rebaseline run completed all measured commands at 5, 10, 20, and
30 requests/s before timing out work at 40 requests/s, but a later three-repeat
sweep was highly non-monotonic:

| Offered rate | Measured scheduled | Measured completed | Measured timed out |
|---:|---:|---:|---:|
| 20/s | 3,600 | 0 | 3,600 |
| 25/s | 4,500 | 47 | 4,453 |
| 30/s | 5,400 | 322 | 5,078 |
| 35/s | 6,300 | 1,248 | 5,052 |
| 40/s | 7,200 | 687 | 6,513 |

Higher offered rate sometimes completed more work than a lower rate. That is
not a valid capacity curve; it is evidence that leadership state and retry
feedback dominated the experiment.

The client protocol did not include a leader hint. On `not_leader`, the driver
rotated to the next node and retried immediately with no backoff. This created a
positive feedback loop: rejection caused retry traffic, retry traffic consumed
owner-loop time, delayed consensus progress caused more rejection, and the
cycle repeated.

The node owner loop ran an overdue Raft tick first but then polled up to ten
client fragments before peer traffic. Because a proposal can include
synchronous WAL work, a fragment-count limit was not a meaningful time budget.
V1 therefore lacked protected scheduling for heartbeat, election, and
replication work.

Consequently, V1 has no certified sustainable throughput or latency SLO. The
correct conclusion is not “capacity is 5/s” or “capacity is 30/s”; the correct
conclusion is “repair routing, retry, admission, and scheduling behavior, then
repeat the experiment.”

#### Known product limits deferred beyond V1.1

- Three voters on one Pi are one physical failure domain, not high availability.
- Static membership only; no joint-consensus membership changes.
- No snapshot transfer or WAL compaction; the WAL grows indefinitely.
- Torn/corrupt final records fail safely but require manual operator recovery.
- Reads are replicated log entries rather than lease or ReadIndex operations.
- Synchronous persistence remains in the proposal path.
- No authenticated or encrypted Raft/client transport; endpoints are restricted
  to loopback for the V1 topology.
- No representative matching-engine, market-data, risk, or venue operation
  classes; the workload is still a replicated counter.

These are real limits, but they are intentionally not mixed into V1.1's retry
and liveness scope. A release should have one coherent risk-reduction objective
rather than becoming an unbounded rewrite.

### Benchmark and lab incidents separated from product findings

- An early benchmark readiness probe exhausted its small `/dev/shm` allocation;
  the harness was corrected to provide explicit Aeron shared-memory capacity.
- A benchmark client later exited with signal 135 (`SIGBUS`) when mapped Aeron
  term buffers exceeded its container shared-memory limit; the driver container
  was raised to 512 MiB.
- A sudden Pi power loss produced unclean journal warnings and a Podman overlay
  link warning. Subsequent `podman system check --quick` completed and the
  production cluster recovered, but this was an infrastructure incident, not a
  controlled Raft durability test.
- Kernel logs recorded historical undervoltage events. After changing the power
  source, `vcgencmd get_throttled` returned `0x0`; benchmark temperature rose
  from approximately 52.4 C to 60.1 C without thermal throttling.
- The harness now resets disposable benchmark volumes for each repetition,
  increases client shared memory, preserves immutable evidence, restores the
  production target through an exit trap, and stops disposable voters before
  capturing their final role/term/index status.

Keeping these incidents separate prevents infrastructure and harness defects
from being misreported as consensus-algorithm defects while still retaining
them as operational lessons.

### Recommended change for V1.1

V1.1 should implement the four staged changes documented in the unreleased
section: leader-aware routing; bounded retry and admission control;
control-plane-first owner-loop scheduling with bounded client work; and
off-path retry-amplification measurement followed by repeatable target-hardware
validation.

### Evidence references

- [`docs/V1_FAILURE_DRILL_RESULTS.md`](docs/V1_FAILURE_DRILL_RESULTS.md)
- [`docs/BENCHMARKING.md`](docs/BENCHMARKING.md)
- [`docs/PI_OPERATOR_RUNBOOK.md`](docs/PI_OPERATOR_RUNBOOK.md)
