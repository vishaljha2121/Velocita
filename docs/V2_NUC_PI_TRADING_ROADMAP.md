# V2 roadmap to a trading pilot on one NUC and one Raspberry Pi

Date: 2026-09-21. Status: owner-approved roadmap, not live-deployment or new
byte-contract approval. Checkpoint progress: `evidence/V2_REMEDIATION_CHECKPOINTS.md`.
Basis: `evidence/V2_PHASE6_9_FULL_AUDIT_2026-09-21.md`, ADR 0001, proposed
ADR 0002, the original implementation plan and end-state guarantee checklist.

## 1. Destination and honest availability

Deliver a **two-machine, fail-closed trading pilot**, not high availability and
not a profit/leaderboard guarantee. No perpetuals, native amend, overlapping
replacement, economic netting or optional V2.2 work. Keep the existing resting-
order machinery; True Markets swap support remains a separate proposed contract.

Approved roadmap topology (implementation/deployment gates still apply):

| Host | Responsibilities | Persistent authority/secrets |
|---|---|---|
| NUC | V2 voter; separate gateway process; passive market edge; operator control; disposable exporter/dashboard | Its own WAL/snapshots; gateway-only trading credentials |
| Pi | Second V2 voter; independent health observation; verified backup/evidence copy | Its own WAL/snapshots; no trading credentials or automatic gateway promotion |

Two voters require both for quorum. Losing either host or their link stops new
commits and, within the declared authority bound, new gateway sends. NUC leadership
is preferred operationally, not a safety assumption: the Pi may become leader
while both remain connected, and the NUC gateway must use the actual authority.
Do not change membership to one automatically during an outage.

Existing venue orders can remain live and fill after quorum/gateway loss. A stop
is not proof of cancellation or a flat account. Keep reservations and reconcile.
Do not promise new emergency cancels can be committed without quorum. Already-
committed cancels remain risk-reducing; a venue cancel-on-disconnect or any
out-of-band emergency policy needs its own verified contract and evidence.

Why this rather than 2 voters on NUC + 1 on Pi: the latter can continue when the
Pi is lost, but two counted copies share one host and storage/power risks. It
does not survive NUC loss and complicates the first pilot for asymmetric benefit.
Why not one NUC voter plus a passive Pi: simpler and more available, but a success
would not promise a second-host durable copy. That requires an explicit weaker
deployment contract, not a runtime fallback. Neither alternative is approved here.

Separate gateway rationale: venue stalls, credentials and TLS live outside the
consensus process; bounded authenticated IPC makes ownership explicit. It adds
IPC/serialization and freshness costs, which must be measured. In-process is
possible with a separate event-loop owner, but does not fix stale authority and
puts a larger fault/security surface in the voter. This recommendation is based
on ADR 0001, not an unread external book.

## 2. Fix order before feature expansion

### Owner scope update — 2026-09-24, CP-051

The owner keeps one voter per host for MVP. A 2-on-NUC + 1-on-Pi layout is
deferred until a compute budget is measured. Pilot E1 is native log catch-up
(pinned, mutation-covered) plus offline lost-voter repair before live canary;
the online snapshot installer and its E3 drain move to the three-voter release.
Next work is M2 (startable node). See `evidence/e1_two_voter_2026-09-24/`.

### Owner completion rule — 2026-09-23, CP-037

2026-09-24 execution update (CP-047): owner requests parallel completion of the
EXISTING E1/E2/E3 exits, then the startable V2 node. Maximum two implementation/test
agents, main owns architecture and integration. No additional feature workstream
is authorized. Nonessential findings belong in the next iteration; do not defer
an original safety guarantee or report a target gate as passed without evidence.

