# Current development handoff

## Cluster upgrade checkpoint approved; owner halt verified — 2026-10-15

Owner halt committed: sequence10, index16, term5, mode5. All three voters confirmed
halted/applied16; driver inactive at sequence2/next3, account flat$25/no effects.
**Next operator sequence11. Do not rerun halt10.** Owner approved the scoped upgrade-tool
checkpoint; committed-source staging and owner preflight are next. Nothing installed yet.

Qualified `ba299ff` images are packaged and verified on NUC1, Pi and NUC2. The runtime
payload is staged on NUC1 with all four hashes checked. The halted-only upgrade wrapper
passes eight tests and three mutation checks; the three previous installer tests also pass.
[Upgrade procedure and evidence](evidence/automated_launch_2026-10-15/strategy-grant/deploy/README.md).
After owner runtime upgrade: rolling voter/exporter pairs, then replace only the stopped
strategy driver's client image. Preserve incarnation, WAL, journals, funding and sequences.
No grant or driver restart yet. Review drains October16 13:41:34UTC; expiry13:48:34UTC.

## Strategy grant qualified on both architectures — 2026-10-15 22:35 UTC

Executable source `ba299ff6c89392177cfd4702fda11dabb7ee1719` is qualified on NUC1 and Pi.
Each passed 59 CTest targets, 407 Python/CLI checks, eight process cases and nine continuous
cases /54 settlements. All308 collected artifact hashes verified. Three controlled ordinary-read
outages per host recovered before signing effect4. No duplicate create/sign/execute in any case.
[Final result, warnings, limitations and next deployment step](evidence/automated_launch_2026-10-15/strategy-grant/native/ba299ff/RESULTS.md).
Original Pi failure remains preserved; its exact operation/root cause is still unproven.

**Qualified only: not staged, installed or activated.** Production images and journals unchanged.
At22:31UTC driver inactive, gateway/signer active; producer index15 flat$25, zero effects/fees/pending.
At22:32UTC all three voters up/ready, term5/leader2, commit/applied15. No fresh TM wallet read.
Next: prepare coordinated single-cluster voter/runtime/client upgrade while owner-halted, then
review/commit the one-time strategy grant before resuming. Preserve driver sequence2(next3);
verify expected operator sequence10 before any ceremony. No grant sent. Review drains
October16 13:41:34UTC and expires13:48:34UTC. Gateway-only upgrade remains insufficient.

## Read-recovery drill checkpoint committed — 2026-10-15 22:24 UTC

Approved scoped checkpoint `ba299ff6c89392177cfd4702fda11dabb7ee1719` is committed.
Only tests and evidence changed; production source is byte-identical to `b490d68`.
Exact-build NUC1/Pi qualification is running concurrently from preserved caches with fixed
stop-on-failure suites. [Progress commands](evidence/automated_launch_2026-10-15/strategy-grant/native/ba299ff/COMMANDS.md).
Not qualified, staged or installed yet. Original Pi failure and the test mismatch investigation
remain preserved. No driver restart, operator grant, deployment or funds movement is authorized
by this qualification step. Production readback remains the historical22:06UTC observation below.

## Strategy grant native status — 2026-10-15 22:06 UTC

Checkpoint `b490d68` is committed. NUC1 passed 59 CTest targets, 407 Python/CLI tests and
54 continuous fixture settlements. Pi compiled but failed its first continuous case after
three settlements, before signing effect four. Stop-on-failure evidence is preserved.
A bounded diagnostic baseline and three-scenario set passed, but do not explain or erase
the original failure. Pi is **not qualified** and no deployment occurred.
[Native results and investigation](evidence/automated_launch_2026-10-15/strategy-grant/native/RESULTS.md).

A test-only correction now passes locally: production read-recovery settings plus a controlled
ordinary-state-read outage. Three cases/18 settlements and two fresh-compiled mutant kills.
Owner approved the scoped test/evidence checkpoint and new native qualification; no deployment.
Production trading logic is unchanged. The exact
original failed operation remains unknown; do not label this a proven root-cause fix.
At22:06UTC driver inactive, gateway/signer active; producer index14 flat$25, no effects/fees.
Preserve strategy sequence2/next3, operator next10 subject to verification, all journals and
funding. Coordinated upgrade and explicit operator grant remain pending; gateway-only
upgrade is insufficient. Review drains October16 13:41:34UTC, expires13:48:34UTC.

## Strategy permission correction implemented locally — 2026-10-15 21:06 UTC

The approved explicit operator grant is implemented and tested; it is not committed, native-qualified
or deployed. [Evidence and deployment proposal](evidence/automated_launch_2026-10-15/strategy-grant/README.md).
The grant binds the opening, strategy caller, minimum sequence and control epochs; default absence
preserves old refusals and snapshot bytes. It changes only direct swap-intent admission, retaining
the existing risk and protected-signing checks. ADR0016 records this direct-intent exception.

Eleven CTest targets and eight CLI tests pass; 15 freshly compiled mutants fail at their named
assertions. Three six-trade fake-venue cases now use authenticated strategy caller21, including
crash recovery. An old/new fixture replay has identical code71 and snapshot hash for requests1/2.
The grant adds 72 fixed state bytes; the full state stays within its unchanged 80KiB budget.

Fresh readback21:02:26UTC: driver inactive, gateway/signer active, account index14 flat$25,
zero effects/pending/fees. No venue wallet read or live changes during this implementation.
Next: scoped checkpoint, exact-build NUC/Pi qualification, then coordinated single-cluster upgrade
while halted before owner grant/resume. Preserve strategy sequence2(next3), funding/WAL/journals.
Expected owner sequence10 must be reverified. Old readers cannot process a granted state;
no gateway-only upgrade or rollback to old readers after activation. Epoch advancement invalidates
this one-time grant; renewal is not implemented. Review drains October16 13:41:34UTC.

## Focused correction batch natively qualified — 2026-10-15

**Executable checkpoint `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc` is qualified on NUC1/x86_64
and Pi/aarch64; now owner-installed, with trading still blocked.** Read the
[batch report](evidence/automated_launch_2026-10-15/focused-mvp/README.md) and
[native evidence](evidence/automated_launch_2026-10-15/focused-mvp/native/RESULTS.md).
Each architecture passed 59 CTest targets, 383 Python/CLI checks, eight process-drill cases,
and three fixed repetitions of three six-effect continuous cases: 54 fixture trades, exactly
one create/sign/execute/settlement per effect. All 151 retained artifact hashes per host verified.
Local 33 named mutation kills remain recorded separately. Fake economics do not show alpha.

Commits: `c19a675` fixes F1–F5; `887d6f5` corrects the test packaging boundary; `26b1e51`
uses fresh Raft deadlines after blocking ports; `1f8d7b6` separates signer connection wait
from the accepted request deadline. Failed native attempts and independently reproduced
regressions are retained. Compiler warnings are recorded for the cleanup iteration.

Owner installation succeeded after the scoped `24ecbec` tool qualification: exact `1f8d7b6`
binary/manifest installed, missing caller keys restored create-only, retired originals retained,
observer updated. No funding/journal reset or live startup. Gateway inactive, signer retains prior
failed state, both PID 0 and RefuseManualStart=yes; driver inactive with no sequence state.
See [owner installation evidence](evidence/automated_launch_2026-10-15/focused-deploy/OWNER_INSTALL_RESULT.json).
Observer timer is active; collector still exits 1 before first-start journals exist, which its
snapshot requires. Recheck after startup rather than claiming monitoring is fully operational.

Tools `24ecbec` passed 13 installer and 24 observer tests on NUC1. Executable remains `1f8d7b6`;
one production cluster and all funding/history are preserved. Sudo remains owner-only.
The six-effect driver is already staged: never stage it again or reset its sequences.

Prepared [first-start/seal wrapper](evidence/automated_launch_2026-10-15/focused-deploy/START_AND_SEAL.md):
8 new tests and 6 mutation kills; 13 installer and 19 seal-dependency checks pass.
Fresh voter health agrees on halted mode 5, term 1, leader 3, index 7. Protected owner
preflight and native startup admission are still pending; no services have started.
The helper is committed as `f30e6cc`; all 64 Python checks passed on NUC1. Its exact
source and owner command are staged and hash-verified. Await owner execution; services
remain guarded/stopped and the driver remains inactive.

Next: verify protected resources and fresh authenticated halted state/authorization, then start halted services
and run the supervised six-effect drill. Do not repeat arm, funding or retirement; never reset
driver sequences. Recheck recorded expiry (2026-10-16 13:48:34 UTC). The 240-effect extension
remains separate. Current unfinished create/prepared crashes may require supervised recovery;
automatic continuation is qualified at completed settlements, not every partial prepare.
Earlier review sections below describe historical states, not current deployment instructions.

## Current review entry point — 2026-10-15

Claude's continuation is committed through `12d0be6`; production release is `54a5fcb`.
Read [its launch handoff](handoffs/AUTOMATED_LAUNCH_HANDOFF_2026-10-15.md), then the
[post-handoff correctness review](evidence/automated_launch_2026-10-15/CODEX_REVIEW.md).
The new incarnation `aae119b9` is funded/halted; signer failed64, gateway inactive. The
missing caller keys are confirmed, but repairing startup alone does not enable repeated
trades: the production runtime still terminates after one effect. Review findings and
minimal remediation precede the six-order drill. No implementation/deployment performed
in this review. Earlier October14 and paper sections below are historical.

## Authoritative checkpoint — 2026-10-07 stochastic paper

Start with [BASELINE_2026-10-07_STOCHASTIC.md](../docs/BASELINE_2026-10-07_STOCHASTIC.md)
and tag `v2-paper-stochastic-2026-10-07`. This records the exact deployed component
commits, per-host image identities, paper state and documentation starting point.
The ongoing session is running, policy/schema 129/139, 20 bp paper fees. ntfy's
mount and Alertmanager storage were repaired; the delivery warning cleared.
Older status sections below are historical and may contradict this checkpoint.
The shared working tree still contains excluded research/gateway changes.

Updated: 2026-09-27

Purpose: compact working memory for the next agent session. Verify this file
against Git before acting; do not reconstruct the project from chat history.

## PHASE 2 PAPER TRADING — PAUSED MID-IMPLEMENTATION — 2026-09-29 21:20 EDT (current)

Start at `docs/PHASE2_PAPER_HANDOFF.md`, then ADRs 0015–0018. The work is split across
per-agent copies in `~/.cache/velocita-work/`. Only the dashboard split is merged into this tree.
Open P0 items:
- mass-cancel result-detail admission (the kill path);
- the gateway stream is unfinished.

The live cluster is unchanged and healthy in shadow mode.

## CP-061 — PHASE 1 SHADOW LOOP: SESSION LIBRARY DONE — 2026-09-28 (current)

Owner approved Phase 1 (tick -> DecisionFrame -> replicated shadow decision,
measured end to end) with real fees and no assumptions; demo program BTC trend
SMA 125 (the only one the battery supports). TDD throughout.

DONE: step 1, `NodeClientSession` + `AeronClientTransport`; client binary
ported; Release 112/112 in ~/.cache/velocita-build/release (NOT /tmp).

STEP 3 DONE (CP-062): `velocita-v2-shadow-submitter` + library; Release 114/114.
DEPLOYMENT RULES found by the tests:
1. Fresh cluster ceremony (operator client, in order): kill-epoch 1,
   executor-generation 1, install-catalog ROOT 1 1 (normal lane). Only then can
   any policy be published.
2. The submitter needs its OWN caller route, e.g. `caller.11.caller_id = 9`
   (operator band, for SetStrategyMode) with its own key and response port.
   Sharing caller 9 with the operator client trips the replay fence (seen as
   silent timeouts and fence_refusals on the leader).
3. The live demo still needs an execution-catalog row whose units can carry a
   real BTC price (the fixture row allows 1..100 ticks): decide before step 5.
STEP 4 DONE (CP-063): `cli shadow-edge`; live Mac smoke 71/71 agreement.
Two system findings: (a) tick-to-decision ~460 ms because follower durable
acks wait for the 200 ms heartbeat (fix identified, not made); (b) unseeded
rand() gave every voter identical election timeouts: FIXED in the node binary
(needs a new image on every host). Release 115/115 x3.
STEP 5 CODE DONE (CP-064): `velocita-v2-shadow-catalog --truemarkets-cbbtc`
(root 167b67a7...7526; account 1, venue 20, instrument 1, spec 1, caps 1;
$1 ticks, 0.00001 BTC lots, FOK, no post-only); caller.11 in every template;
`Containerfile --target shadow`; `velocita-v2-shadow-edge.service`; dashboard
row. Edge now drops True Markets' 1 s resends (finding 29). Live two-voter Mac
rehearsal: 50/50 proposed + agreed, data age 0.3 s. Release 116/116.
STEP 5 DEPLOYED (M3 log finding 31): live on nuc1 + Pi, incarnation
8d1fd769..., 87/87 live ticks recorded + agreed, tick-to-decision ~356 ms.
THREE VOTERS LIVE (M3 log finding 37): incarnation 8d9d7c95..., nuc2 leads;
shadow edge on all three voters. nuc2 has watchdog + panic reboot (36).
COMPACTION + SHADOW BOOK LIVE (CP-070..074, M3 log findings 40-42): fresh
cluster 453345d0... on nuc1 + Pi + nuc2. Compaction is floor-gated (the lowest
index every voter holds, carried on every append; V2 peer body version 2); a
stranded voter can no longer disrupt the cluster; compacted voters can elect
(willemt last-log-term fix). Shadow book: $1,000, tick and daily-close books
at measured costs; promotion gate in docs/STRATEGY_PROMOTION_GATE.md.
TRADING BUDGET: $25 total (owner, 2026-09-29). Paper and shadow books are sized
to it; kernel limits $25/order, one open position, halt on unknown outcome.
Phase 2 decisions (owner): separate velocita-v2-gateway process on nuc1; new
node-protocol bytes approved outright; paper fills FOK at the TM reference
price with measured costs.
NEXT: Phase 2 paper trading (gateway + TrueMarkets simulator), owner-chosen
2026-09-29. Deferred to the next phase by the owner: Raft pre-vote (every wifi
drop forces one leadership change on rejoin; M3 log finding 43). Watch the
first live compaction pause; then relax the 30 s tick throttle.
EARLY DURABLE ACKS (CP-068): follower tells the leader what is newly durable
without waiting for a heartbeat (`FollowerAckTracker`); Mac rehearsal
tick-to-decision 374 -> 21.7 ms. Rolling upgrade in progress (images
`:m3-ack`, previous kept as `:m3-pre-ack`; followers first, leader last).
NEXT: finish the rollout and measure live; watchdog + panic reboot on nuc1
and the Pi (the NUCs are on separate power, owner-confirmed); verify
election bursts shrink (finding 28); shadow ledger + promotion gate
(awaiting owner lgtm).

HISTORY, step 2: shadow command builder (pure library, TDD against the real
reducer via `catalog_test::apply`). Kernel facts it must honour, read this
session:
- policy: PublishPolicy (proposed) -> ValidatePolicy -> ActivatePolicy; versions
  monotonic per policy id.
- StageExecutablePolicy needs verified feature-schema and executable-policy
  digests, templates whose instrument_spec_version / venue_capabilities_version
  match, AND an execution-catalog row matching account, venue, instrument and
  both versions with byte-identical profile and capabilities. The catalog is
  bound into cluster identity at bootstrap: the live demo needs a catalog row
  for its instrument (fixture row is venue 9 / instrument 4 / account 5).
- ActivateExecutablePolicy(policy, version, executable digest) after staging.
- SetStrategyMode(normal) needs the CURRENT kill epoch and executor generation
  and an operator caller (<= 15).
- RecordSourceEvent: per source, generation starts at 1 and sequences are
  contiguous from 1; a restarted edge must use generation+1 (persist it).
- EvaluateDecisionFrame(shadow) -> decision_code shadow_recorded, effective
  no_action; non-authorizable decisions are swept, so a steady stream should
  not wedge (prove with a long test before deploying).
Then step 3: `velocita-v2-shadow-submitter` (session + builder, line protocol
from the Python edge), step 4: Python shadow edge, step 5: deploy + Grafana.

STEP 2 DONE (same day): `shadow_program.hpp` (`velocita_v2_node`):
`shadow_setup` = Publish -> Validate -> Stage -> ActivateExecutable ->
SetStrategyMode(normal); `shadow_tick` = RecordSourceEvent +
EvaluateDecisionFrame(shadow). Units come only from the catalog row passed in.
Payload digest = sha256 of the command encoded with a constant digest
placeholder. 9 tests through the real reducer (incl. 5000-tick stream never
wedges, generation restart, stale kill epoch refused, exact retry returns the
retained result and leaves state unchanged, reused sequence with new content is
duplicate_conflict, changed rule under same version is
immutable_policy_conflict); 11 mutants killed by intended assertions (two
first killed by the encoder instead, re-run with valid mutants). Note: exact
retries return the ORIGINAL result (code applied), not already_applied.

## CP-059 — THREE VOTERS; M4 READ-ONLY DATA APPROVED — 2026-09-27

### Owner decisions this checkpoint

- Topology: THREE voters, one per host: nuc1 (voter 1, 100.72.127.102), Pi
  (voter 2, 100.84.151.116), nuc2 (voter 3, 100.85.36.22). A commit needs two
  durable copies; any one voter may be lost. This supersedes CP-051's two-voter
  MVP topology. The online snapshot installer is STILL deferred, because the
  node never compacts: a returning voter always catches up from retained log.
  Enabling auto-compaction with three voters requires the installer first.
- M4: GET-only, credential-free calls to True Markets are approved, with order
  sends mocked (simulator only). No keys, no POST, no `/orders` path.

### What changed

- Fixed: one unreachable follower stalled all peer traffic (`send_peers`), so a
  three-voter cluster with two voters alive committed nothing. Per-peer hold;
  RED-first node/mailbox tests, a real three-process Aeron scenario, five
  mutants killed. Evidence `evidence/three_voter_2026-09-27/`. Release 111/111.
- `deploy/v2/three-host/node-{1,2,3}.conf.template` + `v2-deploy-template-tests`.
- Prometheus targets for nuc2, `VelocitaQuorumAtRisk` / `VelocitaQuorumLost`
  alerts with a promtool test, dashboard retitled; host label `nuc` -> `nuc1`.
- Soak bots send to 1, 3, 2 (NVMe first) and REQUIRE `VELOCITA_INCARNATION`.

### Next

1. Done: soak PASSED (zero elections in its window). Recurring elections are
   wifi carrier loss every ~8 h 24 min per host (deployment log finding 24):
   wire every voter before the three-voter soak. Owner: install the Mac key
   for `vishal` on nuc1 (finding 25).
2. nuc2 host prep: storage benchmark, `ufw` check, image copy from nuc1,
   units, exporters; BIOS power-on + power-cut test; Ethernet if possible.
3. Fresh three-host cluster (new incarnation), secrets `link-1-3`, `link-2-3`,
   drills killing each voter in turn, then a three-voter soak.
4. M4: read-only market-data collector into the non-sending shadow loop.

CP-060 (2026-09-28): market-data recorder live on nuc1; research lab built;
Minara REJECTED on a point-in-time universe (`evidence/minara_battery_2026-09-28/`);
volume must be bought at the markup (`evidence/volume_strategy_2026-09-28/`).
MARKUP STILL UNMEASURED. `tools/measure-markup.py` fixed this session: explicit
User-Agent (Cloudflare 403'd Python's default), stale-keystroke flush and
empty-code re-prompt, quote retries with a scrubbed reason, base asset resolved
to the catalog contract address (bare "SOL" gave "resolve base asset: 404"),
and a True Markets 30s reference close captured beside each quote. The owner's
login succeeded twice; the quote call then failed (fixed). Latest state: True
Markets stopped emailing login codes after about five requests in ~30 minutes
(probable per-address throttle). Next: one attempt after an hour, output
through `tee`; if no email, ask True Markets support.

## CP-051 — E1 RE-SCOPED TO THE TWO-VOTER PILOT — 2026-09-24 (topology superseded by CP-059)

This section is current status; lower checkpoints (including the CP-050 Claude
handover below, whose workspace rules still apply) are an audit trail.

### Owner decisions this checkpoint

- MVP topology stays ONE voter per host: NUC + Pi, two voters, fail-closed.
  The owner considered 2 voters on NUC + 1 on Pi; that is deferred until a
  measured compute budget justifies it, and it would bring the online
  installer back onto the critical path.
- The online snapshot installer (stable facade, nonserving candidate, history
  bridge activation, same-ledger reconciliation) is DEFERRED to the three-voter
  release, not abandoned. Its CP-036/045 approvals remain valid for that work.

Reason: with exactly two voters every commit needs both durable copies and
compaction never passes commit, so an intact voter can always catch up from its
own log. Falling behind the compacted prefix requires lost storage, which is the
approved OFFLINE lost-voter repair, not an online install. Estimated installer
cost was five to eight sessions for a scenario the pilot cannot reach.

### What changed

- New `tests/v2_two_voter_catchup_tests.cpp` (registered; Pi gate inventory
  updated). Partitioned peer and unsynced-acknowledgement power-loss scenarios;
  independent reducer oracle. Pins existing behavior (not RED-first); the
  follower durable-ack clamp mutant fails the exact intended assertion.
- The approved "no automatic leader no-op" cost is now asserted in a catch-up
  trace: a healed old-term suffix waits for one current-term command.
- `tests/v2_snapshot_catchup_red.cpp` stays a tracked, freshly reproduced RED
  for the three-voter release. Do not delete it or count it as passing.
- Evidence: `evidence/e1_two_voter_2026-09-24/`. Release 98/98; sanitizer in
  `verification.txt` there. No production source changed.

### Exit status after CP-051

| Exit | Pilot (two voters) | Three-voter release |
|---|---|---|
| E1 | Native catch-up GREEN + mutation-covered; lost-storage repair still required before live canary (M6) | Online installer; lagger RED open |
| E2 | Native local + fresh destination verification done; authenticated copy transport needs the node process (M2/M3) | Same |
| E3 | Native worker persistence done; target measurements needed (M3) | Install-time drain of old jobs, with the installer |

### Next

Full machine sign-off runs for E1/E2/E3: `V2_E_EXIT_SIGNOFF_PLAN.md`.

M2 design APPROVED (decisions C/D/E, 2026-09-24): `M2_NODE_PROCESS_REVIEW.md`; implement §5 in order.
CP-053 progress: §5 steps 1 (envelope) and 2 (session fence + epoch file) DONE in
`velocita_v2_node`, RED-first and mutation-covered; evidence `evidence/m2_*`.
CP-054: step 3 config parser DONE (`evidence/m2_node_config_2026-09-24/`).
CP-055: ingress/response codec DONE (`evidence/m2_node_protocol_2026-09-24/`).
CP-056: step 4 V2Node owner loop DONE (`evidence/m2_node_loop_2026-09-24/`).
CP-057: steps 5–6 DONE. Binaries `velocita-v2-node`/`-client`/`-status`; the
two-process loopback exit test passes (`evidence/m2_node_process_2026-09-24/`).
M2 exit met on one host.

Owner decision on the secrets gaps (2026-09-24): MVP is a <= $20 canary, so
DEFER to the next iteration: asymmetric caller signatures (voters can forge
caller frames today), WireGuard confidentiality, key-generation tooling and
persistent receiver epoch watermarks. IN SCOPE for MVP: secret rotation and
fetching secrets from the owner's Mac instead of storing them on NUC/Pi disk.
Design APPROVED (RAM-only push from Mac Keychain; SIGHUP overlap rotation): `M2_SECRETS_ROTATION_REVIEW.md`.
CP-058: implemented and live-tested (`evidence/m2_rotation_2026-09-24/`); runbook in
the review §5. Next: M3 NUC deployment (systemd unit with a RuntimeDirectory
tmpfs, dedicated user), first real push from the Mac, then the E sign-off runs.
Start M2, the startable V2 node: two local processes that start, restart,
partition, rejoin and replay with no venue send capability. This is the book's
first end-to-end milestone and is required before authenticated E2 transport,
M3 deployment and M3a observability. In parallel, M5 venue-contract discovery
(no venue calls authorized); the snapshot has only 53 bytes of headroom, so any
new venue state needs a byte review first.

## CLAUDE HANDOVER — CP-050 — 2026-09-24 (historical)

This section was current status at CP-050; lower checkpoints are an audit trail. This is a
documentation-only handoff after CP-049, not another implementation or gate.

### Verify and preserve the actual workspace

- Repo `/Users/vishaljha/aeron-raft-pi`, branch `v2/phase-5-corrections`, HEAD
  `d44a4a964fc8c13279878e53d245ab56b921a824`. Verify before acting.
- Extensive remediation is MODIFIED/UNTRACKED, not committed at that HEAD. A
  checkout of HEAD alone loses the current implementation. Preserve EVERY existing
  dirty/untracked path, not just the protected list. Do not reset, clean, stash,
  switch branches or reconstruct from historical chat. No new commit/tag made.
- Specifically protected: `model/`, `docs/README.md`,
  `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
  `docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, and `docs/adr/0002-*`.
  Never `git add .` or `git add -A`; any future staging is explicit-path only.
- No AGENTS.md was found at the repository or checked ancestor roots at handoff;
  check again. No code/test/build changes in CP-050. Test figures below are
  verified saved CP-049 evidence, not freshly rerun in this handoff turn.

### Read next, in order

1. ADR0001 invariants, ADR0014 (current amendment plus applicable historical
   decisions), ADR0006's new current-status amendment.
2. `F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md` in full: decisions A–F APPROVED.
3. `F5_INSTALL_HISTORY_BRIDGE_REVIEW.md` in full: bridge, candidate budget and
   recovery direction APPROVED. Do not reopen these as new approval requests.
4. `F5_GENERATION_STORAGE_REVIEW.md`, `F5_LOCAL_GENERATION_BYTE_REVIEW.md`, and
   `F5_ARCHIVE_PUBLICATION_REVIEW.md` before touching those boundaries.
5. `evidence/f5_writer_handoff_2026-09-24/{README.md,verification.txt}`;
   CP-048 destination verification and CP-046 history recovery evidence as needed.
6. `V2_NUC_PI_TRADING_ROADMAP.md` §2 and `evidence/V2_REMEDIATION_CHECKPOINTS.md`.

### What actually exists, and what does not

| Exit | Implemented native behavior | Outstanding closure requirement |
|---|---|---|
| E1: lagger catches up | Verified storage/layout, suffix/result inspection, cold history-bridge recovery | Actual online candidate/selection/activation preserving the SAME live ledger; real lagger still fails |
| E2: compaction preserves evidence | Local archive pipeline and CP-048 fresh destination evidence gates actual reclamation/relevant restart | Authenticated copy/verification process, independent-host evidence and target gates |
| E3: nonblocking persistence | Worker-backed hard state/suffix/compaction/history recovery; CP-049 serial target handoff | Actual install-time drain of ALL old jobs and generation activation; deployment measurements/gates |

E1/E2/E3 and F5/F6/F7 remain OPEN. Existing host suites do not close these exits.
Startable V2 node follows native E1/E3 integration. Authenticated process behavior
necessarily requires that shell; do not demand proof of a nonexistent deployed
transport before starting it, or call native simulation deployed acceptance.
Pi testing is deferred until final gates, not waived. No device/venue action now.

### Exact next work — one integration, not more unrelated helpers

Reproduce `tests/v2_snapshot_catchup_red.cpp`: actual three-voter cluster,
healthy peers compact through12, lagger stays applied0/commit0 after seven failed
AppendEntries replies. Require its real catch-up to become GREEN. Do not replace
it with a cold-start fixture, skip it, or count helper tests as E1 completion.

1. Refactor ownership under the approved direction: stable facade keeps SAME
   `ResultLedger` and serial worker; replaceable generation owns Raft callbacks,
   state, stores, arenas, retained payloads and outbound traffic. Do not move/reset
   the ledger, copy READY results into a new one, or let old outbound traffic escape.
2. `VerifiedIdentityStartup` currently combines root lock, selected directory and
   selection validation. Separate stable root ownership from VERIFIED NONSERVING
   candidate ownership. No raw-path constructor, placement-new, repeated root-lock
   recovery as a workaround, or shallow swapping of objects referenced by callbacks.
   In particular `catalog_` references the owned startup token; jobs borrow stores,
   and `CompactionJob` borrows the kernel itself. Preserve these lifetimes.
3. Worker builds bounded destination-owned candidate with existing selector/base/
   WAL formats, verified history and compatible suffix. Keep root archive unchanged
   before selection. Missing `(A,S]` history lives in candidate WAL until selected
   bridge recovery publishes it. Respect approved1GiB lab whole-WAL cap, archive
   quotas,8192 live entries,1024 results, finite pinned replay interval and bounded
   replay turns. No automatic deletion of candidates/old generations.
4. Quiesce and revalidate full local tuple, leader evidence and obligation inventory.
   Seal/submit old staging and drain ALL actual writes/jobs before selector change.
   CP-049 switches writer targets only: it neither selects nor activates a kernel.
   Keep both targets alive through acquired completion and apply old completions
   before retiring arenas. Do not owner-thread stop/join/fsync to make tests pass.
5. Durably select then activate and finish bridge recovery before serving. Before
   selection old authority remains intact; once rename may have happened, uncertainty
   stops and cold-recovers—never silently fall back. Preserve READY values/FIFO;
   reconstruct exact covered RESERVED outcomes from verified history, retain
   compatible uncommitted reservations, release divergent uncommitted obligations
   only after selection, refuse committed divergence. ACK proves S, not suffix end.
6. Test actual old-write stalls, changed final-validation inputs, full result
   cohorts, selector write/sync/rename/activation cuts, retry and saturation. Use
   reachable fixtures; approved review explains why committed-above-S cohort is
   defensive rather than a proven healthy current live interleaving. Mutate each
   decisive guard and verify the intended assertion, then run regressions.

Native tests may explicitly simulate trusted authentication through a private
fixture boundary. A public boolean or static-identity digest is not authentication.
New material wire/durable/economic choices still require owner review BEFORE
encoding. Existing approved meanings/layouts do not require another approval.

### Verification and reproduction

CP-049 saved final Release97/97 (28.92s); full C++ ASan+UBSan97/97 (286.92s), then
freshly rebuilt focused2/2 after final test strengthening. Nonrecovering UB/halt,
leaks off, Raft C uninstrumented. Focused TSan covers actual WAL source+test only,
not support libraries/full process. Inventory97host/94V2; gate-manifest16 PASS.
No fresh model corpus/target/RSS/latency evidence. `git diff --check` clean.

```sh
cd /Users/vishaljha/aeron-raft-pi
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j4
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j4
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

Always build before CTest. Run Release/sanitizer tests SEQUENTIALLY: old fixtures
share fixed temp names. Reconfigure if targets change. Both builds use offline
pinned Raft at `/private/tmp/velocita-cp043.SaGIxe/raft-build-suffix`; inspect
CMakeCache before reuse, and do not substitute upstream/unpatched Raft. If scratch
is gone, reconstruct the pinned dependency/build settings from repo and evidence.

To run the original RED after compiling against freshly built libraries, supply
a NONEXISTENT child of a fresh temporary directory (not the mktemp directory
itself). `/private/tmp/velocita-cp049-lagger` is a historical built executable,
not a source of truth. Missing argument/existing-root refusal is not intended RED.
Direct compile uses C++20, cacheline64, include/tests/pinned Raft includes, then
replication/kernel/evaluator/contracts/core/willemt_raft static libraries in that
order. See CP-049 evidence. Do not mutate shared production files for experiments.

CP-049 draft preceded tests: do not relabel it as chronological TDD. Two later
deterministic mutations separately prove the post-acquire submission recheck and
completion-drain guard. Earlier scheduling-sensitive mutant was withdrawn.
Continue genuinely RED-first. A test result cannot be its own oracle; never call
a mutation killed by a harness/compile error a successful guard test.

### Working agreement

Lead owns architecture/correctness; maximum TWO bounded cheaper implementation/
test agents if available, disjoint files, no overlapping kernel rewrites. Keep
MVP scope: every task enables E1/E2/E3 or an existing launch requirement; defer
nonessential additions rather than create new E-items. Keep handoff/checkpoint/
CHANGELOG and affected ADRs current. Explain problem, importance, intuition,
trade-offs and C++ ownership/memory/concurrency before any new approval request.
Do not weaken safety to accelerate the schedule. `model/` must contain no trading,
signing or submission capability; preserve its interlock. No perpetuals for MVP,
new venue requests, credentials, tag changes or deployment authorized by handoff.

## CP-049 — E1/E3 SERIAL WRITER TARGET HANDOFF — 2026-09-24 (historical)

Branch/HEAD unchanged: `v2/phase-5-corrections` / `d44a4a9`; remediation
UNCOMMITTED. This is a prerequisite inside the approved online installer, NOT
E1/E3 closure. No facade/core replacement, candidate publication or live ledger
reconciliation has landed in this increment. CP-048 native E2 remains intact.

The existing serial `WalWriter` can hand off its borrowed segment/arena without
an owner-thread stop/join/fsync. Owner admission pauses blocks and maintenance;
worker finishes older blocks and syncs, waits for their completion queue to drain,
then reports an exact generation/sequence result. Both generations must stay
alive through acquired completion. Caller must seal/submit all old staging first:
this queue fence cannot see unsubmitted records. Dequeuing a completion is NOT applying
it to its arena. The owner must finish that accounting before retiring storage.
The switch is NOT permission to publish a selector or resume consensus service.

Main review found a draft race: worker pops an empty queue; owner submits an old
block then publishes the switch; worker sees the switch and changes targets.
It must recheck the submission queue AFTER acquiring the switch publication.
Otherwise old descriptors can be read against the new arena. Permanent tests
and direct mutations exercise this boundary and outstanding-completion fencing.
Initial code preceded its new tests: report draft/mutation RED honestly, not a
chronological test-first implementation. Two deterministic direct guard mutants
now fail; an earlier scheduling-sensitive completion mutant is explicitly
withdrawn and superseded by a two-idle-pass witness. Main also fixed the test
waiter's double-consumption, unwind ordering and saturation payload oracle.
Host Release97/97 and C++ ASan+UBSan97/97 pass; final strengthened test rebuild
and focused sanitizer rerun are recorded in
`evidence/f5_writer_handoff_2026-09-24/verification.txt`. Inventory97host/94V2;
gate-manifest16 PASS. Focused TSan covers actual WAL source+test, support libraries
uninstrumented. No whole-process TSan or target/RSS/latency acceptance. Sanitizer
Raft C uninstrumented and leaks off, as before.

NEXT remains the actual `tests/v2_snapshot_catchup_red.cpp` trace. Implement the
approved stable facade retaining SAME ResultLedger and serial worker; isolated
generation owns Raft/state/stores/outbound/retained payloads. Verified startup
currently combines root lock and selected-directory authority and rejects an
unselected candidate: do not bypass it with raw paths or reuse `recover()` as
online preparation. Separate root ownership from nonserving verified candidate
ownership before constructing a candidate core. Drain actual jobs that borrow
stores/core, not just WAL descriptors. Prepare history bridge and captured result
outcomes; final quiesce revalidates complete obligations/leader/local tuple;
selector uncertainty stops and cold-recovers. Preserve READY FIFO, reconstruct
covered RESERVED outcomes from verified history, preserve compatible uncommitted
suffix; ACK only S. No new bytes, auth boolean, cleanup or fresh ledger approved.

All E exits remain open. Authenticated process integration and target gates still
follow native installation; Pi deferred, not waived. Max two Sol lanes; no new
feature workstreams, protected-path/staging/commit/tag/device/venue changes.

## CP-048 — FRESH DESTINATION VERIFICATION: NATIVE E2 — 2026-09-24 (historical)

Branch/HEAD `v2/phase-5-corrections` / `d44a4a9`; remediation UNCOMMITTED.
Owner approved fresh authenticated destination verification; persistent receipts
are deferred. No new durable or transport bytes; exact authentication/envelope
integration is still not implemented. Do not reopen this semantic approval.

New private destination verifier reuses existing catalog/object/dependency checks
and destination file/directory sync. It preserves original publisher identity;
backup storage must be a separately owned namespace, not the other voter's own
active archive. Pin the other configured voter in the same manifest. Its static
identity digest is NOT authentication or physical-host independence: future
transport must prove those and hold root ownership. Tests explicitly simulate
that trusted boundary; there is no public raw mint/auth-success boolean.

Native request/evidence includes exact catalog generation/digest, publisher,
manifest, range, publication/current storage generations, pinned destination,
attempt and challenge. Evidence is move-only/one-use. Actual two-host compaction
waits AFTER acquired local archive/snapshot completion and BEFORE Raft prefix
removal/WAL rewrite. Missing evidence retains history. Missing pin still refuses
live compaction. Relevant cold live recovery refuses without a pin; with one,
it remains nonserving until fresh destination evidence arrives. Mismatched pending
completion latches closed; duplicate does not advance anything.

Review caught a real draft replay gap: attempt1 can repeat after restart. Permanent
RED reused a previous response and released recovery. Fix: cold getentropy32-byte
nonce + domain-separated SHA256(nonce, nonwrapping attempt) for fresh challenges.
Entropy failure refuses startup; no fallback. No order-path entropy/network/I/O.
The entropy EIO test privately includes production code, not a production hook.

Host witnesses exercise real WAL removal/restart, exact authored commands1..3 in
backup, missing/payload-corrupt backup after deletion, all serving gates, stale
attempt/prior-run response/duplicate, exact dependency/pin/catalog checks, one-use
evidence, and separate real file/directory fsync EIO. Three direct mutants fail
intended catalog-digest, recovery-gate and entropy-failure assertions. Main fixed
two corruption fixtures to flip payload rather than header bytes.
Evidence and final verified counts:
`evidence/f5_destination_verification_2026-09-24/README.md` and `verification.txt`.
Fresh final Release95/95 and C++ ASan+UBSan95/95; Raft C uninstrumented, leaks off,
nonrecovering UB/halt. Gate-manifest16 PASS; diff check clean.
Inventory95host/92V2; native sizes: replica124904 (+464), request184, evidence192.
Not RSS/target budgets. Full archive verification currently repeats twice off-owner;
optimization deferred, not a new work item. No fresh TSan/target/copy-transport proof.

NEXT: actual E1/E3 online installer, with SAME ledger and serial persistence owner.
One implementation owner for facade/generation-core switch; second agent tests
old-write drain, exact result preservation and selector failure cuts. Approved
history bridge remains required. Original `tests/v2_snapshot_catchup_red.cpp`
freshly remains RED: compacted12/applied0/seven failed replies. Do not substitute
another cold witness or rebuild/reset the ledger. Native E2 now supplies its
verification boundary; authenticated copy service and NUC/Pi evidence still gate
deployed E2. E1/E2/E3 and F5/F6/F7 NOT closed. Startable node follows native exits;
deployed authentication/gates require process integration. Pi deferred, not waived.
No extra feature items, protected-path/staging/commit/tag/device/venue changes.
Two bounded Sol lanes implemented backend/tests; main integrated kernel, reviewed
and corrected lifetime/freshness/oracle details, and owns final regression.

## CP-047 — FIXED E1/E2/E3 PARALLEL SCOPE — 2026-09-24 (historical)

Branch/HEAD unchanged: `v2/phase-5-corrections` / `d44a4a9`, remediation uncommitted.
Owner requests parallel completion of existing E1/E2/E3 then startable V2 node;
no extra feature items. Nonessential work stays next iteration. Max two agents,
main owns architecture/correctness. This checkpoint is review/documentation only:
no new C++, RED/GREEN runs or acceptance claim; CP-046 is last runtime evidence.

Two bounded Sol reviews confirmed:
- E1/E3 require ONE integrated stable-facade/generation-core implementation and
  disjoint adversarial-test lane, not parallel conflicting kernel rewrites.
  Same ResultLedger/handles/FIFO survive; Raft outbound traffic is generation-local
  and must not leak across activation. Drain actual worker writes/completions
  before swapping cores. Current jobs borrow store/segment/archive pointers and
  compaction borrows the whole kernel: callback identity alone cannot prevent a
  stale write or use-after-free. Facade/core is ALREADY approved, not a new choice.
- E2 live independent-destination evidence remains unapproved in CP-038 §6.
  Both compact and capability-consumption gates intentionally require laboratory
  mode today. Preserve `test_two_host_mode_refuses_local_only_compaction`.

OWNER REVIEW requested within existing E2: main recommends fresh authenticated
destination verification at relevant restart and each reclamation, reusing exact
existing archive bytes, rather than adding persistent receipt bytes for MVP.
Backup worker must verify exact catalog/object/dependency set and destination
file/directory durability. One-use native capability, identity/generation/attempt
bound; no boolean/rsync/checksum-only proof. Failure retains history/stops required
service. Explicit cost: backup availability and revalidation I/O. This changes
recovery availability and MUST be approved; no semantics were encoded. Alternative:
durable destination receipt with separately reviewed persistent bytes/trust rules.
Detailed recommendation: top of `F5_ARCHIVE_PUBLICATION_REVIEW.md`.

Native E1 work remains approved with exact RED `tests/v2_snapshot_catchup_red.cpp`;
do not reopen its approved byte/ownership decisions. The E2 choice and concrete
authentication/envelopes must be settled before implementing live backup authority.
Node/process integration is required for deployed authenticated evidence, so host
E proofs precede node work but cannot claim all deployed exits closed beforehand.
Target acceptance remains later; Pi deferred, not waived. No extra feature scope,
no protected-path/staging/commit/tag/device/venue changes. Roadmap and deferral doc
updated; no numerical completion claim. E1/E2/E3 and F5/F6/F7 still OPEN.

## CP-046 — APPROVED HISTORY BRIDGE: RECOVERY SIDE — 2026-09-24 (historical)

Branch/HEAD `v2/phase-5-corrections` / `d44a4a9`; remediation UNCOMMITTED.
Owner approved the history bridge, candidate budget and recovery approach in
`F5_INSTALL_HISTORY_BRIDGE_REVIEW.md`. No new canonical bytes. This checkpoint
implements selected-generation cold recovery, NOT online snapshot installation.

Snapshot-covered unarchived WAL is separately accounted recovery debt. It no
longer trips the live8192-entry quota, but the whole existing WAL must fit the
positive configured cap (maximum1GiB). Consensus/admission/output/proofs remain
quarantined. `flush_durability` submits one owned job to the serial writer; it
validates the whole stream, preflights exact missing-history archive objects/bytes
plus live-suffix reservation, then publishes bounded segments. Owner releases the
fence only after acquiring and matching completion identity and unchanged state.
No WAL deletion; live independent-copy rules unchanged. Recovery can take time:
the two-voter deployment has no quorum while one voter is quarantined.

Permanent witnesses:8193 covered records, complete archive payload reconstruction,
subsequent commit/restart, paused worker before publication and after durable
catalog/before completion, worker exception/retry, WAL-cap and object-quota refusal,
partial archive overlap, five completion corruptions and valid/duplicate handling.
Draft overlap failure was real: preflight counted already-archived rows again;
fixed to account only `(A,S]` while validating all WAL. Three intended mutations
fail tick-state, partial-catalog and completion-sequence assertions. Evidence:
`evidence/f5_history_recovery_2026-09-24/README.md`.

Native host sizes: recovery job8920 bytes, replica124440 bytes; not heap/RSS or
target budgets. One segment's input is bounded, but descriptor/encoder/catalog
scratch also exists. Corrected the review's linear-time claim: each publication
re-verifies archive prefixes, so total verification I/O can grow superlinearly.
Cold startup verification is synchronous; worker pumping is not a startup SLA.

Fresh host Release93/93 and C++ ASan+UBSan93/93 (Raft C uninstrumented, leaks off,
nonrecovering UB). Inventory93host/90V2 matches; gate-manifest16 PASS. Details in
the evidence verification file. First parallel Release/sanitizer attempt
collided in an older fixed-name temporary fixture; run those suites SEQUENTIALLY.
An existing cold-result fixture now pumps the newly required recovery and async
metadata completion before expecting its original exact retained result.

NEXT exact RED remains `tests/v2_snapshot_catchup_red.cpp`: freshly rebuilt,
compacted12/applied0/seven failed replies. Implement the actual native online
transaction: stable facade retains SAME result ledger and serial persistence
ownership; bounded worker builds destination-bound candidate WAL/history; drain
actual old writes before selector publication; activate verified generation;
recover bridge before serving and release only approved installed/result outcomes.
No raw auth bypass, new transport bytes, independent-copy waiver, automatic
deletion or new acceptance is approved. Do not call cold witnesses real catch-up.
E1/E2/E3 and F5/F6/F7 remain OPEN. Pi deferred, not waived; NUC/ARM/Pi re-gates,
independent destination evidence and target memory/timing still outstanding.
Two bounded Sol workstreams plus main review/mutations; protected paths untouched.
No staging, commit, tag, device, venue or deployment action.

## CP-045 — E1 ARCHIVE/SELECTOR INTEGRATION REVIEW — 2026-09-24 (historical)

Branch/HEAD `v2/phase-5-corrections` / `d44a4a9`, remediation UNCOMMITTED.
No production changes. Original real lagger RED freshly rebuilt: compacted12,
lagger applied0, seven failed append replies. E1 is NOT implemented or accepted.

Found a missing integration decision: root-global archive catalog and runtime
selector cannot be published atomically. Archive-first can make old selected
history refuse recovery; selector-first with snapshot+suffix lacks audit prefix.
Do not remove either existing refusal or stage unlisted objects in the live archive.

RECOMMENDED, awaiting owner approval: isolate exact missing history in the new
generation's existing bound WAL, keep root archive unchanged before selection,
then publish archive coverage after selecting the self-contained new generation.
Separate accounted snapshot-covered recovery work from the live8192-entry bound;
propose1-GiB laboratory candidate-WAL budget plus exact existing archive-quota
reservations. Consensus/admission stay gated until local archive completion;
live independent-copy requirements still govern deletion. No new durable bytes.
Full reasoning/costs/approval boundary: `F5_INSTALL_HISTORY_BRIDGE_REVIEW.md`.

Three permanent cold-layout witnesses prove missing-history refusal, exact small
WAL-overlap recovery, and current8193-covered-entry quota refusal. The last is
CURRENT behavior, to change after the proposed accounted bridge is approved; it
is not desired final E1 behavior. Two intended guard mutations fail. Main corrected
the expected WAL oracle to authored records rather than a readback of fixture output.
No witness implies authenticated transfer, online activation or result preservation.

Rebuilt Release92/92 and C++ ASan+UBSan92/92 (Raft C uninstrumented, leaks off);
final inventory/raw results are in
`evidence/f5_install_bridge_review_2026-09-24/`. Previous CP-044 production stands.
Protected paths untouched; no commit/tag/device/venue action. E1/E2/E3 and F5/F6/F7
remain OPEN. NEXT: owner review of bridge ordering/accounting/storage cost; then
integrate actual native catch-up with the same ledger and serial persistence owner.
Do not reopen already approved CP-036 results/ACK semantics or implement another
unconnected helper. Authentication transport remains separately unapproved.

## CP-044 — COMPOSED ASYNC CRASH-IMAGE PROOF — 2026-09-24 (historical)

Branch/HEAD unchanged: `v2/phase-5-corrections` / `d44a4a9`; remediation
UNCOMMITTED. Test/evidence-only checkpoint; no production or canonical-byte
changes. Actual metadata and WAL worker operations now share one durable-image
model. Higher-term suffix replacement and subsequent commit:20 cuts per name
schedule; standalone vote:5 cuts per schedule. Three schedules =>75 cuts, plus
three committed-seed recoveries and six successful promise-producing baselines.

Before recovery, assert exact authored WAL and metadata tuples. A malformed
temporary metadata file can justify a typed startup refusal, never a missing
active tuple or lost committed prefix. Clean images must recover. Two direct
mutations fail intended vote-durability/committed-prefix checks. Review corrected
fixture commitment, active-vs-temp loading, runtime path, and permissive oracles;
these were harness issues, not newly discovered production defects.

Rebuilt Release91/91 and C++ Debug ASan+UBSan91/91 (Raft C uninstrumented,
leaks off); final verification results and scope:
`evidence/f6_async_crash_model_2026-09-24/README.md` and `verification.txt`.
Independent inventory91host/88V2; tools24+16. No target acceptance, venue/device
action, staging, commit or tag changes. Protected paths remain untouched.

NEXT: actual E1 lagger transfer/install, including delayed writes across active
generation changes. The current-generation hard-state/suffix crash-model step
is complete; do not implement another helper instead of real catch-up. E2
independent-destination evidence and NUC/ARM/Pi memory/durability/timing gates
remain. E1/E2/E3 and F5/F6/F7 acceptance stay OPEN; Pi deferred, not waived.
Existing approvals stand; new material transport/byte choices need review.

## CP-043 — ASYNC HARD STATE AND WHOLE-SUFFIX PERSISTENCE — 2026-09-23 (historical)

Branch/HEAD: `v2/phase-5-corrections` / `d44a4a9`; remediation UNCOMMITTED.
Owner approved all five CP-042 choices. Both hard state and runtime suffix
rewrite now run on the serial WAL worker when start_writer is active. No new
wire/economic/durable bytes. Deterministic no-worker mode remains explicit;
live integration must start the worker, never join/inline-flush as fallback.

One desired-vs-durable metadata fence withholds outbound/apply/proofs/new turns.
Raft's optional whole-cut preparation hook selects suffix deletion before any
pop. Drain staged work, rewrite once, validate exact completion, replay ONE
owned32-KiB frame, consume exact grant, then release cells/RESERVED results.
Unused replay grant and worker/identity failures latch closed. Runtime log clear
is cold-only: ignored library callback errors do not make it resumable.

Permanent host evidence includes actual metadata fsync-thread/pause/EIO tests,
16 suffix conflict/term/staging/durability cells plus2 worker-failure/restarts,
9 completion corruption/duplicate cases, self-vote callback error propagation,
and a real authorized order/marker proof across metadata directory durability.
Five direct-source mutations fail intended output/apply/identity/ownership/error
checks. ReplicatedKernel124,416 bytes, one heap job168(bound256), inline retry32KiB;
these are not whole-process memory or target latency measurements.

Final rebuilt Release90/90(21.33s), C++ ASan+UBSan90/90(192.48s), focused
hard-state TSan PASS, tools24+16 PASS. Nonrecovering UB/halt, leaks off, Raft C
uninstrumented in the sanitizer build. No NUC/ARM/Pi or latency/RSS result.
Evidence and final verification: `evidence/f6_async_persistence_2026-09-23/README.md`
and its `verification.txt`. Four new registered tests; inventory90host/87V2.
Sanitizer limits and fixture corrections are recorded, not silently reclassified.
Two bounded Sol workstreams, main review/corrections/patch/mutations. Protected
paths untouched; no staging/commit/tag/deployment/device/venue action.

NEXT: finish composed asynchronous persistence/crash coverage (including delayed
old writes at E1 activation), then actual E1 lagger transfer/install. Do not
reimplement the completed worker callbacks or treat a helper as catch-up. E2
independent-destination durable evidence and target re-gates/memory/timing remain.
E1/E2/E3 acceptance and F5/F6/F7 OPEN; Pi deferred, not waived. See F6 review §9
and ADR0014. Existing approvals stand; new material transport/byte choices still
need the owner's plain-language plus C++ review before encoding.

## CP-042 — ASYNC PERSISTENCE CONTINUATION REVIEW — 2026-09-23 (historical)

Branch/HEAD unchanged: `v2/phase-5-corrections` / `d44a4a9`; remediation remains
uncommitted. This checkpoint changes documentation ONLY. CP-041 evidence stands;
no new build, test pass or implementation is claimed.

NEXT: owner review of `F6_ASYNC_DURABILITY_FENCE_REVIEW.md` §8's five concrete
choices. Direction was approved earlier; this exact continuation remains PROPOSED.
Two bounded read-only Sol audits and main source inspection covered hard-state,
periodic apply, outbound callbacks, suffix deletion and clear. Recommendations:
one pending protocol turn; reuse existing mailboxes but withhold all output;
dedicated pending status/before-apply fence; optional prepare-truncate hook inside
Raft; one owned32-KiB retry frame; typed generation/sequence-bound job on existing
serial WAL worker. No new wire/durable bytes or generic scheduler proposed.

Do not replay a partially completed campaign or release per-entry suffix cells
before one durable whole-cut rewrite. A higher-term conflicting append may need
metadata THEN truncate, sequentially. Current clear ignores callback errors and
snapshot load mutates term first: keep constructor clear cold-only, fail closed
on runtime misuse, and leave real E1 activation to the approved installer path.
Generation selection must drain jobs; ignoring a stale completion is insufficient.

After approval: permanent paused-worker hard-state REDs -> minimum GREEN and
guard mutations -> actual conflicting-suffix RED/GREEN -> composed failures and
regression. Existing READY results readable; no new apply/results/proofs while
pending. No owner-thread sync/join fallback in live mode. See §8 for costs,
source-call matrix, native ownership/status choices and rejected alternatives.
E1/E2/E3 and F5/F6/F7 remain OPEN. Protected paths unchanged; no staging,
commit/tag/deployment/device/venue action. Pi still deferred, not waived.

## CP-041 — COMPOSED COMPACTION CRASH MODEL — 2026-09-23 (historical)

Verified branch/HEAD: `v2/phase-5-corrections` / `d44a4a9`; remediation
UNCOMMITTED. CP-038 archive and CP-036 install approvals stand. No production
source, canonical byte contract or hot memory layout changed this increment.
Do not reconstruct current state from historical sections below.

The actual laboratory archive → snapshot → WAL transaction passes243 modeled
crash cuts: first compaction45/replacement36, each across three namespace
schedules. Every image is inspected before recovery: exact old WAL must survive,
or matching new snapshot/full archive/exact uncommitted suffix must already be
durable. Refusal cannot mask invalid ordering. Accepted recovery compares the
entire history by tuple AND command bytes; baseline must actually finish the
cut and remove the WAL prefix. No deferral/no-op counted as completion.

Selective independent WAL-name persistence matters because snapshot and WAL
share a parent: a later directory sync hides a missing earlier sync in simpler
models. Actual-source mutations prove sensitivity: omitted snapshot directory
sync fails only the selective schedule; omitted WAL file sync and dropped first
suffix record fail exact durable-image checks. Production source is unchanged.

Two bounded Sol workstreams supplied implementation and independent review;
main corrected draft catch/oracle/namespace/descriptor-lifetime problems,
strengthened pre-recovery checks and ran all verification. Harness failures
are explicitly NOT product REDs. A wrong drafted WAL filename and a recovery
fixture directly beneath macOS's /tmp symlink were corrected, not production
guards weakened. CP-040 cache-line128 standalone mutations against64 libraries
were rerun at64; intended failures reproduce and the old evidence is annotated.

Final rebuilt Release86/86(18.08s), C++ Debug ASan+UBSan86/86(241.77s),
focused1/1 with243 cuts, tools40/40, independent inventory86host/83V2.
UB halt/nonrecovering, leaks off, linked Raft C uninstrumented. No target,
TSan, Python model-corpus, latency/RSS or device power-loss result.
Evidence and commands: `evidence/f5_composed_persistence_2026-09-23/README.md`.
Protected paths untouched; no stage/commit/tag/deployment/device/venue action.

NEXT: E3 actual nonblocking hard-state and suffix-truncation persistence.
The existing `tests/v2_async_hard_state_red.cpp` was freshly rebuilt against
current libraries and failed for the intended reason in ALL three scenarios:
campaign4/4, higher-term vote4/4, commit2/2 metadata syncs run on the Raft owner.
Exact commands/output: `evidence/f5_composed_persistence_2026-09-23/next-hard-state-red.txt`.
These are syscall counts, not latency claims or evidence of premature votes.

Read `F6_ASYNC_DURABILITY_FENCE_REVIEW.md` and ADR0014 before adapting callbacks.
Direction is approved; exact continuation/API and all-call-site proof still need
work. Do NOT enqueue hard-state and simply return success: quarantine outgoing
responses, application and authority until matching durable completion. Include
runtime suffix truncation, not only the three metadata saves. Present genuinely
new material contract choices with the owner's plain-language + C++ explanation
before encoding. Then make actual E1 lagger transfer/install/activation work.

CP-040's bounded composed host-model step is DONE; don't repeat it or count
another helper as an exit. Broad E2 still needs independently authenticated
destination durability and target gates. E1/E2/E3 and F5/F6/F7 remain OPEN.
This is not phase acceptance; Pi deferred, not waived.

## CP-040 — ARCHIVE PERSISTENCE FAULT VALIDATION — 2026-09-23 (historical)

Verified branch/HEAD: `v2/phase-5-corrections` / `d44a4a9`; remediation remains
UNCOMMITTED. CP-038 archive and CP-036 install approvals stand. This increment
validates E2 local compaction under faults; it changes tests/build inventory/docs,
NOT production behavior or canonical bytes. No new production defect established.

Three permanent suites now exercise:

1. Actual archive source with139 reached EIO sites across fresh dependency
   creation, first/replacement publication and selected second-object failures.
   Three interruption/short-write cases must successfully retry. Recovery checks
   exact record identities and full command bytes, not just digests, and cannot
   silently rewrite fault evidence or adopt an incomplete prefix.
2. Actual archive source with124 modeled durable-image cuts. File bytes and
   directory names persist separately; explicit-sync-only and eager-name
   schedules cover fresh33/replacement29 boundaries each. Both direct-source
   file-sync/directory-sync omission mutants fail at the intended assertion.
3. Actual laboratory kernel compaction with nine snapshot/WAL syscall errors.
   Two voters commit1..3; a locally durable uncommitted4 forces nonempty suffix
   rewrite. Failure latches service closed; restart preserves archived tuples1..3,
   kill_epoch3/applied3 and exact suffix4. Every configured fault must fire.

Main rejected and corrected weak worker harnesses: missed syscall ordinals,
retry tests that accepted another error, fresh fixtures destroyed before recovery,
first-object faults mislabeled second-object, and fixed-yield false timeout.
The timeout is retained as HARNESS failure, not a Raft defect. Test-only source
objects replace global libc interposition. One worker improperly spawned a nested
reviewer despite the two-worker limit; stopped/disclosed. No more agents active.

Final rebuilt Release85/85(15.60s), C++ Debug ASan+UBSan85/85(228.88s), focused3/3,
tools40/40; independent inventory85host/82V2. UB halt/nonrecovering, leaks off,
Raft C uninstrumented. Evidence/commands/caveats:
`evidence/f5_archive_faults_2026-09-23/README.md`. Partial source fingerprints are
NOT a target pin. No new TSan/NUC/ARM/Pi or Python model result. Protected paths
untouched; no stage/commit/tag/deployment/device/venue interaction.

NEXT, still measured by the owner's three real exits:

1. E2: complete the COMPOSED archive → snapshot → WAL durable-image sweep.
   This increment's124 cuts are archive-only; nine integrated EIO cases are
   not an end-to-end power-loss model. Preserve exact history/suffix or refuse,
   never infer salvage/adopt orphans. Do not duplicate the completed local
   syscall suite. Live reclamation separately requires approved/authenticated
   independent-destination durable evidence; local capability is lab-only.
2. E3: integrate the shared persistence fence into actual hard-state and suffix
   truncation, still synchronous today; preserve no-premature-success behavior.
3. E1: actual lagger transfer/install/activation under approved semantics.
   Existing witnesses are not a working installer. New transport/destination
   byte choices still require review before encoding.

Broad E1/E2/E3 and F5/F6/F7 remain OPEN. No phase acceptance or live deployment
claim. Pi deferred, not waived; target re-gates/independent-copy obligations stay.

## CP-039 — REAL LOCAL ARCHIVE COMPACTION — 2026-09-23 (historical)

Branch/HEAD verified `v2/phase-5-corrections` / `d44a4a9`; remediation remains
UNCOMMITTED. CP-038's five archive decisions and CP-036 install decisions remain
approved. Do not reconstruct work from the historical sections below.

Implemented the real E2 laboratory path, not another disconnected codec:
`ReplicatedKernel::compact()` is now an asynchronous pump. The owner captures
bounded owned command/state copies; the existing WAL worker verifies the durable
source, publishes immutable archive segments and the V2AC catalog, and saves and
reloads the snapshot. The owner rechecks the fence and consumes a one-shot
coverage capability before Raft releases its prefix. A second job rewrites WAL
LAST. No borrowed retained-cell memory crosses to the worker. Pending work
withholds authority/results and refuses protocol progress without owner-thread
filesystem I/O or a join fallback. Cold destruction still joins for lifetime safety.

Permanent integrated tests check exact independent command bytes in catalog-named
segments before/after restart, not just digests or state. Unread results survive;
successive cuts work; real two-voter pilot mode refuses local-only reclamation.
Four high-level persistence interruption points recover; a parked worker cannot
release authority/results or delete the WAL. This is NOT exhaustive syscall or
device power-loss coverage. No independent-copy receipt/transport was invented.

Corrections driven by REDs and independent review: underfilled cuts must defer
without killing service when unfinished liabilities need the last object slot;
cached catalog metadata is not permission to delete after a segment changes;
startup rejects future publication generations, archive-ahead history and
inconsistent cross-artifact terms. Archive publication packs up to four 4-MiB
objects in one catalog transaction. All admitted command/marker liabilities
reserve bytes AND packed objects; ordinary traffic retains the approved margin.
Native constructor configuration defaults to laboratory 1GiB/4096 objects.

Full ASan uncovered real worker-stack overflow in snapshot verification. Fixed
save/load codec workspaces are heap-owned now; file read lookahead stays 65,588
bytes. The allocation test now bounds total fixed decoder workspace, rather than
mistaking relocated codec scratch for file-proportional allocation. TradingState
and canonical bytes are unchanged. Peak process memory and target timings remain
unmeasured. Snapshot decode can allocate/fail on the storage worker, not the hot
reducer; failures latch service closed.

Final rebuilt Release82/82 (18.44s), C++ ASan+UBSan82/82 (200.87s), focused7/7,
tooling40/40 and independent inventory82 host/79 V2. UB halt/nonrecovering,
leaks off, linked Raft C uninstrumented. Focused actual WAL-worker TSan passed;
this is not whole-replica TSan coverage. Two consumer-bypass mutants plus one
file-proportional-allocation mutant failed at their intended assertions.
See `evidence/f5_archive_integration_2026-09-23/README.md` for commands, actual
results and retained failed runs. Do not infer acceptance from old PASS records.
Two bounded Sol workers; main reviewed integration, corrected ordering, ownership,
quota and stack issues. Protected paths untouched; no stage/commit/tag/deployment,
venue/device interaction. The Python model was not modified or rerun here.

NEXT, measured by the owner's three exits:

1. E2: systematically sweep actual archive dependency/segment/catalog,
   snapshot and WAL publication write/sync/rename failures and crash images,
   including incomplete namespaces. Preserve evidence; never auto-adopt or
   delete orphans. Current four transaction checkpoints do not cover this.
2. E3: extend the shared persistence fence to real hard-state/suffix-truncation
   paths; they remain synchronous today. No helper-only or background-thread-only
   closure claim.
3. E1: actual lagger transfer/install/activation using the approved installer
   semantics and preserved historical evidence. The existing contract witnesses
   are not a working installer. Authenticated transport/destination proof remain
   separate material reviews before new bytes are encoded.

Broad E1/E2/E3 and F5/F6/F7 remain OPEN. Local-only reclamation cannot enable live
two-host deployment. Pi remains deferred, not waived; NUC/ARM/Pi re-gates and
independent-copy durability remain mandatory launch work.

## IMPLEMENTING CP-038 — ARCHIVE CONTRACT APPROVED — 2026-09-23 (historical)

Branch/HEAD verified v2/phase-5-corrections / d44a4a9; remediation UNCOMMITTED.
Read `F5_ARCHIVE_CATALOG_BYTE_REVIEW.md` next: exact NEW bytes/names/bounds are
APPROVED: owner explicitly approved all five points. CP-036 installer decisions
remain approved; do not reopen either review.
This review enables E2 actual evidence-preserving compaction/restart and E1
historical replay. E3's shared fence is required for the actual transaction.
Owner measures these finished behaviors, not helper/suite counts.

Recommendation: one self-contained atomic-replaced local catalog, complete
genesis prefix, no pages/chain/extra selector. Exact96-byte header +112-byte rows
+32-byte digest, max4096 rows/458880 bytes. Each row retains its own source
storage generation; current native capability instead binds CURRENT storage and
catalog generation/digest. Immutable segment/dependency files are never pruned;
only current catalog index metadata is replaced. Fixed identity+execution-catalog
dependencies reuse existing formats. New named v2-archive root child requires
verified ownership/validation, not a loose startup allowlist exemption.

Lab quota proposal:1GiB published logical segment bytes,4096objects; ordinary
high-water subtracts16MiB/four rows WITH unfinished reservations. Separate fixed
temp/catalog/dependency accounting reserve5120380 bytes. Backlog proposal counts
both unconfirmed published objects AND unfinished liabilities: normal4/16MiB,
hard5/20MiB. Underfilled cuts must recompute all remaining reservations before
publication; otherwise retain WAL and defer the cut. These are starting limits,
not measured live retention or physical filesystem-capacity guarantees.

Catalog asserts LOCAL committed coverage only. Lab may use it; two-host/live
reclamation refuses without separately authenticated destination-durable evidence.
No placeholder backup bit/receipt bytes. This sequences the implementation; it
does not remove live independent-copy/Pi requirements. Destination wire/receipts
and live quota validation remain separate boundaries, honestly named in the sheet.

Two Sol read-only reviewers challenged provenance and publication/quota math.
Main rejected chain/pages/extra selector and added per-row generation, strict
early-cut reaccounting and bounded orphan refusal. Both reviewed final sheet;
no blocking contradiction found. No code/build/test/device/network changes;
CP-037 remains last runtime evidence. Protected paths untouched, no stage/commit.

NEXT: approval obtained; work directly from
the original archive-loss RED toward real publication+compaction+restart, with
F6 fence integration. No separate codec-only completion milestone. E1/E2/E3 and
F5/F6/F7 remain OPEN. Pi deferred, not waived.

## REMEDIATION CP-037 — INSTALL SEMANTICS APPROVED / CONTRACT WITNESSES — 2026-09-23 (historical)

Branch/HEAD verified v2/phase-5-corrections / d44a4a9; remediation UNCOMMITTED.
Owner explicitly approved all six CP-036 decisions after the plain-language/C++
explanation. `F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md` records the approval;
do not ask for those rules again. Archive publication/catalog/quota/receipt bytes
and real transport framing remain separate unapproved contracts.

Owner's current completion rule: count only E1 safe actual lagger catch-up,
E2 actual compaction preserving verified historical evidence, E3 actual
nonblocking persistence with no premature protocol success. Every additional
task must name one of these exits or a separate approved launch requirement;
otherwise defer it. See the roadmap section2 table. No helper/test count closes
an exit. Both current contract witnesses support E1 only.

Reachability correction: normal public propose/receive/tick apply through commit
before returning, while install requires S>old applied. Thus S<local commit is
not currently established as a healthy safe-boundary install case. Cold replay
suppression is proved on a valid disk fixture; the committed-suffix result cohort
is defensive future/intermediate behavior, not a reproduced current live bug.

Implemented two permanent contract witness suites using two Sol workers and main
independent corrections, runtime verification and build/gate integration. There is
no live-install API to execute the flawed draft against. ACK overclaim is a
deliberately wrong test policy, not a current production-install bug. Cold replay
suppression is intentional restart behavior, not a defect to change globally.
Do not fake an install method or change restart semantics to manufacture GREEN.

Main rebuilt baseline Release74/74 (11.96s), and independently rebuilt/reran the
original lagger RED (snapshot12, lagger0, seven retries) and archive-loss RED
(snapshot3, original command bytes absent from retained WAL and all files).
Raw logs: evidence/f5_install_contract_2026-09-23/. Final Release76/76 (12.64s),
focused2/2, tooling40/40 and independent inventory76 host/73 V2. Main killed two
direct production-source recovery mutants at the intended assertions. ACK negative
policy reports commit20/applied20 after only boundary12 evidence. That is a
test-only rejected policy, not a current production installer. Full rebuilt C++
ASan+UBSan76/76 (155.31s); UB nonrecovering/halt, leaks off, Raft C uninstrumented.
No model/TSan/target/latency/power-loss claim; git diff --check clean.

Main rejected ACK fixture self-certification, fixed callback-address lifetime,
required coherent divergent terms and real replacement/commit exchanges. Main
also fixed result fixture's /tmp symlink path and unauthorized caller17; those
setup failures are retained but are NOT product REDs. Production source unchanged.

NEXT OUTCOME: E2 integrated publication-to-compaction/restart, with the smallest
concrete archive catalog/provenance/quota review BEFORE encoding. Do not open
another disconnected codec/helper milestone. The archive owner must feed the
actual compaction guard and supply E1's historical replay evidence. In parallel,
the shared F6 persistence fence must make real hard-state/truncation/selection
operations satisfy E3, not merely move WAL appends. Return to actual E1 lagger
activation using those dependencies. No archive format or authenticated wire is
implicitly approved by CP-036. Explain any material review in chat as problem,
importance, intuition, mechanism, trade-offs and C++ ownership/performance.

F5/F6/F7 remain OPEN. Protected paths preserved; no stage/commit/tag/device/venue
or deployment work. Pi remains deferred, not waived.

## REVIEW CP-036 — INTEGRATED INSTALL CONTRACT — 2026-09-23 (historical)

Branch/HEAD verified v2/phase-5-corrections / d44a4a9; remediation UNCOMMITTED.
Read `F5_INTEGRATED_INSTALL_CONTRACT_REVIEW.md` next. Six remaining decisions
are PROPOSED, not owner-approved. Two Sol read-only reviewers; main reconciled
findings against code/contracts. No code, bytes, builds or runtime tests changed.
CP-031 selector and CP-033 base seal/layout remain approved; do not reopen them.

Two draft errors corrected with reasoning, NOT claimed as executed REDs:
(1) activated ACK proves only snapshot S, never the retained local suffix end;
otherwise unverified/divergent entries can be counted toward leader quorum.
(2) RESERVED S<i<=max(S,captured commit) needs outcome capture during staged
suffix apply; ordinary restoring replay suppresses notifications and would strand
those handles. Only retained entries above that commit remain RESERVED.

Recommend same live ledger/exact inventory, no separate durable install receipt;
canonical snapshot transfer (four 16-KiB chunks max), defer actual wire envelope
until authenticated transport is selected; async generation-fenced selection;
finite pinned replay range rather than arbitrary8192 total historical records.
Each cost, rejected shortcut and approval boundary is in the review. Missing
archive publication/provenance formats and quotas remain genuine separate work.
Independent-copy policy still gates live reclamation, not activation that keeps
all old evidence. No cleanup or Pi waiver follows.

NEXT: owner review, then permanent REDs for both counterexamples and integrated
archive/async/staged-owner work. Do not manufacture approval from “proceed” or
call simulated authentication live catch-up acceptance. Original lagger, archive
loss and owner-fsync REDs and F5/F6/F7 remain OPEN. Last verified tests are CP-035,
not rerun here. Protected paths untouched; no stage/commit/tag/venue/device action.

## REMEDIATION CP-035 — READ-ONLY INSTALL PREFLIGHT / RESULT INVENTORY — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner requested two Sol implementation workers with main leading/verifying.
Used two gpt-5.6-sol workers on disjoint native-only components. Main refined
interfaces, corrected tests/reasoning, added a validator fix and independently
reviewed/mutation-checked/integrated. No new wire/persistent/economic contract.

Implemented `snapshot_install_preflight.hpp/.cpp`: read-only bounded classifier
over caller-validated immutable records. Exact boundary tuple+payload required
before describing retained suffix; preserve all entries above local commit too.
Checks full local structure, term/snapshot consistency and live==durable tail;
volatile tails require flush/retry, not omission. Old snapshot remains committed
even with metadata commit0. Covered-only WAL prefixes and ordinary beyond-tail
catch-up supported. Returns borrowed descriptive range, NOT installation/deletion/
commit/authentication authority. Not wired into live install yet.

Implemented `ResultLedger::visit_obligations`: validate native partition/links/
counts/pool tags before callbacks; copy READY in take FIFO, RESERVED by slot.
Exact original handle/binding/full result values, no mutable alias or allocation.
Same ledger ownership/capacity/229400-byte size unchanged. Owner-thread only,
callback non-reentrant, no revision/root/concurrent-change-detector claim.

Preserved component runtime REDs (worker suffix logs explicitly transcribed),
main occupied-pool validator RED/GREEN, five intended direct source mutants.
Main corrected a false test oracle that rejected a valid snapshot beyond local
tail; other floor/layout/evidence corrections are review-driven, not original REDs.
Raw main rerun of original lagger integration remains RED: compacted12, lagger0,
seven actual retries. This is not part of passing CTest or fixed by the helpers.

Fresh Release74/74 (12.36s), full C++ ASan+UBSan74/74 (159.69s), then final rebuilt
focused sanitizer2/2 (0.37s) for last test-only additions;
tooling40/40, independent inventory74 host/71 V2. Nonrecovering UB/halt, leaks off,
Raft C uninstrumented. Evidence: evidence/f5_install_preflight_2026-09-23/README.md.
No target/model/TSan/latency/power-loss claim. Always build before CTest.

NEXT: close the remaining material install/transfer/coverage/budget review before
encoding or exposing live install. F5_SNAPSHOT_INSTALL_REVIEW.md sections5/7/9
still contain pending choices; CP-031 selector and CP-033 seal/layout are approved
and MUST NOT be re-requested. Draft section6 now correctly uses V2LG + closed base,
not extra descriptor/receipt files or the superseded V2GS proposal. Then integrate
staged owner, exact archive-backed historical results, SAME live ledger, suffix
reattachment, writer quiesce, final tuple revalidation and one-way durable selection.
Avoid more disconnected helper work as a substitute for that end-to-end path.

Original archive-loss/catchup/owner-fsync REDs, archive publication, fenced repair,
async owner fence and final gates remain open. F5/F6/F7 NOT CLOSED. Protected
tracked paths clean, model untouched. No stage/commit/tag/source freeze/device/
venue/credentials/deployment. Pi deferred, not waived.

## REMEDIATION CP-034 — INITIAL GENERATION PUBLICATION / SELECTED STARTUP — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner approved CP-031 selector and CP-033 base/layout/startup contracts; this is
their integration, not a new byte review. No implicit migration or voter reset.

Implemented fresh empty-root genesis publication and selected cold recovery.
VerifiedIdentityStartup holds root + generations + selected-runtime locks;
directory() now routes existing kernel stores to selected runtime. Initial bound
metadata, root identity, empty base and seal are durable before selector rename;
final root sync/revalidation precedes returning ownership. No root metadata copy.
Recovery validates final selector/base, current metadata and snapshot base floor.
A valid successor makes an existing token stale. Temp selector is inspected as
absent/valid/invalid for diagnosis, preserved, never promoted or authoritative.
Malformed temp does not displace a valid final; actual inspection I/O fails closed.
Interrupted nonempty setup and legacy roots stay stopped and untouched.

IMPORTANT: token is cold ownership, NOT full-history/serving authority. Recovery
can re-sync WAL before semantic admission. Existing ReplicatedKernel still checks
full mutable WAL/commands/suffix/commit coverage before writable WAL open. Do not
duplicate that decoder or claim the token alone proves voting safety. All users
must freshly rebuild: token layout changed and ReplicatedKernel embeds it.

Retained pre-integration routing RED and temporary-diagnostic stub RED. Native
cut/floor/lock/evidence-preservation tests; actual startup syscall model sweeps
26 persistence cuts + 26 EIO positions, short/EINTR writes and four checked output
closes. Four direct guard mutants fail intended assertions. Model is genesis-only,
not physical power-loss or snapshot-install evidence; snapshot floor fixtures are
manually authored valid selected images. Two workers, main architecture/review.
Release 72/72 (12.35s); full C++ ASan+UBSan 72/72 (151.83s), nonrecovering
UB/halt, leaks off, Raft C uninstrumented. Always rebuilt before CTest.
Tooling 40/40; independent inventory 72 host / 69 V2. Evidence:
evidence/f5_generation_startup_2026-09-23/README.md. No target/model/TSan/latency claim.

NEXT: integrate actual snapshot-generation installation with retained unknown
suffix and READY/RESERVED result obligations. Review existing approved contracts
and exact next RED in F5_SNAPSHOT_INSTALL_REVIEW.md and the snapshot-catchup
reproducer; initial publication is NOT a reusable live replacement transaction.
Transfer wire bytes/budgets and coverage details in that draft are still pending;
present those remaining material choices before encoding, not the approved seal.
Archive publication/reclamation, fenced repair and async owner fence also remain;
preserve original archive-loss/catchup/owner-fsync REDs. F5/F6/F7 NOT CLOSED.
Do not re-request approved selector/seal bytes. New material protocol/budget choices
still need review. No source freeze, stage/commit/tag, device/venue, deployment or
credentials. Protected tracked paths clean, model untouched. Pi deferred, not waived.

## REMEDIATION CP-033 — APPROVED BASE SEAL / ON-DISK BASE VERIFICATION — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner APPROVED F5_GENERATION_STORAGE_REVIEW.md: exact 40-byte base seal/digest,
fixed layout/root ownership, runtime snapshot floor/refusal, automatic migration
deferred. CP-031 selector approval unchanged. No further byte approval needed for
these named integrations; do not re-request the same decision.

Implemented first dependencies, NOT the full startup transaction:
- generation_base.hpp/.cpp: exact seal codec and semantic base validation; hashes
  complete immutable bound snapshot plus canonical local identity/generation.
  Positive snapshot coordinates, bound/canonical validity and catalog dependencies
  required. Caller-owned fixed scratch, staged output, small summary, no I/O.
- generation_base_files.hpp/.cpp: read-only root identity/final selector/canonical
  generation/base verifier. Non-following bounded descriptor-relative reads,
  owner-only single-link files, closed immutable namespace, full snapshot bytes
  returned in caller buffer, selector/base digest binding, failure-atomic summary.
  Caller retains trusted root ownership; result grants no startup/voting authority.

New-component stub REDs retained (not historical production-defect claims).
Literal Python golden independently recomputed by main; truncation/corruption,
rehashed unsupported headers, validly framed foreign catalog dependency, zero
coordinates, full snapshot bytes, symlink/hardlink/permissions, no temp/scan
fallback, generation relabelling and unchanged-output tests. Six direct mutants
fail intended assertions. Two bounded workers, main independent integration/review.
Final Release 70/70 (12.01s), C++ ASan+UBSan 70/70 (155.68s) then rebuilt final
focused 2/2 after test-only additions; tooling 40/40; independent inventory
70 host / 67 V2. Nonrecovering UB/halt, leaks off, Raft C uninstrumented.
Evidence: evidence/f5_generation_base_2026-09-23/README.md. No new target/model/
TSan/latency or power-cut claim. All builds precede CTest; diff-check clean.

NEXT: TDD the approved actual startup/publication transaction. Adapt existing
VerifiedIdentityStartup ownership to root plus selected runtime directory; fresh
bootstrap only, preserve/refuse nonempty legacy roots, no automatic migration.
Validate complete mutable metadata/WAL/runtime snapshot before writable startup,
enforce immutable snapshot floor, then test selector preparation/publication at
every durable cut. CP-032 refusal stays active until this path replaces it safely.
Current verifier does not validate mutable history, acquire long-lived locks,
publish/select a generation, or implement the runtime floor. Do not treat it as
a ready-to-start capability. Reuse existing formats; no generic storage framework.
Then integrate snapshot install with unknown suffix/result obligations intact.

Original archive-loss/catchup/owner-fsync REDs unchanged; archive publication,
fenced repair, async owner fence and final gates still open. F5/F6/F7 NOT CLOSED.
Protected tracked paths clean, model untouched. No staging, commit/tag, source
freeze, device/venue, deployment or credentials. Pi deferred, not waived.

## REMEDIATION CP-032 — GENERATION STARTUP GUARD / MINIMAL INTEGRATION REVIEW — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner requested the next item without overengineering. Exact CP-031 selector
approval remains intact; no additional persistent bytes were encoded this turn.

Fixed a transitional integration bypass by TDD: old-layout recover ignored a
valid selector naming a missing generation. Recovery now refuses any selector,
temporary selector or generations entry before identity/metadata resync; existing
ownership tokens recheck too. Three descriptor-relative non-following stat calls,
under held root ownership; ENOENT alone is absent, other errors fail closed.
Bootstrap's generic unknown-content refusal is unchanged. No hot-path cost added.
Healthy no-marker legacy identity startup remains compatible; this does not
implement generation-aware startup, publication, migration or snapshot install.
Trusted directories/cooperative locks remain assumptions, not hostile-writer fencing.

Actual pre-guard production RED retained. Main expanded tests to 3 names x 3
entry types, complete evidence preservation, zero pre-refusal checkpoints and
existing-token refusal. Corrected a scope claim: the opaque-WAL fixture tests
identity-boundary preservation, not full history validity. Independent review
plus direct recover/token/no-follow mutants fail intended assertions.
Fresh Release 68/68 (21.81s), focused startup 3/3, C++ Debug ASan+UBSan 68/68
(163.04s), tooling 40/40; independent inventory 68 host / 65 V2. Built before
CTest; nonrecovering UB/halt, leaks off, Raft C uninstrumented. No new target,
model, TSan, latency or power-cut claim. Evidence:
evidence/f5_generation_guard_2026-09-23/README.md.

NEXT APPROVAL: F5_GENERATION_STORAGE_REVIEW.md. One consolidated PROPOSED
integration contract: reuse existing formats, exact 40-byte immutable-base seal
and digest, fixed generation layout, root-owned startup, seeded runtime snapshot
with a no-below-base floor, and defer automatic legacy migration. No generic
manifest database or repair framework. DO NOT encode these new bytes until the
owner approves. Then TDD real generation preparation/selector publication/recovery
at each durable cut, followed by snapshot integration; preserve unknown suffix
and result obligations. No fallback or implicit voter reset.

Archive publication/reclamation, snapshot catch-up, fenced repair, async owner
fence and final gates remain open; original integration REDs unchanged. F5/F6/F7
NOT CLOSED. Two bounded workers, main review/integration. Protected tracked paths
clean; model untouched. No staging, commit/tag, device/venue or credentials.
Pi deferred to final hardware stage, not waived. No live trading admission.

## REMEDIATION CP-031 — APPROVED LOCAL-GENERATION CODEC — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner approved exact F5_LOCAL_GENERATION_BYTE_REVIEW.md after its explanation:
160-byte selector, local/shared binding, strict sequence/generation rules,
exclusive-directory/seal-last and atomic selector/no-fallback directions.
Prepared-base format, concrete migration/root-layout integration, archive coverage,
repair capacity/protocol and async continuation remain separate unresolved work.
Owner asked if this closes all F: explicitly answered NO; approval/component
GREEN must never be represented as end-to-end F5/F6/F7 closure.

Implemented local_generation.hpp/.cpp: pure explicit-LE160-byte codec and pure
value-transition checks. Fixed frame staging; no I/O/allocation, no startup or
active selector switch. Native value56 bytes on host, never serialized directly.
Encode/decode failure-atomic and overlap-safe as tested. Maximum counters can be
decoded/recovered; further transition refuses. Generation gaps allowed, selection
sequence gaps refused. Base digest remains opaque, not base verification.

Worker component-stub RED and main independent literal-golden RED retained, then
GREEN. Independent Python struct/hashlib golden, every truncation/byte corruption,
resealed owner/manifest/reserved/shape failures, exact output tails,546 authored
transition cells, final nonwrapping/max counters, overlap and allocation checks.
Independent worker reviewed implementation; main direct owner/digest/sequence
mutants fail intended assertions. No filesystem/power-cut proof claimed.

Fresh Release67/67 (19.80s), C++ Debug ASan+UBSan67/67 (160.90s), tooling40/40,
independent inventory67 host/64 V2. UBSan nonrecovering/halt, leaks off, Raft C
uninstrumented; no new model/TSan/target/latency claim. Built before CTest. Evidence:
evidence/f5_local_generation_2026-09-23/README.md. diff-check clean.

Next: connect the selected-generation storage transaction, not more changes to
approved selector bytes. Consolidate the remaining prepared-base/root-layout/
bootstrap-migration contract for owner review before encoding those dependencies;
then TDD selector publication/recovery at every durable cut, followed by snapshot
integration. Preserve unknown suffix/result obligations; no directory-scan/temp/
previous-generation fallback. Do not mistake codec success for safe activation.
Archive publication/reclamation, snapshot catch-up, fenced repair, async owner
fence and final acceptance gates still open; original integration REDs unchanged.

At most TWO workers used; protected tracked paths clean, model/ untouched. No
staging, commit/tag, source freeze, device, venue, deployment or credentials.
Pi remains deferred to final hardware stage, not waived. No live trading admission.

## REMEDIATION CP-030 — FOUR DIRECTIONS APPROVED / ARCHIVE FILE VERIFIER — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner approved all four CP-029 directions: durable archive coverage, transactional
local generation, offline fenced two-voter repair with dedicated control headroom,
and one pending async persistence fence. This is NOT approval of exact new bytes,
filenames, numeric partitions/quotas or unresolved callback/repair protocols.
Normal max TWO workers used; no new four-worker exception inferred. Protected
tracked paths clean; model/ untouched. No staging/commit/tag or device/venue action.

Implemented smallest existing-byte slice: read-only archive_file.hpp/.cpp around
the approved CP-028 codec. Caller-owned buffer, no internal allocation; openat
relative basename with nofollow/nonblock, regular single-link file, bounded length,
short/EINTR read handling, EOF/post-stat checks, close-once, full codec validation
before output metadata publication. No archive publication/coverage/commit/backup/
deletion permission. Exclusive trusted directory/input and nonaliasing required;
opened-fd verification does not promise pathname identity after return.

Two new suites (filesystem plus main independent syscall shims), literal existing
CP-028 golden and full bytes. Retained original implementation-stub RED, smallest
GREEN, three direct guard mutants killed intended size-change/trailing/short-codec
assertions. Main corrected wrong-guard fixture ordering, stat error classification
and interruption handling; independent worker reviewed ownership/limits. Failing
logs with generic assertions are not promoted to named production-bug evidence.

Fresh Release65/65; full C++ Debug ASan+UBSan65/65 (155.23s), then rebuilt focused
final2/2 after test diagnostic-text change. Nonrecovering UB/halt; leaks off; Raft
C uninstrumented. Tooling40/40; independent inventory65 host/62 V2. No new model,
TSan, target, latency or power-loss claim. Evidence/reproduction:
evidence/f5_archive_file_2026-09-23/README.md. diff-check clean.

NEXT OWNER BYTE REVIEW: F5_LOCAL_GENERATION_BYTE_REVIEW.md. Proposed 160-byte
selector binds shared identity, local voter, nonwrapping generation/selection
sequence, predecessor and immutable-base digest. Recommend exclusive generation
mkdir with seal-last, atomic selector replacement and no fallback. Only standalone
codec is next implementation slice after approval; prepared-base bytes, root-lock/
genesis migration, archive catalog, transfer/repair and async glue stay separate.
Main reviewed/corrected directory link-count assumption, immutable base versus
mutable runtime snapshot and first-selection migration semantics. Question sent
to owner; DO NOT infer approval until answered.

CP-026 archive-loss/catchup REDs and CP-029 owner-fsync REDs remain open; no behavior
changes to compact, snapshot install, repair or Raft callbacks. F5/F6/F7 OPEN.
Pi deferred to final hardware stage, not waived. No live trading admission.

## REMEDIATION CP-029 — FOUR-LANE CLOSURE REVIEW / NEW ASYNC REDS — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner requested four parallel agents for archive, install, repair and async.
Runtime allowed four active including main: three bounded workers plus main on
async/integration. This exception is TASK-ONLY; normal two-worker limit resumes.
No production/CMake change, staging/commit/tag, device, venue or credential action.
Protected tracked paths remain clean; untracked model/ untouched.

READ NEXT: F5_F6_FOUR_LANE_CLOSURE_REVIEW.md and its four linked lane reviews.
All new publication/coverage/selector/transfer/repair/async protocols remain
PROPOSED. Parallel permission did not approve unspecified bytes or quotas. A/B
and CP-028 codec approval remain intact. None of the four items is closed.

New permanent tests outside CTest:
- v2_async_hard_state_red.cpp: actual production BoundMetadataStore compiled behind
  forwarding fsync observer. With WAL worker running, campaign/vote/commit still
  perform4/4/2 syncs on owner. Each exits1 at intended owner-I/O assertion, after
  real persisted tuple and protocol/state checks. Not premature-vote or latency
  evidence; original checkpoint-only logs superseded by actual syscall logs.
- lost_voter_repair_reachability.cpp: worker characterization, independently read,
  strict-built and reproduced by main. Two byte-identical donor disks correspond
  to different old quorum commit outcomes. Full unknown suffix must survive;
  ordinary missing-hard-state refusal stays correct. This PASS is NOT repair GREEN.

Shared design dependencies: destination-local generation distinct from archive/
session/cluster identity; immutable prepared base versus mutable live WAL/metadata;
no stale write/completion crossing selection; one selector, fail closed rather
than fallback. Main corrected draft-only unsafe suffix splice, fabricated losing
reservation results and archive checksum-as-commit permission; removed unnecessary
new durable result FIFO and redundant selectors. Repair saturation can prevent a
new-term barrier; reserved internal headroom is PROPOSED, not approved, and cannot
retroactively solve existing-full histories. Async callback continuation and
runtime suffix rewrite remain explicit implementation proof obligations.

Fresh build then Release63/63,11.16s. No current full sanitizer/model/tool/target
rerun; CP-028 results remain historical. diff-check clean. Evidence and exact
commands: evidence/f5_f6_parallel_contracts_2026-09-23/README.md. CP-026 archive/
catchup integration REDs remain open (last rerun CP-028). F5/F6/F7 OPEN.

NEXT: owner review of shared generation/coverage/fenced-repair/one-pending-fence
direction and capacity tradeoff; finalize exact minimal byte/budget sheets before
production encoding. Then disjoint component TDD in parallel, single-owner
replication integration, crash/stale-generation/saturation sweeps and fresh NUC
gate. Pi deferred to final hardware stage, not waived. No automatic live admission.

## REMEDIATION CP-028 — APPROVED PORTABLE ARCHIVE CODEC — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner approved the EXACT F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md contract after CP-027:
96-byte header / 32-byte record header, both domain-separated SHA-256 digests,
4-MiB complete-object and 8,192-record caps. A/B policy remains approved. No new
publication/coverage/quota, snapshot/install/repair or asynchronous fence contract
is implied. Protected tracked paths remain clean; untracked model/ untouched.
No staging, commit/tag, source freeze, target, venue or credential action.

Implemented standalone archive codec plus 112-byte incremental SHA owner; existing
one-shot digest API delegates with unchanged known bytes. Encoder uses bounded
pull-source views, not per-record descriptors; decoder validates the entire stable
input before any visitor. Preserve refused commands and failed authority attempts:
marker gateway incarnation is not cluster incarnation, and requested catalog may
legitimately mismatch. Current WAL admission semantics, NOT economic revalidation.

Important API limits: input/output are caller-owned contiguous buffers; file-backed
streaming/publication is NOT implemented. Immutable source/input and payload/output
nonaliasing are preconditions. Preflight refusal leaves output unchanged; a later
source failure leaves possible partial UNPUBLISHABLE bytes. Visitor refusal does
not roll back earlier callbacks; destination metadata stays unchanged. Native host
scratch 2,120, view 40, source 24, metadata 80 bytes; excludes caller buffers and
other fixed stack work. No process RSS or target-latency claim.

Two implementation workers, main integration and independent adversarial tests.
Literal independent goldens, full payload assertions, truncation/byte corruption,
resealed invalid coordinates/typed content, source/visitor failures and actual
8,192-record / exact-4-MiB boundaries pass. Main direct source-copy mutations:
seven archive guards and two SHA defects fail intended assertions. Initial new-
component REDs were compile-only and worker-reported without retained raw logs;
do NOT relabel them behavioral production REDs. Initial archive mutants raced
worker edits and are excluded; final single-condition diffs were rerun on stable
source. Evidence: evidence/f5_archive_codec_2026-09-23/README.md.

Fresh final Release 63/63 (11.65s), C++ Debug ASan+UBSan 63/63 (161.37s), tooling
40/40. Independent inventory 63 host / 60 V2. Nonrecovering UB + halt, leaks off,
Raft C uninstrumented. Model109 is historical, not rerun. No target acceptance.

Main rebuilt/reran BOTH CP-026 integration diagnostics: catch-up remains at 0
after peers compact through 12; archive-loss still deletes the pinned raw command
at 1. These remain RED, outside passing CTest. This codec is NOT wired to compact,
does not certify commitment/durability/backup, and grants no deletion permission.
F5/F6/F7 stay OPEN. Next required review slice: durable archive publication,
dependency coverage and generation-bound reclamation capability with explicit
quota/full behavior; then transactional snapshot catch-up and fenced repair.
Do not reopen A/B or encode unspecified contracts. Preserve result obligations
and unknown-commit suffixes per F5_COMPLETION_AND_NUC_PLAN.md. NUC shadow still
needs the process/observability shell; live needs remaining authority/venue/target
gates. Pi remains deferred until final hardware stage, not waived.

## REMEDIATION CP-027 — A/B APPROVED, BOUND-METADATA MODEL / ARCHIVE BYTE REVIEW — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner approved CP-026 A/B: operator-fenced stopped-cluster repair after lost
voting memory; immutable/no-auto-prune archives, fail-closed full behavior and
independent live audit copies. Do NOT reopen those policy choices. No unspecified
bytes/quotas/install/async protocol, source freeze, deployment or trading approved.
Protected paths preserved; at most two workers, main review/integration.

Implemented test-only port of persistence model to ACTUAL BoundMetadataStore;
production save/load compiled behind POSIX shims, independent authored old/new
tuples and packed/golden frame, distinct durable bytes and directory names. Sweeps
14 absent-temp /21 existing-temp /15 short-write syscall cut paths; EIO sweeps on
both normal paths. Main tightened model exception handling and added actual load
cross-check of modeled power-loss image. Existing production ordering unchanged.
Main direct file-sync-only and dir-sync-only mutants compile then fail the intended
recovered-value assertions. These are retrospective mutations, NOT original REDs.
New CTest+independent inventory:60 host/57 V2. Release60/60, C++ ASan+UBSan60/60,
tooling40/40. UBSan nonrecovering/halt, leaks off, Raft C uninstrumented. Model109
is historical, not rerun.
Evidence/limits: `evidence/f6_bound_metadata_model_2026-09-23/README.md`.

NEXT REVIEW: `F5_ARCHIVE_SEGMENT_BYTE_REVIEW.md`. Exact proposed portable archive
codec:96-byte segment header+32 digest;32-byte record header+unchanged payload+32
digest;4-MiB total/8192-record caps. Shared manifest binding; separate codec so hot
scratch does not grow. Not encoded. This approval slice does NOT freeze global
archive quotas/coverage publication, snapshot transfer/generation or repair bytes.
Main corrected worker review: marker incarnation is gateway, not cluster; an
admitted marker may request the wrong catalog and get no proof. Archive must retain
that failed attempt, not retrospectively filter it. Digests do not prove commit,
durability, independent backup or deletion permission. All offsets/reasons in sheet.

CP-026 catch-up/archive-loss diagnostics remain RED and outside passing CTest.
No archive/install/replacement GREEN, no async-hard-state closure, no new target/
power-loss/latency claim. F5/F6/F7 remain open. Pi deferred, not waived. NUC shadow
still needs the node/process shell and observable service wiring; live needs the
remaining host/venue/authority gates. No staging, commit/tag or device action.

## REMEDIATION CP-026 — F5 COMPLETION PLAN / TWO CURRENT REDS — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner asked to plan/parallelize remaining required work and reach NUC promptly.
Two bounded workers added isolated diagnostics; main reviewed, independently
compiled/ran both and recorded `F5_COMPLETION_AND_NUC_PLAN.md`. Protected paths
preserved; no staging, commit/tag, freeze, target, venue or credential action.

Current permanent REDs, deliberately outside passing CTest:
- tests/v2_snapshot_catchup_red.cpp: intact lagger remains applied/commit0 after
  healthy quorum commits/compacts12 authored entries and2000 routed retry rounds.
  Explicit kill_epoch/state_version12 plus independent reducer-state oracle.
- tests/v2_archive_reclamation_red.cpp: compact through3 removes the independently
  pinned canonical command at1 from all fixture files; valid snapshot is not audit.
  Presence-anywhere is ONLY a loss diagnostic, not a future verified-archive oracle.
Both compile and fail at their intended assertions. Retained logs/provenance:
`evidence/f5_completion_review_2026-09-23/README.md`. Old REDs kept as audit trail.

Plan: archive component and snapshot generation/staging component in parallel;
main integrates transfer, result ownership and reclamation; then safe replacement,
F6 durable fences/model, frozen NUC gates and M2/M3 non-trading process deployment.
M3a subsystem/machine observability remains required before live. Pi deferred,
not waived; no automatic single-voter weakening. There is still no V2 node binary.

Owner APPROVED material choices A/B after CP-026: operator-fenced stopped-
cluster lost-hard-state repair instead of automatic online replacement; immutable
archives/no automatic pruning, fail-closed fullness and independent-copy policy
for live audit. Existing quota candidates remain unapproved, not measured sizing.
Finalize the shared byte/budget sheet BEFORE encoding; do not reopen A/B.
Do not interpret permission to parallelize as approval of unspecified contracts.

Integration traps: pinned raft_begin_load_snapshot resets term/vote/membership
and cannot preserve a suffix on the serving owner. Prepare a separate owner and
generation, revalidate current hard state before durable selector switch, and
never resume the old generation afterward. READY result FIFO must survive;
covered RESERVED results need historical outcome reconstruction, not reapply to
final snapshot. Proposed archive-assisted prefix replay preserves that contract.
Unread READY values do NOT automatically pin raw log cells. Preserve unknown-
commit suffix during repair; stale metadata commit is not a deletion watermark.

Fresh build+Release59/59. No new sanitizer/model/tooling/target/performance claim;
CP-025 sanitizer59/59/tools40 remain historical. No production behavior, new byte
contract or acceptance guarantee changed. F5/F6/F7 remain OPEN. Next is the
reviewed contract/design above, not automatic compaction or an unsafe bootstrap.

## REMEDIATION CP-025 — APPROVED OUTBOUND/LOG BOUNDS INTEGRATED — 2026-09-23 (historical)

Branch/HEAD v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Owner approved all four CP-024 choices before implementation. Protected paths
preserved; max two workers, main architecture/integration/review. No staging,
commit/tag, source freeze, target, venue or credential action.

Kernel outbound deque is gone. Four owned coalescing canonical-frame mailboxes
per peer, same-kind replacement only, lower-term watermark, fair cursor, bounded
copy-out. Small frames160 bytes/AppendEntries32768; approved16-entry prefix
batching remains (do NOT reject a longer library suffix). Public dynamic egress
removed from V2; test adapter alone decodes to PeerMessage. Unexpected encoding
fault latches service/authority failure. Actual mailbox owner66680 bytes for TWO
fixed remote slots even on the two-voter pilot. Earlier per-peer prototype was
not deployed footprint. Frame-size arithmetic corrected by4 bytes; caps unchanged.

Kernel retained map/vector owner is gone.8192 stable2048-byte cells;8128 ordinary,
64 protected tokens using existing operator classifier; markers ordinary. Local
typed refusal log_capacity_exhausted appended at8. Whole follower result/log
preflight credits actual suffix pools without releasing them. CP-023 control-only
fallback learns terms and responds for verified durable prefix; no partial batch
admission on log/result pressure. Successful durable deletion precedes cell/handle
release. WAL staging remains independently bounded/fallible, not an atomic batch
disk transaction. No change to bytes, classifier, result quota or pinned Raft.

Startup streams validation then restoration before writable WalSegment creation;
committed records also consume log cells but produce no replay notifications.
Oversized committed or marker-only uncommitted history refuses unchanged. Store
constructor and suffix/prefix rewrite stream bounded blocks; rewrite reads its
existing fd, not a newly opened/maybe missing path. Diagnostic vector readers
remain; production callers no longer use them. Tail-by-tail rewrite can still be
quadratic I/O. No allocation-free admission or whole-process memory claim.

Permanent original growth REDs now GREEN/registered. New tests cover stalled-drain
retry recovery, queued bytes across durable replacement, input/output aliasing,
mixed-batch refusal, protected saturation, markers, exact full restart, unchanged
oversized startup, modulo reuse, and suffix storage failure. Four main and five
worker direct mutations hit intended assertions. Evidence/provenance caveats:
`evidence/f5_capacity_integration_2026-09-23/README.md`. Worker new-component REDs
were compile-only; worker WAL RED used unverified build archives and is NOT
credited as current original RED. Main fresh materialization mutant covers it.

Final fresh Release59/59, C++ ASan+UBSan59/59, tooling40/40; inventory59 host/56 V2.
Sanitizer nonrecovering UB + halt, leaks off, Raft C uninstrumented. Model109 is
CP-020 historical. Actual kernel inline91424; log owner64+16777216 cells+327680
descriptors; result ledger229400; mailbox66680; library/scratch/allocator extra.

NEXT: this closes the approved outbound/log ownership slice, NOT full F5/F6/F7.
Admitted replacement, transactional snapshot install/catch-up and archive-before-
last-copy reclamation remain OPEN and their material contracts need review.
Never introduce automatic compact/deletion to escape fullness. Finite reserve
does not guarantee quorum/emergency deadline or ordinary fill ingestion; live
risk remains held. F6 bound-metadata model port/async fences and F7 frozen target
gates remain. Pi deferred, not waived. M3a observability/process wiring before live.

## REMEDIATION CP-024 — OUTBOUND/LOG REVIEW, APPROVAL PENDING — 2026-09-23 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation UNCOMMITTED.
Protected/dirty paths preserved. Two read-only workers; main owns review/tests/docs.
No production change, staging, commit/tag, source freeze, target or venue action.

Owner asked to tackle outbound/log saturation. Existing candidates were expressly
unapproved; do not infer approval from the request to start. Detailed proposal:
`F5_OUTBOUND_LOG_CAPACITY_REVIEW.md`. Review its four decisions before encoding.

Three original behavioral REDs in tests/v2_outbound_log_capacity_red.cpp (NOT
CTest yet):512 retained heartbeat replies;129 retry packets/542604 bytes for one
peer while source log stays16;8129 ordinary retained entries after draining all
earlier results. These establish growth beyond candidate thresholds, not breach
of an already approved quota. Full source reasoning, logs, reproduction and
prototype memory probe: `evidence/f5_capacity_review_2026-09-23/README.md`.

Recommended, NOT implemented: four owned latest-per-kind mailboxes/remote peer,
32-KiB AppendEntries plus three160-byte small cells; whole-message same-kind
replacement, never lower term; fair drain and bounded copy-out. These are Raft
packets, NOT replaceable trading outbox effects. Probe33336 native bytes/peer,
plus separate egress/scratch. Retained log8192 entries,8128 ordinary/64 existing
operator-control tokens,2048-byte cells (16 MiB backing); markers ordinary. Old
independent16-MiB/256-KiB live-byte quota claims are superseded as nonbinding.
Keep current Raft ring growth for MVP and explicitly budget its resize peak,
rather than expand fork for allocation-free admission. No whole-process ceiling.

Fullness stops admission, never evicts history. Reuse approved CP-023 follower
control-only protocol for log pressure; preserve actual suffix ownership until
durable deletion. Need bounded recovery/rewrite scratch too: current vector
materialization defeats a slab-only memory claim. No automatic compact shortcut:
archive-before-last-copy remains open.64 operator slots do not guarantee quorum,
kill deadline or continued ordinary fill ingestion. Live deployment remains gated.

Fresh build+Release54/54. Sanitizers/tools remain CP-023 historical; model remains
CP-020 historical. Diagnostic/probe are outside CTest, no new GREEN or mutation
claim. NEXT after owner approval: mailbox component+egress; fixed log+all admission
paths; bounded recovery/rewrites; adversarial failure/mutation/full regressions.
Detailed sequence in review §5. F5 install/archive/replacement, F6 async fences/
bound-metadata model port and F7 targets remain OPEN. Pi deferred, not waived.

## REMEDIATION CP-023 — RESULT RESERVATIONS INTEGRATED — 2026-09-23 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation UNCOMMITTED.
Preserve dirty/protected paths. Two workers maximum; main integrated and reviewed.
No staging, commit/tag, source freeze, target, venue or credential action.

Owner separately approved F5_ADMISSION_RESULT_REVIEW §8 after CP-022. Kernel now
uses the approved1024-slot ledger (960 ordinary/64 protected), not a result deque.
Both admission paths, startup reservations, pre-apply binding checks, publication,
consumer take and durable suffix ownership are integrated. Markers emit no result;
committed replay remains suppressed. Ready notifications survive compaction/terms.
Native ledger229400 heap bytes, kernel91472 inline; optional per-log handle24
native bytes. This is NOT a whole-process memory bound.

Follower capacity planning is read-only and simulates actual released slot pools
and remaining generations. No old reservation is freed before successful durable
truncation. Single synchronous Raft owner means no interleaving consumer/admission;
log_offer reserves before accepted log count advances. A failed offer cleans its
unaccepted payload and handle. If capacity cannot fit the whole batch, process
original term/prev without new entries, cap commit at min(advertised,prev), and
reply only for verified durable prefix. This is valid control processing, not a
no-mutation malformed-frame refusal. Local refusal is result_capacity_exhausted;
fixed refusal/reserved/ready counters expose ownership.

Original four CP-022 REDs reproduced on fresh pre-integration build, now GREEN
and IN CTEST as v2-result-admission-tests (historical _red filename retained).
New edge suite covers saturated prev mismatch, failed second tail rewrite with
exact surviving reservations/restart identities, and actual-pool replacement.
Five direct source-copy mutants fail intended assertions. The old WAL-reserve
fixture now uses larger ordinary records to hit WAL pressure before result
pressure; it still proves reserved blocks and now catches failed-offer slot leaks.
Ledger-preflight worker's original RED was compile-only, NOT behavioral; the
kernel's original four REDs are behavioral. Full evidence/reproduction/limitations:
`evidence/f5_result_integration_2026-09-23/README.md`.

Verification: fresh Release54/54, ASan+UBSan54/54, tooling40/40; independent
inventory54 host/51 V2. See retained logs in that evidence directory. No new
target/TSan/power-loss/performance claim; C++ ASan/UBSan excludes Raft C and leaks.
Model109/109 remains CP-020 historical, not rerun. Protected tracked paths clean;
model remains untracked/untouched.

NEXT: do not reopen §7/§8 approval. Result ownership slice is integrated, NOT full
F5 closure. Review remaining outbound and retained-log descriptor/byte/backing-RAM
budgets and full policies in F5_F7_MVP_CLOSURE_PLAN §3 / admission review §4.
Those candidate quotas are NOT approved. Need permanent saturation REDs and review
of bounded control response ownership before encoding that protocol. Replacement
voting admission/install/catch-up/archive, F6 bound-metadata persistence model port
and async fences, F7 frozen targets remain OPEN. Pi deferred, not waived. Processes
and M3a observability still precede live. No new venue/product scope.

## REMEDIATION CP-022 — RESULT COMPONENT GREEN / INTEGRATION PENDING — 2026-09-23 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation UNCOMMITTED;
preserve dirty/protected paths. Two bounded workers; main independently reviewed,
strengthened tests and rebuilt. No staging, tag, freeze, target or venue action.

Owner-approved §7 ResultLedger is implemented as a STANDALONE component:1024
slots (960 ordinary/64 protected), protected-first control allocation, generation-
checked handles, exact identity/digest/term/index binding, ready FIFO retained until
take. No allocation inside ledger operations. Native object229400 bytes, slot224,
handle16; owner-thread only, handles scoped to ONE ledger. CommittedResult moved
verbatim to a shared header. No wire/disk/economic or result-layout change.

IMPORTANT: ReplicatedKernel still uses its growable result deque. No kernel
result-bound or allocation-free apply claim. §8 follower control-only saturation
protocol is awaiting separate owner approval; expressly excluded from §7 approval.
Do not half-integrate only the leader path. Fullness must not hide newer terms,
and header processing is not a no-mutation refusal: it can change term/leadership,
truncate a conflicting uncommitted suffix and apply a verified committed prefix.

Exact next REDs: tests/v2_result_admission_red.cpp, selectors leader, follower,
recovery, oversized-recovery. All four CURRENTLY FAIL for the intended missing
admission/reservation behavior; deliberately outside CTest. Replacement selector
is baseline GREEN on old unbounded code, to preserve during ownership integration.
After §8 approval, integrate both admission paths, retained-entry handles,
pre-apply validation/publication, durable suffix transfer and startup reservations.
Never release an old slot before its durable truncation succeeds.

Basic signed peer-coordinate guards also landed: invalid terms/indices refuse
before Raft mutation; term0 headers/replies remain valid. No new cross-field or
transport-authentication guarantee. Main caught a vacuous stale-handle test and
made reuse carry the SAME binding so only generation distinguishes ownership.
Direct-copy publish/release/routing/coordinate mutants fail intended assertions.

Evidence correction: peer worker's initial manually linked RED/GREEN used stale
build archives, so those runs are NOT credited as verified-current original REDs.
Fresh main builds and a direct guard-removal mutant supply current evidence; do
not retroactively relabel the mutation as original TDD RED. Full details/logs:
`evidence/f5_result_ledger_2026-09-23/README.md`.

Fresh host Release52/52, Debug ASan+UBSan52/52, tools40/40; inventory52 host/49 V2.
C++ instrumentation, UB halt, leaks off; Raft C NOT instrumented. Model109/109 is
CP-020 historical, not rerun. No new TSan/target/power-loss/performance claim.
F5 whole-node bounds/replacement/install/archive, F6 bound-metadata persistence
model port/async fences and F7 frozen targets OPEN. Pi deferred, not waived.

## REMEDIATION CP-021 — VERIFIED APPEND PREFIX / RESULT CONTRACT REVIEW — 2026-09-23 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9; ALL remediation UNCOMMITTED.
Preserve dirty/protected paths. Two bounded review workers; main implemented and
verified this increment. No commit/tag, source freeze, target, venue or credentials.

F5 admission review found pinned raft_recv_appendentries caps commitment by the
FULL local log, not the received batch. Added original synthetic ingress REDs:
matching1..16 plus divergent17..20; a message verifying only1..16 with leader_commit20
applied kill120 instead of kill16. Empty heartbeat variant also RED. Arithmetic
RED accepted negative/overflowing prev. Permanent tests precede the fix.

IMPORTANT CORRECTION: this is NOT an established honest-three-node divergence
trace. Independent review could not produce that public scheduling witness;
current sender backtracking eagerly deletes the mismatching suffix. Do not claim
production split-brain or exhaustive unreachability. The receiver boundary still
must be explicit before future shortened/capacity control processing.

Smallest fix: V2 wrapper passes min(leader_commit, prev+count); bound-peer codec
rejects prev/count arithmetic without room for the following signed64 index.
Pinned fork/V1/bytes/economics unchanged. Tests assert kill, exact result identities,
commit/apply/reply indices, subsequent repair/restart, lower commit, failed prev
and empty-log heartbeat. Three direct-copy mutants fail at intended assertions.
Original restart fixture needed tick(0) after flush to checkpoint commit, per the
existing API; no production persistence behavior changed to make it pass.

Fresh host Release51/51 and Debug ASan+UBSan51/51; C++ instrumentation, leaks off,
UB halt, Raft C NOT instrumented. No new TSan/target/power-loss claim. Tools40 and
model109 are CP-020 historical results, not rerun. Evidence and reproduction:
`evidence/f5_verified_prefix_2026-09-23/README.md`. Protected tracked files clean;
model remains untracked/untouched. No suite/inventory or native layout changes.

OWNER APPROVED after CP-021: `F5_ADMISSION_RESULT_REVIEW.md` §7 result ledger
only:1024 slots (960 ordinary/64 protected), every-replica reserve-before-accept,
retain-until-take and explicit finite backpressure. Host layout proposal224 bytes
per slot,229400 total allocation, excludes per-log handles and other ownership.
The1024/64 result budget and three local ownership/backpressure choices ARE now
approved; do not ask again. Owner requested the C++ implementation explainer
FIRST; no ledger code has been implemented. Other budgets/protocols remain
separate. Authority markers produce no result and need no result slot.

Before integration resolve: full uncommitted suffix must be replaceable without
speculative slot release or requiring impossible spare capacity; higher terms
cannot be hidden by fullness; outbound control response ownership needs a bounded
policy; a header-only Raft call can mutate term/log and is NOT a no-mutation
capacity refusal. Existing typed peer wrapper also needs focused signed-coordinate
REDs (negative terms/commit/reply indices, nonpositive entry terms); CP-021 covers
prefix arithmetic only. Keep cases tied to precise observable behavior.

F5 whole-node bounds/replacement/install/archive, F6 bound-metadata persistence
model port/async fences and F7 final targets remain OPEN. Pi deferred, not waived;
processes and M3a observability still precede live. No new venue/product scope.

## REMEDIATION CP-020 — BOUND LOCAL HISTORY / CRASH-SAFE GENESIS — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation UNCOMMITTED;
preserve existing dirty/protected paths. At most TWO workers; main reviews and
integrates. No commit/tag, source freeze, target, venue or credential action.

Implemented the already-approved local portions of `F5_BOUND_FRAME_BYTE_REVIEW.md`:
mandatory 112-byte bound metadata, block-v2 WAL containing bound records, bound
local snapshots, metadata-FIRST/identity-LAST bootstrap and explicit retry/cut
handling. Kernel stores require verified identity. No flat-WAL fallback: even an
empty `v2-raft.wal` refuses. V1 storage remains unchanged. All selected records,
including uncommitted and snapshot-covered entries, validate before writable WAL
open. Foreign/unsupported artifacts throw typed errors, not optional fallback.

Original current-source REDs preserved: foreign metadata accepted, foreign WAL/
snapshot restored kill9 instead of kill1, missing hard state reset term1 to0,
bootstrap published identity without hard state. New review-discovered REDs:
complete corrupt block became empty history; metadata term below WAL/snapshot
term accepted; duplicate snapshot-covered record accepted. Permanent tests now
cover exact refusals, unchanged files and healthy recovery. Never count broad
startup failure as sufficient proof. Evidence: `evidence/f5_local_history_2026-09-22/README.md`.

Recovery checks retained index continuity/nondecreasing terms and snapshot overlap.
Metadata term cannot trail retained history. BUT snapshot index may exceed the
older metadata commit checkpoint: durable snapshot publication legitimately
establishes that committed prefix. Rejecting it would break the existing volatile-
follower snapshot case. Full nonzero corrupt WAL blocks now refuse unchanged;
physical short tail still yields only the complete prefix, which must cover local
commit. All-zero complete blocks end preallocation. No scanning beyond damage.

Genesis retry follows the approved table; process cuts cover metadata creation,
partial/complete writes, sync, rename, directory sync, then equivalent identity
publication boundaries. Incomplete/foreign metadata temp stops recovery unchanged.
Complete matching replacement temp is validated and retained, never promoted;
a later legitimate save may replace it with its NEW tuple. This deliberately
trades recovery availability for preserving ambiguous voting evidence.

Host Release51/51; Debug ASan+UBSan51/51 (C++ instrumentation, UB halt, leaks off;
Raft C is NOT instrumented; no full-program TSan/new target claim). Tools40/40,
model109/109. Six isolated direct-compiled mutants fail at intended assertions.
IMPORTANT F6 limitation: CP-014's syscall persistence model STILL tests the old
generic MetadataStore, not new BoundMetadataStore. Its green result is historical
algorithm evidence, NOT proof of the new routine. Port the model before claiming
current V2 sync-sensitive acceptance. Process cuts do not model power loss.

NEXT: finish F5 via `F5_F7_MVP_CLOSURE_PLAN.md`: whole-node byte/entry budgets and
result reservation; admitted replacement plus transactional install/catch-up;
verified archive before last-copy reclamation. Their detailed protocols/quotas
are NOT approved by the local frame approval. Review `F5_ADMISSION_RESULT_REVIEW.md`
before bounded-container implementation. In particular reserve results on EVERY
replica, retain committed results across compaction/election, and reserve for the
uncommitted suffix restored at startup (replayed committed results stay suppressed).
Capacity refusal must not suppress higher-term handling. Do not silently invent
credit/install/archive bytes or candidate constants. F5/F6/F7 remain OPEN;
Pi deferred until the final gate, not waived. Processes/observability precede live.

`V2_MESSAGE_MEMORY_ATLAS.md` now lives in docs, with CP-020 storage status updated.
TradingState stays81,392; native ReplicatedKernel now91,504 inline bytes, excluding
owned heaps. Do not confuse either with a whole-process memory ceiling.

## REMEDIATION CP-019 — APPROVED FRAMING / PEER ADMISSION — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation UNCOMMITTED;
preserve dirty/protected paths and existing caches. No target, venue, credentials,
source freeze, commit/tag action. At most TWO bounded workers; main owns review,
integration/builds. User approved `F5_BOUND_FRAME_BYTE_REVIEW.md` in full after
CP-018: bytes, limits, block-v2/no flat fallback, metadata-FIRST/identity-LAST
bootstrap publication and retry table. Do not ask again for these choices.

Implemented THIS checkpoint: common V2BF framing plus typed ordinary-peer codec
and kernel admission. Public raw PeerMessage receive is REMOVED; callers supply
bound bytes and inspect BoundFrameError. All four requests/replies validate before
Raft mutation. Full inner shape is checked before vector allocation; route/member/
candidate and canonical command/marker-domain validation precede mutable callbacks.
The sender caps AppendEntries at sixteen entries. Forty queued entries converge
on all three voters; this is per-message boundedness, NOT whole-node queue limits.

Generic framing validates scope/layout/integrity, NOT metadata tuples, record
coordinates or snapshot state. Local metadata/WAL/snapshot runtime codecs and
genesis/recovery ordering are NOT integrated yet. Existing live stores still use
old formats. Four standalone intended REDs remain: foreign metadata accepted;
foreign WAL/snapshot restore kill9 instead of kill1; missing hard state restores
participating term1 as term0. Their fresh outputs are in the CP-019 evidence.
Foreign actual-kernel vote diagnostic is now GREEN (term0 stays0/exact refusal).

TDD/evidence: original generic-frame and typed-peer stub REDs; independent golden
recomputed using struct/hashlib; in-bounds aliasing fix reviewed by main; all-kind
admission asserts unchanged files/state/protocol/outbound; malformed third entry
allocation probe; isolated manifest/batch/preflight mutants fail for observable
effects. No shared production source mutation or copied CMake caches. One catalog
latch fixture was corrected to exercise the actual fault guard instead of failing
in sender-identity setup. Real Cluster routes use actual source identities.

Final host Release49/49, Debug ASan+UBSan49/49 (existing C++ scope, UB runtime halt,
leaks off; no new Raft-C/TSan claim), tools40/40, passive model109/109. Inventory49
(46 V2) matches independent gate list. `git diff --check` clean. Main integration
builds and mutants use cacheline64; initial codec-only peer RED used128 and never
constructed a kernel. Evidence: `evidence/f5_bound_peer_2026-09-22/README.md`.

NEXT ALREADY-APPROVED work: retain metadata/missing-hard-state REDs, implement
typed local codecs and metadata-first bootstrap/recover publication with exact
retry/cut tests, then bind WAL block-v2 and snapshot recovery. Foreign/unsupported
history must never become corrupt-tail/optional-snapshot fallback. No raw store
bypass, no automatic adoption/reset of voting memory. Preserve current fixture
guard intent when migrating legacy raw-history corruption setups.

After that: `F5_F7_MVP_CLOSURE_PLAN.md`. Whole-node queues/result reservations,
replacement fencing/install, verified archive and async persistence fences remain
separate unapproved detailed contracts. Do not silently waive them for MVP or
implement candidate quota numbers as approved constants. F5/F6/F7 remain OPEN;
Pi deferred, not waived. Processes and M3a observability still precede live.

## REMEDIATION CP-018 — PARALLEL F5 REDS / F6 WORKER FAILURE — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation UNCOMMITTED;
preserve dirty/protected paths and pre-existing caches. No venue, credentials,
target, source freeze, commit/tag action. Owner requested parallel completion of
all remaining F items with MVP focus. At most TWO workers; main reviews/integrates.
Do not interpret that request as approval of unspecified bytes or reduced safety.

Completed existing-contract F6 increment: WalWriter catches worker exceptions,
publishes a permanent release/acquire error latch, joins even if the worker stopped
itself, and refuses restart. Kernel owner service boundaries/is_leader check it.
Main found a draft bypass: direct submit/poll could resume a failed writer and
publish durability. Permanent `writer-retry` RED preceded submit refusal/poll
rethrow fix. No new persisted/economic/peer bytes or async Raft callback semantics.

Evidence honesty: worker did NOT capture original thread-crash RED before editing.
Only main's polling-bypass case is original RED/GREEN; final isolated latch/owner-
guard mutants are retrospective sensitivity checks. Worker also copied a CMake
cache that redirected to the existing sanitized root; main discarded that result,
checked flags and rebuilt real roots. Do not copy caches to simulate isolation.

New standalone `tests/v2_history_identity_red.cpp`, outside passing CTest, records
FIVE intended current-source REDs: foreign metadata accepted; foreign WAL/snapshot
change recovered kill1->9; real foreign vote request changes term0->1; missing
metadata restores participating voter's term1->0. Keep these until precise typed
refusal/healthy-positive tests replace them. Original catch-up/archive REDs remain.

Final rebuilt host Release47/47, Debug ASan+UBSan47/47 (existing C++ instrumentation,
UB runtime halt, leaks off; no new Raft-C claim), tools40/40, passive model109/109.
Focused TSan threaded-flush/writer-retry pass: test, WAL, replication and storage
translation units instrumented; other archives not. Not full-program TSan.
Evidence: `evidence/f5_f6_parallel_2026-09-22/README.md`. No target acceptance.

OWNER APPROVED AFTER CP-018: `F5_BOUND_FRAME_BYTE_REVIEW.md` exact header/kind payloads,
integrity, block v2, local/peer bounds and metadata-FIRST/identity-LAST bootstrap
publication/retry table. Encoding/integration is now in progress, not accepted. All six identity SEMANTICS
remain approved; do not ask again. Original metadata-first omission was caught
in independent review; document now specifies durable initialization order rather
than treating an identity-only directory as permission to invent voting memory.

Execution/deferral plan: `F5_F7_MVP_CLOSURE_PLAN.md`. Whole-node budgets are candidate
numbers, not approved constants. Define result reservation before making queues
bounded; do not throw/drop after committed apply. Do not assume distributed credit
messages are necessary without checking local admission/Raft retry alternatives.
Archive/install bytes, replacement voting admission, quotas and concrete async
fences still require review. F5/F6/F7 remain OPEN; Pi deferred to final stage, not
waived. Processes and M3a detailed observability still precede live admission.

## REVIEW CP-017 — HISTORY/PEER IDENTITY BINDING — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9; preserve the full uncommitted
remediation tree, protected paths/model and pre-existing caches. REVIEW ONLY:
no production/test changes, build, target action, commit/tag or new acceptance.
Latest executed host baseline remains CP-016: Release/sanitized47/47, tools40/40,
model109/109. It was not rerun for this documentation-only review.

Owner APPROVED all six identity-binding recommendations after CP-017. Read
`F5_HISTORY_PEER_IDENTITY_REVIEW.md` in full: semantics accepted, no new bytes
frozen or implemented. Do not ask again for the six choices. Main and one
independent agent inspected current storage, peer admission and replacement seams.

Recommendations: use existing manifest_digest; bind local hard state/WAL records/
snapshot wrappers to manifest+local voter; portable snapshot/archive content to
shared manifest; validate all peer requests AND replies before Raft mutation;
foreign/unsupported history is not a corrupt tail or snapshot fallback. Recommend
record-level WAL wrappers and one active block-WAL format in fresh bound lineages,
refusing legacy flat WAL rather than preferring one source silently. Removing
that compatibility in fresh bound deployments is included in the owner approval;
V1/history stay intact. Exact bytes and concrete recovery protocols remain open.

Important admission distinction: static identity does not restore forgotten
term/vote, prevent same-lineage rollback, authenticate peers or prove durability.
Genesis bootstrap must not reset a previously participating voter. Replacement
needs reviewed fencing/admission before normal Raft participation. Existing
catch-up RED remains evidence, but the final fix must not copy its empty normal-
voter bootstrap shortcut. Missing hard state with history needs a permanent RED.

NEXT: exact storage/peer byte sheets and startup
publication states before encoding. Preserve foreign-artifact/high-term-peer REDs
before fixes. Archive/install generation formats, quantified budgets, async fences
and target gates still require separate review. F5/F6 and live readiness OPEN.
Corrected stale CP-015 status text in the two review docs; audit trail retained.

F5 closure checklist: (1) implement approved history/peer binding after byte review;
(2) bound whole-node queues, retained log bytes and owner work with control reserves;
(3) safe replacement admission plus bounded crash-safe peer snapshot install/suffix
catch-up; (4) verified audit archive before last-copy reclamation with bounded
outage policy; (5) integrated RED/GREEN, mutants, saturation/recovery/crash evidence
and checkpointed acceptance. Final frozen target validation is F7 (Pi deferred,
not waived). F6 async persistence fences are a separate dependency, not closed by
F5. Processes/observability/live admission remain later roadmap milestones.

## REMEDIATION CP-016 — KERNEL REQUIRES VERIFIED STARTUP — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation is UNCOMMITTED;
preserve the full dirty tree, protected model/docs and pre-existing caches. No
source freeze, commit/tag, target, credential or venue action.

Completed the already-approved integration: ReplicatedKernel's raw/default
constructor is REMOVED. Its only constructor consumes VerifiedIdentityStartup,
revalidates moved ownership/name/identity BEFORE storage constructors, and retains
the token as its FIRST member (destroyed LAST). ID, voters, catalog and authority
domain derive solely from the token. Catalog is referenced from the owned token,
not copied twice or borrowed from the caller. Token is not serving authority.

A04 startup membership drift is now host GREEN: compacted three-voter history
refuses a one-voter recovery with exact identity_mismatch, unchanged directory
names/bytes, then healthy original-state recovery. This is in regular replication
CTest as `identity-membership`, plus the standalone diagnostic. New kernel-startup
suite pins moved token, changed path/file, writer/lock lifetime, three-voter quorum
and corrupt-metadata unwind. CMake and independent gate inventory include it.

Fixtures now explicitly bootstrap or recover; damaged history is planted AFTER
identity provisioning. Missing catalog legitimately refuses earlier; genuine
before-apply dependency cases use valid alternate-catalog startup and foreign-root
installs. Main caught/copied-history bootstrap misuse; corrected tests still reach
their old corruption refusal. Guard mutations are isolated source copies ONLY:
ownership bypass creates files in replacement directory; self-only membership
elects without quorum; removing before-apply catalog check advances apply/state.

Evidence: `evidence/f5_kernel_startup_2026-09-22/README.md`, CP-016. Final rebuilt
host Release47/47, Debug ASan+UBSan47/47 (UB halt/leaks off, existing C++ flags,
no new Raft-C instrumentation), tools40/40, passive model109/109; inventory47
total/44 V2. Original API RED, retained pre-fix A04 rerun, new GREEN and exact
mutants retained with provenance. Later tests are not mislabeled original RED.

NEXT: continue remaining F5/F6 under ADR0014. Identity-bound history/peer/install/
archive frames and numerical budgets still need material review BEFORE encoding;
the approved startup bytes/token do not require reapproval. Fresh current-source
replacement-catchup and archival diagnostics still RED and retained. Design the
shared-lineage envelopes/recovery boundary before layering replacement snapshot
installation and archive-gated reclamation on it; preserve these REDs. F6 still
requires bounded async persistence ownership, error reporting and Raft fences;
do not turn sync callbacks into early-success callbacks.

Explicit limit: stores still open by pathname. Revalidation catches ALREADY
replaced names but cannot prevent later concurrent path substitution. Cooperative
ownership and stable trusted paths are assumptions, not hostile same-UID fencing.
Sidecar does not bind substituted foreign history or fence old executables.
Startup-specific power-loss modeling and all final target evidence remain open.
F5/F6, startable processes, M3a observability, frozen NUC/Pi gates and venue/live
admission remain OPEN. Pi deferred, not waived.

## REMEDIATION CP-015 — APPROVED IDENTITY CODEC + STARTUP OWNER — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9. ALL remediation is UNCOMMITTED;
preserve the full dirty tree, protected model/docs and pre-existing caches. No
source freeze, commit/tag, target, credential or venue action.

OWNER APPROVED the exact `F5_STATIC_IDENTITY_BYTE_REVIEW.md` frame, hash domains,
modes/epochs and max3 voter limit after CP-014. Do not ask again. Other storage/
peer/archive/install bytes, numerical runtime budgets and concrete async Raft
fences remain separate review boundaries under ADR0014.

Implemented prerequisites:

- StaticIdentity codec: exact116/120/124 bytes, bounded arrays/scratch, independent
  literal/hash vectors, strict schema/mode/voter/digest validation, unchanged
  outputs on failure. Verified identity derives shared manifest/authority digests;
  local node ID is excluded from those but included in whole-file integrity.
- VerifiedIdentityStartup: move-only retained directory-flock owner; explicit
  bootstrap/recover; mandatory exact catalog before filesystem side effects;
  no lockfile, bounded125-byte input, descriptor-relative startup I/O, no legacy
  adoption or ambiguous-temp deletion. Valid complete temp can retry; partial
  temp is preserved/refused. Effective-UID ownership/no other write permission.
- Main caught visible-but-not-durable identity after interrupted rename. Matching
  recovery now re-establishes file/directory/parent durability before returning
  ownership; mismatches still refuse before those success-path fences. Retained
  control-flow RED is not presented as a physical power-loss proof.

IMPORTANT: ReplicatedKernel still has its OLD RAW CONSTRUCTOR and does NOT consume
this token. A04 kernel membership diagnostic was rebuilt and remains RED (old
three-voter history reopens as a one-voter leader). This checkpoint is standalone
codec/startup GREEN, NOT a fixed kernel restart, full F5, or live readiness.

Evidence: `evidence/f5_identity_2026-09-22/README.md`, CP-015. Final rebuilt host
Release46/46, Debug ASan+UBSan46/46 (runtime UB halt, leaks off, existing C++ flags;
no new Raft-C instrumentation), tools40/40, passive model109/109; inventory46
total/43 V2. Original codec/startup/permission/recovery-fence REDs and isolated
mutants retained. No shared production source was mutated for testing.

EXACT NEXT: integrate token as FIRST ReplicatedKernel member and remove the public
raw/default constructor. Derive node ID, canonical membership, catalog and F4
authority domain solely from the token. Reject moved-from ownership before stores
construct; retain lock until all stores/writer die. No additional approval needed
for this already-approved integration. Keep catalog ownership stable without an
unnecessary second catalog copy. Account for descriptor-versus-path storage I/O:
an advisory inode lock does not defend later pathname replacement by an operator.

Convert `tests/v2_f5_recovery_red.cpp` membership case into exact typed refusal
plus unchanged-tree and healthy restart tests. Migrate fixtures EXPLICITLY: create
identity before injecting damaged/unsupported history, or tests will pass for the
wrong missing-identity reason. Missing catalog now fails earlier; retain genuine
before-apply coverage with mismatched committed installs. Marker helpers must use
derived domains, not an arbitrary injected digest. Preserve one/two/three voter
lab coverage, never auto-shrink production quorum, never backfill legacy identity.

Limits: trusted cooperative local ownership/parent chain, not hostile same-UID
mutation or NFS lock proof. Process cuts do not discard OS cache; startup-specific
persistence model and target evidence remain open. Sidecar does not bind foreign
WAL/snapshot/history or fence old executables. F5/F6, processes, M3a observability,
frozen NUC/Pi gates and venue/live admission remain open. Pi deferred, not waived.

## REMEDIATION CP-014 — DIRECTION APPROVED, PERSISTENCE MODEL — 2026-09-22 (historical)

Branch/HEAD still v2/phase-5-corrections / d44a4a9. All remediation remains
UNCOMMITTED. Preserve the whole dirty tree, protected model/docs and pre-existing
cache changes. No source freeze, commit/tag, target, credential or venue action.

OWNER APPROVED ADR0014 / F5_F6_RECOVERY_DURABILITY_REVIEW direction after CP-013,
including provisional atomic metadata replacement. Do NOT ask again for that
direction. Exact new bytes, numerical budgets, archival retention and concrete
asynchronous Raft fencing were not specified by that approval and remain review
boundaries. No production bytes or behavior changed in CP-014.

Completed test-only F6 prerequisite: v2-metadata-persistence-model-tests executes
actual MetadataStore::save with isolated syscall shims, separate volatile/durable
data and directory names, frozen power-cut state, boundary/short-write/failure
sweeps and independent old/new tuple oracle. Materialized recovered bytes are
also read by real MetadataStore::load. Removing ONLY file sync loses new contents;
removing ONLY directory sync recovers the old tuple after acknowledged success.
Main independently compiled both isolated production-call mutants; intended REDs
and exact patches retained. This is new coverage of existing GREEN behavior, not
a production RED/GREEN fix. Never mutate shared production source for mutants.

Evidence: `evidence/f6_persistence_model_2026-09-22/README.md`, CP-014. Rebuilt host
Release44/44, Debug ASan+UBSan44/44 (runtime UB halt/leaks off, existing C++ flags;
Raft C not newly instrumented), tools40/40, passive model109/109. Independent gate
inventory44 total/41 V2; diff check clean. No device/power-loss acceptance claim.
Model excludes spontaneous writeback, partial persistence on failed sync, exhaustive
sector tears and first-directory creation. F6/G-047 stay OPEN; stronger evidence,
async fences/worker error reporting and target storage gates remain required.

NEXT OWNER BYTE REVIEW: `F5_STATIC_IDENTITY_BYTE_REVIEW.md` (PROPOSED, no codec).
One atomic local V2ID file, logically shared manifest + local voter binding;
116/120/124 bytes for one/two/three voters. Proposal includes max3 voters, explicit
lab/pilot mode, intended catalog root, semantic schemas/epochs and exact digest
domains. Main and independent reviewer checked offsets and identity separation.
Exact byte approval is required before encoding; bootstrap/fresh-lineage semantics
are already approved. The two-voter production topology is unchanged.

After byte approval: literal-vector/refusal codec REDs, then startup drift refusal
BEFORE writable constructors, complete atomic bootstrap/recover and exclusive
directory ownership. Recovery must not create a lockfile before mismatch refusal;
concrete ownership mechanism remains an implementation review item. Convert CP-013
membership diagnostic into exact refusal/unchanged-tree plus healthy restart tests.
Do not call the sidecar full history binding: foreign same-schema WAL substitution
still needs shared-domain storage/peer/snapshot/archive envelopes. Old executables
can ignore a sidecar, so anti-rollback is also not solved by the file alone.

Full F5/F6 remain OPEN. ADR0014 direction does not approve unspecified peer/install/
archive bytes, capacity/full policies or async callbacks returning success early.
F4 host composition remains CP-012; startable processes, M3a observability, frozen
NUC/Pi gates and venue/live admission are still required. Pi deferred, not waived.

## REMEDIATION CP-013 — PARALLEL F5/F6 FIRST FIXES — 2026-09-22 (historical)

Branch/HEAD remain v2/phase-5-corrections / d44a4a9; all remediation remains
UNCOMMITTED. Preserve the full dirty tree, protected files/model and pre-existing
cache changes. No external contact, source freeze, credentials, commit or tag.

Two bounded agents reviewed F5 and implemented F6; main reviewed both and added
the snapshot-memory fix plus a missed self-vote failure guard. Completed under
EXISTING contracts, without new persisted/economic bytes:

- SnapshotStore reads at most65,522 bytes (existing canonical max + frame + one
  lookahead), rather than allocating two file-sized buffers before validation.
  Unsupported future framing/schema still refuses; supported corruption fallback
  is retained. New v2-snapshot-store-bounds-tests and independent gate entry.
- WAL flush and term/vote/commit metadata exceptions latch the node unusable.
  Metadata cache updates only after save succeeds. Default-inert checkpoint
  injection reaches before-write and after-rename cases through public APIs.
- Pinned Raft ignores its self-vote callback return during election. Main RED
  exposed a successful tick despite failed persistence; tick now rechecks the
  latch immediately after raft_periodic. No pinned-fork or framing change.

Evidence: `evidence/f5_f6_2026-09-22/README.md`, CP-013. Final rebuilt Release43/43,
Debug ASan+UBSan43/43 (UB halt/leaks off; existing C++ flags), tools40/40,
model109/109; inventory43 total/40 V2. Snapshot/owner-tick original REDs retained;
agent original REDs were console-only, so main captured reruns of retained old
binaries and independently compiled fresh isolated latch mutants. Vote-specific
and post-rename cases are not claimed as original RED-first. No shared-source
mutation or target acceptance in this increment.

F5 remains OPEN. Standalone `tests/v2_f5_recovery_red.cpp` is intentionally outside
CTest and reproduces membership drift, missing peer snapshot catch-up and exact
command-history loss across BOTH WAL formats after compaction. Do not count these
diagnostics as passing acceptance. The snapshot bound fix is not whole-node bounds.
F6 remains OPEN: synchronous owner hard-state writes, sync-sensitive power-loss
proof, background-writer exception reporting/fencing, broader log_offer/compact
failure paths and target evidence. A background writer exception currently escapes
its thread (source-traced), not the synchronous flush path just fixed.

APPROVAL RECEIVED after CP-013: the owner approved
`F5_F6_RECOVERY_DURABILITY_REVIEW.md` / ADR0014 direction, including provisional
retention of atomic replacement versus the plan's two slots. Do not ask for this
direction approval again. Read `F5_STATIC_IDENTITY_BYTE_REVIEW.md` next: the
precise startup-binding layout remains proposed, not implemented.
Exact manifest/snapshot/archive bytes, numerical budgets/full policies, retention
policy and concrete async Raft fencing still require review before encoding.
The next implementation after approval starts with static identity preflight;
in parallel, build persistence-model evidence without returning success early
from Raft persistence callbacks. Preserve one-voter lab semantics; no automatic
production quorum shrink. F4 host composition remains CP-012 GREEN, not deployment
acceptance. M3a observability and final NUC/Pi/venue gates remain required.

## REMEDIATION CP-012 — F4 HOST COMPOSITION COMPLETE — 2026-09-22 (historical)

Branch/HEAD remain `v2/phase-5-corrections` / `d44a4a9`. All remediation remains
UNCOMMITTED; preserve the whole existing dirty tree and protected paths. No
target, venue, credentials, commit, tag or source-freeze action occurred.

F4's approved host-library path now composes: current-term durable marker apply
creates one owned `AuthorityProof`; dequeue rechecks current term/controls/exact
binding; `GatewayAuthority` accepts only its pending incarnation/sequence and
expires from original request start; `dispatch_gateway_send` rechecks eligibility,
known fences, exact prepared binding and final clock, consumes permission and
records uncertainty/quota before invoking the synchronous transport callback.
Replay/restart cannot mint a live proof. Occupied/lost results never block kill.
Committed cancels retain emergency quota and stop-new-risk exception; stop-all
blocks them. Unknown submits cannot retry merely because a new proof exists.

Removed the dangerous renewal meaning: `renew_authority` now always refuses;
`evaluate_pre_send` was renamed `evaluate_delivery_candidate` (no unsafe alias).
It is candidate filtering, NOT send permission. Local `authoritative` role flags
are diagnostic only. No new canonical bytes or economic state/schema change.

Evidence: `evidence/f4_proof_2026-09-22/README.md`, CP-012. Final host Release
42/42 and Debug ASan+UBSan42/42, tools40/40, passive model109/109; independent
inventory42 total/39 V2. Sanitizers use runtime UB halt and leaks off; the C
dependency is not newly instrumented. This is NOT a target acceptance gate.
The original producer/consumer implementation lacked a preserved RED-first run;
retrospective mutations are explicitly labeled. Boolean-renewal and composed
transport stub REDs were retained. An agent's shared-source mutation contaminated
an intermediate build; main restored/verified, reran mutations on isolated source
copies, and rebuilt the final tree. Do not reuse intermediate build claims.

Important limit: an already issued, unexpired proof can precede an unseen remote
kill. Tests explicitly allow that bounded-observation window and retain in-flight
ambiguity; quorum proof is not a future lock or external fence. Clock callback
and admission inputs have one non-reentrant owner. No arbitrary-pause wall-clock
venue cutoff is claimed. Production suspend-aware clock, fresh CSPRNG incarnation,
authenticated IPC, static-domain provenance, operator activation/reconciliation,
real sender, measured nonzero bound and target gates remain integration work.
`GatewayAuthority` defaults to zero bound and closed latches. G-052..G-060 remain
open for deployment; CP-012 closes the A01 host-library composition, not the pilot.

NEXT: F5 read-only contract review before bytes/implementation: persisted static
cluster/membership identity and authority-domain derivation; bounded message,
result and retained-log budgets/backpressure; peer snapshot catch-up; archival
evidence before last-copy reclamation. Start from audit A04/A07/A08/A14, ADR 0001,
architecture/persistence contracts and the approved NUC/Pi roadmap. Present new
identity/snapshot/archive byte or economic choices for owner review, then exact
permanent REDs. Do not quietly add dynamic membership or weaken two-voter quorum.

Roadmap addition requested by owner: M3a is now an explicit pre-live observability
gate with execution-stage Grafana diagnostics, all subsystem/machine health,
stale/unknown telemetry, tested alerts and runbooks. Define bounded reason metrics
during fixes; deploy the read-only pipeline with processes, fault-drill it in
shadow, never put scrape/formatting work or dashboards on the authority path.
After F5/F6: startable node + separate gateway IPC, NUC/Pi shadow + M3a, F7 frozen
target/operational gates, verified venue/economics and owner-approved live canary.
Pi stays deferred to final gates, not waived before live deployment.

## REMEDIATION CP-011 — F4 MARKER PREREQUISITE — 2026-09-22 (historical)

Owner APPROVED type 101 / 186-byte schema-1 V2AB, domain equality and conservative
drain admission, explicitly requesting careful availability proof. Do not ask for
these approvals again. Branch/HEAD remain v2/phase-5-corrections / d44a4a9; all
remediation is UNCOMMITTED and protected paths remain untouched.

Implemented: standalone canonical marker codec; typed WAL/peer/pre-apply dispatch;
replication-only apply; explicit configured domain; one pending local admission;
on-demand highest-unapplied-marker drain from retained typed log history. No
economic StateVersion, receipt, schema, TradingState or snapshot expansion.
Historical markers do not require their effects to remain retained.

Availability tests prove a higher-term leader with an old marker stays blocked
without an automatic no-op, repeated fresh requests cannot append around it,
and a real current-term command through EITHER normal or control lane commits
and drains it. Lagging followers may retain A/B/C; removing C leaves B blocking.
One-voter lab behavior differs: its pinned durability path can commit retained
history directly. Do not generalize the multi-voter stall to all configurations.

Review found and fixed two nearby bugs through REDs: persisted commit exceeded
disk coverage when follower durability was zero; and failed suffix truncation
released owned payload before Raft removed its entry. Clamp includes zero now;
payload release follows storage success, and truncate/clear failure latches the
node unusable. No blanket storage-failure completeness claim: log_offer and
broader F6 error handling still need explicit coverage.

Evidence: `evidence/f4_marker_2026-09-22/README.md` and checkpoint CP-011. Two new
CTest suites; final rebuilt Release40/40, Debug ASan+UBSan40/40 (runtime UB halt,
leaks disabled), offline tools40/40 and model109/109; inventory40 total/37 V2.
Marker body144,
frame186, fixed pending storage <=192 bytes outside TradingState. Drain lookup is
O(retained unapplied suffix), constant auxiliary space; retained-history bound
remains F5, not solved by one pending slot.

EXACT NEXT RED: prove a healthy committed/applied marker yields exactly one
owned, non-forgeable live result bound to the actual retained BoundOrderEffect
and expected controls; before durable quorum, stale term/controls, replay/restart,
missing dependency or abandoned request yields none. Install the live pending
slot before apply can run; a full result slot must never block kill/apply.
Then wire gateway request-start age/correlation/one-shot final admission and
convert CP-009 partition/delayed-reply REDs into composed regressions. The old
boolean renewal path STILL EXISTS; F4/A01 is NOT closed. Marker admission is
explicitly NOT send permission, nor global per-correlation dedup after resolution.
No authenticated IPC, real sender, production duration, target gate or source
freeze. Pi remains deferred; no target/venue/commit/tag action this increment.

## REMEDIATION CP-010 — F4 SEMANTICS APPROVED, BYTE REVIEW — 2026-09-22 (historical)

The owner approved ADR 0013's direction, explicitly one freshly committed
marker per new-risk send attempt. Do NOT ask for that semantic approval again.
Read `F4_AUTHORITY_BARRIER_BYTE_REVIEW.md` next: exact bytes remain PROPOSED,
not encoded. Recommendation: V2-local Raft entry type 101, schema-1 V2AB frame
of 186 bytes, exact intent/control/catalog/domain binding. No business schema,
TradingState or snapshot expansion; external IPC is not specified by this frame.

Review also fixes the boundedness argument: one locally outstanding request is
not one marker in retained history. A healthy lagging follower can retain many.
Use a highest-retained-marker drain watermark; timeout/restart/term change must
not allow another local append until the obligation applies or is overwritten.
The library has no automatic leader no-op: old-term markers may need an ordinary
current-term command to drain. Do not silently add a bridge entry or reject
healthy follower history as corruption. Total retained-history bounds remain F5.

Exact next step: owner byte/domain/drain-rule review, then the documented codec
and composed RED-to-GREEN sequence. CP-009's two authority failures remain open.
No F4 production code/bytes changed this increment; no new test-pass claim.
CP-008 remains the last full host checkpoint. Preserve all uncommitted work,
protected paths and existing cache changes; no targets/venues/commits/tags touched.

## REMEDIATION CP-009 — F4 REVIEW — 2026-09-22 (historical; approval above supersedes)

F4 has started at the required contract-review boundary. Read
`F4_AUTHORITY_PROTOCOL_REVIEW.md` and proposed ADR 0013. Main reproduced A01 on
the CP-008 catalog-bound tree, plus a delayed-reply freshness failure, with
permanent audit sources in `evidence/f4_authority_2026-09-22/`. Both intentionally
exit 1 at their named authority assertions; they are not passing CTest entries.

Recommendation awaiting owner approval: one fresh quorum-committed,
replication-only non-economic barrier per new-risk attempt; one-shot permission
expires from gateway request start, not reply receipt. Keep committed-cancel
exception, distinguish local stop-new-risk/stop-all, no automatic promotion,
and state timing/in-flight limitations explicitly. Zero default live bound;
no arbitrary production millisecond value selected. This reuses durable Raft
commit rather than inventing ReadIndex in a library without correlated reads.
Cost: another durable round trip/WAL entry; F5/F6 remain deployment dependencies.

No F4 production code/bytes changed. Exact barrier type/body and IPC encoding
need review AFTER the semantic choice and BEFORE serialization. Do not infer
approval from this recommendation. Next work is owner review, then permanent
composed RED-to-GREEN tests in the sequence specified in the review document.
CP-008 below remains the implemented host checkpoint; all dirty/protected-path,
no-live-sender and target-gate limitations remain unchanged. Existing generated
Python cache changes observed at this takeover were not cleaned or overwritten.

## REMEDIATION CP-008 — 2026-09-22 (implemented host checkpoint)

Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; all remediation is UNCOMMITTED.
Preserve the existing audit/tooling/code/docs work and protected paths. Do not
stage directories, touch protected model/, contact venues/targets, or move tags.

The owner approved proceeding with the catalog integration. Implemented together:
InstallExecutionCatalog kind 20, appended refusal codes 62..66, immutable 32-byte
TradingState root, mandatory catalog context, and command/result/snapshot schema
8. Do not replay schema-7 history under these semantics: preserve old directories
and use the already-approved fresh pre-live lineage. OrderEffect bytes stay unchanged.

Staging checks exact catalog profile/capabilities; authorization rechecks the
historical row. Historical materialization returns copied root/spec/capability
versions. Gateway pre-send/planning require explicit matching verified context,
historical capability version and the operation bit, including cancel. Both
unbound body encoders remain quarantined: no real venue mapping or live sender.

Missing required artifact is a local node failure, never an ApplyCode. The
pinned Raft optional before_apply hook executes BEFORE last_applied_idx changes;
the original applylog callback was too late. Missing bytes latch serving closed.
Recovery validates canonical snapshot structure AND retained metadata against
catalog rows before publishing state. Root equality alone was insufficient;
a permanent RED reproduced a correctly hashed contradictory instrument profile.
Replication owns its catalog by const value; no caller-buffer lifetime dependency.

Working evidence and current verification: `evidence/f3_binding_2026-09-22/README.md`
and `evidence/V2_REMEDIATION_CHECKPOINTS.md` CP-008. Final rebuilt host Release
38/38, ASan+UBSan 38/38 (runtime UB halt, leaks disabled), offline tools 40/40,
passive model 109/109; independent inventory 38 total / 35 V2; diff check clean.
The host budget is 81,392-byte
TradingState (528 spare), 65,483-byte maximum snapshot (53 spare), plus a separate
8,488-byte verified context. No budget increase was approved or taken.

Remaining: full F3 needs reviewed real encoder/venue admission, F4 needs the
authority/fencing protocol review, F5 needs identity/transfer/retention contracts,
and F6/F7 remain partial. No live sender is authorized by local leader status or
a catalog hash. Re-freeze and target gates are outstanding; Pi remains deferred
to the final target stage. No target, venue, commit or tag operation this increment.
Next design step after this host checkpoint: F4 authority contract review, keeping
the F3 encoder quarantine explicit rather than fabricating venue evidence.

## REMEDIATION F3/F6/F7 — 2026-09-21 (superseded by CP-008 above)

Current checkpoint: `docs/evidence/V2_REMEDIATION_CHECKPOINTS.md`. Read it first,
then the approved `docs/V2_NUC_PI_TRADING_ROADMAP.md` and full audit below.
CP-007 adds the approved standalone catalog-v1 codec and owned verified context,
with independent golden-byte review and main-agent boundary/mutation checks.
Full F3 remains OPEN: root installation, mandatory reducer binding, snapshot
root and artifact-readiness integration are not implemented. CP-006's F6/F7
prerequisites also remain partial. `docs/F3_CATALOG_V1_BYTE_REVIEW.md` is APPROVED:
16 metadata/version rows, 8 KiB artifact ceiling, exact identity/precision binding,
account denomination consistency and five appended refusal codes. No catalog
bytes, refusal ordinals or schema-8 semantics were encoded in CP-006. CP-007
implements catalog schema 1 only; kernel/result/snapshot remain schema 7 and
ApplyCode ordinals are unchanged. Do not ask for catalog-v1 approval again.
F4's authority protocol and F5's identity/transfer/retention contracts still need
design review before encoding. No live sender is authorized. Pi remains deferred
to the final target-gate stage by owner instruction; no target was contacted.
CP-007 verification: rebuilt Release 37/37; ASan+UBSan 37/37 with runtime halt on
UB and leaks disabled; offline tooling 40/40 (16 gate-helper cases); passive model
109/109. Main validated actual Release CTest inventory against the independent
gate list (37 total / 34 V2), and directly compiled/passed the catalog suite with
cache-line setting 128. Context is 8,488 bytes outside TradingState; maximum
artifact is 8,076 bytes. Host settings are not target hardware measurements.
Six targeted mutants were killed; one initially surviving rounding mutant led
to a permanent exact-remainder test. Evidence: `evidence/f3_catalog_2026-09-21/`.
Container image and all target gates remain unrun for this candidate.
Base remains branch v2/phase-5-corrections, HEAD d44a4a9. Work is UNCOMMITTED;
preserve all earlier audit/tooling/docs changes and protected model/ paths.
F1 implements A02 (planner retains emergency cancel quota) and A06 (authorization
refuses uninitialized control identities). Permanent regression tests preserve
the RED cases. No wire/layout changes, live venue calls or deployment occurred.
F2 is host-complete: A05 rejects malformed proposals before log admission; A10
flushes distinct blocks once and retains retry ownership after write/sync failure;
A11 atomically writes private token files and keeps bearer credentials off argv.
CP-002 records permanent RED tests, review corrections and mutation kills.
Rebuilt Release 36/36, ASan+UBSan 36/36 (leaks disabled, halt on UB), offline tools
24/24, passive model 109/109; diff check clean. No target run or acceptance re-pin.
F3/A03 immediate refusal increment is host GREEN (Release 36/36, ASan+UBSan
36/36); CP-003 records evidence and limits. F3 itself remains OPEN.
Owner approved the single immutable committed catalog per deployment lineage.
Owner also approved the installation/recovery contract and fresh-lineage
command/result/snapshot schema 7 -> 8 boundary. ADR 0012 records the decisions;
`docs/F3_EXECUTION_METADATA_REVIEW.md` retains their detailed reasoning.
Neither unbound body API can safely emit: market changes limit economics, and
the limit API has only a caller-set verification flag with no committed catalog
binding. Quarantine these APIs, preserve arithmetic/reducer/lifecycle utilities.
Do not call this completed metadata binding. Catalog row bytes are now approved;
transport envelope and TradingState/snapshot budget increases remain unapproved.
A01 remains open: no live sender is authorized.
CP-005: strict read-only recovery compatibility preflight is host GREEN. Unknown
snapshot versions cannot fall through to replay; complete retained commands in
both WAL layouts (including uncommitted suffixes) are checked before writable
startup. Exact filename/byte preservation is tested. Rebuilt Release 36/36 and
ASan+UBSan 36/36 (runtime halt-on-UB, leaks disabled); raw logs are under
`docs/evidence/f3_recovery_2026-09-21/`. No target run or acceptance re-pin.
Main independently killed five directly compiled guard mutants at their intended
assertions; scoped subagent code review found no blocker. Keep schema
constants at 7 until catalog-root semantics can switch coherently with version 8;
do not create partially upgraded history. Missing local artifact bytes must not
create replica-dependent economic refusals; old WAL must not be silently replayed
into different outcomes. Next RED: valid catalog installation must persist its
root exactly once and survive canonical snapshot round-trip; zero, conflicting,
stale-control or economically nonempty installation must not change the root.
Then mandatory exact-row authorization and missing-artifact readiness before
EVERY Raft apply/index-advancement path. Implement these as one coherent
schema-8 boundary; do not ship an intermediate version-8 history. The overall
install/fresh-lineage decision is already approved; do not ask for it again.
The checkpoint log records verification and the next RED; target acceptance
remains OPEN. Approval beyond the contracts explicitly recorded above is still
required for new material byte/economic choices. Keep the ADRs and checkpoint current.

## Historical full-audit checkpoint — 2026-09-21 (superseded status)

This records the audit before F1 implementation; see the current checkpoint above.
Verified base: branch v2/phase-5-corrections, HEAD d44a4a964fc8c13279878e53d245ab56b921a824.
Review/tooling changes are UNCOMMITTED. Preserve them and untracked model/;
never stage directories wholesale. No production C++ changed in this audit.

Read first:

1. docs/evidence/V2_PHASE6_9_FULL_AUDIT_2026-09-21.md — consolidated findings,
   severity, source anchors, proof limits and guarantee mapping.
2. docs/evidence/phase6_9_audit_2026-09-21/README.md — ten reproducible RED runs,
   sources, commands and observed output. These are NOT in the passing CTest count.
3. docs/V2_NUC_PI_TRADING_ROADMAP.md — fix order F0–F7 and milestones M0–M7.
4. Revised ADR 0011 and review notes on ADRs 0005/0006/0009/0010; ADR 0001
   remains the governing architecture. ADR 0002 is protected and unchanged.

Host verification: fresh isolated Release 36/36; rebuilt Debug ASan+UBSan 36/36
with UBSAN_OPTIONS=halt_on_error=1 and leak detection explicitly disabled; tool
regressions 9/9; passive model 109/109 with bytecode writes disabled. No new TSan,
target hardware or venue run. Initial existing Release build directory was
broken; its stale tests were not used. Fresh isolated build supplied the result.

Confirmed findings: A01 stale leader renews sender after majority kill; A02
planner loses emergency quota; A03 wire helpers drop limit/post-only and do not
bind exact metadata; A04 persisted directory accepts changed quorum config;
A05 malformed normal command poisons restart progress; A06 zero authority can
authorize unmaterializable bytes; A07 replication queues/log are unbounded;
A08 replacement voter cannot fetch compacted history; A09 synchronous metadata
and missing hard-state cut evidence; A10 multi-block synchronous flush duplicates
the first block; A11 token destination can remain 0644; A12 incomplete/stale
gate manifest; A13 missing node/process/auth/venue integration; A14 compaction
deletes the command history ADR 0007 relies on without a verified archive gate.

The earlier provisional Phase 6/7 findings were independently checked here.
Correction: atomic replacement is not inherently invalid, and loading the intact
metadata file was intentional. The missing test is persistence cut coverage.
No new mass-cancel RED was found in the reducer; the reserve defect is its planner.

Phase 6/7 current-tree target acceptance is OPEN, including G-049's membership
part; Phase 8/9 remain OPEN. Historical tags stay untouched. The partial gate
recipe computes c62e995439e12d97b1683849e2cf675055c3b27bbeaa981b48cb78340c8c02f5;
it omits shared dependencies and is not a deployment seal. Fix the manifest
closure before re-pinning or asking the owner to run targets.

Recommended next RED: promote gateway_probe.cpp into the gateway delivery suite
to prove quota {normal=0, emergency=1} schedules the same cancel the guard admits.
Then follow F1/F2. Do not wire a live sender while A01/A03 remain. User requested
audit and planning this turn, not implementation of these newly found C++ defects.

Deployment recommendation (NOT approval): one voter on NUC, one on Pi, both
needed for quorum; separate NUC gateway; no automatic gateway promotion. An async
question requested owner confirmation; until explicitly answered, keep it a
proposal. Do not reinstall either OS or modify services/networking automatically.
No new byte/economic/protocol contract is approved by this roadmap. No perps,
native amend/overlap, netting, venue inquiries or real-money actions.

### Historical review checkpoint (superseded)

This checkpoint supersedes the historical Phase 9 narrative below. Base verified:
branch v2/phase-5-corrections, d44a4a9. Current edits are uncommitted; inspect Git.
model/ remains untracked and protected. No C++ or canonical bytes changed.

Read revised ADR 0011, ADR 0009's applicability amendment, and the qualification
above D-22..28. The MCP capture does not establish venue-wide capability absence.
Expected output is not an enforceable bound; submitted is not filled. Live swap
admission remains blocked pending verified execution and settlement contracts.

Tooling: nine offline tests showed RED on the original scripts, then GREEN.
Run python3 tests/test_readonly_tools.py. No auth, venue calls or trading occurred.
measure-markup.py now captures unverified quote evidence only (exit 2); it does
not estimate markup, infer units, or construct a sell. New byte choices still
require owner approval. Do not restore guessed parsing to obtain a number.

Audit: docs/evidence/V2_PHASE8_9_AUDIT_2026-09-21.md records pure seam coverage
versus absent V2 process integration. No reachable mass-cancel bug was established.
Phase 6/7 independent reviewer hit a usage limit before delivering findings:
the report was subsequently found at docs/evidence/V2_PHASE6_7_AUDIT_2026-09-21.md.
The parent source-checked authority-bit, synchronous metadata and hard-state
coverage findings. G-040, G-043 and G-047 are reopened. Probe and gate claims
remain provisional pending independent verification. Resume against ADR 0001, ADRs 0005/0006
including amendments, architecture contracts, plan, and G-035..G-051. Trace
durability, follower ACK clamping, recovery and snapshot claims through actual
code and tests; mutation-check important guards. No new target acceptance claimed.

Next: finish Phase 6/7 audit, strengthen protocol/error-path tooling tests, then
resolve gateway topology before S1. Market inputs are DecisionFrame, execution
evidence VenueObservation. No external book recommendation has been verified.

### Historical handoff (superseded)

This section supersedes every older one below it. Those remain as an audit trail
and must not be read as current status.

### Where the tree is

- branch `v2/phase-5-corrections`; use Git for HEAD rather than trusting a value
  written here;
- development host was last green at 34/34 Release and 34/34 Debug ASan+UBSan,
  BEFORE this block of work. **This block changed no C++ at all** - it is
  documentation, evidence and four read-only Python tools - so the test state is
  unchanged, but re-run before trusting it;
- tags `v2-phase7-nuc-gated`, `v2-phase7-complete`, `v2-phase8-mechanisms`;
- no Phase 8 or Phase 9 gate has ever been run.

### Read these first, in this order

1. This section, all of it. The premise change in step 2 invalidates design work
   that still looks current elsewhere in the tree.
2. `docs/adr/0011-the-swap-execution-model.md` - PROPOSED, not accepted.
3. `docs/TRUEMARKETS_DOC_DISCREPANCIES.md`, rows D-22 to D-28.
4. The 2026-09-21 CHANGELOG entries.
5. `docs/evidence/V2_TRUEMARKETS_MCP_SCHEMA_2026-09-18.md` - the venue's own
   tool schemas, which are the authority wherever prose disagrees.

### THE PREMISE THAT CHANGED

True Markets publishes two MCP servers at `mcp.truemarkets.co`. Both answer
`tools/list` anonymously with a JSON Schema per tool. Read from that source:

    get_quote      -> indicative, binds nothing, returns no quote id
    trade_prepare  -> "Create a market order"
    trade_execute  -> submit signatures

No price parameter. No order type. No time-in-force. Across all seventeen tools:
no cancel, no open-orders query, no order book.

**True Markets is a request-for-quote swap venue, not an exchange.** V2 is built
to post and manage resting liquidity - `OrderEffect` carries `PriceTicks`, Phase
8 built mass cancel, ADR 0008 reconciles resting orders. None of that has a
counterpart on this venue. D-1 concluded from the endpoint reference that DeFi
limit orders existed; that conclusion was wrong, and the MCP schema settles it.

The mechanisms remain correct for Hyperliquid, which has a real book. Do not
delete them. Do not claim V2 market-makes on True Markets.

### DECISIONS ALREADY TAKEN, WITH THEIR REASONING

Recorded in full in ADR 0011 and the 2026-09-21 CHANGELOG. In brief:

- **A swap is `EffectOperation::swap_market`**, not a parallel effect type, so
  Phase 8's mechanisms cannot come to cover one path and not the other.
- **Chain becomes part of `VenueId`** (`truemarkets-solana` / `-base` / `-cefi`).
  Rejected alternative: a validated `chain` field. Folding it into the venue id
  makes D-25 unrepresentable rather than guarded, and stops per-venue risk
  limits pooling on-chain and exchange exposure.
- **Quantity is denominated in what the side spends** - quote micros for buys,
  base lots for sells - which makes risk reservation exact on buys and inventory
  exact on sells. Rejected alternative: deriving the basis from `side` at encode
  time, which puts one rule in two places and, on divergence, buys 113 USDC of
  something instead of 113 units.
- **The price bound lives at the signing step**, because D-28 leaves nowhere
  else: no slippage parameter exists and `get_quote` binds nothing. An unsigned
  payload does not execute, so declining to sign IS the slippage control.

### DECISIONS WITHDRAWN — do not act on these if you find them elsewhere

- **Trading PURR is withdrawn.** `trade_prepare` accepts solana and base only;
  PURR is `hypercore-spot` and is not addressable through this API. Any note
  recommending PURR predates 2026-09-18.
- **The significant-figure pricing ADR (D-15) is deferred**, not cancelled. It
  is moot on True Markets - there is no price to render - and belongs to the
  Hyperliquid adapter. This cancels the byte-contract churn that work was about
  to cause; do not start it for True Markets.
- **D-1's "limit orders exist" is withdrawn**, superseded by D-23.
- `docs/QUESTIONS_FOR_TRUEMARKETS.md` is SHELVED. The project owner is not
  asking the venue anything this iteration, by explicit decision. Do not write
  work that depends on a venue answer.

### TWO THINGS BLOCK PROGRESS, AND THEY ARE INDEPENDENT

**BLOCKER 1 - the markup, which gates all economics.**

The objective is maximum volume traded for a leaderboard. On a swap venue
capital recycles, so volume is bounded by the loss budget:

    max_volume = loss_budget x 2 / c        (c = round-trip cost fraction)

Measured `c` on Solana through Jupiter: SOL 0.1-0.7 bps, JUP 1.5-10.6, JTO
46-72, DRIFT 71-772, W 58-357. **These are a LOWER BOUND** - they are the raw
on-chain route, and any True Markets markup sits on top and dominates. Per $100
of budget: $4M of volume at zero markup, $190k at 10 bps, $66k at 30 bps, $20k
at 100 bps.

`tools/measure-markup.py <email>` measures it. It is read-only, needs an email
OTP login, and places no order. IT HAS NOT BEEN RUN - it needs the project
owner's login, so it cannot be run by an agent. Ask for the output; do not
size a strategy against the Jupiter figures.

**BLOCKER 2 - the gateway process topology, which gates the node binary.**

OPEN QUESTION, and the project owner asked what "the Velocita book" recommends.
THE BOOK IS NOT IN THIS REPOSITORY - it is an external source, quoted secondhand
in `docs/V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md` (Chapter 13, the mapped Raft log),
`docs/PI_OPERATOR_RUNBOOK.md` §14 ("the book's reference shape" for metrics) and
`docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md`. **Do not invent what it says.**
Ask the owner for the chapter before deciding.

What the repository already constrains, independent of the book:

- ADR 0001 lists "venue stalls cannot block Raft apply" as a property to
  preserve, and gives the gateway TLS/REST/WebSocket session mechanics and
  transport attempts. In-process means blocking venue I/O shares an address
  space and scheduler with a voter whose tick regularity is already monitored
  (`owner_loop_max_gap_nanoseconds`, tick age, both on the dashboard).
- ADR 0001 has the gateway consume committed outbox effects AFTER apply, which
  is already a queue-shaped relationship and a natural process boundary.
- A TRAP WORTH NAMING: §14's book-derived metrics shape - each voter writes a
  page, a SEPARATE exporter maps all three read-only - looks like precedent for
  separation. It is not. ADR 0001 classes read models as disposable projections
  that never become trading authority, and `renew_authority`'s read is
  authoritative by definition. The pattern does not transfer.

The outgoing session's recommendation, NOT the book's and NOT accepted:
separate process, same host, outbox over Aeron IPC, lease bound sized to a local
IPC round trip rather than a network one. Reasoning: it keeps the authoritative
read sub-millisecond while putting a process boundary between apply and True
Markets' TLS stack; it makes gateway restart independent of voter restart, so a
wedged venue session does not force a Raft membership event; and it makes Phase
8 demonstrable - killing the gateway shows the cluster holding consensus while
the lease expires and trading stops fail-closed.

### WHAT IS LEFT TO RUN SHADOW MODE ON THE NUC

The observability stack is BUILT AND WORKING - Prometheus, Grafana, three-node
quadlet cluster, alert and recording rules, process exporter, an 18-panel
dashboard. It is all pointed at `velocita-raft:v1` running `counter_fsm`.

**THERE IS NO V2 NODE BINARY.** Every V2 target in `CMakeLists.txt` is a test, a
benchmark or a report tool. Nothing links the reducer to the replication layer
in a process that can be started. That is the single largest item and everything
else stacks on it.

| | item | why | size |
|---|---|---|---|
| S1 | `v2-node` binary: WAL + replication + `apply()` in one process | nothing runs today | large |
| S2 | V2 metrics -> shared page -> exporter | `collect_metrics` is a pure function nobody writes anywhere; the exporter reads the V1 page | medium |
| S3 | quadlets for 3x `v2-node`, plus a V2 dashboard | V1 quadlets hardcode `raft-node` and the counter FSM | small-medium |
| S4 | observation ingest -> `VenueObservation` | a feed now exists (TM 5s price history, Jupiter) but nothing converts it | medium |
| S5 | minimal gateway loop, shadow only: decides, never sends | drives the lease and heartbeat | medium |

THE USEFUL PART OF THE SEQUENCING: shadow mode is blocked on strictly less than
live trading, and the two tracks do not touch. Blocker 1 and the V3 byte contract
gate anything that SENDS; S1-S3 gate the shadow run and need no venue decision
at all - no price rendering, no `qty_unit`, no signing. So stability data can be
gathered while the economics question is still open, which is the right order:
if the cluster cannot hold a steady apply rate for hours on the NUC, the
strategy does not matter.

S5 also partly discharges the gateway loop Phase 8 deliberately deferred. That
deferral was justified by needing real round-trip times and venue rate limits to
choose the lease bound and heartbeat cadence - shadow mode is where those get
measured without money at risk.

### Still carried, unrelated to shadow mode

ARM64 container gate never run; Phase 8 gate never run; physical Pi gate remains
a Phase 5 obligation; peer snapshot transport does not exist.

`docs/README.md` is CLEAN as of 2026-09-21 and tracked. Older sections below
describe it as modified and uncommitted; that is stale. `model/` remains
untracked and protected - never `git add .` or `git add -A`, commit by explicit
path only.

### Tools added this block, all read-only

- `tools/mcp-tools.py <url>` - `initialize` + `tools/list` only, refuses
  `tools/call`. How the schemas were captured.
- `tools/mcp-read.py <tool> k=v ...` - market data. Pinned to the marketdata
  server, which has NO trading tool to call, so it is safe BY CONSTRUCTION.
- `tools/roundtrip-cost.py [sizes]` - Jupiter round-trip cost. Simulates routes;
  builds no transaction.
- `tools/measure-markup.py <email> [sizes]` - the Blocker 1 measurement. Its
  safety is a RULE, not a property: `get_quote` exists only on the trading
  server, so it holds a session that can trade. Four-name allowlist, by-name
  refusal of `trade_prepare`/`trade_execute`, no signing code. Do not add tools
  to that allowlist.

### Method notes from this block

- A MACHINE-READABLE SCHEMA BEATS PROSE, AND IS OFTEN CHEAPER TO GET. D-3 sat
  open for four days because a documentation page would not render, and the
  fallbacks considered were a browser DevTools session (needs the owner's login,
  and to see a request body, a real order) and scraping the frontend bundle
  (blocked by a Cloudflare JS challenge on `app.truemarkets.co`). The MCP
  `tools/list` endpoint answered it anonymously in one call. Look for a
  machine-readable contract before reverse-engineering a human one.
- AN ERROR CODE DESCRIBES THE REQUEST THAT PRODUCED IT AND NOTHING ELSE. The MCP
  servers report RATE LIMITING as `-32602 invalid params`; a twelve-call sweep
  returned it eleven times and every call succeeded when retried alone. Several
  minutes went into looking for an argument bug that did not exist.
- CHECK ARITHMETIC THAT CARRIES A CONCLUSION. ADR 0011's volume table was
  committed 10x high and corrected in `191a741`. `2/c` at c = 0.5 bps is the
  MULTIPLIER 40,000, so $100 buys $4M - not $40M. A figure that reads as a
  headline deserves a second evaluation before it is committed.
- A REFUSAL WRITTEN BEFORE THE ANSWER IS KNOWN CAN BE THE THING THAT SAVES YOU.
  `encode_limit_order_body`'s `schema_unverified` guard was written against a
  guess nobody had yet made, on the reasoning that an ignored price field turns
  a limit order into a market order silently. The venue turned out to have no
  price field at all, so that is precisely what a guess would have produced.
- DO NOT ATTRIBUTE A RECOMMENDATION TO A SOURCE YOU HAVE NOT READ. The book is
  not in the repository. Saying so was more useful than producing a plausible
  answer the owner would have acted on believing it was theirs.

## PHASE 8 SUBSTANTIALLY BUILT, DRIVER DEFERRED TO PHASE 9 — 2026-09-17

This section supersedes every older one below it. Those remain as an audit trail
and must not be read as current status.

### Where the tree is

- branch `v2/phase-5-corrections`; use Git for HEAD rather than trusting a value
  written here;
- development host Release 34/34 and Debug ASan+UBSan 34/34;
- tags `v2-phase7-nuc-gated` (the tree that actually passed the NUC gate, root
  `b241ad40...`) and `v2-phase7-complete` (guarantees closed, NOT gate-covered);
- NO Phase 8 gate exists yet, and the Phase 7 gate pins were deliberately frozen
  rather than allowed to drift with Phase 8 work.

### Read these first, in this order

1. This section.
2. `docs/adr/0007` (mass cancel; the log as audit record) and `docs/adr/0008`
   (a resting order is confirmed, not assumed). Both carry amendments recording
   claims that turned out false - read the amendments, not just the decisions.
3. The Phase 8 block in `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`.
4. The 2026-09-16 and 2026-09-17 CHANGELOG entries.

### What Phase 8 built

FOUR MECHANISMS, none subsuming another, all enforced and mutation-checked:

- THE AUTHORITY LEASE. Generation fencing compares a cached view against cluster
  state, which only helps a gateway that can still READ the cluster; a
  partitioned one believes its last good answer indefinitely. The lease bounds
  how stale that belief may be, renews ONLY on an authoritative read, and is
  fail-closed - a zero bound means no authority, not no limit. A cancel is
  exempt: a lapsed lease means stop opening exposure, not stop closing it.
- OPERATOR AUTHORIZATION WITH NO NEW BYTES. The reducer cannot verify a
  signature (G-066 keeps keys out of replicated state, `apply` does no I/O), so
  authentication is necessarily an edge property. `kMaxOperatorCallerId` reserves
  a CallerId band. THE DEPLOYMENT OBLIGATION: the edge must never issue a
  reserved CallerId to a strategy.
- TWO RESERVED LANES, gateway-side and cluster-side. `ProposeLane::control`
  decodes and checks before the entry exists, because a refusal that costs the
  resource it was protecting is a denial of service on the kill path.
- MASS CANCEL as one command (ADR 0007), atomic over the cancellable set.

PLUS: resuming `normal` requires reconciliation (G-059); leadership is never
promotion (G-053); an unsolicited venue cancellation is evidence (ADR 0008 r1);
and a resting order is confirmed rather than assumed (ADR 0008 r2/r3).

### What is NOT done, and why

THE GATEWAY LOOP IS DEFERRED TO PHASE 9, deliberately. Every mechanism is
enforced and none is DRIVEN: nothing calls `renew_authority`, nothing chooses a
lease bound, nothing reaches for the emergency lane, nothing runs the
resting-order heartbeat. That loop's shape is decided by the adapter it talks to
- the lease bound by real round-trip times, the heartbeat cadence by venue rate
limits, the authoritative read by whether the gateway is in-process with a voter.
Building it first would mean choosing those numbers twice.

CONSEQUENTLY NO PHASE 8 GUARANTEE IS TICKED. That is a sequencing decision, not
an omission, and ticking one on a refusal nothing calls would claim a working
kill path from a guard sitting in isolation.

Also open: the ARM64 container gate and a Phase 8 gate have never been run; the
physical Pi gate remains a carried Phase 5 obligation; peer snapshot transport
does not exist; `docs/README.md` is modified and uncommitted (protected path,
not touched).

### Where a reviewer should push hardest

1. ADR 0008's amendments. Two claims I wrote were falsified by tests I then
   wrote - that an out-of-band venue action reconciles back (it could not be
   represented at all), and that closing the gap needed a byte contract (it did
   not). Both corrections are recorded; check the reasoning, not just the result.
2. The two ACCEPTED TESTS whose expectations changed under ADR 0008. Both
   required a reconciliation answer for a healthy order to be refused. That
   assumption is what the ADR overturns, but it is still an accepted test being
   rewritten and deserves a second opinion.
3. `disposition_of_cancel`. Mass cancel's feasibility pass and acting pass read
   one rule so they cannot drift; verify they genuinely do.

### Method notes that cost something to learn

- MUTATION TESTING THROUGH CMAKE IS UNRELIABLE ON THIS HOST, and it produced
  WRONG RESULTS REPEATEDLY on 2026-09-17: mutants reported as surviving that were
  never compiled, and mutants killed by an assertion belonging to a different
  property. Deleting the object file is not enough, `touch` is not enough, and a
  fresh build directory costs 46 seconds of configure each time - which is what
  pushed a four-mutant loop past the command timeout and left a mutation applied
  in the working tree.

  WHAT WORKS: compile the suite DIRECTLY, no cmake.

      c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=128 -Iinclude \
          -o /tmp/t tests/<suite>.cpp <its .cpp deps>

  Seconds rather than minutes, and immune to staleness because there are no
  objects to go stale. Always check WHICH assertion died: a mutant that dies on
  an assertion belonging to a different property is a false pass wearing a green
  coat, and that happened three times in one day.

- A REFUSAL CODE TELLS YOU WHICH CHECK FAILED ONLY IF YOU CONFIRM WHICH CHECK
  RAN. A day was lost blaming applicability gates for a refusal that was
  `validate` rejecting a test fixture with a zero venue sequence - and the
  instrumentation used to chase it was misattributing single-line returns while
  the lines being read came from other tests in the same suite run.
- MUTATE, THEN CHECK WHICH ASSERTION DIED. Several mutants were killed by the
  wrong test, which is a false pass in disguise: the suite caught something, not
  the thing.
- A SWEEP OVER AN ARBITRARY RANGE PROVES LESS THAN IT LOOKS. The identities a
  wrong lookup returns are the other ids in the same records, not random numbers.
- `cp` RESTORING A FILE CAN LEAVE A STALE OBJECT on this host's mtime
  granularity, so the next run measures the previous binary. Delete the object
  file, do not rely on `touch`.

## REVIEW LANDED — six Phase 6/7 defects corrected, gates pinned and unrun — 2026-09-16

This section supersedes every older section below it, including the one
immediately following. Those remain as an audit trail and must not be read as
current status.

### Where the tree is

- branch `v2/phase-5-corrections`, HEAD `fe78105`; use Git rather than trusting
  this value;
- source root `543eb468167ed51dd8c918d8de356c7028dec736a9f223fe1618f5e40b83c14d`.
  THIS IS THE VALUE TO COMPARE ON THE TARGET, and the reason it is written here
  is that the pin inside `deploy/` travels with the tree and therefore agrees
  with a stale copy just as happily as with the right one;
- development host: Release 34/34, Debug ASan+UBSan 31/31 (the `v2-` suites),
  both also from a clean-slate configure;
- tags `v2-phase5-accepted` and `v2-phase5-corrected-accepted` unmoved.

### What happened

An independent review found five defects in Phases 6 and 7; a sixth came out of
the same pass. All six are fixed and committed. The full account is
`docs/evidence/V2_PHASE6_7_CORRECTION_RED_GREEN.md`; the CHANGELOG entries for
2026-09-16 carry the narrative.

The worst was mine: `WalSegment::flush` wrote each block, RELEASED it, and
synced after the loop. Releasing raises the durable index, and the durable index
is what this node reports to Raft for its own quorum vote - so a crash in that
window left this node having already counted entries that were not on disk. The
function's own comment stated the correct order and the code did the opposite.

Also fixed: membership lost across a snapshot load; truncation and compaction
racing the writer thread; a durability advance not reconsidering quorum; and the
patch-stamp gap below.

ONE DECISION WAS REVERSED. Recovery now REFUSES a commit index past the end of
the log rather than clamping it. My clamp was a response to a sweep that
truncates the segment externally - which cannot happen while the durability
contract holds. I had taught the node to tolerate a broken disk silently, on the
single-voter shape that has no peer to re-supply what was lost. ADR 0006 carries
the amendment, the accepted cost (such a node does not start without a human),
and the condition that would revisit it (peer snapshot transport, which does not
exist).

### What I added on top of the review

MUTATION-CHECKED ALL SIX FIXES, and three had no test that failed when the fix
was reverted - including the flush ordering, the most serious of them. Each gap
is now closed by a test that kills its mutant:

- the durable index is read from INSIDE the sync, through the
  `before_file_sync` checkpoint. A crash cannot expose this bug: `_exit` does
  not drop the page cache, so the file looks identical either way.
- the power-cut sweep asserts the REASON recovery refused. Without the explicit
  check the node still refuses, via an unrelated replay failure with an empty
  cause - which tells an operator nothing, defeating the point of refusing.
- `ReplicatedKernel` gained an optional `StorageCheckpoints` seam, defaulted off
  and carrying no behaviour, so a test can PARK the writer mid-drain and deliver
  a replacing suffix while it sits there. The mutant had survived forty
  consecutive runs of the old threaded test; it now dies 5/5.

THE PATCH STAMP WAS RECORDING, NOT CHECKING. The Raft fork's stamp compares
digests only on the branch that finds an existing stamp. The branch that APPLIES
the patch - every first configure on a clean machine, so every container gate
and both physical targets - recorded the result without comparing it. The patch
carries zero context, so hunks apply by line number alone and `patch` exiting
zero proves placement happened, not that it happened where the review put it.
Now FATAL on mismatch, verified by perturbing a digest.

### What is NOT done

- NEITHER PHASE 6/7 GATE HAS BEEN RUN. `deploy/run-v2-phase7-container-gate.sh`
  (ARM64) and `deploy/run-v2-phase7-nuc-gate.sh` (x86-64) are written and pinned
  to the root above. Expected counts 34 / 31 / 31.
  `docs/NUC_OPERATOR_RUNBOOK.md` section 7 is the procedure. THE NUC IS THE NEXT
  TARGET GATE.
- The physical Pi gate remains a carried Phase 5 obligation, deferred while the
  machine is offline. It gates the TAG, not HEAD.
- Peer snapshot transport does not exist, so a follower that loses committed
  history needs an operator, not just a leader.
- `docs/README.md` is modified and uncommitted. It is a protected path; I did
  not touch it and did not commit it.

### Where a reviewer should push hardest

1. `writer-truncation-race`. It is the newest test and the one whose correctness
   is hardest to see: a parked writer, a bounded release so the passing case
   cannot deadlock against its own join, and an invariant (the durable index may
   never name an index the segment does not hold) chosen because the obvious
   assertion collapses when the replacement reuses an index.
2. Whether refusing recovery is right for a MULTI-VOTER follower. I argue it is
   correct today only because peer snapshot transport is missing. If you think
   the single-voter and follower cases should already diverge, say so - that is
   a material change, not a tweak.
3. The three mutants that survived. Two were in fixes nobody doubted, which is
   the point: a green suite said nothing about them.

## CODEX HANDOVER — Phases 5, 6 and 7 complete; re-gate pending — 2026-09-15

This section supersedes every older section below it. Those remain as an audit
trail and must not be read as current status.

### Read these, in this order

1. This section.
2. `docs/adr/0003` through `0006` - the four material decisions taken since the
   last takeover, each with the alternatives rejected and why.
3. `docs/V2_ARCHITECTURE_CONTRACTS.md` sections 10.15 through 10.18.9, which are
   the detailed reasoning the ADRs summarise.
4. `docs/evidence/V2_TRUEMARKETS_CONTRACT_VERIFICATION.md`.

Verify every claim against Git before acting on it.

### Where the tree is

- branch `v2/phase-5-corrections`; use Git for HEAD rather than a value embedded
  here;
- tag `v2-phase5-corrected-accepted` at `b39edf1`; `v2-phase5-accepted`
  (`04329fa`) preserved and NOT moved;
- host Release 32/32, Debug ASan+UBSan 32/32 with
  `-fno-sanitize-recover=undefined` and `halt_on_error=1`, ThreadSanitizer clean
  on the ring and the replication suite, model 109/109 unmodified,
  `git diff --check` clean;
- THE SOURCE ROOT HAS MOVED well past the Phase 5 pin
  (`681e222d…6976f`), because Phases 6 and 7 added manifest files. Target gates
  will correctly refuse until the tree is re-frozen and re-pinned.

### What was done since the takeover, and why

FOUR MATERIAL DECISIONS, each an ADR:

- ADR 0003, NATIVE VERSUS SYNTHETIC OBSERVATION IDENTITY. Gateway-authored
  observations were sharing the venue's sequence-deduplication domain, so a
  number the shell chose could refuse real venue evidence, and real venue
  evidence could refuse the only answer able to resolve an unknown effect. Zero
  bytes added; codec and snapshot schemas V6 to V7.
- ADR 0004, MVP V1 EXCLUDES PERPETUALS. The verified DeFi surface is Hyperliquid
  perpetuals, and `apply_observe_fill` requires every fill to name an authorizing
  submit effect. Liquidation, ADL and funding carry no `effect_id`, so no command
  in the kernel can represent them. Fail-closed scope decision, not a claim that
  spot accounting approximates perps.
- ADR 0005, THE REDUCER BEHIND CONSENSUS. `ReplicatedKernel` is a separate class
  from V1's node, reusing the payload-generic storage and peer wire. Exactly one
  call to `apply()`, in the Raft apply callback, so G-035 is the shape of the
  code. G-035 through G-041 closed.
- ADR 0006, ASYNCHRONOUS DURABILITY AND A PINNED RAFT FORK. Staging plus an
  off-thread sync, with a pinned patch adding an optional `durable_idx` callback
  so a voter counts its own vote only for entries it has made durable. Phase 7
  complete in implementation scope and test-first list.

### What is NOT done, stated plainly

1. THE PHYSICAL PI GATE. A carried Phase 5 obligation; the machine was offline
   for the whole session. It blocks live deployment, not development. Gate the
   TAG, not HEAD - `git checkout --detach v2-phase5-corrected-accepted` - because
   the obligation is against that tree.
2. RE-FREEZE AND RE-GATE FOR PHASES 6 AND 7. Nothing since `b39edf1` has run on
   ARM64 container, NUC or Pi. This is the highest-value next step: it converts
   "green on a laptop" into evidence at a pinned root.
3. A READ-INDEX DISCIPLINE for authority-sensitive reads. `CommittedOutboxView`
   proves only that this node believed itself leader when the bytes were taken.
4. PERFORMANCE ON THE DEPLOYMENT TARGETS. The storage benchmark has only run on
   a development laptop.

### Where a reviewer should push hardest

- THE PINNED RAFT PATCH (`deploy/patches/0001-raft-durable-quorum.patch`). It
  changes quorum arithmetic. Is the single-voter re-check in `raft_periodic`
  reachable in every path that matters, and does the NULL-callback path really
  leave V1 byte-for-byte unchanged?
- THE FOLLOWER ACK CLAMP in `receive_peer`. It lowers a reported `current_idx`.
  Does that interact safely with `first_idx` and with willemt's next-index
  backoff, or can it wedge a follower that is behind?
- `recovered_commit_clamped`. Clamping rather than refusing is a deliberate trade
  argued in ADR 0006. On a single voter a non-zero value is real loss; is
  reporting it enough, or does it need to refuse new risk until an operator
  acknowledges?
- WHETHER ANY OF THE NEW TESTS ARE VACUOUS. Three were, during this session, and
  all three were found by mutation rather than by reading. Assume there is a
  fourth.

### Method notes, each of which cost something to learn

- MUTATION-CHECK A GUARD OR DO NOT CLAIM IT. Three tests in this session passed
  with the code under test removed: the durability invariant (the harness closed
  the gap before anything could observe it), a WAL test that staged arbitrary
  bytes so the record decoder had nothing to find, and an earlier oracle that
  read its expected answer off the code under test.
- A SWEEP BEATS HAND-PICKED POINTS. The power-cut sweep found two defects on its
  first run that three durability points had been green over for a day.
- A GATE PASS PROVES THIS TREE MATCHES ITS OWN PIN, never that it is the
  candidate you meant to test - the pin travels inside the tree. NUC run
  `20260915T152553Z` passed against the superseded candidate for exactly this
  reason. Compare the gate-computed root against a value held OUTSIDE the tree,
  and check the `ubsan_*` metadata keys are PRESENT, since a stale gate script
  omits them rather than printing them wrong.
- NOTHING THAT DECIDES BEHAVIOUR MAY BE IMPLICIT. The simulator's clock, the WAL
  writer's poll, and `flush_durability` are all explicit for the same reason: an
  implicit one makes the behaviour it decides unobservable, including to a test
  trying to prove it.

### Constraints that still bind

- Protected uncommitted paths: `model/`, `docs/README.md`,
  `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
  `docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, ADR 0002. Never `git add .` or
  `git add -A`; stage explicit paths only.
- `model/` must never be able to trade. Its interlock fails the build if
  `OrderEffect` or `VenueObservation` appears; do not weaken it to compile.
- `ReadOnlyHttp` refuses non-GET and any path containing `/orders`. Do not route
  around it to answer a documentation question.
- The API key is an EC P-256 private key that signs on-chain actions. G-066: it
  never enters replicated policy, commands, snapshots, logs, test fixtures or
  evidence bundles.
- Review any NEW material byte or economic contract choice with the user BEFORE
  encoding it.
- Do not move or delete `v2-phase5-accepted`.

## SUPERSEDED — Phase 5 accepted, MVP V1 excludes perpetuals — 2026-09-15

This section supersedes every older section below it. Those remain as an audit
trail and must not be read as current status.

### Where the tree is

- branch `v2/phase-5-corrections`; the handover review started at `698a01a`.
  Use Git for the current HEAD rather than embedding a self-staling value here;
- tag `v2-phase5-corrected-accepted` at `b39edf1`; `v2-phase5-accepted`
  (`04329fa`) preserved as history and NOT moved;
- frozen source root
  `681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`, pinned in
  all three Phase 5 gate scripts. Commits after `b39edf1` are documentation only
  and do not move it - verify before assuming;
- host Release 29/29, host Debug ASan+UBSan 29/29 with
  `-fno-sanitize-recover=undefined` and `halt_on_error=1`, model 109/109
  unmodified, `git diff --check` clean.

### What was accepted, and the one exception

Phase 5 is ACCEPTED on host, ARM64 Linux container (`20260915T134221Z`) and
x86-64 NUC (`20260915T134322Z` and `20260915T154328Z`, two accounts, two
independently synced trees). G-025 through G-034 are closed.

THE PI GATE HAS NOT RUN against this source; the machine was offline. That is a
CARRIED OBLIGATION, recorded in the acceptance record and the checklist, and a
later failure voids the acceptance. The ARM64 container gate proves logic and
byte layout on a real aarch64 kernel, so the gap is physical hardware and clean
power evidence, not unverified behaviour.

Evidence: `docs/evidence/V2_PHASE5_CORRECTED_ACCEPTANCE.md`, which supersedes
`V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

### MVP V1 scope decision and review conclusions

MVP V1 WILL NOT TRADE PERPETUALS. The True Markets DeFi surface verified in the
venue record is Hyperliquid perpetuals, so it is not a V1 execution target. No
perpetual position, liquidation, ADL, funding, leverage, or margin-maintenance
contract is to be added to the reducer for V1. This is a fail-closed scope
decision, not a claim that the existing spot accounting can safely approximate
perpetuals.

Before any later perpetual connection, implement the deferred ledger-first
design in `V2_DEFERRED_FUNCTIONAL_SCOPE.md`: separately identified external
executions and funding settlements, with an authoritative account snapshot used
as a reconciliation check rather than a blind overwrite. Exact bytes remain
unfrozen until the venue's exposed identities and finality fields are verified.

The native/synthetic identity split in architecture 10.15 is sound, but its old
explanation overstated cumulative-value idempotence. Stable command identity
returns the original result while one gateway evidence obligation is retried.
After that answer resolves the effect, the same observation under a fresh
identity is refused as `venue_observation_not_applicable`; it does not compute
and apply an economic delta. A restart discards gateway-local obligations and
uses replicated lifecycle state plus a fresh reconciliation query if one is
still needed. Existing tests separately pin identical-command result replay,
fresh-identity refusal, startup recovery, and reconciliation. An additional
single-trace characterization was deliberately deferred because it would change
the accepted source root without correcting production behavior and force every
target gate to be repeated.

Three-environment acceptance is sufficient to continue development, not to
authorize live deployment. The physical Pi gate remains mandatory before any
live venue connection.

### Open work, ranked by what it blocks

1. PHASE 6 CONSENSUS INTEGRATION. This is the next implementation phase. Do not
   pull the deferred perpetual contracts into it.
2. THE PHYSICAL PI GATE. It remains a carried Phase 5 obligation and blocks live
   deployment, though not Phase 6 development.
3. CREATE-ORDER SCHEMA. Every DeFi-limit statement found concerns CANCELLING a
   resting order; the create schema is client-rendered and unread. A read-only
   `GET` probe resolves it. Because V1 excludes the verified DeFi perpetual
   surface, this is deferred until a later perpetual iteration or a different
   spot venue is selected.
4. UNVERIFIED DEFI ECONOMICS: maker/taker fee (the published schedule is CeFi
   only), default leverage, per-fill on-chain cost. Any one of these can invert
   the sign of the edge. Deferred with perpetual trading.
5. `PostOnlyMode::required` CANNOT BE SATISFIED. Post-only is absent from an
   otherwise exhaustive True Markets order model. The pre-send guard would refuse
   every order with `venue_cannot_represent_intent`, correctly. Decide whether
   the strategy drops post-only - the cost of an accidental cross is 4 bps taker
   on CeFi, unknown on DeFi - or whether the venue exposes it undocumented.
6. PREPARE/EXECUTE DOUBLE AMBIGUITY. One economic intent crosses the process
   boundary twice, each crossing separately ambiguous. `DeliveryAttempt` models
   one send per effect.
7. `initialized` HAS NO V2 EQUIVALENT. The venue holds unsigned payloads: it
   knows the intent, the order is not in the book, no venue risk exists.
   `dispatch_authorized` and `acknowledged` each assert something stronger.
8. TICK SIZE IS PRICE-BANDED across nine buckets (CeFi table; no DeFi table
   published), so a cached `InstrumentExecutionProfile` goes stale when price
   crosses a boundary.

### Constraints that still bind

- Protected uncommitted paths: `model/`, `docs/README.md`,
  `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
  `docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, ADR 0002. Never `git add .` or
  `git add -A`; stage explicit paths only. A directory-wide add has already swept
  unrelated research into a commit once.
- `model/` must never be able to trade: no wallet keys, no signing, no order
  submission. Its interlock fails the build if `OrderEffect` or `VenueObservation`
  appears; do not weaken it to make something compile.
- `ReadOnlyHttp` refuses any non-GET and any path containing `/orders` before a
  socket opens. Do not route around it to answer a documentation question.
- The API key is an EC P-256 private key that signs on-chain actions. G-066: it
  never enters replicated policy, commands, snapshots, logs, test fixtures or
  evidence bundles.
- Review any NEW material byte or economic contract choice with the user BEFORE
  encoding it.
- Do not move or delete `v2-phase5-accepted`.

### Method notes worth carrying

- A GATE PASS PROVES THIS TREE MATCHES ITS OWN PIN, never that it is the
  candidate you meant to test - the pin travels inside the tree. NUC run
  `20260915T152553Z` passed against the superseded candidate for exactly this
  reason. Compare the gate-computed root against a value held OUTSIDE the tree,
  and check that the `ubsan_*` metadata keys are PRESENT, since a stale gate
  script omits them rather than printing them wrong.
- A test result may never serve as its own expected answer. Two oracles in this
  phase reported green while losing venue evidence.
- The quickstart was the least reliable page on the venue's documentation site.
  Endpoint reference pages carried the accurate contract.

## PHASE 5 ACCEPTED ON THREE ENVIRONMENTS — Pi gate carried — 2026-09-15

This section supersedes every older section below it, including the CODEX
TAKEOVER section that follows. Those remain as an audit trail and must not be
read as current status.

### What is done

Every P1 and P2 from the 2026-09-12 acceptance review is corrected, every
approved material contract is encoded, and the source is frozen.

- correction range: `65cadc2..85a2d26` on branch `v2/phase-5-corrections`;
- frozen source root
  `681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`, pinned in
  all three Phase 5 gate scripts;
- the last open material contract, native versus synthetic observation identity,
  is approved and implemented (architecture 10.15). Sequence deduplication now
  searches the native domain only; synthetic replay is caught by the retained
  request receipt and by cumulative-by-value adjudication. Zero bytes added;
  retained result VALUES change, so kernel-codec and state-snapshot schemas move
  V6 to V7 and the golden corpora were regenerated as a reviewed migration;
- comment pruning is finished at the reviewed density: `v2_kernel.cpp` 13%,
  `v2_gateway.cpp` 5%, `v2_venue_simulator.cpp` 9%, delivery tests 1%. Reasoning
  lives in architecture sections 9.4, 9.5 and 10.9 through 10.15.

### Verification at the frozen root

- host Release 29/29, no warnings from project targets;
- host Debug ASan+UBSan 29/29, configured `-fno-sanitize-recover=undefined` and
  run under `UBSAN_OPTIONS=halt_on_error=1`;
- model 109/109, unmodified;
- `git diff --check` clean;
- ARM64 Linux container gate `20260915T134221Z`: PASS, gate-computed root
  matching the pin, Release 29/29, instrumented 26/26, sanitized 26/26, UBSan
  negative probe diagnosing and exiting nonzero, `TradingState` 81,360 and
  maximum canonical snapshot 65,451;
- lifecycle reachability: 42 signatures, conflict a sink, no unsettled dead end;
- horizons, nominal 48 seeds x 40 cycles: zero capacity refusals, zero orders
  reclaimed while unresolved, 17 groups reclaimed, max order age 123 versions,
  both attempt bounds reached;
- horizons, conflict-inducing: peak conflicts 1, max order age 210 versions,
  zero orders vanished while unresolved.

- x86-64 NUC gates `20260915T134322Z` (account `bulbazar`) and
  `20260915T154328Z` (account `vishal`, synced independently by the operator):
  both PASS, gate-computed root matching the pin, Release 29/29, instrumented
  26/26, sanitized 26/26, UBSan negative probe exit status 1. Layout
  BYTE-IDENTICAL to ARM64: `TradingState` 81,360, maximum canonical snapshot
  65,451, `ApplyResult` 112, `ClusterOrder` 48.

BEFORE RUNNING ANY GATE, compare the tree's computed root against the value in
THIS FILE, not against the pin inside `deploy/`. The pin travels with the source,
so a stale tree agrees with itself and passes. NUC run `20260915T152553Z` did
exactly that against the superseded candidate. Each NUC account has its own
`~/aeron-raft-pi-phase5-nuc` and its own podman storage, so a tree synced for one
user can sit at an old commit indefinitely, invisible to the other.

### Acceptance decision

Phase 5 is ACCEPTED on host, ARM64 Linux container, and x86-64 NUC at the frozen
root, by explicit decision on 2026-09-15, with ONE CARRIED OBLIGATION: the
physical Pi gate. G-025 through G-034 are closed for this candidate and Phase 6
may begin.

The grounds, so a later reader does not have to reconstruct them: the ARM64
container gate runs a real aarch64 kernel, so the logic and the byte layout are
already proven on ARM64 - what the Pi adds is PHYSICAL HARDWARE and CLEAN POWER
EVIDENCE, not any unverified behaviour. The root is frozen, so the Pi will test
exactly the source the other three tested.

The exception is scoped to the Pi being unavailable. It does not license
accepting a future phase on partial target evidence.

### The carried obligation

THE PHYSICAL RASPBERRY PI 5 GATE, at the frozen root. The Pi (`vishalkapi`,
100.84.151.116) was offline for this entire session, last seen roughly 12 hours
before the other gates ran, so the gate was never attempted against this source.
Three passes do not imply the fourth. IF IT FAILS, THE ACCEPTANCE IS VOID AND
PHASE 5 REOPENS.

```sh
# development host, once the Pi is reachable
cd /Users/vishaljha
rsync -av --delete --exclude build/ --exclude 'build-*/' --exclude .git/ \
  aeron-raft-pi/ bhaikapi@vishalkapi.local:~/aeron-raft-pi/

# on the Pi, after `sudo -v`
cd ~/aeron-raft-pi
./deploy/run-v2-phase5-pi-gate.sh --yes
```

Verify the tree's root before running; it must be
`681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f`. Clean power
evidence - `throttled=0x0` before and after, plus temperature - is part of the
record. Then append the result to the Pi section of
`docs/evidence/V2_PHASE5_CORRECTED_ACCEPTANCE.md` and remove the outstanding-
obligation wording from its status line.

Until then Phase 5 remains outside the MVP boundary in every other respect: no
real venue connectivity, no strategy logic, no netting, no WAL implementation,
and no full Raft integration.

Until all four environments record the same root, Phase 5 stays REOPENED, G-025
through G-034 stay open, and Phase 6 does not begin.

### Protected paths, unchanged

`model/`, `docs/README.md`, `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, and ADR 0002 remain
uncommitted and untouched. Never `git add .` or `git add -A`; stage explicit
Phase 5 paths only.

## CODEX TAKEOVER — Phase 5 corrections remain open — 2026-09-14

This section supersedes every older `NEXT` section below it. Claude stopped at
commit `5b9eaa2` on branch `v2/phase-5-corrections`, with an uncommitted
multi-slot conflict test and its architecture text. Those two edits are preserved
but are not correct enough to commit: eight templates in one active executable
policy do not prove an eight-conflict cluster-lifetime bound, because policy
rotation can introduce different nonzero `LogicalOrderSlot` values while old
conflicts remain retained.

### Verified checkpoint

- committed correction range: `65cadc2..5b9eaa2`;
- protected dirty paths remain `docs/README.md`,
  `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
  `docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, ADR 0002, and `model/`;
- current dirty Phase 5 paths additionally include
  `docs/V2_ARCHITECTURE_CONTRACTS.md`, `src/v2_kernel.cpp`, and
  `tests/v2_gateway_delivery_tests.cpp`;
- current-tree host Release passed 29/29 after a build;
- current-tree Debug ASan+UBSan passed 29/29 after a build;
- the protected Python model passed 109/109 and was not modified; and
- `git diff --check` was clean before the new intentional RED described below.

These green runs are a reproducible baseline, not corrected Phase 5 acceptance.
They predate the permanent counterexamples added during this takeover and do not
replace fresh ARM64, Pi, and NUC gates after the source is frozen.

### New P1 finding — the venue-event sweep mixes identity domains

`VenueCursor::expired_below` assumes every observation carrying the same
`(VenueId, VenueSessionId)` belongs to one monotonic venue sequence. The Phase 5
shell violates that assumption: native acknowledgements and fills use the
venue's sequence, while `effect_unknown` and reconciliation observations are
gateway-authored and use synthetic high ranges. The capacity sweep computes a
midpoint from those mixed values and treats everything below it as expired.

Concrete public-command trace now pinned in
`test_a_fresh_fill_cannot_be_expired_by_gateway_authored_control_sequences`:

```text
native acknowledgement 7,100
  -> repeated cancel/reconcile evidence around 900,000
  -> capacity sweep advances the shared floor above 8,000
  -> fresh native fill 8,000 with a new ExecutionId
  -> venue_event_expired (wrong)
```

The first RED returned code 59, `venue_event_expired`. The smallest GREEN removes
fill admission from this cursor. That is correct because fill idempotency is
already `(VenueId, ExecutionId)`, and fill receipts are deliberately never
reclaimed while their order is retained. The focused gateway-delivery suite then
passed.

The next permanent RED applies the same reachable trace to a fresh cancellation
response. It currently fails with code 59. This proves the fill change is a
containment, not closure: the shared cursor can discard any fresh native control
response after unrelated gateway-authored evidence advances it.

There is a second independent unsoundness even if gateway-authored values are
separated. The sweep advances to a midpoint of the values already observed. A
transport may deliver native sequence 100 before delayed native sequence 50;
seeing 100 is not proof that every value below 100 is complete. Only a
venue/session replay barrier or a proven contiguous prefix can establish such a
floor. Arithmetic over a sparse observed set cannot.

### Recommended replacement — proof-carrying reclamation

Do not patch the midpoint or reserve disjoint number ranges. Both retain the
false premise that a large observed number proves older evidence cannot still be
fresh. Replace it with a reference-and-acknowledgement rule:

```text
receipt is economically unneeded
  AND its owning effect is settled or gone
  AND the gateway has observed the committed command result
  => receipt payload may be compacted
```

Why each term is required:

1. Economic unneededness prevents reclaiming fill identity or materialization
   provenance while a live order can still consume it.
2. Effect settlement prevents deleting the only exact answer while an unknown or
   reconciling effect may still receive a duplicate or final response.
3. Gateway commit acknowledgement prevents the cluster from dropping a result
   while the gateway is retrying the identical observation after a lost Raft
   client response. “Applied” and “the producer learned it applied” are different
   facts.

Recommended zero/unfavourable alternatives:

- Merely exempt fills: necessary containment, but fresh acknowledgements and
  cancellations remain losable, as the second RED proves.
- Use separate numeric ranges: prevents collision but not reordered native gaps;
  a high value is still not a contiguous-prefix proof.
- Keep the midpoint and add more capacity: delays the same semantic failure and
  consumes snapshot headroom without making reclamation correct.
- Never reclaim: safe for identity but restores P1-5, where legal serial cancel
  cycles consume completion capacity reserved for a live order's real fills.
- Refuse further cancels: preserves memory by blocking a risk-reducing action and
  is unacceptable without an equally strong emergency cancel path.

### Material contract approved and implemented — 2026-09-14

The user approved all five choices below before canonical bytes changed:

1. Remove/deprecate the heuristic `VenueCursor` floor and reserve
   `venue_event_expired`'s numeric code without emitting it from new state.
2. Record the observation's `EffectId` in the currently unused metadata words of
   a venue-event `ApplyResult`. Object size remains 112 bytes, but canonical
   result bytes change. This supplies the ownership edge the sweep currently
   guesses.
3. Add a monotonic gateway result-acknowledgement coordinate, advanced by a
   replicated command from the stable gateway `CallerId`. A receipt cannot be
   compacted before this coordinate covers its request identity.
4. Replace Decision B's receipt COUNT with a monotonic per-order
   `last_evidence_state_version`. Counts can decrease during reclamation and then
   return to the old value after new evidence: an ABA that can make a stale query
   appear comparable. A revision never moves backward.
5. Require a separate gateway-local venue finality fact—query window complete AND
   ordered after relevant sends—before a query may release risk or create
   conflict. The per-order revision only proves cluster evidence did not change
   while the query was in flight; it does not prove the venue's snapshot was
   final.

Implementation is now GREEN on the host:

- kernel/result schema V6 reserves historical `venue_event_expired` code 59 but
  never emits it; the heuristic venue cursor and its eight-slot arena are gone;
- command kind 18 is `AcknowledgeGatewayResults`, with an eight-byte inclusive
  request-prefix body. It can acknowledge only an earlier request of the same
  stable `CallerId`, advances monotonically, and cannot regress the cursor;
- an applied venue-event result stores its exact `EffectId` in metadata word 0,
  so reclamation follows an ownership edge rather than a venue-sequence guess;
- a venue-event receipt stays pinned until the producer prefix covers its
  request and its exact effect is settled/superseded. Current unresolved effects
  and all fill receipts remain pinned fail-closed;
- `ClusterOrder::last_evidence_state_version` replaces the receipt-count query
  barrier. It advances on accepted evidence and on state-changing fill refusals,
  survives receipt compaction and snapshot restore, and cannot exhibit count ABA;
- `VenueQueryView::ordered_after_relevant_sends` is independent of
  `window_complete`; both must be true before `classify_query` submits an answer;
- simulator polling is non-destructive. A gateway restart replays unacknowledged
  canonical observations, while explicit acknowledgement removes them only
  after the committed result is observed; and
- permanent tests cover the original fresh-fill and fresh-cancel failures,
  unacknowledged-result backpressure, safe post-ack reclamation, monotonic query
  revision, venue finality, and crashes after poll, obligation creation, and
  cluster apply.

Measured rather than estimated: `TradingState` is 81,360 bytes with 560 bytes of
headroom; maximum canonical snapshot is 65,451 bytes with 85 bytes of headroom;
`ClusterOrder` is 48 bytes; `ApplyResult` remains 112 bytes; and the result frame
remains 128 bytes. Release is 29/29, full Debug ASan+UBSan is 29/29, and the
horizon report passes. ARM64, Pi, and NUC gates have not yet been rerun for this
correction.

The four follow-on acceptance corrections are also implemented:

- the per-execution oracle rejects a never-booked execution unless its owning
  order remains conflicted and its exact `(VenueId, ExecutionId)` result receipt
  remains in replicated state; resolved-and-swept is no longer an excuse;
- restart recovery excludes `OutboxState::incomplete` from its suspect set,
  matching the reducer and pre-send guard's terminal/non-deliverable meaning and
  avoiding a barrier no transport response can clear;
- a public-command policy rotation exposes logical slot 9 after eight retained
  conflicts. Authorization is refused by `outbox_capacity_exceeded`, proving the
  measured eight-order bound comes from sixteen submit-plus-cancel outbox units,
  not one policy's template count; and
- the shared Linux target gate compiles UBSan non-recovering, exports
  `halt_on_error=1`, records both settings, and runs a permanent signed-overflow
  negative probe which must diagnose UB and terminate nonzero.

The focused gateway-delivery suite is GREEN after each RED/GREEN increment. The
UBSan probe reproduced the old false-green shape with exit status 0, then exited
134 under the corrected settings. Full host suites and every target gate still
need to be rerun after documentation/comment cleanup and source freeze.

### Additional adversarial corrections — 2026-09-15

Three independent audits found that the proof machinery still shared causes
with the behavior it claimed to verify. Each counterexample is now a permanent
test and each implemented correction is GREEN in its focused suite.

1. Recovery inspected a process-local `awaiting_response` flag before the
   current replicated outbox state. A cancel reconciliation can resolve risk and
   change an older submit from pending to `incomplete` after startup scanning;
   the stale local flag then blocked recovery forever. Recovery completion now
   treats the current replicated effect state, or safe post-sweep absence, as
   authoritative. The test reaches the transition through public cancel,
   unknown, and reconciliation commands rather than mutating a fixture.
2. The execution oracle counted bookings from `ApplyResult::filled_quantity`.
   Its per-order total and per-execution ledger therefore repeated the same
   answer and could both agree while the risk-account position did not move.
   It now samples the authoritative position immediately around every fill
   apply, before simulated result loss, attributes the delta by execution and
   side, and checks the signed total against final position movement. Separate
   negative controls reject a successful-looking receipt with no mutation.
3. The simulator cleared scheduled observations on send and query disconnect.
   That deleted unacknowledged input before poll or after poll/before commit,
   while the old oracle learned executions only from the same damaged queue.
   Disconnect now changes the session for future evidence without changing
   ownership of prior evidence. A separate simulator execution ledger records
   venue-book mutation before delivery; the matrix requires every such execution
   to appear in replayable delivery with identical identity and economics.
4. Snapshot validation checked V6 proof fields individually but not as a graph.
   It now rejects an applied venue-event receipt whose retained effect belongs
   to another decision, and rejects a retained order whose evidence revision is
   older than an applied fill/event receipt for that decision. Encoder and
   forged-decoder tests cover the impossible states.
5. Gateway receipt expiry could jump across an uncommitted sequence gap when a
   later acknowledgement-command receipt was reclaimed. All gateway receipts
   above the acknowledged producer prefix are now pinned, regardless of command
   kind. Applied full fills and state-changing fill conflicts retain their exact
   execution identity and owning decision through group and receipt pressure.
6. A completely full receipt arena originally refused the acknowledgement
   command needed to release it. Valid monotonic acknowledgements now receive a
   bounded control admission path: reclamation is evaluated against the proposed
   prefix, then the command advances state and stores its own ordinary receipt.
   Invalid or regressive acknowledgements receive no special admission.
7. Retaining every fill-tagged response pinned acknowledged duplicate deliveries
   forever. Replay-only `already_applied` and identity-conflict responses now
   compact after acknowledgement because the original applied receipt reproduces
   their answer. The first exact capacity/quantity-conflict receipt stays pinned
   while its order exists; duplicates of that same evidence do not. Every
   `fill_quantity_conflict` path now leaves a sticky conflict and advances the
   evidence revision, so the result code has one snapshot-verifiable meaning.

One material correction is deliberately not encoded yet. Native venue control
events and gateway-synthesized reconciliation events still share the same
`(VenueId, VenueSessionId, VenueSequence)` deduplication domain. The recommended
zero-size change derives a two-valued origin domain from observation kind,
stores it in venue-event result metadata word 1, and acknowledges simulator
input by exact canonical observation rather than the three-number tuple. User
approval is required because retained canonical result bytes change.

### Crash-cut obligation which the sweep rule does not replace

`GatewayEvidenceState` is local, and `SimulatedVenue::poll` destructively removes
an observation. A crash after poll but before committed apply loses the canonical
fill command; the venue query exposes only a cumulative quantity and cannot
reconstruct its `ExecutionId`. Therefore restart reconciliation can discover a
gap but cannot supply the missing execution evidence.

The Phase 5 seam must model one of two real guarantees:

- replayable venue input whose cursor is acknowledged only after the observation
  command commits; or
- durable local journalling before acknowledging non-replayable input.

Recommend the first for the simulator because it proves the transactional
ordering without pulling WAL implementation into Phase 5. The real adapter must
select replay or durability from actual venue capability later. Add crash cuts
after poll/before obligation, after obligation/before proposal, and after
apply/before result before closing P1-3.

### Exact next steps

1. Obtain the material identity-domain decision and implement it by RED/GREEN.
2. Finish comment pruning while retaining only invariants that are unsafe to
   infer locally; keep the full reasoning in architecture docs.
3. Run full Release, freshly configured fatal ASan+UBSan, lifecycle/horizon/matrix
   reports, model 109/109, and `git diff --check`.
4. Review the complete diff, commit only explicit Phase 5 paths, compute and pin
   the new source root, then run fresh ARM64 container, Pi, and NUC gates.
5. Write superseding Phase 5 acceptance evidence and tag the corrected checkpoint;
   preserve `v2-phase5-accepted` as historical evidence.

## CLAUDE ACTION HANDOFF — Phase 5 acceptance review failed — 2026-09-12

This section supersedes the `CODEX REVIEW REQUEST` and every older Phase 5
`NEXT` section below it. The older sections remain in the file as an audit trail;
they must not be read as current approval.

### Executive decision

Treat Phase 5 as REOPENED. Do not begin Phase 6 implementation and do not describe
G-025 through G-034 as closed until the findings below are corrected and the
candidate is re-gated.

Reason: the review found direct reducer and pre-send correctness defects, a
gateway-restart hole, and test oracles that can report green while losing venue
evidence. Four-platform execution proves that the tested program behaves the same
on those platforms; it cannot compensate for a test oracle that accepts the wrong
answer.

Do not delete or move `v2-phase5-accepted`. It is historical evidence for the
candidate that was accepted at the time. After corrections, create a new
superseding checkpoint and state explicitly why the earlier acceptance was
superseded. Moving the old tag would destroy the audit trail and make the old
evidence claim a different source than the source it actually tested.

### Git and evidence identity verified by the review

- The review checkout was DETACHED at
  `ee2c9d9791d597008f7782e8b9e407914fd485cb`, not attached to
  `v2/phase-4-risk-orders-outbox`. The named branch still pointed at `aadc2f5`.
  This matters because a later commit made without first choosing a branch would
  be easy to orphan even though its files remained in the object database.
- `v2-phase5-accepted` is an annotated tag which peels to
  `04329fad05837e7f2c6e9d8d075ba8e5ede90700`. Preserve it.
- The canonical Phase 5 source-root recipe, including `LC_ALL=C`, reproduced
  `b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`
  locally. Therefore the source-root value itself is not one of the findings.
- The target PASS claims were present in repository documentation, but the Pi,
  container, and NUC raw evidence bundles were not present on the review host.
  Therefore this review verified repository consistency, not the contents of the
  remote raw bundles. Do not turn that limited statement into a claim that the
  target runs did not happen.
- No build or test was run during this review because the handoff explicitly
  requested analysis before further construction. The findings are static and
  include concrete reachable traces; the first implementation step is to convert
  each trace into a permanent RED test.

Protected dirty paths at the review checkout were exactly:

```text
 M docs/README.md
?? docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md
?? docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md
?? docs/adr/0002-two-machine-pi-nuc-deployment-topology.md
?? model/
```

Preserve all five. Never use `git add .` or `git add -A`; stage only explicit
Phase 5 paths. The reason is operational, not stylistic: a directory-wide add has
already swept unrelated research and topology work into a Phase 4 commit once.

### Severity vocabulary used below

- P1 means an acceptance blocker: a reachable path can send under stale
  authority, lose or strand economic evidence, release risk on insufficient
  evidence, or let the gate pass while such a defect exists.
- P2 means the implementation may still be correct on a narrower path, but the
  current proof or documented claim is materially stronger than the evidence.
- P3 means documentation, reproducibility, or maintainability drift. It does not
  independently make economics wrong, but it makes a later review easier to get
  wrong.

The labels describe blast radius, not implementation effort. Several P1 fixes are
small; they remain P1 because their failure mode is economic divergence.

### P1-1 — the final pre-send guard can authorize against stale control

Evidence: `evaluate_pre_send` in `src/v2_gateway.cpp` compares the materialized
effect's executor generation and kill epoch only with the separately supplied
`GatewayControlView`. It does not compare that view with
`TradingState::executor_generation`, `TradingState::kill_epoch`, or the state's
current strategy mode. `plan_reconciliation` already performs the first of these
coherency checks, so the delivery and reconciliation halves currently apply
different authority rules.

Reachable counterexample:

1. authorize effect E under executor generation 3 and kill epoch 2;
2. cache a matching `GatewayControlView{3, 2, normal}`;
3. apply `AdvanceKillEpoch`, so the supplied `TradingState` now has a newer kill
   epoch and is halted;
4. call `evaluate_pre_send(new_state, E, old_control, ...)`;
5. E passes because the effect and the stale view still agree with each other.

The result is unsafe because the final guard has authoritative state containing
the revocation and nevertheless authorizes the external side effect. Comparing
two matching stale values does not prove either is current.

Required RED: preserve an old matching view, advance kill epoch in the state, and
assert that the old effect is refused. Repeat for executor generation and strategy
mode so the test proves one coherent control snapshot rather than one field.

Recommended GREEN: derive current generation, epoch, and strategy mode directly
from the same `TradingState` used to find and materialize the effect. If a separate
view remains necessary for the future process boundary, require and verify a
state-version/commit-watermark identity proving the view and outbox came from one
coherent read. Do not merely add one more caller convention; the existing defect
is precisely that the type permits an incoherent pair.

Acceptance condition: the last callable decision before transport refuses when
either the state or its coherent authority snapshot has advanced since effect
authorization.

### P1-2 — gateway restart can blindly resend a possibly accepted submit

Evidence: `GatewayDeliveryState` is deliberately local and disposable. After it
is reset, `plan_delivery` sees a replicated `pending` submit with no attempt
record and schedules it as a first send. `plan_reconciliation` targets a
`dispatch_authorized` order only when the now-lost local attempt count is
exhausted. The header says a restart rebuilds by reconciliation, but no recovery
mode, startup barrier, or state seeding operation exists.

Crash counterexample:

1. the cluster authorizes submit E and retains it as pending;
2. the gateway sends E and the venue accepts it;
3. the gateway crashes before the acknowledgement is applied to the cluster;
4. the restarted gateway has empty delivery bookkeeping while cluster state still
   says pending;
5. `plan_delivery` schedules E immediately instead of querying first.

Stable `EffectId` reduces the damage only if the venue deduplicates that identity
across the exact reconnect/session failure being tested. Relying on simulator
deduplication is not the same guarantee as reconciliation-before-resend, and the
architecture explicitly chose the latter for ambiguous risk-increasing sends.

Required REDs: cut and restart the gateway (while preserving cluster and venue
state) before send, after venue acceptance but before local attempt recording,
after local recording but before venue observation, and after observation
submission but before its cluster result. The post-possible-send cuts must query
or otherwise establish absence before a risk-increasing resend. They must also
block new risk until the startup recovery barrier completes, as G-032 requires.

Recommended GREEN: add an explicit gateway recovery mode. At startup, treat every
replicated unresolved submit as potentially attempted unless durable local proof
says it was never sent. Rebuild open-order/relevant execution state, then enable
ordinary delivery. A cancel may retain its approved idempotent-redelivery rule;
the submit path may not inherit it.

Acceptance condition: clearing every gateway-local structure at every crash cut
cannot produce a blind risk-increasing resend or allow new risk before recovery.

### P1-3 — venue response receipt and cluster evidence commit are conflated

Evidence: the in-process delivery shell calls `record_response` before applying a
venue observation, and the timeout path ignores the observation apply result
before clearing the local awaiting-response flag. That is harmless only while
`apply` is a synchronous infallible function call. Phase 6 makes proposal,
leadership, commit, and apply separate outcomes.

Failure trace under Raft:

1. the venue answers E, so the gateway correctly stops the venue-response timer;
2. the gateway proposes the canonical observation to the current leader;
3. the proposal response is lost or leadership changes before the gateway learns
   the committed result;
4. local `awaiting_response` is already false, while replicated E remains pending;
5. submit redelivery is forbidden, ambiguity is no longer scheduled, and
   reconciliation does not target the shape;
6. the effect is stranded without an owner retrying its evidence command.

The venue answer and the Raft acceptance are two distinct acknowledgements. The
first completes transport; only the second completes delivery of evidence to the
authoritative state machine.

Required RED: inject loss of the command response and leader change after the
venue answer but before confirmed commit; retry the identical canonical command
and request identity; assert one economic mutation and eventual terminal evidence
delivery.

Recommended GREEN: keep a bounded gateway-local queue of canonical observation
commands until a committed retained result is observed. Retrying must preserve
the original request identity and bytes. Backpressure when this queue is full must
stop additional external sends before evidence can be dropped.

Acceptance condition: a venue answer can stop its transport timeout without
discarding the separate obligation to commit that answer into the cluster.

### P1-4 — rejected-cancel success reconciliation is still impossible

Evidence: `order_rejected` for the current cancel produces
`(order=reconciling, effect=rejected)`. `cancel_reconcilable` accepts only
`(reconciling,reconciling)` or `(submission_unknown,unknown)`. The later
resolved-effect guard also refuses the rejected effect. Existing tests cover only
the opposite query answer: “the order is still working,” encoded as
`order_reconciled`.

Reachable counterexample:

1. request cancel C;
2. receive `order_rejected(C)`, which correctly does not prove the order is live;
3. query the venue and receive authoritative evidence that the order is cancelled;
4. encode that evidence as `cancel_reconciled(C)`;
5. apply refuses it, so the residual reservation and slot remain blocked forever.

This is not an exotic branch. Section 10.3.6 specifically motivates the rejected
state with a duplicate rejection overtaking the original successful
cancellation. The implementation then refuses the final evidence for the exact
case used to justify the state.

Required REDs from `(reconciling,rejected)`:

- equal cumulative `cancel_reconciled` must cancel and release residual risk;
- below-booked final evidence must retain risk and enter conflict;
- above-booked evidence must remain unresolved as missing fill evidence; and
- snapshot restore before each answer must preserve the same outcome.

Recommended GREEN: admit the rejected-current-cancel/reconciling-order pair to
the cancel reconciliation adjudication. Keep ordinary resolved-effect rejection
for every unrelated event; widen only the explicitly final reconciliation path.

Acceptance condition: both possible truths after an opaque cancel rejection —
“still working” and “cancelled” — have a bounded path out, without treating the
rejection itself as proof of either.

### P1-5 — repeated cancel/reconcile cycles invalidate the venue-event reserve

Evidence: `future_venue_events_for_order` budgets a finite three-control-event
tail for a live/partially-filled order. But the accepted lifecycle permits this
cycle repeatedly:

```text
RequestCancel
  -> order_rejected
  -> order_reconciled(still working)
  -> RequestCancel with a fresh generation
```

The outbox record is reused, so outbox capacity stays healthy. The two applied
venue observations from every cycle remain request receipts pinned to the still
unresolved order. `apply_request_cancel` does not re-check completion headroom.
After enough cycles, `accepted_venue_event_count` reaches 64 and a subsequent
valid fill is refused at the capacity check before the kernel can account it or
mark its owning order conflicted.

The defect is more serious than a typed availability refusal: the venue has
executed a fill while the cluster position remains unchanged, and the order may
still appear live rather than conflicted. The original authorization reserve was
supposed to make that impossible.

Required RED: use only public commands to repeat the cycle until close to the
venue-event ceiling, then deliver the remaining valid fill fragments. The test
must prove every admitted order always retains enough event capacity to account
all remaining minimum-lot fills and the one final control path.

Do not apply a local arithmetic patch before choosing a retention contract. The
safe choices have different semantics:

1. reclaim superseded cancel-generation event identities after a declared
   retry/reconnect horizon while retaining a non-reusable watermark/tombstone;
2. bound cancel re-authorization cycles and enter explicit operator escalation
   before completion headroom is consumed; or
3. increase/repartition canonical state, which reopens the reviewed size ceiling
   and snapshot budgets.

Reject the tempting fix “refuse RequestCancel when headroom is low” unless an
equally strong emergency cancellation path exists. Cancel is risk-reducing; using
capacity protection to prevent it can preserve bookkeeping by abandoning actual
exposure.

Acceptance condition: no legal repetition of the serial cancel lifecycle can
consume capacity reserved for already-live fills or final evidence.

### MATERIAL DECISION A — how fills behave after an order enters conflict

Current implementation: `apply_observe_fill` refuses every fill when the order is
already `conflict`.

Why the current behavior is attractive: conflict remains a literal sink, so no
automatic observation can make the order look healthy or reclaimable. This
protects the operator-visible disagreement.

Why the current behavior is economically dangerous: a unique execution carrying
venue identity, execution identity, side, price, quantity, and fee is stronger
evidence than the order-level cumulative snapshot that commonly created the
conflict. If its quantity still fits the retained reservation, refusing it leaves
the known position and remaining hold wrong even though the kernel has all fields
needed to book it safely. Further real fills then widen the divergence the system
claims to expose.

RECOMMENDATION, awaiting user approval: book every unique fill that fits inside
the still-retained reservation, update position and holds atomically, but preserve
`order.state == conflict` and the conflicting effect state. Even when the final
reserved atom fills and the reservation becomes released, keep the order group
unreclaimable until explicit operator resolution because the historical
disagreement still exists. Continue to refuse any quantity outside the authorized
remaining hold and keep conflict visible.

This separates two properties that the current code conflates:

- `conflict is sticky` means later evidence cannot automatically declare the
  dispute solved; and
- `valid execution accounting continues` means authoritative fill evidence is
  not discarded merely because a dispute already exists.

Do not implement this economic change until the user approves it. The RED should
then assert position, reservation, risk totals, sticky conflict, deduplication,
snapshot restore, and non-reclamation together.

### MATERIAL DECISION B — query freshness and finality are not represented

Current rule: a reconciliation total below locally booked quantity becomes
permanent conflict; `not_found` or an agreeing final cancellation can release
risk. That rule is sound only if the answer is comparable with the cluster's
evidence — meaning it is ordered after the relevant sends and complete through a
venue execution watermark.

Current `VenueQueryView::authoritative` says only that the venue claims the query
window is complete. `VenueObservation` carries a gateway-authored event identity,
not a native query watermark. One outstanding query orders queries relative to
other queries, but does not order a query relative to fills arriving concurrently.

Truthful stale-answer counterexample:

1. cancel outcome is unknown and the gateway starts a query;
2. the venue snapshots the order as working with cumulative fill zero;
3. a fill executes and its submit-scoped observation commits in the cluster;
4. the delayed query answer arrives with cumulative zero;
5. the reducer sees “venue below cluster” and enters permanent conflict even
   though neither side contradicted the other at a common time.

The same missing ordering proof makes `not_found` unsafe after a possibly in-flight
submit: absence at the query snapshot does not prove the earlier send cannot arrive
later. It also makes “still working” insufficient proof that the last in-flight
cancel failed.

RECOMMENDATION, awaiting user approval: reconciliation may release risk or create
a permanent conflict only when the gateway has venue-specific proof that the
answer is ordered after every relevant send and complete through the relevant
execution stream watermark. Without that proof, classify the query locally as
incomplete/unavailable and submit no economic observation.

For the simulator, model the ordering barrier explicitly rather than setting an
unconditional Boolean. For a real adapter, the proof may be a venue monotonic
sequence, a session barrier, an executions cursor plus order-query watermark, or
a documented venue idempotency/finality guarantee. Do not invent a universal
wire field before a venue supplies the semantics it would carry.

Do not implement this material reconciliation rule until the user approves it.
Once approved, document the exact meaning of “authoritative” in architecture and
write stale-query/fill-overlap tests before touching the reducer.

### Assessment of the five changed Phase 4 behaviors

1. REJECTED CANCEL -> RECONCILING: retain. An opaque business rejection does not
   distinguish already-cancelled, already-filled, absent, or still-working. Moving
   directly to live would convert ambiguity into false certainty. P1-4 must add
   the missing successful-cancel exit.
2. FILL QUANTITY CONFLICT MUTATES TO CONFLICT: retain the visibility goal, but
   make the result semantics explicit. A venue trade that cannot be booked must
   not disappear behind a non-mutating-looking refusal. The current receipt path
   also needs review because non-`applied` observations are not counted as applied
   venue identities and may be reclaimed without retaining the offending evidence.
3. FILL ON CONFLICT IS REFUSED: reject as currently implemented, subject to user
   approval of Material Decision A. Sticky lifecycle and continued valid fill
   accounting are compatible; one does not require freezing the other.
4. `order_reconciled` SPEAKS ABOUT CANCEL: acceptable for the MVP only if Material
   Decision B supplies finality. The observation's fact is “the venue still holds
   this order with cumulative quantity Q”; the current effect operation determines
   whether that resolves a submit or disproves a cancel. Split names would improve
   readability but would not create the missing watermark.
5. RECONCILE BEFORE REUSING A REJECTED CANCEL RECORD: retain in principle. Reuse
   is safe only after finality proves the prior cancel did not take and cannot take
   later. Without Material Decision B, overwriting the old effect record can make a
   late successful cancellation inapplicable, so the current proof is incomplete.

### P1/P2 — the 27-position lifecycle enumeration is not exhaustive

The new test is useful as a sampled regression inventory, but its current claims
must be narrowed or its algorithm replaced.

Why it omits reachable states:

- exploration discards every command whose `ApplyCode` is not `applied`;
- `fill_quantity_conflict` deliberately mutates the order to conflict while
  returning a non-applied code;
- therefore authorized-submit -> oversized-fill and cancelled-order -> late-fill
  conflict positions never enter the 27-position set;
- its `Signature` records only current order/effect/reservation categories and a
  filled/not-filled Boolean, omitting receipt occupancy, exact cumulative
  quantity, historical sibling effects, and effect generations; and
- visited-signature pruning expands only the first concrete state that maps to a
  coarse signature, so a later history with different future behavior is dropped.

Why its liveness assertion is insufficient: “no unsettled effect is a dead end”
currently means only that the coarse position has at least one outgoing
`ApplyCode::applied` edge. The edge may be a self-loop, may not be producible by
the real gateway, and need not lead to settlement. It also examines only the
current effect, while reclamation requires every sibling effect in the order group
to be settled.

There is an internal contradiction once refusal-mutations are included. An
oversized fill can create `order=conflict` with `effect=pending`. “Conflict is a
sink” correctly forbids automatic escape, while “every unsettled effect must have
an applied successor” would require an escape. The correct property is narrower:
every RECOVERABLE, non-conflict position must have a gateway-producible path to a
settled or explicitly escalated state; adjudicated conflict is intentionally an
operator-owned sink even if a historical effect remains nonterminal.

Required replacement proof:

1. retain every state-changing result, regardless of `ApplyCode`;
2. assert a contract-derived transition table over observation kind x order state
   x current effect state x operation, including expected code, mutation, economic
   delta, and destination;
3. separately explore reachable concrete representatives, preserving the state
   fields that affect future transitions;
4. prove reachability to settlement/escalation, not merely one outgoing edge;
5. check all effects belonging to the order group; and
6. cross-check that each claimed recovery edge is producible by the gateway, not
   only constructible as a direct kernel command.

Do not pin a new inventory count until the omitted refusal-mutation states and the
two material decisions above are settled. A changing count is evidence of a
contract change; forcing it back to 27 would hide the review result.

### P1 — the delivery harness can pass while losing fills

The fault matrix's fill oracle checks only `booked <= reported`. That prevents the
cluster inventing quantity, but it permits every reported fill to remain unbooked.
Its global allow-list treats `venue_observation_not_applicable`, `order_not_found`,
and identity conflicts as lossless without proving that the particular execution
was already booked or belonged to a safely reclaimed order. One conflicted order
also globally excuses all `fill_quantity_conflict` refusals.

Required correction: track every distinct `(VenueId, ExecutionId)` from venue
creation through cluster result. Each execution must end in exactly one of:

- booked exactly once with matching order and quantity;
- proven duplicate of that already-booked execution; or
- retained, order-specific conflict evidence explaining why it could not be
  booked within authorized risk.

A refusal-code allow-list cannot prove this because the same code can be safe for
one execution and evidence loss for another. The oracle must bind the explanation
to the execution and owning order.

### P1 — the reconciliation harness excuses reducer applicability defects

The harness labels every `venue_observation_not_applicable` reconciliation result
as `answered_ahead`. It does not independently verify that the answer's cumulative
quantity was actually above the cluster's booked quantity. P1-4 can therefore be
misreported as permitted missing evidence rather than fail the gate.

Required correction: compute the expected relation from pre-apply reservation
state and submitted answer, then compare the reducer result against that
independent expectation. Never infer why a refusal was correct from the refusal
code produced by the code under test.

### P2 — the simulator and saturation claims exceed their tests

- The fault selector chooses one fault category per operation, so cross-category
  combinations remain unreachable. This is documented as deferred, but the
  deferred-scope trigger says it is required before claiming G-033 in full.
  Therefore either compose the fault axes and remeasure, or narrow Phase 5's
  acceptance claim and keep G-033 open.
- Selecting the `loss` label for a cancel does not lose its cancellation response:
  the cancel branch schedules the response and returns before the submit loss
  branch. Counting selected labels therefore does not prove the named behavior
  occurred. Assert observable fault effects, not enum reachability.
- The “sixteen conflicts saturate the cluster” harness uses one logical slot and
  asserts at most one unresolved order. It proves that conflict is retained under
  that workload, not that sixteen concurrent conflicts reach refusal. The number
  sixteen is a valid static consequence of equal 16-slot arenas and was shown to
  have identical layout on ARM64 and x86-64; call it an ANALYTIC BOUND unless a
  multi-slot test actually reaches all sixteen and the seventeenth refusal.

### P2 — snapshot validation accepts gateway-local outbox states

Architecture 9.1.2 says `claimed` and `attempted` are gateway-local and that
snapshot validation must reject them because the reducer cannot produce them.
`valid_state` currently accepts the entire contiguous `OutboxState` range,
including both values.

Required RED: encode and decode candidate states containing each forbidden value
and require `invalid_state` without mutating the destination. Recommended GREEN:
separate “known enum value” from “legal replicated value”; retain the numeric enum
values for schema stability while excluding them from canonical `TradingState`.

### P1 verification defect — UBSan diagnostics are not forced to fail the gate

The shared target gate enables `-fsanitize=undefined` but neither compiles with
`-fno-sanitize-recover=undefined` nor exports a fatal UBSan runtime option. It
explicitly configures ASan leak behavior only. A sanitizer gate must make every
diagnostic produce a nonzero test outcome; otherwise `ctest --output-on-failure`
can retain no visible output for a process that recovered and exited zero.

Required correction: pin non-recovering UBSan behavior in the shared gate and
record the setting in metadata. Add a gate self-test or a tiny purpose-built
negative probe which proves a UBSan finding makes the gate fail; do not rely on
configuration inspection alone. Re-run every claimed sanitizer environment after
the change because the old green result did not establish this property.

LeakSanitizer remains a separate deferred coverage decision. Making UBSan fatal
does not silently enable LSan and therefore does not contradict the explicit
cross-architecture `detect_leaks=0` choice.

### P2/P3 documentation and evidence corrections

- `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md` still marks G-025 through G-034
  open, while the acceptance document says G-025 and G-026 are closed. Until this
  review is remediated, keep them open and add the exact blocking evidence rather
  than merely toggling boxes.
- The acceptance document still says it “currently records one” environment while
  its status says four. Correct the stale sentence when the document is rewritten.
- `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md` still calls the state ceiling provisional
  pending NUC validation even though the recorded NUC run discharged that caveat.
- Architecture 10.6 says fills must precede cancel authorization. That is false as
  a kernel invariant: `find_order_by_effect` deliberately recognizes retained
  historical submit effects, and late submit-scoped fills preserve an unknown
  current cancel. Correct the document; the old fixture merely lost the saved
  submit `EffectId` after requesting cancel.
- The annotated acceptance tag points at the source candidate before later gate,
  NUC, sanitizer-policy, and final evidence-document commits. This can be retained
  as a source-candidate checkpoint, but the superseding acceptance tag must point
  at a commit containing the final gates and final evidence so a checkout of the
  tag explains how it was accepted.

### Direct answer for Phase 6: the current seam does not survive unchanged

The deterministic economic reducer can survive commit-before-apply. Its inputs
are canonical and its mutation is already deterministic. The statement
“the gateway reads a post-apply state and only cares that it is authoritative” is
nevertheless insufficient for the execution boundary.

Phase 6 must add all of the following:

1. A COHERENT COMMIT/APPLY VIEW. Delivery may consume an effect only after its log
   entry is committed and applied, and its control fields must come from that same
   state view.
2. MONOTONIC STATE CONSUMPTION. `reclaim_delivery_attempts` treats an absent effect
   as swept. If the gateway reads committed/applied index 100, then reroutes to a
   lagging voter at index 90, it can delete attempt history for an effect which has
   not actually disappeared. Catch-up to 100 then makes it look fresh and permits
   resend. Every individual read was committed; monotonicity is the missing
   property.
3. SEPARATE VENUE AND RAFT ACKNOWLEDGEMENTS. P1-3 explains why a venue answer must
   remain queued until its canonical observation command has a committed result.
4. A STARTUP RECOVERY BARRIER. P1-2 explains why disposable local state otherwise
   turns an ambiguous submit back into an apparent first send.
5. A LAST-MOMENT AUTHORITY FENCE. Executor generation, kill epoch, and mode can
   advance between plan creation and socket write. Planning cannot be the final
   check.
6. DISTINCT COORDINATES. `OrderEffect::committed_directive_index` currently holds
   an authorization `state_version` and participates in canonical payload bytes.
   Raft log index can advance for duplicates, no-ops, and configuration entries,
   so it is not the same coordinate. Preserve the existing field as a domain
   revision and carry commit provenance separately unless the user explicitly
   approves a byte-contract migration.

Required Phase 6 seam tests before implementation: append without commit, commit
without apply, apply after commit, leader change with a lost client result, lost
venue-observation proposal result, regressing follower read, gateway restart at
every send/response/commit cut, and kill/generation advance between plan and send.
Re-running the synchronous Phase 5 matrix with an `apply_is_committed` label would
not exercise any of these intervals.

### C++ quality and optimisation assessment

Keep the core bounded design. Fixed arrays, injected ticks, value-returning pure
planners, no allocator in the reducer, and exact payload reconstruction are good
MVP choices. Their worst cases are explicit and small enough to measure.

Do not optimize the repeated effect materialization yet. `plan_delivery` invokes
the guard, and the shell materializes selected effects again, costing roughly
`kMaxOutboxEffects * kMaxRequestReceipts` bounded comparisons per tick. That is
real duplicated work, but it is a performance cost rather than a correctness or
availability limit, and removing it prematurely risks creating two authorities
for whether a payload is sendable. Keep it in V2.1 until measurement shows it on
the critical path.

After correctness is restored, refactor `apply_observe_order_event`. Ten coupled
booleans feeding negated guard terms are difficult to review because state,
applicability, quantity adjudication, and economic mutation are interleaved.
Prefer explicit operation-specific transition functions or a reviewed transition
table, while keeping all capacity and arithmetic checks before mutation. This is
a maintainability refactor, not permission to alter bytes or economics silently.

### Comment cleanup requested by the user

The cleanup has NOT happened. At review time the eight Phase 5 C++ files held 999
full-line comments in 5,002 lines, about 20 percent. The original seven files were
unchanged since the earlier review; the lifecycle test added another 57 comment
lines.

Perform cleanup only after the RED/GREEN lifecycle corrections, because several
comments currently expose reasoning that tests fail to encode. Then remove most:

- historical narratives about how an earlier version was wrong;
- headings that restate the following function or assertion;
- multi-paragraph copies of architecture sections 9 and 10;
- rhetoric such as “the single most important”; and
- stale or false statements identified above.

Retain short comments only where the type system cannot express the constraint:

- sweep before any index lookup/reference borrow;
- terminal is not equivalent to risk-resolved;
- submit versus cancel retry asymmetry;
- proof required before a risk-increasing resend;
- ownership/publication and memory-order requirements;
- canonical-byte equality rather than struct-padding equality; and
- non-obvious checked-arithmetic or capacity-reserve derivations.

The target is not zero comments. The target is that every surviving comment
prevents a plausible future safety regression which names and tests alone cannot
make obvious.

### Exact remediation sequence for Claude

Do not combine these into one large change. Preserve RED evidence and keep every
contract decision auditable.

1. Attach or create an explicit correction branch from `ee2c9d9`; do not commit on
   an unnamed detached HEAD.
2. RED/GREEN P1-1: coherent final pre-send authority. This is an implementation
   invariant, not a new byte/economic contract.
3. RED/GREEN P1-4: rejected cancel accepting final cancellation evidence, including
   equality/below/above and snapshot suffixes. This completes the already-approved
   opaque-rejection semantics.
4. RED/GREEN snapshot rejection of `claimed` and `attempted`. This implements the
   already-written architecture contract.
5. Replace the lifecycle inventory/oracle so it retains state-changing refusals
   and asserts contract-derived transitions. Re-run it before changing economic
   behavior so its first REDs describe the current candidate honestly.
6. Present Material Decisions A and B to the user. Do not encode them before
   approval.
7. After approval, TDD sticky-conflict fill accounting and query finality/freshness.
8. Resolve P1-5's repeated-cancel event-retention contract. This may require a
   separate user decision because the safe alternatives change retention or
   operational escalation semantics.
9. Add gateway restart cut tests and the bounded venue-evidence-to-Raft-commit
   queue/seam. Keep Phase 6 transport implementation out; Phase 5 should define
   and prove the behavior with an injected commit-result boundary.
10. Repair the delivery harness with per-execution accounting and an independent
    reconciliation oracle. A test result may never serve as its own expected
    answer.
11. Either compose simulator fault axes and remeasure, or explicitly narrow G-033
    and Phase 5 acceptance. At minimum make cancel loss observable and test the
    behavior rather than the selected fault label.
12. Add a true multi-slot sixteen-conflict saturation test or rename the claim as
    an analytic capacity bound.
13. Make UBSan findings fatal, add a negative gate self-check, and regenerate all
    affected sanitizer evidence.
14. Remove/condense Phase 5 C++ comments using the policy above. Comment-only
    cleanup comes after semantic stability so it cannot obscure blame.
15. Correct architecture, deferred-scope, checklist, changelog, acceptance, and
    this handoff together. Record every added and removed guarantee with its reason.
16. Always build before CTest. Run focused RED/GREEN tests, then host Release
    29/29 or the new count, full Debug ASan+UBSan, lifecycle report, horizon report,
    matrix trace, model 109/109, and `git diff --check`.
17. Freeze a new source root only after all semantic and test changes land. Then
    rerun ARM64 container, physical Pi, and x86-64 NUC gates against that exact
    root. Do not reuse the old target evidence for changed source.
18. Write a superseding Phase 5 acceptance record and tag without moving or
    deleting the historical `v2-phase5-accepted` checkpoint.

### MVP boundary remains unchanged

The corrections above do not authorize real venue connectivity, strategy logic,
netting, WAL implementation, or full Raft integration. They repair the simulator,
gateway contract, reducer transitions, and evidence needed to finish Phase 5.
Phase 6 begins only after the corrected Phase 5 seam is accepted; Phase 6 then
implements the actual commit/apply and leader-change machinery against that seam.

## CODEX REVIEW REQUEST — Phase 5 complete and accepted — 2026-09-12

Phase 5 is ACCEPTED on all four environments and tagged `v2-phase5-accepted`.
Twenty commits since Phase 4, from `4409ea3` to `6487a26`. What is wanted now is
ADVERSARIAL REVIEW AND ANALYSIS, not more features. Nothing below should be built
until the review lands.

### Read in this order

1. This section, then the acceptance table below it.
2. `docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md` - what was proved,
   where, and the five gate defects found doing it.
3. `docs/V2_ARCHITECTURE_CONTRACTS.md` §§9.1-9.3 and §§10.1-10.8 - every
   non-obvious decision, argued with the alternative that was rejected and why.
   READ THESE BEFORE THE CODE. The code will not tell you what was considered.
4. `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md` - what was consciously left, each item
   with a blast radius and a TRIGGER.
5. `CHANGELOG.md`, the `[V2] - In development` section.

### What to review hardest, ordered by how likely I think it is to be wrong

ONE. `apply_observe_order_event` in `src/v2_kernel.cpp`. Nine interacting
predicates - `reconciled_submit`, `reconciled_cancel`, `cancel_reconcilable`,
`cancel_agrees`, `cancel_contradicts`, `late_cancel_completion`, `reconciliation`,
`terminal`, `effect_resolved`, `terminal_cancel_rejection` - feeding a guard
disjunction with negations. `v2-lifecycle-reachability-tests` enumerates the
reachable lifecycle and asserts a 27-position inventory, which found three real
defects reading never would have. It is still the highest-risk surface in the
phase. The review that would settle it is a full truth table over (observation
kind) x (order state) x (effect state) x (effect operation) checked against the
CONTRACT rather than against the code.

TWO. Five accepted Phase 4 behaviours were changed. Each needs an independent
"is this right?", not merely "is it consistent?":
  - a rejected cancel now moves the order to `reconciling`, not `live` (§10.3.6);
  - `fill_quantity_conflict` now marks the order `conflict` instead of refusing
    silently, mutating on a refusal path (§10.5.5);
  - a fill on a conflicted order is now refused outright (§10.8.1);
  - `order_reconciled` was widened to speak about cancel effects (§10.3.6); and
  - Phase 4's outbox-reuse test now reconciles between a rejected cancel and a
    fresh one.

THREE. The asymmetric disagreement rule (§10.3.4). I got this wrong first - both
directions as conflict - and an accepted Phase 4 permutation test caught it.
Challenge the "above" case specifically: if fills are genuinely lost the order
stays unresolved forever rather than becoming a conflict, and I argued
unresolved-and-recoverable beats terminal. That deserves a second opinion.

FOUR. `order_reconciled` is overloaded: the same kind means "the submit arrived"
against a submit effect and "the cancel did not take" against a cancel effect.
Defensible - the observation states one fact, and what it resolves depends on what
was in doubt - but it is the kind of overload that reads fine until a third case
arrives.

FIVE. The test harness is unverified code that found FIVE bugs in itself: it
leaked simulator ground truth into the gateway's `authoritative` flag; it
conflated "the venue answered" with "the cluster accepted the answer"; its
identity gate was scoped wrongly once cancels existed; and two fixtures built
states the kernel cannot reach and passed anyway. Treat
`tests/v2_gateway_delivery_tests.cpp` as suspect in the same way as the code it
tests.

SIX. The gateway is still pure functions plus an in-process shell. No transport,
no socket, no credential isolation, no Raft. §9.1.5 records that as the plan;
nothing has been proven about the real process boundary.

### Known and deliberate - do not "fix" these without reading the reasoning

- `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md` §1: policies, sources and caller cursors
  are never reclaimed, so sixteen `PublishPolicy` commands is a cluster LIFETIME
  budget. An availability defect of the same class §8.6 fixed for receipts,
  parked for the MVP deliberately.
- §2: the simulator's faults do not compose, so fills never arrive out of order
  end to end and the matrix cannot produce a reconciliation conflict.
- §3: a capacity rebalance was investigated and REJECTED. Venue event receipts
  live inside the request receipt arena, so that sub-budget - not
  `kMaxClusterOrders` - limits concurrent orders; and the policy arena that looks
  like free space is an unreclaimed lifetime budget.
- §4a: leak detection is not part of any gate's sanitizer coverage, now pinned off
  explicitly rather than differing silently by architecture.

### A question I would like answered

Phase 5's guarantees are conditional on §9.1.1: "committed" currently collapses to
"applied" because there is no commit index until Phase 6. Is there anything in the
delivery or reconciliation design that will NOT survive re-tightening that to
genuine quorum commitment? I believe the answer is no, because the gateway reads a
post-apply state and cares only that the state it read is authoritative - but I
have not tried to break it, and Phase 6 is the wrong time to find out.

## V2 PHASE 5 ACCEPTED ON ALL FOUR ENVIRONMENTS — 2026-09-11

| environment | Release | instrumented | ASan+UBSan |
|---|---|---|---|
| development host | 29/29 | - | 29/29 |
| ARM64 Linux container | 29/29 | 26/26 | 26/26 |
| physical Raspberry Pi 5 | 29/29 | 26/26 | 26/26 |
| x86-64 NUC | 29/29 | 26/26 | 26/26 |

Source root `b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`
reproduced exactly on every one. Checkpoint `v2-phase5-accepted`. Record:
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

THE PHASE 4 PROVISIONAL-CAPACITY CAVEAT IS DISCHARGED. The NUC reproduced the
bounded layout byte-for-byte against ARM64 - `TradingState` 81,168 of 81,920, 752
bytes headroom, snapshot 65,259 of 65,536 - so the 80-KiB ceiling holds on both
deployment architectures, and the sixteen-conflict saturation bound of §10.5.3 is
architecture-independent. Neither machine in the ADR 0002 topology binds tighter.

FIVE GATE-LEVEL DEFECTS were found across the campaign and none were in the code:
a locale-dependent source root, a build target list drifted from `CMakeLists.txt`,
two gates using different container engines, a container image pinned to ARM64
that made the NUC gate unrunnable since Phase 3D, and a sanitizer whose coverage
silently differed by architecture. All are recorded with cause and structural fix.

NUC setup is documented in `docs/NUC_OPERATOR_RUNBOOK.md`, including the three
host-level prerequisites that are not obvious: cgroup `cpu` delegation for
rootless podman, system-scoped NetworkManager credentials for headless boot, and
Tailscale key-expiry disablement.

NEXT:

1. Phase 6 Raft integration planning. Read architecture §9.1.1 first: "committed"
   currently collapses to "applied", and Phase 6 must re-tighten it to genuine
   quorum commitment AND re-run the Phase 5 delivery gates against the stricter
   definition. Phase 5's guarantees are conditional on that until it happens.
2. Deferred functional work in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`, each item
   with a blast radius and a trigger. The one with a realistic trigger: policies,
   sources and caller cursors are never reclaimed, so sixteen `PublishPolicy`
   commands is a cluster lifetime budget.

## V2 Phase 5 acceptance detail — 2026-09-11

All three required gates passed at checkout
`04329fad05837e7f2c6e9d8d075ba8e5ede90700`, source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`. The annotated
checkpoint is `v2-phase5-accepted`. Record:
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

| environment | Release | instrumented | ASan+UBSan |
|---|---|---|---|
| development host | 29/29 | - | 29/29 |
| ARM64 Linux container | 29/29 | 26/26 | 26/26 |
| physical Raspberry Pi 5 | 29/29 | 26/26 | 26/26 |

Pi power evidence: `throttled=0x0` before and after, 46.30 C to 50.70 C, governor
`ondemand`. Bounded layout reproduced byte-for-byte on ARM64/GCC 15.2:
`TradingState` 81,168 of 81,920, snapshot 65,259 of 65,536.

G-025 and G-026 are closed. G-074a remains open BY DESIGN and is now measured
rather than assumed: architecture §10.5.3 records that sixteen simultaneous
unresolved conflicts saturate the cluster, which makes operator escalation
load-bearing rather than a convenience.

NEXT, in order:

1. NUC validation, outstanding since Phase 4. It is the last environment the
   80-KiB state ceiling has not been checked on, and that ceiling is what makes
   the sixteen-conflict bound immovable - so this is a layout-and-capacity gate
   more than a functional one.
2. Phase 6 Raft integration planning. Before writing code, re-read architecture
   §9.1.1: "committed" currently collapses to "applied", and Phase 6 must
   re-tighten it to genuine quorum commitment and re-run the Phase 5 delivery
   gates against the stricter definition.
3. Deferred functional work in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`, each item
   carrying a blast radius and a trigger. The one with a realistic trigger:
   policies, sources and caller cursors are never reclaimed, so sixteen
   `PublishPolicy` commands is a cluster lifetime budget.

Three gate-level defects were found and fixed along the way, none of them in the
candidate: a source root that depended on ambient locale, a build target list that
had drifted from `CMakeLists.txt`, and two gates attesting the same candidate
through different container engines. All three are recorded in the acceptance
document, because a gate that only records its successes teaches nothing about its
own blind spots.

## Phase 5 physical Pi gate detail — 2026-09-11

`BUILD_JOBS=2 ./deploy/run-v2-phase5-pi-gate.sh --yes` passed on the physical
Raspberry Pi 5 at checkout `04329fad05837e7f2c6e9d8d075ba8e5ede90700`, source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`. Release 29/29,
benchmark-instrumented 26/26, Debug ASan+UBSan 26/26, `throttled=0x0` before and
after, 46.30 C to 50.70 C. Full record in
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`.

The bounded layout reproduced byte-for-byte on ARM64/GCC 15.2: `TradingState`
81,168 of 81,920, snapshot 65,259 of 65,536. The sixteen-conflict saturation bound
is now a hardware fact, not a host measurement.

STILL OUTSTANDING before Phase 5 can be called accepted:

1. the standalone ARM64 container gate, for parity with the Phase 4 promotion
   rule - `BUILD_JOBS=2 ./deploy/run-v2-phase5-container-gate.sh --yes`. The Pi
   gate runs the same container and adds hardware evidence, so this is a parity
   run rather than new information, but the rule asks for three passes and this
   candidate has two;
2. the annotated checkpoint tag, once that passes; and
3. NUC validation, still outstanding from Phase 4 and now load-bearing, since the
   80-KiB ceiling is what makes the conflict bound immovable.

Two earlier gate attempts failed in the GATE rather than the code, and both fixes
were structural: a source root that depended on ambient locale (now `LC_ALL=C`
inside the recipe) and a hand-maintained build target list that had drifted from
`CMakeLists.txt` (now derived from `BUILDSYSTEM_TARGETS`). Both are recorded in
the acceptance document, because a gate that only records its successes teaches
nothing about its own blind spots.

## Phase 5 review outcome — 2026-09-10 (superseded by the section above)

Supersedes the review-request section below, which is retained for its risk
inventory. Codex was unavailable, so the review named there was performed in
session.

THE REVIEW FOUND THREE CORRECTNESS DEFECTS, all fixed in `c13c014`. They were
found by ENUMERATION, not by reading: `v2-lifecycle-reachability-tests` drives the
kernel breadth-first through every command it accepts and converges at 27
reachable lifecycle positions. Details in architecture §10.8.

1. A fill could erase a conflict. `apply_observe_order_event` refused everything
   from `conflict`; `apply_observe_fill` did not, so a later fill carried a
   conflicted order to `filled`, released its reservation, and removed it from the
   conflict projection.
2. A pending cancel could be reconciled away, marking it rejected and returning
   the order to live - silently abandoning an operator's intent.
3. A cancel could become permanently unsettleable when its order filled while it
   was in flight, pinning one slot in each of four arenas forever.

The tool now asserts the inventory permanently, asserts that `conflict` is a sink,
and asserts that no unsettled effect is a dead end.

GATE READINESS. Host evidence is recorded in
`docs/evidence/V2_PHASE5_GATEWAY_SIMULATOR_ACCEPTANCE.md`: Release 29/29, full
host Debug ASan+UBSan 29/29, model corpus 109/109. Both gate scripts exist and are
pinned to source root
`b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4`:

```sh
BUILD_JOBS=2 ./deploy/run-v2-phase5-container-gate.sh --yes   # ARM64 container
BUILD_JOBS=2 ./deploy/run-v2-phase5-pi-gate.sh --yes          # physical Pi
```

Neither has run. Phase 5 is NOT accepted and cannot be on host evidence alone.

DEFERRED WORK is now collected in `docs/V2_DEFERRED_FUNCTIONAL_SCOPE.md`, with a
blast radius and a TRIGGER for each item. The one to know about: policies,
sources and caller cursors are never reclaimed, so they are lifetime ceilings -
sixteen `PublishPolicy` commands per cluster lifetime, ever. Not a correctness
issue and not an optimisation either; it is the same availability defect §8.6
fixed for receipts, still open for three arenas. Parked for the MVP deliberately.

A capacity rebalance was investigated and REJECTED. The arenas look independent
in a size table and are not: venue event receipts live inside the request receipt
arena, that sub-budget rather than `kMaxClusterOrders` is the real limit on
concurrent orders, and the policy arena that looks like free space is an
unreclaimed lifetime budget. Reasoning in `V2_DEFERRED_FUNCTIONAL_SCOPE.md` §3.

## Phase 5 review handoff — 2026-09-10 (risk inventory, superseded above)

This section supersedes the Phase 4 "next task" text below for anything about
what to do NEXT. The Phase 4 acceptance record itself is unchanged and still
authoritative for Phase 4.

Purpose of this handoff: Phase 5 is CODE COMPLETE ON THE HOST AND UNREVIEWED. It
has never run on the target hardware. What is wanted is adversarial review and
analysis, not more features.

### Identity

- branch: `v2/phase-4-risk-orders-outbox` (Phase 5 work continues on it; a
  rename or a fresh branch is a reviewer call)
- Phase 5 base: `dd77e61420c4ff5428fcd409cebdfebbe545cc77` (Phase 4 accepted)
- HEAD at handoff: `fc21b17e00de6bf9a25399325f0f58cd49340cba`
- source root at HEAD, computed with the Phase 4 gate recipe:
  `d68f23ef5f9ab1ab594db3dec4cb2e0a2c2e654651c6ef75fba550d392479144`
  (computed on the development host with `shasum -a 256`; the gate recomputes it
  with `sha256sum` inside the container and the two must agree — treat a mismatch
  as unverified, not as a defect, until the gate has run once)
- no Phase 5 gate script exists yet. `deploy/run-v2-phase4-pi-gate.sh` is the
  template; a Phase 5 script needs the new root and its own evidence directory.

### Verification status

Host only. Release 28/28 and full host Debug AddressSanitizer + UndefinedBehavior
Sanitizer 28/28 at HEAD. Model corpus 109/109, untouched by Phase 5.

NOT DONE: ARM64 container gate, physical Pi gate, NUC validation. Phase 5 cannot
be accepted on this evidence.

### The nine commits, in order

| commit | what it settled |
|---|---|
| `4409ea3` | resolved five blocking contract contradictions in §9; added the pre-send guard |
| `2b0e847` | deterministic adversarial venue simulator, test-only by CMake construction |
| `4a4ee1a` | delivery planner and the Phase 5 gate; resend requires proof (§9.2.1) |
| `bd39053` | reviewed §10, built the gateway half of reconciliation |
| `f08bac0` | `order_reconciled` as the seventh observation kind (approved byte contract) |
| `c7b1034` | cancel path through reconciliation; four findings |
| `da87f62` | cancel redelivery without proof (approved amendment to §9.2.1) |
| `f482bfa` | measured retention horizons; discharged §9.1.7 |
| `fc21b17` | snapshot coverage for Phase 5 states; corrected two impossible fixtures |

All reasoning is in `docs/V2_ARCHITECTURE_CONTRACTS.md` §§9.1–9.3 and §§10.1–10.7.
Read those before the code; every non-obvious choice is argued there with the
alternative that was rejected and why.

### WHAT TO REVIEW HARDEST

Ordered by how likely I think they are to be wrong.

**1. The applicability matrix in `apply_observe_order_event`.** This is the single
highest-risk area. It is now a large interacting set of booleans —
`reconciled_submit`, `reconciled_cancel`, `cancel_reconcilable`, `cancel_agrees`,
`cancel_contradicts`, `late_cancel_completion`, `reconciliation`, `terminal`,
`effect_resolved` — feeding a six-term guard disjunction with negations. I believe
each term individually; I am not confident about every interaction. The review
that would actually settle it is a full truth table over (observation kind) ×
(order state) × (effect state) × (effect operation), checking each cell against
the contract rather than against the code. If a cell is wrong, this is where.

**2. Three accepted Phase 4 behaviours changed.** Each needs an independent "is
this right?" pass, not just "is it consistent?":
  - a rejected cancel now moves the order to `reconciling`, not `live` (§10.3.6).
    Justification: a venue refuses a cancel for "already cancelled", "already
    filled" and "no such order" as readily as for "still working", and once
    cancels may be redelivered a rejection of the duplicate is the common case.
  - `fill_quantity_conflict` now marks the order `conflict` instead of refusing
    silently (§10.5.5). It mutates on a refusal path, which has precedent in the
    same function but is still worth challenging.
  - Phase 4's outbox-reuse test now reconciles between a rejected cancel and a
    fresh one. The arena-pressure guarantee is unchanged; the path is longer.

**3. The asymmetric disagreement rule (§10.3.4).** I got this wrong first — both
directions as conflict — and Phase 4's cancel/fill/final permutation test caught
it. The corrected rule: a venue reporting MORE than the cluster booked is missing
evidence (refuse, let it settle); reporting LESS is a conflict (no future evidence
reduces a booked fill). Challenge the "above" case specifically: if the fills are
genuinely lost the order stays unresolved forever rather than becoming a conflict,
and I argued unresolved-and-recoverable beats terminal. That argument deserves a
second opinion.

**4. `order_reconciled` is overloaded.** The same kind means "the submit arrived"
against a submit effect and "the cancel did not take" against a cancel effect. I
think this is defensible — the observation states one fact and what it resolves
depends on what was in doubt — but it is the kind of overload that reads fine
until someone adds a third case.

**5. The harness is unverified test code that found three bugs in itself.** It
leaked the simulator's ground truth into the gateway's `authoritative` flag;
it conflated "the venue answered" with "the cluster accepted the answer"; and its
identity gate was scoped wrongly once cancels existed. Treat the ~900 lines of
`tests/v2_gateway_delivery_tests.cpp` as suspect in the same way as the code it
tests. Two fixtures in it built states the kernel cannot reach, and passed.

**6. The gateway is not a process.** Everything is pure functions plus an
in-process shell. No transport, no socket, no credential isolation, no Raft.
§9.1.5 records that as the plan; nothing has been proven about the real boundary.

### Known and deliberate gaps

- **The simulator's faults do not compose.** `choose_fault` returns ONE fault per
  operation by first match, so partial-fill-plus-loss, reordered fills, and late
  duplicates are all unreachable. Consequence: fills never arrive out of order
  end to end, and the matrix cannot produce a reconciliation conflict. Covered by
  direct unit tests instead. Closing it means splitting `Fault` into its three
  actual axes — transport per message, venue behaviour per operation, integrity
  per answer — and re-measuring §10.5, because every seed changes.
- **Sixteen simultaneous unresolved conflicts saturate the cluster** (§10.5.3).
  Correct and fail-closed, but it makes the G-074a operator path load-bearing.
  The capacity cannot be raised: `TradingState` is 81,168 of 81,920 bytes and one
  more decision record costs 704 of the 752 remaining.
- Native amend and overlapping replacement remain disabled, as in Phase 4.
- A venue-initiated cancellation still cannot be reported: the existing
  `cancelled` kind requires a cancel effect the cluster may never have sent. This
  is pre-existing, not introduced by Phase 5.

### Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
ctest --test-dir build --output-on-failure          # expect 28/28

cmake -S . -B build-v2-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-v2-sanitized -j8
ctest --test-dir build-v2-sanitized --output-on-failure   # expect 28/28

VELOCITA_HORIZON_REPORT=1 ./build/v2-gateway-delivery-tests   # §10.5 tables
VELOCITA_MATRIX_TRACE=1   ./build/v2-gateway-delivery-tests   # per-seed matrix

cd model && PYTHONPATH=src python3 -m unittest discover -s tests  # expect 109
```

Always build before `ctest`: `ctest` does not build, and this project has already
produced one false pass from a stale binary.

### Next tasks, in the order I would do them

1. **Review and analysis** — this handoff's purpose. Nothing below should start
   until the items above have had an adversarial pass.
2. **Phase 5 gate scripts and target validation** — ARM64 container, then the
   physical Pi, using `deploy/run-v2-phase4-pi-gate.sh` as the template with the
   new source root. This is what Phase 5 acceptance requires and it is the largest
   remaining unknown.
3. **NUC validation**, still outstanding from Phase 4 and now more load-bearing,
   because the 80-KiB state ceiling is what makes the sixteen-conflict bound
   immovable.
4. **Compose the simulator's faults**, then re-measure §10.5.

### Protected uncommitted paths

`model/`, `docs/README.md`, `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`, and
`docs/adr/0002-two-machine-pi-nuc-deployment-topology.md` are intentionally
uncommitted. Do not `git add .`; commit by explicit path only. This rule exists
because a directory-wide add already swept these into a Phase 4 commit once.

## Phase 4 accepted checkpoint — 2026-09-10 (authoritative)

Phase 4 is accepted. The annotated checkpoint is `v2-phase4-accepted`; the
tested checkout was `9f61247645140a544a4db6bba1ba1d62dcacb080`, and the
accepted source root is
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
The acceptance record is
`docs/evidence/V2_PHASE4_RISK_ORDERS_OUTBOX_ACCEPTANCE.md`.

Host Release and full ASan+UBSan passed 25/25. ARM64 Ubuntu passed Release 25/25
and both V2 matrices 22/22. The physical Pi gate passed the same matrices with
the exact source root, `throttled=0x0` before and after, and 44.10 C to 50.15 C
temperature evidence at
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`.

G-010 through G-024 are closed. G-025/G-026 remain Phase 5 transport work, and
G-074a remains the explicitly accepted fail-closed operational gap owned by
Phases 8 and 10. No Phase 5 implementation has started. Before Phase 5 code,
review its simulator/gateway command, delivery, retry, reconciliation, fencing,
and emergency-lane contracts; do not infer them from Phase 4.

## Codex acceptance-correction checkpoint — 2026-09-10 (read this first)

This section and architecture §§8.4-8.7 supersede the earlier §8.7 review pause
and older next-task text below. The user approved all ten audit corrections. The
working parent was `365b570` on `v2/phase-4-risk-orders-outbox`; verify current
HEAD and status because this checkpoint may be committed after it is written.

Implemented by permanent RED-to-GREEN tests:

1. authorization requires the current active policy/executable/instrument/venue
   contract and rejects an older same-slot proposal once a newer actionable
   decision exists;
2. cancel effects refuse submit-only `order_acknowledged`, and a repeated
   semantic `effect_unknown` is a non-mutating `already_applied` result;
3. applied fill/event results populate the existing candidate-V5 `DecisionId`.
   This zero-byte owner link makes their receipts reclaimable with a resolved
   order group; 119 completed cycles cross the old 64-event lifetime ceiling;
4. authorization reserves worst finite observation capacity as remaining
   one-atom fills plus state-sensitive submit/cancel/reconciliation transitions.
   It also reserves one cancel outbox slot per unresolved order, limiting the
   current 16-effect arena to at most eight newly live orders;
5. a definitively rejected cancel record can be reused at a strictly increasing
   effect generation. Pending/unknown/reconciling/conflict/incomplete records are
   never overwritten;
6. caller cursors register atomically on first admission with a zero expired
   prefix. Snapshot validation bounds the arena and rejects zero/duplicate caller
   IDs. The contract now states the real conservative-prefix semantics and the
   monotonic per-stable-caller requirement;
7. successful observation results require complete owner/stream identity, while
   malformed typed refusals may retain partial non-empty diagnostic detail. An
   admitted invalid fill is now result-encodable and snapshotable;
8. metrics use the exact effect authorization receipt for age, explicitly name
   reconciliation age as since authorization, expose venue/caller identity
   headroom, and classify derived/fill-quantity collisions as conflicts;
9. the Phase 4 property corpus now executes sell accounting, same- and
   cross-request duplicates, conflicting observation identities, seed/step
   diagnostics, and a minimal independent signed-position ledger; and
10. fill evidence is scoped to a submit effect. A partial fill resolves an
    unknown submit, but a historical-submit fill does not erase an unknown
    cancel; a fill that names a cancel effect is refused before economics move.

Candidate V5 bytes did not grow. Canonical VALUES changed because caller cursors
are present from first admission and applied observation results now carry their
owning DecisionId. Old unaccepted development V5 snapshots that omitted that
owner are unsupported. Reviewed replay goldens were updated only after direct,
snapshot-suffix, and independent-model bytes agreed.

Candidate source commit `db13a9c19caf7d5ac67ce1117b78f6302b6bcd3e`
has Phase 4 source root
`a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853`.
Its full Release build and 25/25 CTest suites pass; its full Debug ASan+UBSan
build and 25/25 suites also pass; `git diff --check` passes. Reviewed Phase 4
ARM64-container and physical-Pi wrappers now pin that root, and
`docs/evidence/V2_PHASE4_RISK_ORDERS_OUTBOX_ACCEPTANCE.md` records the promotion
contract without claiming pending target results. The ARM64 container gate is
now PASS at
`~/.local/share/velocita/v2-phase4-container-gates/20260910T151843Z`:
Release 25/25, instrumented V2 22/22, and Debug ASan+UBSan V2 22/22 on Ubuntu
26.04 ARM64/GCC 15.2 with the expected source root. The physical Pi gate also
passed at
`~/.local/share/velocita/v2-phase4-acceptance-pi-gates/20260910T152924Z`:
Release 25/25, instrumented V2 22/22, Debug ASan+UBSan V2 22/22, exact source
root, and `throttled=0x0` before/after. The authoritative accepted checkpoint
above supersedes this development checkpoint.

Preserve the same protected unrelated paths listed near the end of this file:
`docs/README.md`, `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`,
`docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`,
`docs/adr/0002-two-machine-pi-nuc-deployment-topology.md`, and `model/`.

## Phase 5 started — 2026-09-10 (read after the Phase 4 sections)

Phase 4 is accepted (`v2-phase4-accepted`). Phase 5 is venue simulator and
execution gateway.

### Contracts were reviewed BEFORE code, and that was necessary

Sections 9 through 15 are the primary gateway contract and, unlike 8.1 to 8.7,
had never passed an approval gate - the document header still reads "proposed".
A review found thirteen contradictions or gaps in them. Five blocked the first
line of code and are now resolved and recorded in architecture §9.1, with
reasoning:

1. §9.1.1 "committed" collapses to "applied" until Phase 6, because Raft
   integration is Phase 6 and the phrase had no referent in the interim.
2. §9.1.2 `claimed` and `attempted` are gateway-local, resolving a direct
   contradiction between §9's lifecycle diagram and §8.2. No command writes them;
   putting them in consensus would add a quorum round trip to every send.
3. §9.1.3 expiry IS control-generation staleness. §9 and G-029 required checking
   an "expiry" that `OrderEffect` has no field for. Defining it this way deletes
   a phantom field rather than adding a real one, and keeps wall-clock out of the
   authority path.
4. §9.1.4 lowering is representation only, never economics. This was the largest
   under-specified permission in the contract: nothing distinguished permitted
   "lowering" from the forbidden "economically modify". Rounding, TIF
   substitution, and post-only downgrades are now explicitly forbidden; a venue
   that cannot represent exact intent gets a refusal, not an approximation.
5. §9.1.5 the gateway is a separate process in deployment, tested in-process with
   the transport interface as the seam, because G-066 credential isolation is not
   achievable inside a process that also runs the reducer and its fixtures.

§9.1.6 records the shaping decisions that did not block: emergency lane built but
unclaimed until Phase 8, authority-lease deferred to Phase 8, send-then-reconcile
rather than record-before-send, crash scoped to GATEWAY crash since nothing is
durable until Phase 7, simulator test-only, virtual time everywhere, and the five
parked retention horizons not blocking Phase 5.

§9.1.7 records an open obligation inherited from Phase 4: §3 requires retention
to exceed the retry and reconciliation horizons, §8.6/§8.7 made retention finite,
and §8.4 says those horizons are unknown until Phase 5. Phase 4 was accepted with
a retention policy that cannot yet be SHOWN to satisfy §3. Discharge it by
measuring what the simulator produces; if the numbers do not fit, the capacities
move, not the invariant.

§9.1.8 records a trap: `include/velocita/v2/wire.hpp` has a `VLT2` envelope with
`MessageKind::order_effect`, `observe_fill`, `apply_reconciliation` and friends.
Nothing maps them onto `KernelCommandKind`, the numbering does not correspond,
and the only callers are Phase 1 tests and a benchmark. It is not a transport.

### First increment: the pre-send guard (G-029), complete

`include/velocita/v2/gateway.hpp`, `src/v2_gateway.cpp`, target
`v2-gateway-tests`. Release 26/26 and full host Debug ASan+UBSan 26/26.

`evaluate_pre_send` is a PURE FUNCTION: reads no clock, performs no I/O, mutates
nothing, and is deterministic on repeated calls. This follows
`OwnerLoopScheduler` from V1.1, which already takes `now` as a parameter rather
than reading a clock - that precedent is the whole reason Phase 5 can keep the
project's testing discipline while owning a clock and a transport.

A permitted decision CARRIES the materialized effect; a refused one carries
nothing. A caller therefore cannot send a refused effect: the payload is simply
absent rather than present-but-forbidden.

Refusal ordering is fixed and the reasons are documented in `v2_gateway.cpp`:
identity and deliverability, then payload reconstruction, then control authority
(generation before kill epoch), then venue capability, then quota. Control
precedes capability so a stale effect reports as stale even on a venue that could
not have taken it. Quota is last because it is the only "not yet" refusal; every
other one means "never".

Only `pending` is deliverable. `unknown` and `reconciling` are deliberately NOT,
because ADR 0001 requires reconciliation before resending an ambiguous effect and
delivering one straight from this guard would be the blind retry G-026 forbids.

Next: the venue simulator's deterministic core - virtual clock, seeded fault
model covering the nine G-033 classes, and the crash cut points TDD §4.5 names.
Then the delivery loop shell, then reconciliation.

## Claude takeover checkpoint — 2026-09-09 (historical)

This section and architecture contract §8.1 supersede older next-task/scope/schema
statements later in this file. Later dated sections retain development history;
do not restart B1 or restore the removed native-replacement implementation.

The CURRENT WORKING TREE, not HEAD alone, is the development checkpoint.
HEAD remains `aedd658ee0a7fe4e30a2338d2f953fc659306e29` on
`v2/phase-4-risk-orders-outbox`. No takeover commit/tag was created. Preserve and
inspect the existing diff; resetting to HEAD would lose the completed increments.

Completed after HEAD, all uncommitted:

1. Approved scope correction: safe SERIAL replacement is a Phase 4 acceptance
   requirement. Native amend and overlapping replacement remain disabled. Refusal
   alone does not satisfy G-018; the full cancel/reconcile/fresh-authorize lifecycle
   needs proof. Covering two real exposures is not erroneous double-counting.
2. Reclamation identity fix: retained evaluation/authorization receipts prevent
   decision reuse and recover the original submit result after payload compaction.
3. Snapshot accounting fix: checked per-account sums, reservation ownership and
   transactional rejection of inconsistent snapshots; permanent regressions.
4. Approved names: `max_order_gross_notional` (single-order gross notional) and
   `max_reserved_exposure` (aggregate margin + fee hold budget).
5. B1 implemented: full `ConfigureRiskLimits`, command kind 17, 88-byte body,
   strict expected RiskVersion and kernel-selected +1, matching kill/executor
   generations, no economic-balance overwrite, prospective caps, existing-hold
   coverage and quiescent rate/policy updates. See architecture §8.1 for the
   complete approved contract and future user-doc reference.
6. Approved kernel command/result and snapshot schemas V5; V4 rejected, no
   implicit migration. Result detail tag 5 retains scope and resulting RiskVersion.
   OrderEffect unchanged; state 81,024 bytes, max snapshot 65,121 bytes.

Phase 4 paths with existing uncommitted work (preserve all; distinguish from the
unrelated protected list below):

```text
CHANGELOG.md
docs/CURRENT_HANDOFF.md
docs/V2_ARCHITECTURE_CONTRACTS.md
docs/V2_END_STATE_GUARANTEE_CHECKLIST.md
include/velocita/v2/kernel.hpp
include/velocita/v2/kernel_codec.hpp
include/velocita/v2/state_snapshot.hpp
src/v2_kernel.cpp
src/v2_kernel_codec.cpp
src/v2_state_snapshot.cpp
tests/v2_kernel_codec_tests.cpp
tests/v2_model_replay_tests.cpp
tests/v2_replay_tests.cpp
tests/v2_risk_order_outbox_tests.cpp
tests/v2_state_snapshot_tests.cpp
```

Last verified after final source/test edits: Release 23/23; focused host Debug
ASan+UBSan 5/5; `git diff --check` clean. Subsequent takeover edits are docs-only.
Reproduce with:

```sh
cmake -S . -B build-phase4 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-phase4 -j2
ctest --test-dir build-phase4 --output-on-failure
cmake --build build-phase4-review-sanitized --target v2-risk-order-outbox-tests v2-state-snapshot-tests v2-kernel-codec-tests v2-replay-tests v2-model-replay-tests -j2
ctest --test-dir build-phase4-review-sanitized -R 'v2-(risk-order-outbox|state-snapshot|kernel-codec|replay|model-replay)-tests' --output-on-failure
```

The existing sanitizer build is Debug with `-fsanitize=address,undefined
-fno-omit-frame-pointer` in CXX flags and `-fsanitize=address,undefined` in linker
flags. It reuses `build-phase4/_deps/aeron-src` and
`build-phase4/_deps/willemt_raft-src` through CMake FetchContent source overrides.
These are focused host checks, NOT full Phase 4 acceptance or pinned ARM64/Pi.

### Serial-replacement acceptance progress — 2026-09-09 (Claude)

`test_serial_replacement_across_a_risk_limit_update` implements the first
approved slice of the serial-replacement acceptance matrix, in eight legs:
rate/policy change refused while the cancelled order still holds risk; aggregate
cap below the retained hold refused; prospective per-order cap accepted
mid-cancellation without rewriting balances, position version, order state, or
historical effect bytes; stale-version successor refused; venue cancellation not
releasing risk; final reconciliation releasing the residual; freshly evaluated
successor authorizing against the new configuration; and every effect in the
lifecycle reconstructing byte-for-byte across BOTH snapshot restore and canonical
command replay, with the replayed snapshot byte-identical to the direct one.

Legs 1-7 passed on first run. They are acceptance coverage for behavior already
implemented correctly, not bug fixes, and are reported as such rather than as
fabricated RED.

Leg 8 produced a GENUINE RED: `canonical replay changed historical effect bytes`.
The cause is a replay-fidelity property worth stating explicitly, because it will
recur in the Phase 4 model corpus and in any future replay harness:

  `materialize_order_effect` sets `committed_directive_index` from the retained
  authorization receipt's `state_version`. Per architecture §8.1 an admitted
  REFUSAL also retains a receipt and advances the global StateVersion. A replay
  that omits refused commands therefore reaches authorization at a different
  StateVersion and reproduces different canonical effect bytes.

  Consequence: a faithful replay must re-drive every admitted command, refusals
  included. Refusals are part of history, not filler between the interesting
  commands. The original defect was in the test harness, not the kernel, but a
  Phase 4 model corpus that replays only successful commands would report a false
  divergence - or worse, mask a real one.

`evaluate_new_decision` now takes an optional command sink so a caller can
replay a lifecycle faithfully; it previously applied two commands invisibly.

Verified after this increment: Release 23/23; focused host Debug ASan+UBSan 5/5.

### Reclamation inside the cancel/reconcile window — 2026-09-09 (Claude)

`test_reclamation_during_cancellation_preserves_the_recovery_path` produced a
GENUINE RED and exposed an availability defect, now fixed.

An order can be terminal AND risk-resolved while one of its own effects never
received evidence. Observed state: order `cancelled`, reservation `released`,
cancel effect `acknowledged`, submit effect still `pending`. Once the order is
closed no future observation can be scoped to that submit effect, so it was
ORPHANED - yet reclamation required every effect settled, making the group
permanently unreclaimable. With the decision arena full of unauthorized
decisions, evaluation failed closed and could not recover even after the stuck
slot reconciled. Wedged, not degraded.

Fix, approved as option A and specified in architecture §8.2: on DEFINITIVE
evidence only, orphaned effects settle to `OutboxState::incomplete`, an enum
value that already existed and was never set. `incomplete` counts as settled for
reclamation. It is deliberately not `acknowledged`, which would claim evidence
the venue never gave.

Deliberately excluded: the full-fill path. A cancel in flight when a fill lands
is still owed a venue response, so that effect is awaiting evidence rather than
orphaned. Settlement runs only where `releases` is already true - final cancel
reconciliation, or a rejected submit the venue never accepted.

`effect_is_settled` and `order_is_risk_resolved` are now shared helpers, because
reclamation and settlement must agree on what "resolved" means or one will free
capacity the other still considers live.

`TradingState::settled_incomplete_effect_count` is a new monotonic counter in the
snapshot header. It grew the V5 empty payload by eight bytes, which falsified the
guard asserting V5's empty payload matched V4's; that guard is re-purposed to pin
the empty-payload SIZE, which is what unreviewed header growth actually moves.
Empty-state, replay and generated-model goldens were re-pinned for development V5.

Layout: `TradingState` 81,032 bytes (888 headroom), maximum canonical snapshot
65,129 bytes (407 headroom). Release 23/23; focused host Debug ASan+UBSan 5/5.

Phase 8 now carries G-057a: reclaiming an `incomplete` effect makes its bytes
unreconstructable, so a late gateway send against a closed order must be stopped
by executor-generation and kill-epoch fencing plus stable `EffectId` idempotency.
Phase 4 relies on that and it is a Phase 8 acceptance requirement.

### Serial-replacement matrix completed — 2026-09-09 (Claude)

`test_risk_configuration_respects_every_unresolved_order_state` pins the §8.1
rule against all four unresolved states: `cancel_pending`, `submission_unknown`,
`reconciling`, and `conflict`. For each it proves a rate change and a bare
policy-version bump are refused with `risk_reconfiguration_blocked` and mutate
nothing, a prospective per-order cap change is accepted without revaluing the
live hold, and an aggregate cap below the retained hold is refused rather than
clamped. It then proves rates become configurable once the hold is released,
without disturbing the filled position. `submission_unknown` is driven by a real
`effect_unknown` observation; `conflict` still has no producer and is constructed
directly, so the rule is pinned before B4 supplies one.

Why this needed pinning even though the behavior was already correct: the
implementing predicate keys on the reservation not being released, which is the
right axis because risk is what the rule protects, but it means the states §8.1
names are only covered transitively. A future refactor to a lifecycle-state list
would silently drop whichever state it forgot.

`test_two_sided_slots_requote_independently` is the first fixture in the suite
that quotes two sides. `two_sided_policy` and `two_sided_executable_policy` add a
second ordered rule bound to a second template on slot 2 with the sell side, so
first-match steers a rich feature to the buy template on slot 1 and anything
above the floor to the sell template on slot 2. Prices are kept small
deliberately: the shared instrument profile caps price at 100 ticks and per-order
gross notional at 4,000, and notional is price * quantity * 100.

It proves the two slots never contend, each slot's exclusivity is enforced
independently, both holds are summed rather than netted, cancelling and
reconciling one side leaves the other's state, pairing and effect bytes
untouched, only the reconciled side's risk is released, the freed slot requotes
while the other still rests, and the whole interleaving survives snapshot
restore. It also separates the two idempotency paths: a same-identity retry
returns the original result verbatim through receipt deduplication, while a
different request re-authorizing a consumed decision returns `already_applied`
with the retained order and effect identities.

#### Fixture hazard for review: a retained decision outlives its policy

Flagged for Codex review because it produced a convincing false positive and the
general shape will recur.

`ready_state()` EVALUATES `DecisionId{20}` as part of building the fixture. Any
helper that layers a different policy on top of `ready_state()` - as
`two_sided_state()` does - leaves that decision resolved against the OLD policy.
Its retained proposal is frozen at evaluation time, so it keeps the old policy's
side, slot, price and quantity no matter what the new bindings say.

What it cost here: the first draft of `test_two_sided_slots_requote_independently`
authorized the inherited `DecisionId{20}` as its "ask". That decision had been
resolved against the single-rule policy and targeted slot 1 buy, so both quotes
landed on slot 1 and the second was refused `order_slot_occupied`. The failure
was indistinguishable from a genuine slot-exclusivity defect - two orders that
should occupy different slots contending for one - and the obvious next step
would have been to "fix" the exclusivity predicate. There was nothing wrong with
it. Printing the resolved `order_slot` and `side` of both proposals is what
settled it: both read slot 1, side buy.

What it could cost elsewhere, and why this is not merely a test-hygiene note:

- a test that swaps policy mid-fixture is exercising YESTERDAY's policy while
  reading today's, so it can report a pass for behavior that was never executed,
  which is the more dangerous direction;
- the same shape appears in any future fixture that stages or activates a policy
  after `ready_state()`, including executable-policy rebinding tests; and
- a generated model corpus that mixes policy activation with decision evaluation
  must order them deliberately, or the model and the kernel will disagree about
  which policy a decision was resolved under and the divergence will be blamed on
  the reducer.

This is correct kernel behavior, not a bug: a decision record must retain the
proposal it was evaluated with, because that is what makes decisions replayable
and attributable. The hazard is entirely in fixture construction.

Mitigations in place: `two_sided_state()` documents the constraint at its return
point, and `evaluate_at_feature()` exists so a caller can evaluate its own
decisions after the swap at a chosen feature value. A stronger guard - having
`ready_state()` not evaluate a decision at all, or having policy-swapping helpers
assert `decision_count == 0` - was NOT implemented, because changing
`ready_state()` touches every test in the suite. Worth considering when the Phase
4 model corpus lands, since that is the next place the ordering will matter.

Both tests are acceptance coverage: no kernel defect was found by either.
Release 23/23; focused host Debug ASan+UBSan 5/5.

### Independent Phase 4 property corpus — 2026-09-09 (Claude)

`tests/v2_phase4_property_tests.cpp`, target `v2-phase4-property-tests`. 24
seeds, 220 steps each, currently 2,312 of 5,264 generated commands admitted.

Approach and why. The Phase 4 gate is stated as a PROPERTY - fault/property tests
must not be able to produce an unreserved effect, a duplicate economic mutation,
a negative reservation, or a replay mismatch - so this corpus checks properties
derived from the CONTRACTS after every command rather than agreeing with a second
implementation. A hand-written mirror catches wrong-but-self-consistent behavior
that invariants miss, which is real, but it is also the approach most at risk of
transcribing the reducer including its bugs. The Phase 1-3 mirror in
`v2_model_replay_tests.cpp` remains the reference for that style; this file
deliberately takes the other side so the two fail differently. A Phase 4 mirror
model is still worth adding later and is NOT superseded by this.

Leverage worth knowing: `encode_state_snapshot` runs the full structural
validator, which already enforces per-account conservation - reserved totals must
equal the sum over that account's reservations - plus reservation ownership,
non-negativity and index bounds. Snapshotting after every command is therefore
itself a dense invariant check. The corpus adds what the validator cannot see:
no effect outliving its cluster order, per-record reservation arithmetic,
cross-command monotonicity of both accounting counters, reclamation being
economically inert, and byte-identical canonical replay of the entire history.

Phase 4 state is created only by commands - the risk account through B1
`ConfigureRiskLimits` creation, decisions through real source admission and
evaluation - so the corpus cannot hand-seed a state the kernel would never
produce. Only Phase 1-3 scaffolding is built directly.

Coverage is asserted, not hoped for: the run fails unless it exercised
authorization, partial fill, full fill, cancel, reconciliation, unknown
submission, configuration, reclamation, orphan settlement and refusals. A vacuity
guard additionally requires at least a third of commands to be admitted, so a
future change that exhausts an arena early cannot silently turn later steps into
no-ops. The threshold is deliberately loose rather than tuned to the current
ratio, which would make it a change detector instead of a vacuity guard. A high
refusal rate is wanted: the generator aims commands at stale decisions, released
reservations, occupied slots and closed orders on purpose.

Three fixture defects were found and fixed while building it, all in the corpus
rather than the kernel, and each is a hazard for any future hand-built state: a
source cursor with a zero sequence is invalid, so the corpus lets the first
`RecordSourceEvent` establish it; an executable-policy bundle records the
StateVersions at which it was staged and activated and both must be at or below
the current version, so a bootstrap sitting at version zero describes a history
that never happened; and asserting `replacement_not_supported` specifically is
wrong once the 256-receipt arena fills, because admission then refuses on
capacity before dispatch reaches the replacement handler - the property is that
replacement never succeeds, not that it always fails the same way.

No kernel defect was found by the corpus. Release 24/24; focused host Debug
ASan+UBSan 6/6, the sanitized build reconfigured to include the new target.

### Unauthorized-decision retention — 2026-09-09 (Claude)

Implemented per architecture §8.4. Two rules now make retained decisions
reclaimable: structurally non-authorizable records, whose authorizability is
decidable from immutable fields alone and therefore needs no horizon; and
superseded records, where a newer actionable proposal exists for the same
account, instrument, venue and logical order slot. Ordering is by array position,
never `DecisionId`, because position is arrival order and is canonical under
stable compaction.

The blocker recorded in an earlier session - that reclaiming a decision would
narrow `DecisionId` conflict detection - was ALREADY SOLVED by the
reclamation-identity fix: `apply_evaluate_decision` falls back to scanning
applied evaluation receipts, which outlive the decision arena and retain the
DecisionId, digest and outcome. No new contract was needed for it.

A structural change to the sweep was: it previously returned early when no order
GROUP was reclaimable, which is exactly the case that matters here - an arena
full of no_action records has zero reclaimable groups. Decisions are now assessed
independently of order-group reclamation.

Knock-on effect worth understanding, because it invalidated two existing tests'
premises rather than their intent: the decision arena can no longer be filled by
unauthorized decisions at all, since at most one survives per slot plus those
referenced by live orders. The wedge scenario that
`test_reclamation_during_cancellation_preserves_the_recovery_path` was built to
exercise is now structurally unreachable. That test was rewritten to assert the
STRONGER property - a stuck order cannot wedge evaluation - rather than the
earlier "evaluation halts but can still recover".
`test_conflicted_orders_are_never_swept` was rebuilt to take sweep pressure from
the ORDER arena using the two-sided fixture, which is the more realistic source
anyway.

`observe_fill` gained side and price parameters. A fill whose side disagrees with
the resolved proposal is refused as not applicable, which is correct but reads as
a mysterious fixture failure on a two-sided book.

Implementation hazard for review: `decision_is_authorizable` mirrors the
predicate in `apply_authorize_decision`. If that gains a condition and the mirror
does not, the sweep will reclaim records the authorizer would still have
accepted. There is no compiler check; they must change together.

Deferred to a later version and recorded in §8.4: a state-version horizon for any
unreferenced decision. Deterministic and strictly more general than supersession,
but N cannot be justified until Phase 5 supplies a venue simulator and the
gateway retry horizon becomes measurable. Revisit alongside the reference-horizon
question already deferred from the reclamation contract.

No schema or layout change was required: `TradingState` remains 81,032 bytes and
the maximum canonical snapshot 65,129. Release 24/24; focused host Debug
ASan+UBSan 6/6.

### Phase 4 metrics — 2026-09-09 (Claude)

`include/velocita/v2/metrics.hpp`, `src/v2_metrics.cpp`, target
`v2-metrics-tests`. Specified in architecture §8.5.

`collect_metrics` is a pure function of `TradingState`: no new field, no digest
participation, no clock. Ages are STATE-VERSION distances, not durations, because
apply may not read a clock; the exporter converts to wall-clock on its own clock,
outside the authority boundary. `OutcomeCounters` is separate and deliberately
not in state, because duplicate and refusal counts describe command outcomes
rather than retained records - a duplicate leaves no trace in state, which is
what makes it a duplicate - and putting them in state would drag an observability
counter into the state digest and therefore into consensus.

G-074a now has its signal: `oldest_reconciling_order_age` plus
`oldest_conflicted_order_age` and `unresolved_orders`. A stuck slot is no longer
indistinguishable from a quiet market.

REGRESSION FOUND, AND A PROCESS LESSON. Building only the target under test and
then running `ctest` gives a false pass: `ctest` does not build, so untouched
suites keep running STALE binaries. `v2-decision-kernel-tests` had been passing
against a binary predating the unauthorized-decision reclamation. A full
`cmake --build build-phase4` exposed it immediately. Always build ALL targets
before trusting a regression run.

The regression itself was a premise change, not a defect: that test filled the
decision arena with sixteen same-slot proposals, which supersession now sweeps
down to one so the arena never reaches its bound. The capacity-atomicity
invariant it protects is still worth testing, so the fixture now assigns each
retained proposal a distinct logical slot, making the arena genuinely full and
genuinely unsweepable. That is the third test whose premise this phase's
reclamation work invalidated while leaving its intent intact.

Release 25/25; focused host Debug ASan+UBSan 8/8, the sanitized build
reconfigured to include the metrics and decision-kernel targets.

### Finite identity horizons — decisions approved 2026-09-09 (Claude)

Specified in architecture §8.6; G-023 amended in the guarantee checklist.
Decisions recorded BEFORE implementation so the contract is reviewable
independently of the code that follows.

The defect being fixed: nothing ever reclaimed a request receipt, so the kernel
accepted exactly `kMaxRequestReceipts` commands in its lifetime and then refused
everything permanently. A second ceiling inside the same arena stopped fills
being accepted at `kMaxVenueEventReceipts`, which strands existing risk rather
than merely stopping new work. Both are availability defects.

Approved:

1. G-023 weakened to "within the retention window". Outside it a duplicate
   returns the typed refusal `request_expired`. The ANSWER is weakened, never the
   safety: G-022 holds because an expired request is refused rather than
   re-executed.
2. A new bounded `CallerCursor` arena plus `ApplyCode::request_expired`. Receipts
   may be forgotten freely; a request at or below its caller's watermark with no
   retained receipt is refused.
3. Caller capacity 8, WITH a new deployment requirement: `CallerId` must be a
   stable logical identity assigned by configuration, and `RequestSequence`
   monotonic per caller and non-regressing across restarts. A caller returning
   from a restart with a fresh `CallerId` would fill the cursor arena with dead
   entries and rebuild the wedge one level up. The alternative, caller eviction,
   needs its own horizon and reintroduces the problem it was meant to solve.
4. REVISED from the original proposal. Venue-event identities are reclaimed with
   their order group rather than pinned against expiry. A duplicate fill whose
   order is gone fails to resolve an order and is refused before touching
   economics, so the absence of the order IS the deduplication. This preserves
   the same safety at strictly lower cost AND removes the venue-event wedge,
   which pinning would have left in place.

Parked for Phase 5 review: caller-driven `ExpireIdentity`, already named in the
§7 candidate command family. It is the safest possible expiry because the party
that would retry declares it is finished, but it cannot be the only mechanism
since an uncooperative or crashed caller would leave the wedge. It belongs
alongside the gateway that would send it, and should be reviewed together with
the state-version horizon deferred from §8.4 and the reference horizon deferred
from the reclamation contract - all three are the same question asked about
different arenas, and Phase 5 is where the venue simulator finally makes the
numbers derivable.

The 64-entry venue-event horizon is a reviewed capacity, not a derived one. No
derivation is claimed, and none should be implied in later evidence.

### Finite identity horizons implemented — 2026-09-09 (Claude)

Implements §8.6. Release 25/25 and the FULL host Debug ASan+UBSan suite 25/25,
not the focused subset. `TradingState` 81,168 bytes (752 headroom), maximum
canonical snapshot 65,259 bytes (277 headroom).

`CallerCursor` records the highest `RequestSequence` whose receipt was actually
RECLAIMED for that caller. The first implementation used highest-observed and was
rejected during the work: it refuses a low sequence the cluster never held, which
enforces per-caller monotonicity as a side effect of expiry - a stricter and
different contract than approved, and it broke fixtures with every right to
number their requests freely. The kernel must refuse only what it actually forgot.

Source-event receipts turned out to be a FOURTH arena with the same defect, found
only because the lifetime test drove past 128. `SourceCursor` already holds the
watermark and admission independently refuses a regression, so they are reclaimed
on the same pattern with no new state.

Receipt pinning: authorization receipts while their order group lives, since
`materialize_order_effect` needs them; fill and venue-event receipts
unconditionally, since a redelivered PARTIAL fill would book twice and
reservation arithmetic does not catch it. Everything else, including every
refusal, is reclaimable.

DECISION 4 IS NOT IMPLEMENTED and is pinned by a failing-loud test. A fill
receipt has all five of its slots used and none names the cluster order, so
tying its expiry to order-group reclamation needs the canonical result to carry a
cluster order id - and the result is at 126 of 128 bytes. That is a byte-contract
change awaiting review. Until then fills still stop at `kMaxVenueEventReceipts`
and still strand existing risk.
`test_venue_event_horizon_still_bounds_order_completion` asserts the bound and
instructs its own deletion once lifted.

Suite-wide impact worth expecting again. Five suites needed fixture repair, in
two recurring shapes: fixtures that filled an arena with records that are now
reclaimable, which needed PINNED fillers to keep testing the capacity path rather
than the sweep; and fixtures that reused request identities in permuted or
descending order, which the watermark correctly refuses. Both are premise changes
rather than defects, and this is now the fifth and sixth time reclamation work
has invalidated an arena-filling fixture. A shared `unsweepable_full_arena()`
helper is overdue.

Two self-inflicted test bugs are worth recording because both read exactly like
kernel defects. A diagnostic message that re-ran the command under test corrupted
the run it was measuring, because `require` builds its message argument EAGERLY.
And `digest(static_cast<std::uint8_t>(decision_id))` makes every 256th DecisionId
hash to the all-zero digest, which `apply` correctly refuses as a missing digest -
it surfaced 120 cycles into a loop and looked like an arena bound.

### Historical pause — §8.7 resolved on 2026-09-10

Work stopped here on 2026-09-09 rather than taking the then-open decision
unilaterally. The resolution and implementation are recorded in the first
checkpoint and architecture §8.7; the analysis below is retained as history.

The open question: `apply_observe_fill` refuses at `kMaxVenueEventReceipts`, and
that bound fails in the WRONG DIRECTION. Every other bounded arena stops NEW work
when full; this one stops EXISTING orders completing, stranding live risk with no
resolution path.

The §8.6-approved fix - reclaim a fill identity when its order group is reclaimed
- turned out to be unimplementable as written. A fill receipt cannot be traced to
its order: five slots, all used, none naming the cluster order, and the canonical
result is at 126 of 128 bytes. Repurposing `venue_id` by making the dedup key
session-scoped was examined and rejected as a SAFETY REGRESSION, because a
redelivery after a gateway reconnect carries a new session id and would escape a
session-scoped key.

Recommendation in §8.7, costing zero bytes: keep the horizon but move the
pressure from fills to AUTHORIZATION, refusing new orders when remaining
venue-event capacity would not cover completing the orders already live. The
failure becomes "stops quoting" rather than "cannot get out". This is not a new
idea - §11 already reserves bounded capacity for risk-reducing operations, and
this applies the same principle one layer down. Pair it with a free self-heal:
fill receipts are reclaimable once no reservation is unreleased, because with
everything released a redelivery meets `fill_quantity_conflict` anyway.

Deferred alternative, also costing no result change: a per-`(venue, session)`
watermark on `venue_sequence`, the same pattern as the caller cursor. It needs a
small arena and depends on whether the venue supplies monotonic sequences, which
is unknown until a venue exists to ask. Phase 5, alongside the three horizon
questions already parked there.

A reviewer should decide three things, listed at the end of §8.7: whether the
stop-quoting trade is right, how much completion capacity to reserve (two venue
events per live order is a defensible start), and whether the flat-state reclaim
earns its place given how rarely a quoting maker is genuinely flat.

State at the pause: Release 25/25, full host Debug ASan+UBSan 25/25,
`git diff --check` clean, HEAD `8e8f661`. `TradingState` 81,168 bytes (752
headroom), maximum canonical snapshot 65,259 bytes (277 headroom).

Remaining Phase 4 work: broader accounting and reconciliation evidence, metrics and
age/latency evidence, an optional Phase 4 mirror model to complement this corpus,
the complete sanitizer gate, pinned ARM64 and physical Pi gates, and a Phase 4
acceptance evidence document.

Next bounded work: finish the approved serial-replacement acceptance matrix.
Start with a cancel/reconcile cycle containing a risk-limit update: reject a
stale-version successor proposal; freshly evaluate and authorize; verify full
effect bytes and economic/identity invariants across snapshot restore/replay.
Audit existing coverage first. Add a genuinely failing test for any uncovered
defect before fixing it; do not fabricate RED for behavior already implemented.
Continue the remaining accounting/capacity, metrics and independent Phase 4
model/property corpus work afterward, within the agreed phase ownership.

Standing invariants: sweep BEFORE every arena lookup/borrow; terminal is not
necessarily risk-resolved; payload reclamation must not erase retry/conflict
identity; configuration is validated before its non-throwing commit and cannot
rewrite balances. No wall-clock risk release or blind retry of unknown exposure.

Still open: unauthorized-decision retention, finite request/venue-event horizons,
broader accounting and reconciliation evidence, serial-replacement completion,
metrics/age/latency evidence, independent Phase 4 model, complete sanitizer gate,
pinned ARM64 and physical Pi gates, and a Phase 4 acceptance evidence document.
The Phase 1-3 generated model does not cover B1/Phase 4. No new decisions are
awaiting approval for the implemented B1 contract; review any NEW material byte
or economic contract before encoding. Keep educating the user at CTO/expert-C++
level, and do not pull gateway, Raft, WAL, venue adapter, strategy, netting or
optional V2.2 work forward. No live-trading readiness claim.

## Current branch and HEAD

- Branch: `v2/phase-4-risk-orders-outbox`
- Verified development HEAD: `aedd658ee0a7fe4e30a2338d2f953fc659306e29`
  (`Update Phase 4 handoff and changelog for takeover`)
- Working-tree increments after HEAD: approved serial-replacement contract,
  reclamation-identity/snapshot-accounting corrections, and B1 V5 configuration;
  see the takeover checkpoint above for the full uncommitted file inventory.
- Phase 4 development commits, in order:
  - `401c861` Add V2 Phase 4 risk reservation, orders, and outbox
  - `0491584` Reclaim resolved order groups from bounded arenas
- Phase 4 base commit: `c25c45fe0d05978b9855685622b39fccc1481b84`
- Handoff checkpoint: annotated tag `v2-phase4-handoff-start` (`330cfe1`),
  now an ancestor rather than the tip
- Accepted Phase 3 checkpoint: annotated tag `v2-phase3-accepted`, commit
  `8438d0d`

Run `git rev-parse HEAD` and `git show --no-patch --decorate HEAD` before
acting. Rollback point for all Phase 4 work is `330cfe1`.

## Dirty-worktree status

The following pre-existing work is intentionally uncommitted and must be
preserved. It belongs to passive model/market-data research and the proposed
Pi/NUC topology, not to Phase 4:

```text
 M docs/README.md
?? docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md
?? docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md
?? docs/adr/0002-two-machine-pi-nuc-deployment-topology.md
?? model/
```

`CHANGELOG.md` is no longer in this protected list: it carries committed Phase 4
entries plus current Phase 4 working-tree updates. Preserve its earlier
model/market-data section, which rode along in the committed handoff.

Do not delete, reset, stage, or include these paths in Phase 4 commits. Inspect
`git status --short` at the start and before every commit. Commit Phase 4 files
by explicit path rather than using `git add .`.

## Last accepted phase

Phase 3—policy artifacts and authoritative decision evaluation—is accepted on
the host and physical Raspberry Pi 5.

Accepted behavior:

- an admitted canonical `DecisionFrame` is evaluated deterministically against
  an atomically activated feature-schema/executable-policy/instrument/venue
  bundle;
- bounded rules use reviewed array-order, first-match precedence;
- strategy control modes dominate economic policy;
- a matched template resolves to an exact `EconomicActionProposal` using
  checked fixed-point values and explicit venue/instrument constraints;
- authoritative and shadow outcomes, provenance, and exact proposals are
  retained in bounded hot/cold decision records;
- canonical command/result/snapshot schema V3, direct apply, snapshot replay,
  and generated-model replay agree;
- authoritative apply remains pure, bounded, transport-free, storage-free, and
  venue-free; and
- cache-line/layout ceilings are enforced, including a 64-byte hot decision
  record.

Acceptance evidence:

- Git tag: `v2-phase3-accepted`
- Evidence document:
  `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md`
- Physical Pi evidence directory:
  `~/.local/share/velocita/v2-phase3-acceptance-pi-gates/20260907T212930Z`
- Accepted source root:
  `f2d350757c9f2f71f0eab4e2600af982bea44d56027082fafc70764c77073f5b`
- Pi result: all Phase 3 suites passed; `throttled=0x0` before/after;
  43.55 C -> 49.60 C.
- In-memory Pi measurements only—no Raft/WAL/network:
  32-rule worst case p50/p99 `222/241 ns`, action resolution `185/204 ns`,
  authoritative reducer `740/778 ns`.

The measurements above are development signals, not end-to-end throughput or
venue-latency claims.

## Current phase

Phase 4—risk reservation, orders, and transactional outbox.

Objective: make an accepted authoritative economic decision atomically create
all four of the following, or none of them:

1. risk reservation;
2. cluster-owned order intent;
3. retained idempotency receipt/result; and
4. committed outbound effect with deterministic `EffectId` and payload digest.

Phase 4 owns end-state guarantees G-010 through G-020 and contributes to
G-021 through G-026 in `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`.

## Implemented behavior relevant to Phase 4

Already available as inputs/foundations:

- strong IDs including `DecisionId`, `ClusterOrderId`, and `EffectId`;
- bounded `EconomicActionProposal` created by Phase 3;
- canonical `OrderEffect` contract and payload codec as a boundary type;
- active policy, executor generation, kill epoch, and strategy mode;
- request deduplication and bounded decision hot/cold records;
- canonical kernel command/result/snapshot encoding and deterministic replay;
- compile-time object/payload ceilings and target cache-line reporting.

Phase 4 authorization, canonical V4 state, fill accounting, the cancel/unknown
lifecycle, slot exclusivity, and bounded arena reclamation are GREEN on the host
and COMMITTED (`401c861`, `0491584`). This is a development checkpoint, not a
completed Phase 4 acceptance claim - no sanitizer, ARM64, or physical Pi gate has
been run and no Phase 4 evidence document exists:

- direct amend/replace is refused with `replacement_not_supported`; wire kind 16
  stays allocated. Native amend and overlapping replacement remain disabled.
  Safe serial replacement is a REQUIRED Phase 4 acceptance item: cancel, final
  reconciliation, fresh authoritative decision, fresh authorization. Refusal
  alone does not satisfy it; see the approved scope and acceptance matrix below;
- `AuthorizeDecision` enforces at most one unresolved cluster order per account,
  instrument, venue, and logical order slot, keyed on the reservation not being
  released;
- `sweep_resolved_order_groups` reclaims fully resolved order groups at both
  admission points, and `TradingState::reclaimed_group_count` records how many
  have ever been swept;

- `AuthorizeDecision` accepts only a retained authoritative Phase 3
  `place_limit` action with an exact valid proposal;
- current risk-policy, risk-state, and position versions must match;
- checked gross quantity, full-margin, and ceiling-rounded fee holds are
  validated before mutation;
- one reservation, cluster order, and pending normalized outbox record are
  created in one non-throwing commit section;
- domain-separated SHA-256 derivation supplies stable 64-bit order/effect IDs,
  with zero/collision failure closed;
- request replay returns the retained identities and exposure, while a
  same-request/different-payload retry remains a typed conflict; and
- risk, staleness, capacity, shadow, no-action, and rejected-decision failures
  leave Phase 4 counts and economic balances unchanged.
- kernel command/result schema V4 explicitly encodes `AuthorizeDecision`,
  `ObserveFill`, `RequestCancel`, `RequestReplace`, and `ObserveOrderEvent`; V3 is not
  reinterpreted;
- snapshot schema V4 retains risk accounts, reservations, orders, outbox
  operation/state, position quantity, and fill/venue-event result identities;
- snapshot decision records omit only identities proven equal to the retained
  frame/hot record, keeping the current worst case at 65,121 bytes under 64 KiB;
- accepted partial fills atomically move signed quantity into position,
  proportionally and conservatively reduce the remaining margin/fee/notional
  holds, and increment `PositionVersion`;
- `(VenueId, ExecutionId)` suppresses duplicate fill economics and conflicts
  on a different retained command digest; accepted fill and venue-event
  identities share a reviewed 64-entry logical horizon inside the existing
  request-receipt arena;
- cancel intent appends a distinct generation-2 cancel effect and remains fully
  risk-bearing; `effect_unknown` also retains the hold; only accepted cancel or
  fill evidence releases residual economics; and
- the tested fill/cancel race preserves a filled terminal result even when
  cancel evidence arrives later.

The focused target is `v2-risk-order-outbox-tests`. Intended compile/runtime
REDs were observed for authorization, V4 codec/snapshot state, fill contracts,
cancel/unknown contracts, and cancelled-quantity snapshot conservation. The
focused suites and all 23 Release tests pass. Current host layout is
81,024-byte `TradingState` under the unchanged 81,920-byte ceiling, with
112-byte risk accounts, 64-byte reservations, 40-byte orders, 64-byte
normalized outbox records, and 896 bytes of remaining provisional headroom.
`ApplyResult` remains 112 bytes, canonical results remain 128 bytes, and the
command ceiling remains 1,920 bytes.

Historical review corrections before `401c861` on 2026-09-08 (replacement
branches described below were subsequently removed; these are not current
replacement capabilities):

- New submit/replace authorization rechecks current strategy mode, kill epoch,
  and executor generation. Historical retries still return historical outcomes.
- Delayed acknowledgements/unknown/rejections cannot regress tested terminal
  orders, partial-fill knowledge, or newer cancel intent.
- Provisional replacement keeps old active terms until acknowledgement and
  holds componentwise maximum old/new exposure; acknowledgement/rejection
  collapses that hold. This is NOT a proven venue-neutral safe replacement:
  maximum is sufficient only if old/new executions are mutually exclusive.
- A fill during pending/unknown replacement preserves risk and marks the
  order/current effect reconciling. Ordinary acknowledgement cannot clear it.
  This defers real fill accounting; reconciliation itself remains unimplemented.
- Submit decision retries after full fill return the retained original receipt,
  not the consumed reservation. Replacement decision retries after ack/reject
  likewise return the original effect/exposure without another mutation.
  Receipt lookup is bounded O(receipts * effects), allocation-free, with no
  added resident bytes or canonical schema changes in these retry fixes.

Latest host validation: full `build-phase4` Release build and 23/23 CTest suites
pass. This is development evidence, not Phase 4 acceptance. Existing generated
model coverage is Phase 1–3, not a Phase 4 reference model. Resolved order groups
are reclaimed from the 16-slot arenas; unauthorized decisions and the separate
receipt capacities still bound continued admission. Snapshot headroom is only
415 bytes; do not silently expand capacity.

Not implemented yet:

- complete Phase 4 acceptance evidence for command-configured risk limits (B1
  reducer/codec implementation is now present; see the checkpoint below);
- final review of the normalized `OrderEffect` boundary produced from retained
  outbox/decision state;
- reclamation of decisions that were never authorized (no-action and rejected
  evaluations retain a record no order will ever reference; in a live market
  maker most frames are no-action, so the decision arena can still fill). Needs
  the `DecisionId` conflict-detection horizon settled in Phase 5;
- complete safe serial replacement lifecycle/economic acceptance evidence
  (REQUIRED by the revised 2026-09-08 user approval; native amend and overlapping
  replacement remain disabled);
- ordered venue evidence across fills/order events and complete reconciliation;
- position/margin/fee accounting beyond signed filled position quantity and
  remaining open-order holds;
- broader acknowledgement/reject/reconciliation transition coverage;
- Phase 4 model/property/replay corpus and target gate.

An existing `OrderEffect` value/codec is not evidence that an effect can yet be
created authoritatively. It is only a validated boundary contract.

## Standing invariants added during Phase 4

Break either of these and the failure is silent.

1. **Sweep before you borrow.** `sweep_resolved_order_groups` compacts the
   decision, order, reservation, and outbox arenas. Any index resolved or
   reference borrowed BEFORE it runs stays in bounds afterwards while naming a
   different, possibly cleared, slot. Both call sites therefore sweep before any
   arena lookup, and a new lookup must not be inserted above them. This was a
   real defect: `apply_authorize_decision` borrowed
   `const auto& cold = state.decision_cold[decision_index]` before the capacity
   check, and an order reserved from a zeroed proposal. No crash, no sanitizer
   report; it surfaced fifteen requote cycles later as `fill_quantity_conflict`.
2. **Terminal is not resolved.** Reclamation and any future cleanup must test
   whether a record's RISK is resolved, not whether its state machine stopped.
   `conflict` and `reconciling` are terminal-or-stuck but hold exposure of
   unknown size. Prefer that formulation over enumerating states, so future
   terminal-but-unresolved states inherit the correct behavior.

## Decisions that must be reviewed before broad implementation

Keep the Phase 4 design minimal, bounded, and MVP-focused. Present the proposed
contracts to the user before locking them into canonical bytes:

1. exact risk-account dimensions and conservative reservation formula for
   submit, cancel, and replace;
2. minimum cluster order and reservation lifecycle states;
3. deterministic derivation and collision handling for `ClusterOrderId` and
   `EffectId`;
4. bounded capacities for orders, reservations, fill/event receipts, and
   outbox effects, including full behavior and provisional Pi/NUC headroom;
5. replacement semantics that cannot double reserve or briefly under-reserve;
6. partial-fill, cancel/fill-race, and unknown-submission accounting rules;
7. exact additions to `Command`, `ApplyResult`, canonical schema, snapshot, and
   state digest; and
8. which Phase 4 state is hot versus cold and its cache-line/arena budgets.

Prefer the conservative fail-closed choice when evidence is incomplete. Do not
silently clamp an unsafe proposal into an accepted order; return a typed reason.

## Exact next RED test

MVP replacement scope REVISED AND APPROVED by the user on 2026-09-08:
native amend and overlapping replacement remain disabled, but safe SERIAL
replacement remains a Phase 4 acceptance requirement. This supersedes the
earlier blanket deferral and the claim that refusal alone satisfies G-018.

Rationale: if old and new orders can both execute, covering both exposures is
legitimate risk coverage, not erroneous double-counting. `max(old, new)` can
under-reserve that case. G-011 and safe replacement are not inherently
incompatible. This MVP chooses serialization to avoid overlap and dependence on
native-amend semantics; this decision does not rely on an unverified venue API
claim. No gateway, venue adapter, or external execution is pulled into Phase 4.

Required serial-replacement acceptance matrix (coverage to be audited and
completed by TDD, NOT claimed GREEN by this documentation change):

1. Cancel-pending, unknown, reconciling, and conflict states retain unresolved
   risk and block a fresh authorization on the same logical slot.
2. Late/partial fills update position exactly once. Accepted final reconciliation
   resolves the old exposure before its remaining reservation is released;
   missing or contradictory evidence never causes a timeout-based release.
3. Only after resolution may a fresh authoritative decision create a new order,
   reservation, receipt, and submit effect atomically. Recheck current policy,
   strategy mode, kill epoch, executor generation, risk and position versions,
   and available limits. Do not auto-submit a proposal queued before the wait.
4. Exercise fill/cancel/final-evidence permutations and duplicates under fresh
   request IDs. Assert conservation and identity continuity, not only return codes.
5. Repeat across compaction and snapshot restore/replay. Compare complete
   canonical bytes for every retained pending/unknown effect against independently
   constructed expected payloads; a historical DecisionId must not gain a second
   authorization or alias a new order/effect after reclamation.
6. Keep direct `RequestReplace` refusal tests: no economic mutation or effect.
   These are negative capability tests, not proof of the serial lifecycle.

Execution uses successive reducer transitions, never a blocking wait inside
`apply`. No new canonical command or schema is approved by this scope decision.

Implemented (host GREEN, all 23 CTest suites pass):

- `ApplyCode::replacement_not_supported`: `apply_request_replace` refuses before
  touching economics. Wire kind 16 stays allocated so reintroducing replacement
  needs no renumbering.
- `ApplyCode::order_slot_occupied`: at most one unresolved cluster order per
  `(account, instrument, venue, logical order slot)`, enforced in
  `apply_authorize_decision` before any mutation. Occupancy is defined by a
  reservation that is not `released`, because retained risk is what G-018
  constrains. This closes a real defect: `ClusterOrderId` is derived from the
  varying `DecisionId`, so cancel-then-authorize previously created a second
  order and a second reservation on the same slot - violating the chosen
  no-overlap policy. The RED for
  `test_fresh_submit_cannot_bypass_unresolved_cancelled_slot` observed exactly
  that.
- Deleted: the `max(old, new)` hold in `apply_request_replace` (276 lines) and
  the now-unreachable replace acknowledgement/rejection branch in the observation
  handler (140 lines).
- Deleted: `ClusterOrder::active_filled_baseline`, whose only purpose was
  reconstructing an old residual after a rejected replacement. Every read lived
  in the deleted branch. Removed from the struct, snapshot validation, encode,
  and decode; `kStateSnapshotClusterOrderBytes` 43 -> 35.
- Removed five replacement-economics tests; converted the outbox materialization
  test to a submit/cancel effect pair, and the replacement control-recheck
  assertion to an unconditional-refusal assertion. This proves the disabled
  capability, not safe serial replacement or its control rechecks.

Measured layout after the increment:

```text
ClusterOrder            48 -> 40 bytes
TradingState        81,144 -> 81,016 bytes   (headroom 776 -> 904)
max canonical snapshot 65,241 -> 65,113 bytes (headroom 295 -> 423)
```

The snapshot headroom increase is deliberate and should be spent on B2 below,
not on new capacity.

Two new tests in `v2-risk-order-outbox-tests`:
`test_request_replace_fails_closed_without_economics` and
`test_fresh_submit_cannot_bypass_unresolved_cancelled_slot`. Both assert
economics are byte-unchanged on refusal and hold across snapshot restore. The
second also proves the slot becomes reusable only after `cancel_reconciled`
final evidence releases the residual.

This is unaccepted host development evidence. No sanitizer, ARM64, or physical
Pi gate has been run for it, and no Phase 4 evidence document exists yet.

B2 contract decisions approved on 2026-09-08:

1. reclamation unit is an order and its reservation as a PAIR, using dense
   compaction with one index-fixup pass, not a free list. `ClusterOrder`
   references its reservation by ARRAY INDEX, so compacting reservations
   independently would silently repoint orders at another order's risk. The
   existing `static_assert(kMaxRiskReservations == kMaxClusterOrders)` already
   encodes the 1:1 relationship;
2. reclaimable requires terminal AND unreferenced. Terminality alone is never
   sufficient;
3. the reference check is a bounded graph traversal evaluated across all arenas
   BEFORE anything is reclaimed, never reclaim-then-check. An order,
   reservation, or decision may not be reclaimed while any outbox effect in
   `pending`, `claimed`, `attempted`, `unknown`, or `reconciling` still needs it
   for `materialize_order_effect`;
4. the trigger is an implicit sweep on admission, only when the target arena is
   full, failing closed with the existing typed capacity code if the sweep frees
   nothing. Deterministic: no clock, no randomness, no allocation, one bounded
   pass;
5. the reclaimed count is reported in `ApplyResult` and exposed as a metric, so
   a sweep is an observable event rather than a silent state-digest change;
6. the horizon is REFERENCE-based, not time-based. The arena being full is the
   trigger and "nothing can reference this" is the safety condition, so no time
   horizon is required. An optional state-version guard exists as a configured
   parameter defaulting to unrestricted. It is measured in state versions, never
   seconds, because the reducer may not read a clock. REVISIT IN PHASE 5: a
   defensible bound cannot be derived today because the gateway retry horizon
   and the venue reconciliation window are both unknown until the venue
   simulator exists; and
7. scope is orders, reservations, decisions, and outbox effects. Request
   receipts (256) and venue-event receipts (64) keep their existing horizons and
   are a separate question.

Open question before the RED: whether B2 lands before or after the Phase 7 WAL.
Compaction removes records from the bounded working set, not from history - but
Phase 7 does not exist yet, so today there is no durable log behind the working
set and reclamation genuinely loses data. Audit and analysis are served by the
WAL plus Phase 10 projections, never by `TradingState`, which is deliberately
bounded and is not the archive.

B2 is GREEN on the host (23/23 CTest suites). `sweep_resolved_order_groups`
reclaims fully resolved order groups at both admission points, and
`TradingState::reclaimed_group_count` records how many have ever been swept.

One defect was found by the B2 REDs and fixed: `apply_authorize_decision`
borrowed a reference into `decision_cold` before the capacity check, so running
compaction there left the reference in bounds while naming a different, cleared
slot, and the order reserved from a zeroed proposal. Silent - no crash, no
sanitizer report. Both sweep call sites now run before any index is resolved or
any arena reference is borrowed. Preserve that ordering: adding a lookup above
the sweep reintroduces the hazard.

Still open in B2 scope: decisions that are never authorized (no-action and
rejected evaluations) are retained, because they might still be authorized. In a
live market maker most frames produce no-action, so the decision arena can fill
with records no order will ever reference. Reclaiming them needs the DecisionId
conflict-detection horizon settled, which is the Phase 5 revisit above.

Layout after B2: `TradingState` 81,024 bytes (896 headroom), maximum canonical
snapshot 65,121 bytes (415 headroom).

### Review corrections: reclamation identity and snapshot accounting

Implemented in the working tree on 2026-09-08; not a Phase 4 acceptance claim.
Both permanent regressions were run RED before production edits:

- `test_reclaimed_decision_retains_identity_and_authorization` failed with
  "reclaimed decision retry lost its original authorization receipt" after 20
  real evaluation/authorization/fill cycles forced reclamation.
- `test_snapshot_rejects_unbacked_risk_aggregates` failed with
  "snapshot accepted an aggregate hold inconsistent with live reservations".
  The test repairs the altered snapshot's integrity digest, so rejection must
  come from economic validation rather than checksum failure.

GREEN corrections:

- Applied evaluation receipts retain the decision digest and outcome after its
  hot/cold records are swept. New-request retries return `already_applied` without
  recreating a proposal; different retained digests return `decision_id_conflict`.
- Authorization retries recognize the original submit receipt using the existing
  generation-1 submit EffectId derivation, independently of reclaimed outbox
  records. Cancel receipts share the result tag but are not submit outcomes.
- Snapshot validation joins reservation → order → immutable decision proposal →
  account/instrument. Every reservation must have one owner, and checked sums of
  remaining quantity, margin and fee must exactly match each risk account.
  Missing accounts, orphan reservations, mismatched totals and overflow fail
  `invalid_state`; rejected decoding leaves destination state unchanged.
- Tests cover both direct and restored identity retries, conflicting evaluation,
  fresh authorization with a distinct materializable effect, full canonical effect
  and final snapshot byte equality across restore, all three aggregate fields,
  per-account ownership, orphan holds, and all three accumulator overflows.
- Corrected the maximum-size snapshot fixture's accounting: its orders all belong
  to account zero, so its holds do too. No capacity or encoded size was reduced.

Validation: full Release 23/23; focused Release 2/2; focused host Debug ASan+UBSan
2/2 using `build-phase4-review-sanitized`, `-fsanitize=address,undefined` and
`-fno-omit-frame-pointer`. This is NOT the complete Phase 4 sanitizer acceptance
gate; pinned ARM64, physical Pi, and Phase 4 evidence remain outstanding.

No canonical schema, field, ID derivation, or resident capacity changed.
Evaluation/authorization receipt scans are bounded O(request receipts); snapshot
ownership joins are bounded O(orders * (decisions + risk accounts)), in addition
to existing uniqueness/link checks. Four temporary totals use 96 stack bytes.
State remains 81,024 bytes; maximum snapshot remains 65,121 bytes.
Identity safety still depends on retaining receipts: future receipt reclamation
must preserve the declared duplicate/conflict horizon. No receipt expiry or
indefinite-operation claim was introduced. Sweep-before-borrow remains mandatory.

### B1 checkpoint: replicated risk-limit configuration (2026-09-09)

The eight B1 choices were approved, including full-config replacement after
explaining that masked patches can also be atomic but add unnecessary partial-
update semantics. Authoritative operator/user-doc reference:
`docs/V2_ARCHITECTURE_CONTRACTS.md`, section 8.1. Do not reconstruct those rules
from this handoff's historical text.

Implemented: `ConfigureRiskLimits`, kind 17, 88-byte body/180-byte framed command;
kernel command/result and snapshot schemas V5, with risk-configuration receipt
tag 5 and unchanged resident sizes. Creation, strict compare-and-set updates,
kernel-selected next RiskVersion, control-generation checks, preservation of
balances, prospective per-order caps, aggregate hold coverage, quiescent rate/
policy changes, checked overflow and bounded refusal behavior are implemented.
No native amend, overlapping replacement, gateway, Raft or operator authentication
was added. V4 input is rejected rather than migrated; OrderEffect is unchanged.

Observed REDs, in order: undeclared ConfigureRiskLimits; runtime update failed
to advance exactly one version; runtime command lacked its 88-byte canonical
body. Each was followed by GREEN. Additional coverage checks stale decision
authorization, fresh authorization using command-created risk (not injected
balances), encoded replay, full-body byte expectations, result/snapshot receipt
round trips, old-schema refusal, malformed/truncated input, duplicate conflicts,
account/receipt capacity and invalid-rate/control/version boundaries.

V5 intentionally changes command/result/snapshot framing digests and the replay
goldens derived from them. Legacy source-command and empty-snapshot payload
projections retain their V4 golden checks. The Phase 1-3 generated model explicitly
does not model B1; the independent full Phase 4 model remains outstanding.

Next work after B1 validation: complete the approved safe serial-replacement
acceptance matrix. Start with a cancel/reconcile cycle containing a risk-limit
update, prove an old-version proposal cannot authorize the successor, then
evaluate/authorize freshly and compare full effect bytes through restore/replay.
Audit existing GREEN coverage before selecting the next genuinely failing case;
do not claim an already-passing test was observed RED. Capacity horizons, complete
accounting, metrics/model corpus and Phase 4 target gates remain open.

B1 validation: full Release 23/23; focused host ASan+UBSan 5/5 (risk/outbox,
snapshot, command/result codec, replay, and the Phase 1-3 generated model).
Builds: `build-phase4`, `build-phase4-review-sanitized`. This is not the complete
Phase 4 acceptance gate. TradingState remains 81,024 bytes; maximum snapshot
65,121 bytes; command and result ceilings unchanged. Reducer work is bounded by
four account entries and at most sixteen orders/decision lookups, plus existing
request-receipt handling. No allocation or new resident balances are introduced.

B1 naming/semantics decision approved and implemented on 2026-09-08:
`max_order_notional` is renamed `max_order_gross_notional` (one order's gross
notional ceiling); `max_reserved_notional` is renamed `max_reserved_exposure`
(aggregate reserved margin plus reserved fee budget). This is a behavior-preserving
source rename, not a new aggregate gross-notional limit. Member types/order,
snapshot field widths/order, schemas and risk comparisons remain unchanged.
The later B1 command/schema implementation above preserves these limit meanings.

Reconciliation contract approved and implemented. The runtime RED
`test_cancel_before_earlier_fill_does_not_lose_fill_evidence` failed with
"earlier venue fill delivered after cancel was lost from accounting".
Venue sequence 1601 is a one-lot fill, 1602 is cancellation; delivery is 1602
then 1601. Cancellation currently releases all remaining reservation; the late
fill is refused with fill_quantity_conflict because the reservation is released.
It is now GREEN: cancellation retains the hold and marks order/cancel effect
reconciling; a valid delayed fill consumes only its quantity and does not reopen
the order. Full fill may finish the order because no unfilled residual remains.

Approved economic contract: distinguish stopped
order from reconciled accounting. Cancellation without evidence of fill
completeness retains unresolved hold and enters reconciling; valid delayed fills
must be applied exactly once without reopening the order. Release unused residual
only after scoped final-order evidence establishes final cumulative filled quantity
and agrees with the locally accepted fills. Implemented `cancel_reconciled` kind
6 in VenueObservation schema V2, still 116 bytes: quantity is explicitly final
cumulative filled quantity for this kind; price/fee/execution ID must be zero,
quantity nonnegative, and venue order identity present. The observation is scoped
by venue, cluster order and current cancel effect. It requires matching locally
filled quantity and a reconciling cancellation (or matching already-full fill).
No position is inferred from a cumulative total. Mismatch returns
venue_observation_not_applicable, retaining risk. Evidence received too early is
not buffered for automatic completion: after missing fills arrive, the caller
must submit a new request; retrying the old request preserves its refusal.

Both plain cancellation and final reconciliation remain venue evidence, whose
authenticity/finality must be established upstream. No contiguous stream watermark
is implemented; a larger sequence alone is NOT proof of completeness. This does
not solve arbitrary contradictory terminal evidence or replacement reconciliation.

Observation boundary/fingerprint explicitly advance to V2: SHA-256 over ASCII
`velocita/venue-observation/v2` without NUL plus canonical V2 bytes. V1 observation
payloads are rejected. Kernel command/result/snapshot remain development V4, and
no fields/capacity were added. Tests prove hold preservation before delayed fills,
snapshot restore while pending, matching/mismatched final totals, request/event
dedup, and byte-identical direct vs encoded reconciliation replay snapshots.
Negative final quantity exposed an additional runtime RED: the typed refusal
could not serialize because kernel codec's ContractError range stopped at an old
enum member. Its range now includes all existing declared ContractError values.
No new error enum was introduced. All current changes remain unaccepted Phase 4
development, not sanitizer/ARM64/Pi acceptance.
Fresh full Release build and 23/23 CTest suites pass after reconciliation and
refusal-codec fixes. Layout report remains 112-byte result, 160-byte receipt,
128-byte wire result, 81,144-byte state and 65,241-byte maximum snapshot;
`git diff --check` is clean. Unrelated pre-existing dirty paths remain untouched.

Latest cross-kind identity checkpoint:
- `test_fill_cannot_reuse_an_order_event_identity` observed runtime RED: an
  acknowledgement and fill reused one venue/session/sequence. GREEN adds an
  existing event-receipt lookup before fill deduplication/economics, returning
  venue_event_identity_conflict. The test also verifies the guard after snapshot
  restore. All 23 Release suites passed before the reverse-direction test below.
- The reverse runtime RED: `test_order_event_cannot_reuse_a_fill_identity` failed
  with "cancel evidence reused a fill venue/session/sequence and released risk".
  A partial fill was accepted, then cancel confirmation reused that fill's stream
  identity and incorrectly released residual risk. This is now GREEN using the
  approved tagged metadata implementation below.
- Approved and implemented result/snapshot contract: use explicitly tagged
  16-byte result metadata for policy ID/version on policy-related results, and
  venue session/sequence on fill results. Preserve strongly typed accessors; do
  not silently put venue IDs in fields still named policy IDs. Existing fill
  results do not use those policy fields. `ApplyResult::metadata_words` stores
  two uint64 words, discriminated by existing detail_kind. Typed policy_id(),
  policy_version(), fill_session_id(), and fill_sequence() accessors return an
  absent ID for the wrong tag. No C++ union aliasing or new tag allocation.
  Canonical codecs read/write the same two positions; successful/duplicate fill
  results now require nonzero session/sequence. A separate codec runtime RED
  proved that missing stream metadata needed rejecting before this validation.
  Old unaccepted V4 fill results without stream identity fail validation; this
  amends development V4 semantics, not accepted V3 bytes. Phase 1–3 regression
  goldens remain unchanged.
- Shared retained-event lookup now covers applied fills and order events.
  Cross-kind conflicts and different executions reusing the same stream identity
  return venue_event_identity_conflict before economics. Execution-ID dedup remains
  independently enforced. Tests prove both directions across snapshot restore and
  distinguish identical sequence numbers in different sessions. One legacy race
  fixture incorrectly reused fill sequence 501 for cancel confirmation; it now
  uses distinct sequence 503 so it tests the intended race, not identity conflict.
- Full Release build and 23/23 suites pass. Measured result/receipt/wire-result
  sizes remain 112/160/128 bytes; resident state remains 81,144 bytes; max snapshot
  remains 65,241 bytes. No added resident capacity, sanitizer or target-gate claim.

Cancel contract approved and GREEN: whole-order cancel uses zero price/quantity,
retains scope/provenance and authorization-time controls, and hashes the full
20-word instruction preimage instead of eight identity/control words. Existing
proposal side/TIF/post-only are retained as metadata, not new trading parameters.
Cancel intent does not release risk. Full-byte tests cover partial fill, cancel
completion, control changes and snapshot restore; boundary validation rejects
nonzero cancel price/quantity. All 23 Release suites passed after this increment.
No additional resident fields or payload bytes were added. Cancel digests (and
snapshots containing them) change from the earlier unaccepted development draft;
old draft cancel digests fail reconstruction rather than being silently rewritten.

Observation fingerprint contract approved and GREEN. The copied-digest RED above
was observed, then fixed by `observation_fingerprint` in digest.hpp/v2_digest.cpp.
Current preimage (advanced for reconciliation): ASCII `velocita/venue-observation/v2`
without NUL, followed by the 116 canonical VenueObservation V2 bytes, including schema, all IDs/economic
fields, and evidence_digest. SHA-256 covers the entire preimage. Caller/request
identity is excluded so genuine cross-request retries have the same fingerprint.
The allocation-free implementation uses a fixed stack buffer and explicit LE
encoding, tested against the independent existing payload encoder plus SHA-256.

ObserveFill/ObserveOrderEvent producers must set CommandIdentity.payload_digest
to that fingerprint. Kernel apply verifies it before receipt lookup or economic
deduplication; mismatches return invalid_command without advancing state_version
or retaining a receipt. A separate runtime RED exposed wrong command-wrapper
reuse of a valid observation; wrapper/kind compatibility is now checked before
deduplication too. All other command digest semantics remain unchanged.
Tests cover same/cross-request alteration, correctly fingerprinted execution/event
conflicts, valid duplicate receipts after snapshot restore, and existing fill replay.
Full Release build plus all 23 CTest suites pass after this increment.
Resident layout and serialized field layouts are unchanged; observation digest
values now follow this contract rather than arbitrary caller values. Old unaccepted
development snapshots with unchecked observation digests are not migration-tested.
This proves content consistency, not venue authenticity or ordered evidence.

User approved OrderEffect schema V2 on 2026-09-08. The missing-post-only compile
RED is now GREEN: V2 is 166 bytes with the explicit one-byte PostOnlyMode appended
at offset 165. V1 is rejected, not reinterpreted. Unknown enum values and required
post-only with incompatible time-in-force fail validation. The enum moved from
action_template.hpp to contracts.hpp to share one definition without a circular
include; the in-memory field occupies padding beside time-in-force. This boundary
change does not change kernel V4 command/result/snapshot bytes or resident state.

`test_outbox_reconstructs_full_immutable_payload` first failed compilation for
the missing materialization API, then passed with `materialize_order_effect`.
It independently constructs expected submit/replace payloads and compares every
encoded byte in pending, unknown, and acknowledged/control-changed states, also
after snapshot restore at each stage. Corrupt digests, changed proposal post-only,
changed authorization generation, missing identity, and absent authorization
receipts fail closed. The API returns an owning optional value, is noexcept and
allocation-free, and scans O(effects + receipts + decisions), all bounded.
Canonical boundary encoding remains the existing vector-based outer-layer API;
it is not called by the reducer or materializer.

Authorization-time metadata comes from the original applied receipt, not mutable
order/current controls; economics come from the effect's own retained proposal.
The digest is recomputed using the existing 20-word domain-separated semantic
preimage (including post-only), not recursively hashing the serialized digest
field. Full-byte comparisons are additional evidence, not replaced by hash checks.
No retained receipts/decisions may be reclaimed while their effects need them.
Cancel materialization has subsequently been implemented as recorded above.
No sanitizer/ARM64/Pi claim is made.
After this schema/materialization increment, a fresh full Release rebuild and
all 23 CTest suites passed; `git diff --check` was clean. Layout remains
81,144 bytes for TradingState and 65,241 maximum canonical snapshot bytes.

The eight contract decisions were reviewed and approved on 2026-09-07.
User authorized continued development and corrective review. Earlier replacement
wording claiming old-plus-new exposure must never be held was too strong:
coexistence may require both holds. Do not freeze that assumption into canonical
contracts. Historical replacement fork below is now resolved by the approved
serial-replacement scope above; it is not a separate next-RED instruction:

```text
Keep RequestReplace fail-closed and unresolved-slot refusal tests. Complete the
serial-replacement acceptance matrix above using cancel/reconciliation and fresh
authorization; do not restore native amend or overlapping replacement branches.
```

Fingerprint, cross-kind identity, and first delayed-cancel reconciliation REDs
above are GREEN. Fill receipts retain session/sequence and execution identity.
Tested terminal evidence permutations are GREEN; replacement scope is resolved,
but the full serial-replacement acceptance matrix is not yet proven;
no extra state-capacity budget is approved.
Review any material
new byte-contract choice with the user before freezing it. Then prioritize
observation-content identity, safe replacement/reconciliation, snapshot invariants,
and a real Phase 4 model/replay corpus. No gateway, venue adapter, Raft, WAL,
netting, strategy, or optional V2.2 work belongs in these increments.

## Acceptance evidence required for Phase 4

The phase cannot close until retained evidence proves:

- no effect without reservation and order intent;
- duplicate request/decision returns the same effect, never a second mutation;
- insufficient or stale risk creates no order/effect;
- reservation/filled/remaining quantities conserve under partial fills;
- unknown submission and cancel-pending remain risk-bearing;
- safe serial replacement meets the approved acceptance matrix above: retain old
  risk through cancellation, book late fills exactly once, reconcile, then freshly
  decide and authorize under current controls; native amend and overlap remain
  disabled. Refusal and slot exclusivity alone are insufficient evidence;
- replay reconstructs byte-identical pending/unknown effects;
- snapshot retains receipts/effects through the declared retry horizon;
- bounded full policies fail atomically; and
- fault/property/model tests cannot create an unreserved effect, negative
  reservation, duplicate economics, or replay mismatch.

Evidence must identify commit/source root, canonical schema/digest changes,
Release and sanitizer results, layout/capacity report, ARM64 container result,
physical Pi result, power/temperature state, limitations, and rollback point.

## Unresolved failures and provisional limits

- No known failing accepted Phase 3 test or physical Pi gate.
- Phase 4 is in progress; the host RED/GREEN evidence above is not yet the
  sanitizer/ARM64/physical-Pi acceptance gate.
- The `TradingState` 80-KiB ceiling and current capacities are provisional until
  deferred NUC capacity validation; do not increase them silently.
- Phase 3's benchmark excludes Raft, WAL, networking, and venue I/O.
- TrueMarkets resting-order/post-only/order-book capability remains unconfirmed;
  that blocks adapter/live claims, not Phase 4's venue-neutral OMS work.
- The proposed two-machine topology is not accepted high availability.
- A logical order slot held by an order stuck in `reconciling` is not
  recoverable without operator action, and no operator action exists yet. There
  is no replicated timeout, no escalation, and no override command. The slot
  silently stops quoting and is indistinguishable from a quiet market until the
  Phase 4 metrics in C4 exist. Tracked as G-074a in
  `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`; the resolution is an operator-
  driven replicated transition plus a runbook entry, owned by Phases 8 and 10.
  Do not "fix" this in Phase 4 with a wall-clock timeout in the reducer: that
  breaks determinism, and releasing a hold without venue evidence of final
  cumulative filled quantity is the G-011 violation reconciliation exists to
  prevent.

## Important ADRs and contracts

Read only these first; chase additional references only when the current task
requires them:

1. `docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`, especially sections 2,
   3, 4, and 9 (Phase 4).
2. `docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md`, especially
   transactional outbox, authority, persistence, and non-negotiable invariants.
3. `docs/V2_ARCHITECTURE_CONTRACTS.md`, especially sections 3 and 7–10.
4. `docs/V2_END_STATE_GUARANTEE_CHECKLIST.md`, especially G-010–G-026 and
   deviation tripwires.
5. `docs/V2_PHASE3_DECISION_EVALUATOR_DESIGN.md` for the accepted input boundary.
6. `docs/evidence/V2_PHASE3_EXECUTABLE_POLICY_AND_MEASUREMENT.md` for the exact
   accepted checkpoint and limitations.
7. `docs/V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md` only to avoid pulling deferred
   work into V2.

Primary C++ starting points:

- `include/velocita/v2/kernel.hpp`
- `src/v2_kernel.cpp`
- `include/velocita/v2/action_template.hpp`
- `include/velocita/v2/contracts.hpp`
- `include/velocita/v2/kernel_codec.hpp`
- `src/v2_kernel_codec.cpp`
- `include/velocita/v2/state_snapshot.hpp`
- `src/v2_state_snapshot.cpp`
- `tests/v2_decision_kernel_tests.cpp`
- `tests/v2_replay_tests.cpp`
- `tests/v2_model_replay_tests.cpp`
- `tests/v2_layout_tests.cpp`

## Commands needed to continue

Verify the handoff:

```bash
cd /Users/vishaljha/aeron-raft-pi
git status --short
git branch --show-current
git rev-parse HEAD
git rev-parse v2-phase4-handoff-start
git log --oneline --decorate -8
git diff --check
```

Read the bounded context:

```bash
sed -n '1,260p' docs/CURRENT_HANDOFF.md
sed -n '442,484p' docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
sed -n '1,240p' docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md
sed -n '1,340p' docs/V2_ARCHITECTURE_CONTRACTS.md
sed -n '45,115p' docs/V2_END_STATE_GUARANTEE_CHECKLIST.md
```

Run the existing host regression baseline using an out-of-tree build directory
(do not overwrite an existing user build):

```bash
cmake -S . -B build-phase4 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-phase4 -j2
ctest --test-dir build-phase4 --output-on-failure
```

Before a physical Pi gate, synchronize explicitly while preserving Pi-local
Git/build/evidence state:

```bash
rsync -av --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'build-*/' \
  ./ bhaikapi@100.84.151.116:~/aeron-raft-pi/
```

Do not invent a Phase 4 Pi acceptance command until its gate script and
evidence contract exist. The previous accepted Phase 3 gate is
`deploy/run-v2-phase3-acceptance-pi-gate.sh`; it is a regression reference, not
a Phase 4 acceptance gate.

## Explicit non-goals for Phase 4

- no execution gateway or external venue calls (Phase 5);
- no generic V2 Raft integration (Phase 6);
- no asynchronous WAL, block arena, snapshot publication, or compaction work
  beyond extending deterministic in-memory snapshot bytes (Phase 7);
- no executor fencing, automatic promotion, kill transport, or emergency lane
  implementation (Phase 8);
- no TrueMarkets order adapter, credentials, or live/paper order submission
  (Phase 9);
- no Grafana/release-operating expansion beyond metrics contracts (Phase 10);
- no strategy fitting, profitability claim, leaderboard claim, or real-money
  activation (Phase 11);
- no cross-strategy portfolio netting; it is explicitly deferred to V2.1;
- no `valid_state` performance rewrite without measurement; deferred to V2.1;
- no good-to-have feature that is not necessary for the Phase 4 invariants;
  defer such work to V2.2.

## New-session bootstrap prompt

Copy the prompt below into the new session:

```text
Work in /Users/vishaljha/aeron-raft-pi on Phase 4: risk reservation, cluster
orders, and the transactional outbox.

Read completely:
- AGENTS.md, if present
- docs/CURRENT_HANDOFF.md
- sections 2, 3, 4, and 9 of docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md
- the directly referenced ADR/contracts listed in the handoff

Inspect Git status, branch, HEAD, the v2-phase4-handoff-start tag, and recent
commits to verify the handoff. Preserve every pre-existing dirty path listed in
the handoff. Never use git add . and do not reset or delete unrelated work.

Do not reconstruct the project from historical chat. Git, tests, ADRs,
contracts, evidence, and docs/CURRENT_HANDOFF.md are authoritative memory.

First explain the eight Phase 4 contract decisions in CTO-level and deep C++
terms, including rejected alternatives, and ask me to review material choices
before freezing canonical bytes. Then continue by TDD from the exact next RED
test in docs/CURRENT_HANDOFF.md. Show RED for the intended reason, implement the
smallest GREEN increment, run focused and regression tests, and keep me educated
on what, why, how, ownership, memory layout, complexity, failure behavior, and
trade-offs.

Maintain MVP focus. Phase 4 must not pull gateway, Raft, WAL, venue adapter,
strategy, netting, or optional V2.2 work forward. Update CURRENT_HANDOFF.md as
the phase advances.
```
