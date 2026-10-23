# Fresh paper activation result — October 7, 2026

The owner supplied four COMMITTED/applied responses (indices 1–4, term 1).
The fresh $25 paper account was initialized and the sole stochastic cluster is
running. This is a bounded engineering pilot, not evidence of profitability.

## Active identity and bounds

- Incarnation: c8a6a0a78fbf49a099b222124a5d557f; policy/schema 128/138.
- Edge/observer: localhost/velocita-v2-shadow:paper-1s-0680b08.
- Gateway: localhost/velocita-v2:paper-7297bd5, --venue paper, 20 bp/side.
- Voters/exporters: m3-cp081, one voter per host, exactly one cluster.
- Epoch: 2026-10-07 20:04:58.690616 UTC (16:04:58 EDT).
- Entry stop: 20:34:58 UTC (16:34:58 EDT).
- End: 20:39:58 UTC (16:39:58 EDT), subject to bounded transport shutdown.
- 2400 decision attempts across restarts, 40 fills plus one exit reserve.
- No deadline extension, account refund, data deletion, live order, or kernel
  production-code change during recovery. A stale feed may prevent an exit.

## Initialization and startup ordering failure

Exact activation command:

```sh
ssh velocitas@nuc1 'python3 "$HOME/stochastic-fresh-c8a6a0a7/activate-after-owner.py"'
```

The original c6c1589 script initialized zero lots and 25000000 quote micros at
applied index 4. Setup reported generation=1, first_sequence=1, kill_epoch=1,
executor_generation=1, strategy_mode=1. It started the gateway before policy
setup, however. The subsequent strategy-mode change correctly latched the
gateway's control_change stop. This was an orchestration error.

The first recovery restarted the gateway while an unsent authorized effect
existed. Gateway startup recovery conservatively classified uncertainty and
latched unknown_outcome. No order had reached the paper venue: cash was $25,
orders empty, lots/fees/volume zero. The edge was stopped. Another restart after
a reported position reconciliation match did not resolve the stop; position
match alone was not sufficient diagnostic evidence. Repeated blind restarts
were discontinued in favor of reading the actual committed records.

## Direct committed-state diagnostic

Source: fresh-run/read-committed-state.cpp, commit b1c7cca. It calls only
GatewayLink::request_state, never submit or request_authority. It advances the
existing gateway session epoch while the gateway is stopped, uses the existing
caller-13 key mount and response port, and times out after 15 seconds. No key
contents are logged. It is specific to this run, not a generic operational tool.

It was compiled on nuc1 against libraries from the already validated build
stage 7df34271c5bda85214ca11303166d3bc49acdc4712e3d4868ef9ab874ff5a07f.
No production image was modified. With edge and gateway stopped, it ran as:

```sh
podman run --rm --network host --userns=keep-id --read-only \
  --security-opt no-new-privileges --cap-drop all --memory 512m --shm-size 256m \
  -e AERON_TERM_BUFFER_LENGTH=1048576 \
  -v "$HOME/stochastic-fresh-c8a6a0a7/read-committed-state:/diag:ro" \
  -v "$HOME/velocita-indicator-paper/stochastic/gateway:/state" \
  -v "/run/user/$(id -u)/velocita-secrets/caller-13.key:/run/velocita-secrets/caller-13.key:ro" \
  --entrypoint /diag localhost/velocita-v2-shadow:paper-1s-0680b08
```

Result, applied index 349:

- Order 15666494356934747149: state 11 = rejected.
- Effect 2184018967446018083: state 5 = rejected.
- Account 2: position 0, reserved lots 0, reserved margin 0.

Only after this direct evidence did the gateway start again. Fresh metrics
confirmed new_risk_stopped=0 before the edge resumed. No stop was overridden or
state file edited. The revised activation recipe installs policy, restarts the
gateway, waits for a fresh armed metric, and only then starts the edge. It must
not be rerun against this already initialized account.

## Verified running evidence

At 20:15:10 UTC, both edge and gateway active with NRestarts=0 since the successful
resume. Gateway armed; account reconciled; ledger readable; edge errors zero;
rule disagreements zero; price age 0.404 s. Between 20:14:13 and 20:15:10 the
attempt counter increased 156 to 213: 57 decisions in 57 seconds. Earlier
interrupted periods are retained; do not present them as uninterrupted runtime.

First actual new paper fill:

- Buy order 16885946128455561303, 12 lots = 0.12 SOL.
- Price $116.27; notional $13.952400.
- Fee $0.027905 = ceil(13952400 * 20 / 10000) USDC micros.
- Cash after purchase $11.019695; inventory 0.12 SOL.
- No sell or completed round trip at that snapshot. This is a simulated fill,
  not a live True Markets transaction or profit validation.

Grafana's actual Prometheus queries returned cash, inventory, reconciliation,
price age and pilot-only metrics. New data carries the unique paper_run label;
no old account fills are included. Removed the obsolete unscoped volume-strategy
row. Missing exact bar-to-fill measurement is labeled Not instrumented; p99
requires at least 100 samples. The observer remains read-only.

Raw evidence: scratch/fresh-activation.log, fresh-gateway-stop-investigation.log,
committed-state-diagnostic.log, fresh-proven-reconciliation-start.log,
fresh-runtime-resumed.log, fresh-runtime-final.log, fresh-grafana-queries.log.
These are local evidence logs, not all tracked in Git.

Residual limitation: when the edge closes its SQLite WAL journal, the existing
read-only observer can lose ledger readability until a writer reopens it. This
was observed during maintenance. Do not mistake a post-stop Unknown panel for
zero P&L or claim this monitoring limitation has been fixed by the strategy update.

At 20:16:03 UTC, 129 decisions since the successful resume, zero edge errors
and rule disagreements, gateway armed, one paper fill, and three healthy voters.
Ordinary retained entries 609 on every voter; receipts reclaimed from near 256
back to 100/101 held, one pinned. Prometheus queries confirmed new-risk-stopped=0,
account-ready=1, fill count=1 and voter count=3. See fresh-runtime-stable.log.