E1 and E3 share generation-owned storage and the same serial writer: one agent
owns that implementation, the other owns disjoint adversarial tests/E2 work,
with main reviewing integration. Do not assign overlapping independent rewrites
of `v2_replication.cpp`. Native host behavior precedes the node shell; authenticated
cross-process behavior and NUC/Pi acceptance necessarily require that shell.
This dependency is not permission to mark the deployed exits closed early.
E2's independent-destination requirement was already identified in CP-038 §6.
CP-048 records subsequent owner approval of fresh authenticated verification
(persistent receipts deferred) and its native compaction/recovery integration.
Actual authenticated copy transport and independent-host/target evidence remain
required; the exact native capability is not a network protocol or deployment gate.

Measure remaining F5/F6 work by three finished behaviors, not helper/test counts:

| Exit | Observable completion | Work it authorizes |
|---|---|---|
| E1: lagger catches up safely | Real lagger crosses compacted history; state, voting promises, suffix and live result obligations preserved across failure/retry | Integrated installer, required archive replay/provenance, authenticated transport and activation tests |
| E2: compaction preserves verified history | Actual compaction and restart leave exact committed evidence recoverable; missing coverage refuses deletion | Durable archive publication/catalog, quota admission, coverage-bound reclamation and crash tests |
| E3: persistence does not block protocol owner or expose early success | Actual delayed/failed disk operations leave health polling responsive and withhold vote/ACK/apply/result/proof until permitted | Shared asynchronous persistence owner, resumable/quarantined Raft turns, generation fencing and syscall/output tests |

Every additional task must name E1/E2/E3 or a separate approved launch requirement
(for example M3a observability, authenticated process integration or target gates).
Otherwise defer it. Component GREEN and larger CTest counts close none of these
exits. CP-037's two contract witnesses enable E1 only. Archive provenance enables
E1/E2, and the shared durability fence enables all three; implement their actual
integrations rather than opening further unrelated helper work.

All three exits remain open. This prioritization does not waive byte-contract
review, the independent live archive copy, or the deferred Pi acceptance gate.

Every row starts with a permanent RED in its owning suite, using the retained
audit probe as the initial trace. Show intended refusal/state/bytes, implement
the smallest GREEN, run focused and full regression, then mutation-check the
important guard. Preserve raw RED/GREEN evidence; do not change the oracle to
bless current behavior. Main agent owns architecture; at most two cheaper
subagents may implement disjoint approved tasks.

| Order | Work | Audit IDs | Done when |
|---|---|---|---|
| F0 | Preserve audit baseline, reproduce probes, correct gate claims | all | Every finding has an owner, regression and acceptance condition; no false phase closure |
| F1 | Emergency planner carries/consumes both quotas; zero-authority bootstrap refuses before creating risk | A02,A06 | Real planned sends use reserve; submits never do; every authorized effect reconstructs fully |
| F2 | Reject invalid normal ingress; fix multi-block flush; secure token output and secret transport | A05,A10,A11 | Invalid bytes leave log healthy; unique records survive batched flush; dummy secret tests prove private files/no argv leakage |
| F3 | Fail closed on unsafe wire lowering; bind metadata/capability identity | A03 | Limit/post-only/TIF cannot disappear; wrong metadata and same-product/different-factor cases refuse |
| F4 | Quorum-confirmed authority and final-send freshness | A01 | Old-leader/late-proof/restart/kill traces cannot renew or send after the declared bound |
| F5 | Persist static cluster identity; bound replication queues/log retention; implement safe peer snapshot install and audit-retention boundary | A04,A07,A08,A14 | Config drift refuses; saturation stays bounded; follower catches up; historical evidence is archived before its last copy is reclaimed |
| F6 | Hard-state cut matrix and asynchronous durability fencing | A09 | Required term/vote/log ordering survives cuts; no routine owner-thread sync; disk errors fail closed and are observable |
| F7 | Complete manifest closure and re-gate corrected platform | A12 | Same externally pinned candidate passes host, NUC, Pi and mixed replay tests |

F1/F2 are small existing-contract corrections. F3 has an immediate no-new-contract
refusal fix, then a metadata-binding review. F4–F6 are the larger architectural
work: review them before implementation, not while freezing their bytes.

