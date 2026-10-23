# Qualification maintenance commands — 2026-10-13

Owner authorized pausing the paper cluster to release memory. These commands do
not deploy the candidate or reset any runtime state. Hosts: NUC1 100.72.127.102,
Pi 100.84.151.116, NUC2 100.85.36.22, all using the `velocitas` SSH account.

All SSH calls used:
```sh
ssh -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=yes \
  -o UpdateHostKeys=no -o ControlPath=none velocitas@HOST 'COMMAND'
```

Observed before stopping: all three voter/exporter pairs active; on NUC1 paper
gateway/observer active, stochastic edge already inactive. Recorded states in
`~/.cache/velocita-build/integrated-a890307-20261013/maintenance-20261013/`.

Executed first on NUC1:
```sh
systemctl --user stop velocita-paper-stochastic-edge.service
systemctl --user stop velocita-paper-stochastic-gateway.service velocita-paper-indicator-observer.service
```
Then executed on all three hosts:
```sh
systemctl --user stop velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service
```

All configs were verified to use `startup = recover` and unchanged incarnation
`348e0b2cd4f639ff404df4eeadee0ee5`. Production images remain `roles-884816d`;
qualification source remains isolated at `1d2c820`. Data, ledger, configuration
and secret mounts were not changed. Monitoring and recorder remain running.

Qualification is a private, non-root, offline container: 2816 MiB memory and
equal memory-swap limit (no swap), one CPU quota, `taskset -c 3`, source read-only,
qualification/ccache directories read-write. The test invokes the existing Ninja
build incrementally, then node-process preflight and all registered tests.

Current progress (run separately for each host):
```sh
ssh velocitas@100.72.127.102 'p="$HOME/.cache/velocita-build/integrated-a890307-20261013/qualification"; tail -F "$p/driver.log" "$p/tests.log"'
ssh velocitas@100.84.151.116 'p="$HOME/.cache/velocita-build/integrated-a890307-20261013/qualification"; tail -F "$p/driver.log" "$p/tests.log"'
```

Restoration procedure, after both qualification containers stop: start the
existing voter/exporter pairs on all hosts together, verify a leader and matching
committed/applied state, then restore NUC1 gateway/observer. Do not reset state
or start the previously inactive edge as part of maintenance restoration.
```sh
# All three hosts, then check health before proceeding:
systemctl --user start velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service
# NUC1 only after cluster recovery:
systemctl --user start velocita-paper-stochastic-gateway.service velocita-paper-indicator-observer.service
```

Restoration status is recorded in RESULT.md when completed. This procedure does
not authorize a new release deployment, live mode, or an owner ceremony.
