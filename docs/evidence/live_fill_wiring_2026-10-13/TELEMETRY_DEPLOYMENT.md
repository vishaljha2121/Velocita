# Qualified telemetry deployment, October 13

Owner authorized deployment and locked review-renewal work in parallel.
Committed source69679c081ce2d0b8939a89c3c846948c460d8d3e supplied the collector,
observer service unit and live dashboard. No production source tree was swapped.

Deployed collector SHA256:
`8b198d396f09497c2a263b11f4e61e15690bd721ad56bb5e33b3ad39f081467f`.
Service unit SHA256:
`53e0b320998a2034247aab6e2cb21541b7e0bc7fd2ecb876f87b17c8bd096549`.
Live dashboard SHA256:
`04d46b977d4b1cbf9197a83ae5a635f2dc04d9e36a994ed83e4a218f125ccc2f`.

Procedure used:
1. `git show 69679c0:<path>` extracted each exact committed artifact into
   `~/.cache/velocita-build/telemetry-deploy-69679c0`; SHA256 values recorded.
2. SSH/SCP staged those three files on owner vishal's NUC1 cache of the same name.
3. `podman inspect velocita-v2-grafana --format '{{json .Mounts}}'` identified
   the actual mounted dashboards at `/home/velocitas/read-b79e65be-86f4408/observability`.
4. Root preflight verified old/new hashes and stopped live services. Original
   collector/unit/dashboard bytes were archived under
   `/opt/velocita/live-observer/before-69679c0` before replacement.
5. `systemctl stop velocita-live-observer.timer velocita-live-observer.service`.
   Wrote exclusive temporary files, retained destination owner/mode, fsynced,
   atomically replaced each file and fsynced each containing directory.
6. `systemctl daemon-reload`, then `systemctl start velocita-live-observer.service`
   and `systemctl start velocita-live-observer.timer`. Neither swap service started.
7. Read-only SQLite inspection of Grafana's resource table confirmed all39panels
   including effective settlement entry/exit points and retained fill rows.
   Root observer cache directory has mode0700. Prometheus observer_up=1,
   gateway_active=0, signer_active=0 and every node_textfile_scrape_error=0.

At this initial observer-deployment step, executable migration was still pending.
Missing settlement feed is expected until the upgraded gateway publishes an authenticated snapshot;
no production fill or snapshot was fabricated. No Safari access or order sent.

## Executable installation completed

The owner approved helper checkpoint `fb5f413`. NUC1 passed15application,
7proposal,17transition,9authenticated-read and12upgrade tests. The first upgrade
filesystem test run correctly rejected group-writable shared cache ancestors;
a new private test HOME and umask077 passed with unchanged production checks.
Both original and rerun logs remain in the host helper staging directory.

Root invoked the pinned `upgrade_telemetry.py --apply` from that checkpoint,
using old manifest SHA256910702fe85f8d5de6746d87c5961f6a48885b4a8bbdcc888fb11007059292947.
Qualified gateway installed at `/opt/velocita/releases/69679c081ce2d0b8939a89c3c846948c460d8d3e/velocita-v2-gateway`.
Old manifest/units and preserved hashes are archived under
`/var/lib/velocita-telemetry-upgrade-69679c0`. Both static units now point to the
qualified release. Persistent startup guards remain loaded, PIDs remain zero,
and all account/journal bytes were preserved. No services started.

Fresh renewal account capture completed20read requests, with zero unresolved
TM orders and unchanged0.040764166SOL /40.006989USDC. Raw evidence is private.
Initial proposal preparation refused the17780-byte transaction review because
the helper reused the8192-byte profile parser; no proposal or journal mutation
occurred. Correct the transaction-only bound to the existing32768-byte contract;
the runtime profile bound remains8192. Fixdca6b9a passed66focused checks on NUC1.

## Final stopped-state verification

Locked renewal and same-account bearer handoff completed using helper checkpoint
`f6d2567`; see `../locked_review_renewal_2026-10-13/DEPLOYED.md`. Fresh authenticated
gateway/signer reads both returned index6 and the original funded state digest.
No funding reset, service activation or trade occurred. Qualified producer remains
69679c0; the Python renewal needed no C++ rebuild.

At23:38:57UTC, the observer was healthy, all three node textfile scrape-error
metrics were0, gateway/signer active metrics were0, and Grafana still loaded
39panels/rows. The settlement feed remained absent as expected while its producer
is stopped. This verifies deployment and monitoring, not a real settled fill.
See `deploy/monitoring-verification.json` for exact captured values.