2026-09-22 checkpoint: F4 host-library composition is GREEN at CP-012; this does
not close deployed authority/fencing/timing guarantees. Next is F5's persisted
identity/domain, bounded queues/history, snapshot catch-up and archival contract
review, then F6 completion. Process integration and M3a observability precede
live acceptance; F7 re-gates the final candidate, including the deferred Pi.

CP-013 starts F5/F6 in parallel: snapshot input bound and synchronous persistence
failure latches are host GREEN. The owner subsequently APPROVED the combined
review/ADR0014 direction, including provisional atomic replacement. Exact identity
bytes, numerical budgets, archive/install envelopes and asynchronous Raft fencing
still need review; membership/recovery/archive REDs and asynchronous durability
are not closed. Parallel workers do not waive review or the integration gates.

CP-014 adds sync-sensitive metadata ordering evidence (file-sync-only and
directory-sync-only mutants both fail the recovered-value oracle) and proposes
the exact F5 startup-identity frame. Host44/44 Release/sanitized is not target
acceptance. Startup binding alone is not lineage binding for foreign history;
storage/peer/install/archive envelopes and bounded async ownership stay on F5/F6.

CP-016: approved identity ownership is now mandatory kernel startup; raw/default
construction is removed. A04 startup drift is host GREEN with typed unchanged-tree
refusal and healthy recovery (47/47 Release/sanitized). Foreign-history/peer lineage,
replacement catch-up and verified archive reclamation remain open; the latter two
still have current-source RED diagnostics. Next review their shared-lineage byte
boundaries, not the already-approved startup contract. No target acceptance yet.

After CP-017 the owner approved all six history/peer binding recommendations,
including record-level local WAL binding and block-WAL-only fresh deployments.
Exact frame bytes, capacities and concrete publication/replacement protocols still
need review before implementation. Remaining F5 work, in dependency order:

1. Identity-bound storage/peer formats and pre-mutation refusal, including missing
   voting-memory handling; preserve V1 and prior-lineage evidence.
2. Quantified whole-node byte/entry/work budgets, pre-admission reservations,
   emergency control headroom and explicit saturation/backpressure behavior.
3. Safe replacement admission and bounded transactional snapshot transfer/install,
   compatible suffix catch-up and interrupted-install recovery. Matching identity
   does not authorize voting after lost hard state.
4. Verified committed-history archive before last-copy reclamation, with bounded
   archive-outage admission and declared independent-copy/retention requirements.
5. Integrated adversarial/mutation/saturation/crash evidence and updated acceptance
   record. F7 supplies final frozen NUC/Pi/mixed validation; no host-only claim can
   waive it. F6 async fences remain separate and must compose safely with F5.

A two-slot hard-state journal is an option, not an automatic rewrite: compare it
with tested atomic replacement. In either design, safety comes from persistence
ordering and recovery evidence. Never remove fsync to improve a benchmark.

## 3. Milestones to first trade

CP-026 parallel execution/checkpoints and explicit MVP deferrals are in
`F5_F7_MVP_CLOSURE_PLAN.md`. Worker error propagation and approved local/peer
identity binding plus mandatory hard state are host GREEN. The receiver now
enforces message-verified-prefix commitment; its synthetic regression is not a
proven honest-sender divergence trace. The approved §7 ledger and §8 follower
saturation protocol are now kernel-integrated, including startup and suffix
ownership. CP-025 also integrates approved outbound/log bounds. Current-source
catch-up and archive-loss REDs and the remaining parallel completion plan are in
`F5_COMPLETION_AND_NUC_PLAN.md`. Replacement/install/archive and remaining work
budgets need their material contract review; no new protocol was encoded in
CP-026. NUC shadow is the nearest deployment milestone, not live-trading approval.
F7 cannot
be closed by relabeling host evidence while the Pi gate is deferred.

### M0 — Admission decisions and reproducible baseline

Approve topology and gateway ownership. Freeze findings/reproductions. Separately
resolve whether the chosen True Markets surface can meet identity, exact intent,
bounded execution and attributable settlement requirements. Do not wait on venue
economics to fix the platform, but do not build an assumed swap contract either.

