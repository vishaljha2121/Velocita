# Qualified cluster upgrade completed — 2026-10-15 23:23 UTC

The existing incarnation `aae119b95ddcea5a206f03ab5ca12c15` is preserved. Gateway, signer,
all three voters/exporters, and the stopped driver's client image now use qualified
`ba299ff6c89392177cfd4702fda11dabb7ee1719`. Helper checkpoint was `b78ca0f`.
No strategy grant, mode-normal command, driver restart, trade or funding reset occurred.

[Final readback](final-readback.json): all three voters up/ready, failed0, term6, leader1,
commit/applied/durable17, mode halted. Gateway/signer are active with start guards retained.
The producer reports25000000 quote atoms, zero base/effects/pending/fees/P&L. This is Raft
account telemetry, not a fresh independent TM-wallet read. Next operator sequence11.
Driver state and client session were preserved byte-for-byte; sequence2/next3 and limit240.

Owner runtime upgrade preserved journals and installed manifest
`7668ba129bfdf013ac95ac9407cd6af311113414d2693182abb656fe1e7a8a70`.
[Owner result](owner-install-result.json), [driver-only image change](driver-image-result.json).
Voter/exporter pairs were replaced in order NUC1, NUC2, Pi, with current leader last.
Each stopped data/config tree was copied and hash-verified before the unit-image change.
Original data/config directories were retained; only unit image tokens changed.
Backups on each host: `~/.cache/velocita-build/strategy-grant-backup-ba299ff-node<N>/`.
Driver backup on NUC1: `~/.cache/velocita-build/strategy-grant-driver-backup-ba299ff/`.
The initial and recovery command transcripts are recorded in `ROLLING_COMMAND.py.txt`,
`PI_RECOVERY_COMMAND.py.txt` and `DRIVER_IMAGE_COMMAND.py.txt` beside this file.

## Pi interruption and recovery

The first Pi container start exceeded90 seconds while Podman prepared the keep-id mapped
filesystem. Its first isolated300-second preparation also timed out. Both failures are
preserved. The separate600-second isolated check passed in419.109seconds, with UID1004
and all three qualified binary hashes verified. It had no production mounts, credentials
or network. The Pi then recovered from its original data at index17. See
[incident](PI_STARTUP.md) and `pi/` evidence. This was container storage preparation,
not evidence of a Raft replay failure. Future packaging checks should prepare the image
with the production `--userns=keep-id` before taking a voter down; the prior library/hash
check omitted that namespace and missed the first-use disk cost.

## Monitoring and remaining work

Host-terminal checks: Prometheus has all three current-incarnation voter-up series at1;
Grafana `/api/health` reports database ok. No browser/screen sharing used. Existing alerts
still include ElectionsRecurring, DiskAlmostFull, StrategyDriverSilent and MdStreamDbLarge.
They were not silenced. The driver is deliberately stopped; Pi has about4GB available
and86% used following image preparation. The election alert was observed after the planned
leader restart; its exact rule window was not requalified here. The recorder-size issue
remains open. These alerts are not a claim that monitoring is entirely clean.

A separate Mac client built from committedba299ff passes three grant and five legacy CLI
tests, with no keys/network/funds. [Client and warning evidence](mac-client.json).
Path: `~/.cache/velocita-build/grant-owner-client-ba299ff/build/velocita-v2-client`.
No additional production code was implemented in this deployment.

Next: prepare/review the one-time operator grant against the original opening and reviewed
limits (caller21, floor3, epochs1/1 if still current), then owner sequence11. After its
committed result, owner normal mode at the next sequence and start the retained driver
only. Do not reset or re-stage its state. All readers are upgraded; old-reader rollback
after grant remains unsupported. Review entry drains2026-10-16 13:41:34.459UTC and
expires13:48:34.459UTC; renewal remains owner-run.
