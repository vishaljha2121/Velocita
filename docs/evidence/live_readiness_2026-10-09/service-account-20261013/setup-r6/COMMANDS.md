# Executed command sequence — reference, not a rerun script

Public scripts/configs accompany this record. Do not rerun one-shot setup,
archive or ceremony commands against their now-existing destinations.
Passwords and credential contents are deliberately absent.

- `package_qualified.py`: packages pinned already-qualified native ELF files on
  NUC1/Pi as `localhost/velocita-v2:qualified-86f4408`. NUC2 imports the exact NUC1
  image; public compressed image transport went through the Mac cache.
- `stage_cutover.py`: executed by `velocitas` on each host only after stopping
  old processes; fixed preflight, hash-verified archive and fsynced new config/unit
  writes. `node-*-stage-result.json` records completion.
- `systemctl --user start velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service`
  started the one replacement pair on each host.
- `zsh docs/evidence/live_readiness_2026-10-09/service-account-20261013/setup-r6/owner-ceremony.zsh`
  ran on the Mac. Only sequences1–3 were sent.
- `set_recover.py` ran by SSH stdin under each deployment user after checking fresh
  ready/index/health metrics, then atomically changed bootstrap to recover.
- Administrator `vishal` ran `prepare_reads.py` via `sudo -v` then
  `sudo -n python3 -I -B .../prepare_reads.py`. This provisioned only Raft keys,
  independent epoch files and a disabled new-incarnation manifest.
- The initial bounded reader failed safely. `qualify_private_run_r2.py` applied
  the first reviewed mount correction; its read still refused. Both result sets
  are retained. `prepare_runtime_roots.py` applied the final explicit bind to the
  dormant units; `inspect_read_mounts.py` confirmed safe actual ancestry and tmpfs.
- Successful final read command, from the Mac:

```zsh
ssh -t vishal@100.72.127.102 'sudo -v && cd "$HOME/.cache/velocita-build/swap-cutover-20261013-read-r3" && sudo -n python3 -I -B authenticated_read_run.py --helper-sha256 e8b8fa9888d393cfdcab5a8beda3b2891c8ec8d9aa74e7025a5edee5857e0a58 --catalog-sha256 8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f --incarnation b79e65be1ea14e219016ec48ea02efef --catalog-root 3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe --authority-domain 29b772a949627cd25c657a527ef2ea79a7b470454de9be9aa842462982b9d95e --output "$PWD/read-result" --run'
```

- `export_public_results.py` exported only fixed read reports, manifest and unit
  definitions. Their contents are preserved in this folder; keys were not copied.
- `install_observability.py` checked exact old config hashes, validated the new
  Prometheus config against existing rules with promtool, preserved old unit
  bytes, installed a new public monitoring directory, then called:

```zsh
systemctl --user daemon-reload
systemctl --user restart velocita-v2-prometheus.service velocita-v2-grafana.service
```

- Safe status commands (repeatable, no changes):

```zsh
ssh velocitas@100.72.127.102 'systemctl --user is-active velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service'
ssh velocitas@100.84.151.116 'systemctl --user is-active velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service'
ssh velocitas@100.85.36.22 'systemctl --user is-active velocita-paper-stochastic-node.service velocita-paper-stochastic-exporter.service'
ssh velocitas@100.72.127.102 'curl -fsS http://100.72.127.102:11410/metrics'
ssh velocitas@100.72.127.102 'systemctl show velocita-swap-gateway.service velocita-swap-signer.service -p ActiveState -p UnitFileState'
```

All SSH/SCP invocations used known-host verification, a10-second connection
bound and no multiplexed control socket. SCP transferred public source/configs,
binaries/images and sanitized result files only. Credentials were handled through
existing Keychain/tmpfs mechanisms. No node source-tree swap occurred.