Exit: explicit open/approved choices and a candidate manifest covering actual
inputs. No new acceptance tag simply because the inherited suites pass.

### M1 — Correct the library boundaries (F1–F6)

Complete the fixes above. Test one-, two- and three-voter cases: two is the pilot,
three is necessary to reproduce majority/old-leader separation. Bound peer-message
batch bytes and validation work, not only WAL staging. Specify slow-consumer
policies and prioritize peer, durability and emergency service.

Exit: all audit REDs become meaningful permanent GREENs, no unsafe bootstrap,
current quorum authority proof, bounded whole-node memory, durable identity,
tested snapshot replacement and hard-state cuts. No shrinking the original
guarantees without owner approval.

### M2 — Startable V2 node and non-trading process shell (S1)

Implement the node binary: immutable startup config/cluster ID, distinct V2 data
paths, startup recovery barrier, peer transport, bounded normal/control ingress,
writer lifecycle, result correlation, safe shutdown and storage-fault latching.
Authenticate the caller before mapping it to an operator CallerId; a numeric band
is not authentication. Bound malformed input before allocation/log admission.

The Raft owner alone mutates protocol/reducer state. Persistence owns disk work.
Gateway/edge exchange immutable values or stable descriptors, never borrowed
TradingState references across threads/processes. Replay must have delivery
structurally unavailable. Bind source/config/schema identity before participation.

Exit: two independent local processes restart, partition, rejoin and replay;
no venue send capability is enabled. A SIGKILL test is not a physical power-cut test.

### M3 — Deploy the shadow platform on NUC and Pi (S2/S3)

Create isolated services/containers, credentials-free voter configs, persistent
storage ownership, bounded shared telemetry pages, external exporter and minimal
dashboard. Use existing Linux if suitable; a GUI alone is not a reason to reinstall.
Record hardware/storage/filesystem/power/thermal state rather than assuming it.
Service/OS/storage changes on either machine require operator authorization.

Gate the corrected source on NUC first, then physical Pi and the mixed two-host
cluster. The owner may postpone Pi setup while developing, but not approve live
deployment on an untested two-host topology. Require identical canonical replay
digests, protocol refusal on mismatch and measured storage/lease envelopes.

Exit: inspect authority, stop/restart, see unknowns and restore backups without
dashboards granting authority. Both-host and link-loss drills stop new risk;
no claim of surviving either-host loss with write availability.

### M3a — End-to-end observability and machine/subsystem health (explicit gate)

Owner addition, 2026-09-22: deliver a detailed Grafana dashboard and health monitor
that locate the failing/waiting stage and explain why. This is a required pilot
stage, not optional polish. Introduce the metric/reason vocabulary during F4–F6
and process integration; deploy the read-only pipeline in M3, exercise it in M4,
and require fault-drill evidence in M6 before any live canary. Do not stop current
F4 correctness work to build dashboards against processes that do not exist yet.

The diagnostic path is:

`source -> ingest -> evaluation -> risk -> Raft admission -> durability/quorum ->
apply/outbox -> authority request/proof -> final send -> venue evidence ->
replicated reconciliation/settlement`.

Every stage needs last progress/age, ingress and completion counts, bounded queue
occupancy/capacity, refusal/failure reason, wait reason, and relevant latency.
Do not infer a root cause from an absent downstream event alone: show the last
confirmed stage, explicit upstream reason, and unknown/missing telemetry separately.

