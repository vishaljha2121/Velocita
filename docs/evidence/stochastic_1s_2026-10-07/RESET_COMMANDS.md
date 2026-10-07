# Fresh paper ledger reset: command record

Status on October 7, 2026: old account archived; fresh cluster healthy with three
voters and one leader. Operator ceremony NOT executed. New $25 venue state NOT
initialized. Edge, gateway and observer remain stopped. No new strategy trades.

New incarnation: `c8a6a0a78fbf49a099b222124a5d557f`.
Old incarnation: `19137567d0c14889b29f7674e26a8fd8`.
On every host, the old state is retained at:
`~/velocita-indicator-paper/retired/stochastic-19137567-20261007`.
Nothing in that archive was deleted or refunded. The old account ended flat at
$24.520467, with ten fills and $0.420733 fees.

## Executed: build and verify

Production image compilation ran on nuc1, not the Mac:

```sh
cd "$HOME/src/stochastic-1s-0680b08"
BUILD_JOBS=2 deploy/v2/build-image.sh shadow paper-1s-0680b08
podman run --rm --network none \
  -v "$HOME/src/stochastic-1s-0680b08/model/tests:/checks:ro" \
  --entrypoint python3 localhost/velocita-v2-shadow:paper-1s-0680b08 \
  -m unittest discover -s /checks -p test_stochastic_second.py
```

All 140 C++ tests and all 11 strategy tests passed. Image ID:
`33d6c413a396fddbefd908d86c15f706b4cc4b8b31345bd4cb4b1addc2c7f09f`.
Source: `0680b08a5bbfbcaba32a77fbdf5cbe5d0162f40c`.
Gateway remains `paper-7297bd5`; voters/exporters remain `m3-cp081`.

The Mac built only the local operator client, needed for the owner ceremony:

```sh
cmake --build "$HOME/.cache/velocita-build/stochastic-1s-20261007" \
  --target velocita-v2-client --parallel 2
```

## Executed: stage profiles and stop the old cluster

Committed generated profiles are in `fresh-run/` (ddc3442); follow-up scripts
are committed at c6c1589. Profiles were transferred with scp to each host as
`~/stochastic-fresh-c8a6a0a7.tar`, SHA256:
`a87c9ad14ca74aa70611b91debaad1efe34a21836f6a158f0d9f950b829ce336`.
On each host the checksum was checked, then:

```sh
mkdir "$HOME/stochastic-fresh-c8a6a0a7"
tar -xf "$HOME/stochastic-fresh-c8a6a0a7.tar" -C "$HOME/stochastic-fresh-c8a6a0a7"
```

Before stopping nuc1, venue.json was checked: flat, zero pending reports,
not failed, fresh report, exactly ten orders, cash 24520467 micros.
Then these commands were executed, in this order from the Mac:

```sh
ssh velocitas@nuc1 'systemctl --user stop velocita-paper-stochastic-edge.service velocita-paper-stochastic-gateway.service velocita-paper-indicator-observer.service'
ssh velocitas@nuc1 'systemctl --user stop velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service'
ssh velocitas@100.84.151.116 'systemctl --user stop velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service'
ssh velocitas@nuc2 'systemctl --user stop velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service'
```

All old voters stopped before any replacement voter started.

## Executed: archive and start the sole replacement cluster

```sh
ssh velocitas@nuc1 'sh "$HOME/stochastic-fresh-c8a6a0a7/stage-host.sh" 1'
ssh velocitas@100.84.151.116 'sh "$HOME/stochastic-fresh-c8a6a0a7/stage-host.sh" 2'
ssh velocitas@nuc2 'sh "$HOME/stochastic-fresh-c8a6a0a7/stage-host.sh" 3'
ssh velocitas@nuc1 'systemctl --user start velocita-paper-stochastic-node.service'
ssh velocitas@100.84.151.116 'systemctl --user start velocita-paper-stochastic-node.service'
ssh velocitas@nuc2 'systemctl --user start velocita-paper-stochastic-node.service'
```

