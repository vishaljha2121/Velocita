# Claude to Codex: get the automated stochastic run trading — 2026-10-15 19:50 UTC

Read this whole file before acting. It supersedes the "next steps" in
`CODEX_TO_CLAUDE_MVP_2026-10-15.md`. Branch `v2/phase-5-corrections`. The shared working tree holds
another session's uncommitted edits: commit only explicit owned paths. Never commit a shared dirty
file whole; never reset, clean, stash or broad-stage.

## Goal

The owner wants the paper stochastic one-second rule trading live, unattended, on the existing
cluster. Do not add gates or features that are not needed to make it trade safely. Six attempted
effects is no longer the plan: the driver is already extended to 240 orders (see below).

## State right now

| Item | State |
|---|---|
| Cluster | incarnation `aae119b95ddcea5a206f03ab5ca12c15`; voters on `qualified-54a5fcb`, healthy, term 3, leader 2 (Pi), index 11 |
| Mode | **normal** (owner sequence 9 at index 10). Next operator sequence is **10** |
| Account | flat: $25.000000 managed, 0 SOL, 0 effects, 0 fees. Outside: 0.049972532 SOL + $14.006989 USDC, never traded |
| Signer | running, release `1f8d7b6` |
| Gateway | **failed (exit 2) at 18:35:55 UTC** by itself, release `1f8d7b6` |
| Driver | **stopped on purpose** (`velocita-strategy-driver`, user `velocitas`), state: sequence 0, max_orders 240, not stopped |
| Driver code | model `c0faa1c` deployed in `~/velocita-strategy/aae119b9…/model` (feed catch-up fix + price metrics) |
| Alert silence | Alertmanager silence `db074bd6-cefe-4003-b885-9e8e469c827c` on `VelocitaStrategyDriverSilent` (6 h from 19:20 UTC). Expire it when the driver restarts |
| Review window | ends **2026-10-16 13:48:34 UTC**; the driver drains entries 7 min earlier |

## What Claude did this session (all committed)

