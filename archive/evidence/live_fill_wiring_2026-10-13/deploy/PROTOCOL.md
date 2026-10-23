# Qualified telemetry deployment — stopped production release

Scope: install the gateway producer from exact qualified commit
`69679c081ce2d0b8939a89c3c846948c460d8d3e`, preserving every account, caller,
transaction, risk, runtime-image and journal binding. Keep the gateway and signer
stopped. Deploy the read-only observer/dashboard independently. This procedure
cannot enable continuous trading, submit an intent, or refresh an expired review.

## Verified read-only preparation

On NUC1, the existing cache binary is:
`/home/velocitas/.cache/velocita-build/integrated-a890307-20261013/qualification/build/velocita-v2-gateway`.
Its SHA256 is
`bfd31bdddb9dfc6e5423a769b7b4a789c6abfefbaf7cc3c8c3ef2caa791b3604`.
A fresh SSH `sha256sum` matched the qualified NUC artifact manifest. `readelf -d`
shows libbsd.so.0, libuuid.so.1, libcrypto.so.3, libcurl.so.4, libstdc++.so.6,
libm.so.6, libgcc_s.so.1, libc.so.6. Preserve the qualified runtime image
`076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f`;
do not run the executable against the host's different libc/OpenSSL.

`podman inspect velocita-v2-grafana` confirms its dashboard bind source is
`/home/velocitas/read-b79e65be-86f4408/observability`, destination
`/etc/grafana/dashboards`. The provisioning bind uses the source tree, but this
operation does not swap that tree. Atomic replacement of only
`velocita-v2-live.json` inside the existing dashboard directory is sufficient;
Grafana polls it every30seconds. Preserve the other five dashboard files and
operational banners. Verify the loaded Grafana resource via host-terminal
read-only SQLite and Prometheus queries. No Safari and no Grafana restart needed.

## Executable replacement boundary

The old dormant installer is inappropriate now: it refuses adopted manifests and
present venue credentials. `upgrade_telemetry.py` is a narrow separate operation.
It requires the exact old manifest SHA256 as an argument, preserves its launch
flag (already true) and first-use flag (false), and changes only source_commit,
binary path and binary SHA256. It changes both unit renderings because both roles
execute the same gateway binary and pin the manifest digest.

New immutable release:
`/opt/velocita/releases/69679c081ce2d0b8939a89c3c846948c460d8d3e`.
Old release remains intact. Never overwrite it in place or point it at cache files.
The helper does not touch credentials. It archives the old manifest/units and
hashes of all reviewed public files and all five journals, retaining old release
bytes. Account, runtime, transaction, catalog and journal bytes remain unchanged.

Before mutations it verifies exact binary/source hashes, existing binary hash,
existing unit renderings, fixed runtime marker, and stopped static service states.
An earlier failed exit state is accepted only with both MainPID/ControlPID zero;
it does not clear failure status. Service units retain `Restart=no`.

The helper then creates `/etc/systemd/system/<unit>.d/90-telemetry-upgrade-inhibit.conf`
for both roles, containing `RefuseManualStart=yes` and `ExecCondition=/bin/false`.
After fsync of the new drop-in directory entries and daemon-reload, it verifies
loaded DropInPaths, RefuseManualStart and the false ExecCondition, checks both
processes remain stopped, and acquires nonblocking
exclusive `flock` on all five native journal directory inodes, archives evidence,
installs the immutable release, and atomically replaces the two unit files and
manifest. It checks protected documents/journals byte-for-byte again and verifies
both units. **It leaves both start guards installed on success or failure.**
Do not automatically remove guards, retry partial installs, roll back a manifest,
initialize journals, or start services. A later reviewed locked renewal/start
ceremony owns removal. Guards persist under `/etc` across host reboots. Expired reviews remain expired;
review expiry is not a substitute for these independent start guards.

## Concrete execution after main review/checkpoint

Prepare a fresh owner staging directory with these files extracted using
`git show 69679c0:<path>`: service_launcher.py, service_profile.py, fd_handoff.py.
Copy the qualified cache gateway. All four hashes are pinned in the helper, which
checks again before apply. Stage the newly committed helper alongside them.
Do not copy the dirty working tree. Run review only first:

```sh
python3 -B upgrade_telemetry.py --bundle "$PWD" \
  --old-manifest-sha256 REVIEWED_OLD_MANIFEST_SHA256 \
  --archive /var/lib/velocita-telemetry-upgrade-69679c0
```

The review-only command checks supplied payload bytes; it is not host preflight.
Main must pin the installed manifest from a root read before approving the apply:

```sh
sudo python3 -B upgrade_telemetry.py --bundle "$PWD" \
  --old-manifest-sha256 REVIEWED_OLD_MANIFEST_SHA256 \
  --archive /var/lib/velocita-telemetry-upgrade-69679c0 --apply
```

Stop on any failure and inspect archive/guards/current hashes before deciding
recovery. No automatic repeat. After success, verify the manifest/release hashes,
unit ExecStart paths, retained guards, unchanged public documents/journal hashes,
zero service PIDs, existing single voter/exporter per host, and observer health.
A stopped producer may yield missing/stale settlement telemetry; that is expected.
A deployment is not proof of an actual confirmed fill.

## Validation and limits

Twelve focused tests cover the exact three-field manifest change, preserved
account pins, rejected first-use/recovery/runtime changes, both unit pins,
stopped-vs-active checks, source-hash rejection before Python execution, symlink
refusal and both start-guard mechanisms. Real filesystem apply tests include
a held native directory lock refusing before publication, existing public archive
directories, preserved manifest mode/group, successful
publication, and an injected failure after each unit/manifest replacement. All
failure boundaries preserve journals and start guards. Host UID/service-manager
calls and manifest rendering are injected in those orchestration tests. These
tests do not replace privileged host validation of systemd guards or the runtime
linker. No remote
mutations were made while preparing this protocol/helper.

Two targeted mutations (broaden manifest permissions; remove native directory lock)
each failed a named assertion with zero test errors; see mutation-results.json.