Read `fresh-run/stage-host.sh` for the exact move/copy operations. It refuses
active old voters/exporters and existing archive destinations. It archives the
entire old stochastic directory, installs fresh empty directories/configuration,
backs up installed unit files and reloads systemd. Each voter starts its paired
exporter. These scripts are one-time records; do not rerun them.

After all three reported ready=1, term=1, one leader and zero retained history,
startup was changed to recover for future restarts:

```sh
ssh velocitas@nuc1 'sed -i "s/^startup = bootstrap$/startup = recover/" "$HOME/velocita-indicator-paper/stochastic/config/node-1.conf"'
ssh velocitas@100.84.151.116 'sed -i "s/^startup = bootstrap$/startup = recover/" "$HOME/velocita-indicator-paper/stochastic/config/node-2.conf"'
ssh velocitas@nuc2 'sed -i "s/^startup = bootstrap$/startup = recover/" "$HOME/velocita-indicator-paper/stochastic/config/node-3.conf"'
```

No host reboot or secrets push occurred. Existing keys remain mounted from
`/run/user/<uid>/velocita-secrets`; no key contents were logged.

## Executed: isolate monitoring and verify

```sh
ssh velocitas@nuc1 'python3 "$HOME/stochastic-fresh-c8a6a0a7/install-monitoring.py"'
ssh velocitas@nuc1 'podman exec velocita-v2-prometheus promtool check config /etc/prometheus/prometheus.yml'
ssh velocitas@nuc1 'curl -fsS http://100.72.127.102:3000/api/health'
```

The monitoring installer backs up the old units, changes only the Prometheus
config and Grafana dashboard mounts to the generated fresh-run files, reloads
systemd, and restarts Prometheus/Grafana. Rules/provisioning otherwise stay at
the shipped source paths. New metrics carry `paper_run=c8a6a0a78fbf49a099b222124a5d557f`;
strategy dashboard selectors require that value. Old time series are retained.
Config validation passed with 47 rules; Grafana database health is OK; the mounted
paper dashboard hash matches the generated file. New-run voter count is three.
Strategy panels are intentionally empty until the owner setup and activation.

Raw command output is retained in scratch/reset-*.log, image-final-check.log,
host-build-fix.log and mac-operator-client.log. These logs are local evidence,
not all tracked in Git.

## Pending: owner ceremony on the Mac

Run this once from your Mac terminal:

```sh
zsh "$HOME/aeron-raft-pi/docs/evidence/stochastic_1s_2026-10-07/fresh-run/owner-ceremony.zsh"
```

The script contains the exact four commands: kill epoch 1, executor generation 1,
install the catalog, and install long-only SOL risk limits (12 lots, $25 caps,
30 bp maximum fee reservation). It obtains the operator key through the existing
Keychain tool and pipes it directly to the client; it does not print the key.
Actual paper costs will be 20 bp per side. It stops on failure and does not retry.
Each command must report COMMITTED with code_name=applied. If it fails partway,
retain the output; do not rerun all four blindly.

This is owner-run because ADR 0016 says the ceremony prints commands "for the
owner to run from the Mac". No operator command was issued from a host.

## Pending: initialize and activate after owner success

Prepared, not yet executed:

```sh
ssh velocitas@nuc1 'python3 "$HOME/stochastic-fresh-c8a6a0a7/activate-after-owner.py"'
```

This one-time script initializes the fresh paper venue with $25 and no orders,
starts the paper gateway, validates the empty account at 20 bp, writes the bounded
one-second profile, installs policy/schema 128/138 through setup caller 11, then
starts the edge and read-only observer. Its 30-minute entry and five-minute exit
window start then, not during maintenance. Verify feed/cadence, reconciled cash
and inventory, current fees, capacity and dashboard data before calling deployment
complete. No live venue orders are involved.