| Dashboard / monitor | Required explanation |
|---|---|
| Operator overview | Trading enabled/stopped, reason, unresolved exposure/unknowns, quorum and current gateway generation; telemetry age on every summary |
| Execution path | Counts and delays between stages; schema/catalog/stale-data/risk refusals, authority drain/expiry/correlation failure, quota/evidence backpressure, transport ambiguity and settlement lag |
| Consensus and storage | Leader/term/membership, both voters' connectivity; appended/durable/committed/applied gaps; WAL blocks, writer latency/errors, fsync/checkpoint state, snapshot/catch-up/retention and disk headroom |
| Gateway and venue boundary | Pending proof/result age, local stop latches, prepared/in-flight/unknown outcomes, retry/query/evidence budgets, rate limits, reconciliation progress and last successful observations |
| Data, policy and risk | Source freshness/gaps/generation, active verified catalog/policy versions, decisions/refusals, reservations/balances/inventory and invariant failures; never equate empty data with zero exposure |
| Every service | Process heartbeat versus useful progress, restart/crash count, loop stalls, queues and resource saturation; passive model/edge and observability pipeline included |
| NUC and Pi | CPU/load, memory pressure/OOM, filesystem/inode/free-space, disk latency/errors, network loss/latency, uptime/reboots and available temperature/throttle/power signals; unavailable sensors explicitly marked |

Health states must distinguish ready, intentionally stopped, waiting for quorum,
backpressured, stale, recovering, failed and unknown. Missing/stale samples are
not healthy zeros. Two-voter link/host loss is expected fail-closed unavailability,
not evidence of a bad election algorithm. A process can be alive yet unable to
make safe progress; report liveness and readiness separately.

Use bounded metric labels (host, subsystem, stage, fixed reason), not order IDs,
timestamps or free-form errors. Correlate a specific request/effect with structured
redacted event records/logs; preserve the mapping between request identity,
StateVersion and Raft index rather than assuming those counters coincide. Never
export credentials, signatures, wallet material or unredacted signing payloads.
Telemetry uses disposable bounded snapshots/shared pages: no scrape or dashboard
query may block the Raft owner or read live mutable state across threads.

Alerts need an explanation, severity, duration/staleness threshold and tested
runbook link. Cover no quorum, no applied progress, storage failures/full disk,
stale feed, unknown exposure/evidence backlog, exhausted capacity, stopped/stale
gateway, missing host/exporter and resource/thermal pressure. Target measurements
must set production thresholds; no arbitrary live values are approved here.
Choose notification delivery with the owner before configuring external messages.

Exit: for each required fault drill, Grafana identifies the affected stage and
its observed reason; alerts and recovery/clear transitions are demonstrated.
Collector/exporter/Grafana failure cannot authorize a send, suppress a kernel
refusal or block emergency service. Preserve raw evidence and dashboard/config
versions alongside the same source/config candidate. Detailed presentation polish
may wait; causal diagnostics, whole-system health and actionable alerts may not.

### M4 — Real data into a structurally non-sending shadow loop (S4/S5)

Owner approval 2026-09-27 (CP-059): GET-only, credential-free True Markets calls
for this milestone; every order send is mocked by the simulator. No keys, no
POST, no `/orders` path.

Ingest verified asset identity/units and bounded source events into DecisionFrame.
Execution evidence alone becomes VenueObservation. Research/model remains
passive, credential-free and unable to submit orders. Drive startup recovery,
evidence obligations and authority renewal against the node, using the simulator
instead of a real sender. Document tick units and monotonic-clock semantics.

Exit: long-running, bounded shadow process on actual hardware; disconnects,
stale data, exporter loss and normal-queue saturation cause the intended behavior.
No unverified quote parser, numeric clamping or source-counter reset on restart.

### M5 — Venue contract closure and adapter lifecycle (Phase 9 gate)

This is an independent critical path, not an implementation detail:

1. Establish supported spot/swap surface and exact instrument/network identity.
2. Verify order/effect identity, idempotency scope/retention and ambiguous-prepare
   behavior. An unqueryable outcome stays unknown; it never licenses a fresh send.
3. Verify executable limits, fees/gas, maximum debit, minimum credit, signing
   payload, recipient, validity and finality. Forecast output is insufficient.
4. Define initial account/inventory state and accepted balance/fee/fill evidence;
   never fixture-inject balances or infer fills from aggregate balance changes.
5. Review any needed proposal/reservation/effect/observation and snapshot changes
   BEFORE encoding. Quantity basis and bounds must survive the whole chain.
