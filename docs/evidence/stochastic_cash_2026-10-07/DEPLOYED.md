# Ongoing cash session deployment — October 7, 2026

## Active deployment

- One existing three-voter cluster: `c8a6a0a78fbf49a099b222124a5d557f`.
- Edge/observer image: `localhost/velocita-v2-shadow:paper-cash-9a71762`.
- Image ID: `edf15c640f86a4fe886d087916b762ab1dde013daf1a8be7a5ea6bca35b5b65a`.
- Source: `9a717627b3e1d3971cb889a6517252a45fc45179`.
- Pinned native toolchain: `2a79f82cce5de80ab5afc832af64acba549c963e89df509acfaeb5565cb232ee`.
- Voters/exporters remain `m3-cp081`; gateway remains `paper-7297bd5`, `--venue paper`.
- Policy/schema: 129/139; variant `stochastic_1s_cash_v1`.
- Session baseline: 1791409624162354155 ns = 2026-10-07 21:47:04.162354 UTC.
- No scheduled stop: stop_ns=0; policy validity ends at INT64_MAX.
- Starting continuation cash $24.869170, flat, four preserved historical fills.
  Original virtual funding remains $25. No top-up or new venue initialization.

All 140 native CTest targets passed in the released image build. All 25 focused
Python tests passed again using the final image's packaged code (no source mount).
See VALIDATION.md for model/tooling/mutation and alert validation details.

## Ordered commands and host changes

The source archive came from a commit, built natively on nuc1 in its own directory:

```sh
# Mac, isolated committed checkout:
git archive -o docs/evidence/stochastic_1s_2026-10-07/scratch/cash-source-fixed.tar 9a71762
scp docs/evidence/stochastic_1s_2026-10-07/scratch/cash-source-fixed.tar velocitas@nuc1:stochastic-cash-9a71762.tar
# nuc1:
mkdir -p "$HOME/src/stochastic-cash-9a71762"
tar -xf "$HOME/stochastic-cash-9a71762.tar" -C "$HOME/src/stochastic-cash-9a71762"
cd "$HOME/src/stochastic-cash-9a71762"
printf '%s\n' 9a717627b3e1d3971cb889a6517252a45fc45179 > SOURCE_COMMIT
nohup sh deploy/v2/build-image.sh shadow paper-cash-9a71762 > "$HOME/stochastic-cash-build-9a71762.log" 2>&1 < /dev/null &
```

The first f854001 build failed the dashboard generation/layout gate and was NOT
released. The generated JSON and compatibility assertions were corrected before
building 9a71762. No production tests were bypassed.

With the old edge confirmed inactive, a one-shot, network-disabled container ran
`finalize-old.py` using the released image. Its config/edge/gateway mounts were
`/config`, `/edge`, `/venue`; gateway was read-only. It verified the original
SQLite database, fresh venue report, flat cash $24.869170 and four fills, then
published `/edge/ledger.db.completed`. The original ledger SHA256 stayed:
`ecca343009d309ee1385cf72548997656ec940fdbdbae16e7c542a27acdb1bef`.

Another network-disabled container ran:

```sh
python3 -m velocita_model.shadow.continue_stochastic \
  --config /config/profile-1s.json --output /config/profile-cash.json \
  --ledger /edge/ledger-cash-v1.db
```

It copied all prior fills, observations and marks; retained prior epoch, stop and
completion records; and created a new immutable baseline. It did not change the
venue state or original ledger. The actual finalized pilot was then successfully
read with every mount read-only: equity $24.869170, ledger_readable=1, completed.

Activation ran once:

```sh
python3 -u "$HOME/stochastic-cash-20261007/activate.py" \
  localhost/velocita-v2-shadow:paper-cash-9a71762 \
  > "$HOME/stochastic-cash-20261007/activation.log" 2>&1
```

The script stopped the gateway, then used the existing read-only committed-state
diagnostic with the gateway's internal caller identity. At applied index 3508,
there were zero retained orders/effects and account 2 had zero position, reserved
lots and margin. Terminal old objects had already been reclaimed; the venue and
paper ledger still retain their confirmed history. The diagnostic advanced only
the gateway transport session epoch, not trading state or operator limits.

Setup installed the new policy using the existing model setup caller: generation
2, first_sequence 10001, kill epoch 1, executor generation 1, normal strategy mode.
The gateway restarted only AFTER setup and a fresh new_risk_stopped=0 was required
before starting the edge. No operator risk ceremony was rerun.

Only nuc1 edge/observer images and their units changed. The observer still has
read-only edge/gateway mounts. The edge uses profile-cash.json and
indicator-cash.program. Both use Podman `stop --ignore` for missing-container
cleanup. The existing edge drop-in is consistent with that command.

Prometheus and Grafana use generated configs under:
`~/stochastic-cash-20261007/{observability,rules}`.
Prometheus's paper_run label is now
`c8a6a0a78fbf49a099b222124a5d557f-cash-v1`; the actual cluster incarnation did not
change. Both monitoring services restarted to pick up their new mounts. The
shared `~/src/aeron-raft-pi` tree was NOT swapped, and no voter/exporter, recorder
or host was restarted. No secrets were pushed and no live venue was contacted.

## Monitoring transfer incident and repair

The initial config-only macOS tar included `._*` AppleDouble metadata files.
Prometheus's `*.yml` rule glob tried to read `._velocita-v2-alerts.yml` and refused
startup; this produced 17 restart attempts. The strategy/gateway remained healthy.
The files were verified as AppleDouble and moved intact to
`~/stochastic-cash-20261007/transfer-metadata/` (not deleted). The exact mounted
Prometheus config and all 47 rules then passed promtool validation. Prometheus
started and Grafana restarted. Future activation now refuses transfer metadata;
use git archive for deployment transfers to avoid it.

Grafana's current resource table (not its empty legacy dashboard table) confirms
the persisted paper dashboard: 70 panels, 85 session-scoped queries, no obsolete
Not running footer. Mounted dashboard SHA256:
`a2591305136f54a787c7e78197424c75c61a597183c6524c3b1cece6211310d9`.
All 10 Prometheus targets are up. Verification used terminals only, never Safari.

## Runtime evidence and operational boundary

At 21:51:34 UTC, the edge had 208 decisions, zero errors/disagreements, feed and
submitter ready, gateway armed, account reconciled and ledger_fills_behind=0.
All three voters were healthy with one leader (nuc2 at this check); exactly one
voter cluster was confirmed on each host. Cash/equity were $24.869170, flat, and
no new entry signal had filled yet. These are observed engineering facts, not
profitability evidence. See runtime/ for the actual snapshots and checks.

The $10 drawdown and 240 lifetime-order guard remain in force and can stop the
session before cash is insufficient. Do not reset the ledger or auto-fund it.
Old image/profile artifacts are retained, but rolling back trading is NOT simply
restarting the expired pilot: preserve current gateway/ledger state and reconcile
first. The observer and metrics can remain up after trading stops.

At 21:56:54 UTC, 528 decisions had completed with zero errors; cash remained
$24.869170, flat, with no new orders. Edge, gateway and observer were active with
zero restarts each. All ten monitoring targets were up. The retired-session
heartbeat and observer alerts had expired normally. Only pre-existing
market-data recorder-size and alert-delivery warnings remained. See
runtime/runtime-final.json.