| Commit | What and why |
|---|---|
| `89f4f3e` | Driver: an old but contiguous candle (bootstrap backlog) warms the indicators and returns `catchup` instead of restarting the feed. Future candles and gaps still restart. Live rehearsal on NUC1: 3 catchup, 42 fresh bars, 0 restarts |
| `c0faa1c` | Driver writes `velocita-strategy.prom` (SOL close, EMA 300/1200, K/D, 30 s fill markers); live dashboard plots price with green/red fills |
| `1a4c12e`, `36ecdf5`, `c0c5813` | Codex evidence committed; account-evidence panel recoloured for 24 h; live Prometheus config and alert rules recorded |
| `a665615` | **Gateway refused-route fix.** Before recording `Prepare`, the gateway runs the signer's verifier on TM's unsigned payload (with the RPC lookup table). Refused route, missing lookup or lost create leaves the effect unsent; in continuous mode the gateway waits for `sign_by` and releases it with `ReleaseUnsent` instead of stopping. Previously a refused route left the effect `prepared` forever (no command releases it) — the incarnation could never trade again. Tests: runtime mode 15, prepare-session refused case; 206/206 Mac; native-qualified on NUC1 and Pi |
| `f789c3c` | Owner helper `docs/evidence/automated_launch_2026-10-15/route-fix/owner_upgrade_restart.py`: preflight (read-only) or `--run` (stop both, install pinned release, change only commit/binary pins, journals/credentials/documents byte-identical, start signer then gateway with guards lifted only for the start, restore guards, verify flat). Owner preflight already PASSED on NUC1 for `a665615` |
| (this handoff's commit) | **Gateway Raft-outage fix.** The gateway died at 18:35:55 UTC because a single failed state read (`LinkStatus` not done during the term 1→3 elections) stopped it for good (`failed/state`), and units have `Restart=no`. Now in continuous mode a failed **state read** is retried after `state_interval` and only an outage longer than `read_outage` (production 120 s) stops it. Failed **commands** still stop it. Tests: runtime modes 16 (4.5 s outage survives, trade completes once) and 17 (outage beyond window still stops) |

Other housekeeping done: Grafana dashboards regenerated for run `aae119b9` with `--paper-run`
(old copies in `~/.cache/grafana-dashboard-backups/` on NUC1); Prometheus now reads
`~/live-aae119b9/observability/prometheus.yml` and rules `~/live-aae119b9/rules/` (GatewayDown
watches the live gateway/signer; new `VelocitaStrategyDriverSilent`; paper observer alert removed).
Pi disk freed 91%→83% (unused images only).

## Strict next steps, in order

1. **Native-qualify the outage-fix commit** (HEAD after this handoff; `a665615` is superseded because
   gateway code changed). Use Codex's committed runner exactly as in
   `docs/evidence/automated_launch_2026-10-15/focused-mvp/native/COMMANDS.md`, with
   `git bundle create … 54a5fcb..HEAD` (a raw SHA end refuses to bundle). Both NUC1 and Pi must report
   `qualified: true`. Record the NUC1 `velocita-v2-gateway` SHA-256 from `status.json`.
2. **Retarget the owner helper** to the new commit: in `route-fix/owner_upgrade_restart.py` change
   `COMMIT` and `ARCHIVE` (`/var/lib/velocita-route-fix-upgrade-<short>`); keep `OLD_COMMIT`
   `1f8d7b6…` and `OLD_MANIFEST` `f3df3252…` (nothing was applied yet). Commit.
3. **Stage on NUC1** under `/home/vishal/.cache/velocita-build/route-fix-<short>/`: `source/` =
   `git archive <helper commit>` of `docs/evidence/automated_launch_2026-10-15/{route-fix,focused-deploy,verify_installed.py}`,
   `docs/evidence/live_fill_wiring_2026-10-13/deploy/upgrade_telemetry.py`, `deploy/v2/swap-runtime`,
   `deploy/v2/observability/live_observer.py`; `payload/` = the qualified NUC1 gateway binary (from
   `velocitas:~/.cache/velocita-build/integrated-a890307-20261013/qualification/build/velocita-v2-gateway`)
   plus `service_launcher.py`, `service_profile.py`, `fd_handoff.py` from the commit. Verify every
   SHA-256 (launcher pins are unchanged: `5f1123d9…`, `c03c0ee6…`, `a8ffbfc1…`).
4. **Owner preflight** (read-only), then **owner `--run`**:
   `ssh -t vishal@100.72.127.102 'B=…; sudo python3 -B $B/source/docs/evidence/automated_launch_2026-10-15/route-fix/owner_upgrade_restart.py --bundle $B/payload --binary-sha256 <sha> [--run]; sudo -k'`.
   Expect preflight `services_running: false` (gateway failed, signer running — the helper stops the
   signer itself). On any refusal: stop, read it, never retry blindly.
5. **Start trading:** as `velocitas` on NUC1, `systemctl --user start velocita-strategy-driver`
   (config `max_orders 240`, sequence 0; do not re-stage or reset its state). Expire the Alertmanager
   silence. Watch `journalctl --user -u velocita-strategy-driver -f` for buy/sell/refused/stop and
   `~/velocita-v2/textfile/strategy-settlements.json`. For each effect follow intent → Prepare (or
   route refused + release) → sign → execute → settlement; compare Raft with the wallet after the
   first round trip and report fills, fees and P&L.
6. **Before 2026-10-16 13:41 UTC** tell the owner the run will drain. Continuing needs a fresh account
   snapshot and reviewed documents (the same owner steps as today); there is no automatic renewal.

## Expectations and known limits

- The rule trades about 1.7–1.8 round trips per hour (paper 29 in 17.2 h; replay of the live code over
  the last 3.3 h gave 6). Long quiet stretches are normal; zero trades in 35 minutes was not a bug.
- About 1 in 4 TM routes is refused by the verifier (Oct 14 survey). With `a665615` that costs one
  skipped signal, not the run.
- The gateway now survives Raft elections up to 120 s. It still stops (Restart=no) on storage,
  identity or command failures; a stop leaves the run safe and halted-in-practice — report it.
- Emergency stop: owner `zsh docs/evidence/automated_launch_2026-10-15/operator.zsh aae119b95ddcea5a206f03ab5ca12c15 mode halted 10 --run`.

## Open owner items (do not start without asking)

- NUC1 market-data `stream.db` is 15 GB + 2.9 GB WAL (research collector, not trading). Stop it or fix its Mac backup.
- Controls artifact (kill epoch, executor generation, modes, guards, review window, stopping).
- Daily automatic renewal (only once the run proves worth continuing).

## Rules

Sudo, Keychain and the TM session are owner-only (owner runs `!` commands). No secrets in the repo,
chat, argv or logs. One cluster. Archive, never delete. Never rerun a partial ceremony; reconcile.
No code comments beyond an optional file description. Mutation-test new logic and confirm each
mutant actually recompiled (make can reuse a same-second stale object — this session hit it).
Commit footer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