6. Bind exact instrument metadata, official fixtures, response/error classification,
   reconnect and complete query windows. Retain raw redacted evidence.

Owner-operated quote capture can help with response units/economics; it does not
establish executable limits or uniquely measure markup. Do not contact True Markets
this iteration. If evidence cannot establish the required safety contract, live
admission stays blocked; an alternative verified spot venue is an owner decision,
not an automatic fallback or reason to add perpetuals.

Exit: an explicitly authorized controlled lifecycle from intent through commit,
send, settlement, replay and reconciliation. No autonomous strategy enabled.

### M6 — Operating controls, recovery and full failure drills (Phase 10)

Finish authenticated kill/reset; attributable stuck/conflict resolution (G-074a)
without erasing history or releasing unexplained exposure; bounded evidence
archival and restore; alerts and tested rollback. Preserve causal audit after
WAL compaction: a current snapshot is not the full history of past emergency actions.
State-version/effect directive index and Raft log index must be mapped explicitly,
not assumed numerically identical after retries.

Drill NUC loss, Pi loss, one-way/two-way partition, paused/zombie gateway, slow
disk, disk full, torn metadata/snapshot, peer catch-up, lost response, late fill,
duplicate evidence, stale feed, quota exhaustion and broken observability.
Prove safety separately from availability and recovery time. No manual promotion
without physically/externally fencing the old sender and resolving uncertainty.

Exit: operator can stop, explain, reconcile, restore and roll back using tested
commands; unknowns cannot look healthy; frozen source/image/config evidence.

### M7 — Strategy validation and bounded live canary (Phase 11)

Choose the actual strategy only after venue economics and capabilities are known.
Complete no-lookahead replay, simulator/fault stress, shadow and any available
test-venue/paper stage. If no equivalent sandbox exists, state the missing evidence
and obtain explicit approval for the first controlled real-money test—do not
label simulator success venue UAT.

Before enabling: reconcile starting inventory; set explicit approved limits for
per-order spend, aggregate hold, inventory, fees, loss, rate, stale data and
unknowns; validate breakers and operational response. A nominal $20 allocation
is not a proven hard loss bound without debit/fee/execution constraints.

Exit: owner-approved canary, complete attribution and reconciliation, no unexplained
residuals, and reviewed expansion. Volume or profit alone is never acceptance.

## 4. Work that may wait, with triggers

- Perpetuals, funding/liquidation/ADL; native amend/overlap; netting: excluded.
- Automatic gateway failover and a third voter host: later release; manual
  fail-closed operation first. Do not build an unsafe promotion shortcut now.
- Micro-optimizations (zero-copy, kernel bypass, aggressive batching): after
  full-process target measurements identify a bottleneck.
- Long-lived policy/source/caller reclamation: may remain bounded for a declared
  canary with stable role IDs and an explicit publication budget; required before
  the actual run exceeds its lifetime ceiling. Snapshot restore does not reset it.
- Cosmetic dashboard polish: later. M3a's detailed execution-path diagnostics,
  machine/subsystem health, freshness and tested actionable alerts are required
  before money, not deferred as polish.

Do NOT defer authority, economic fidelity, evidence retention, identity/retry,
bounded admission, durable recovery, token safety or operator stop to accelerate
first trade. Those are the system, not optional features.

## 5. Approval boundaries and pacing

Next review choices: (1) two-voter fail-closed topology, (2) separate NUC gateway,
(3) authority-proof and cluster/membership/snapshot protocols, (4) exact metadata
binding, (5) venue economic contract. They are distinct decisions; agreeing to
the roadmap does not approve unspecified bytes. Physical fault drills and live
orders each require explicit operator authorization.

Track milestones by demonstrated exit criteria, not a misleading overall percent.
M1 and venue discovery can progress in parallel; M2/M3 establish useful shadow
deployment while M5 remains blocked. M5 and M6 join before M7. The largest work
is authority/persistence/process integration and the still-unverified venue
contract, not decimal formatting. Do not promise a trading date until those are
closed; report remaining REDs, decisions and gates after each increment.
