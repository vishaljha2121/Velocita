# Single-cluster cutover and authenticated service-user reads

Completed October 13, 2026 under the owner's approved `setup-r5/CUTOVER_SCOPE.md`.
**Authenticated reads passed; live admission is false.** Exactly one replacement
cluster runs across NUC1, Pi and NUC2. The old paper strategy is stopped and archived.

| Check | Observed result |
|---|---|
| New incarnation | `b79e65be1ea14e219016ec48ea02efef` |
| Production C++ | Qualified `86f4408`; existing native outputs reused, no full rebuild |
| All three voters/exporters | Ready1, failed0, term1, leader3, applied/durable index3 |
| Future starts | `startup = recover`, atomically installed after each voter became ready |
| Gateway read | UID29001, caller22/key13, session epoch2, index3 |
| Signer read | UID29002, caller23/key14, session epoch2, index3 |
| Verified state digest, both reads | `663d7599b83597d327a2991e11c4f13e9f6184d4400d1daae85309c5a32dc47c` |
| Passing run | `782b102342c52aa6` |
| Current trading state | Halted, existing catalog installed, no funded account, risk limits, trading-role assignments or strategy policy |
| Production units | Installed, loaded/inactive/static; manifest launch authorization false, account pins null |
| Credential scope | Raft caller keys only in role tmpfs; no venue bearer or transaction-signing key |
| Monitoring | New run label, `strategy=none`; exact dashboard selector returns three ready voters; Grafana health database OK |

`read-explicit_bind/` retains complete public helper results. Both responses use
the independently derived authority domain
`29b772a949627cd25c657a527ef2ea79a7b470454de9be9aa842462982b9d95e` and the existing
catalog root. Production decoder/GatewayLink verifies authentication,
correlation, full snapshot integrity, embedded state digest and catalog/domain.
No command, authority request, venue connection, signature or transaction occurs
inside the read helper. Each has a12-second read deadline inside60-second service
and75-second controller bounds. Failures are preserved, with no automatic retry.

## Changes and rollback material

1. Verified matching key13/key14 sources across voters without printing or saving
   credential bytes. Key14 is the new separate signer caller; key13 remains gateway.
2. Packaged exact native-qualified node/exporter/client outputs. Image IDs:
   - NUC1/NUC2: `d965170cd8168ada8ea91b6f8a72b46babe0b771f8cc39c7f0de7f1e045d6e04`
   - Pi: `6ecd4141692c3fce6dea7d491b658733163fa665f92884575778b460b6c95a75`
3. Stopped the old paper processes and all voter/exporter pairs. Archived old
   data/configs/book/ledger/session/unit material on each host at
   `/home/velocitas/velocita-indicator-paper/retired/stochastic-348e0b2c-before-schema12-20261013`.
   Hash inventories match before/after rename (35 files NUC1,21 each Pi/NUC2);
   files, archive directories and rename parents were fsynced. Old images remain.
4. Persistently masked the old paper edge/gateway/observer; started only one new
   voter/exporter pair per host. Source deployment trees were not swapped.
5. Mac ceremony committed only kill epoch1, executor generation1 and the existing
   catalog. `owner-ceremony.zsh` and `ceremony.log` preserve exact commands/results.
6. Prepared separate durable caller sessions using the actual C++ session-file
   creator, starting at epoch1. Only the successful read advanced each to epoch2.
   Production manifest now names the new incarnation but stays disabled.
7. Corrected runtime mount ancestry, then ran the bounded reads.
8. Updated monitoring through committed config `eea87c9`: new directory
   `/home/velocitas/read-b79e65be-86f4408/observability`, fresh run selectors and an
   inactive-trading banner. Only Prometheus/Grafana restarted. Their original
   units/configs remain preserved. Old metric samples remain historical data;
   exact run selectors avoid mixing them with the replacement cluster.

Rollback is not an instruction to run now. Stop replacement voters first, preserve
new state, and restore old units/configs/data/images as one matching set. Never
load schema12 state with schema11 binaries or run two incarnations concurrently.

## Refusals, fixes and validation

- Stage preflight initially mistook the host node-exporter for a voter. All hosts
  refused before mutation. `0ffaad8` corrected exact container-name matching;
  `stage-initial-failure.json` preserves the original failure.
- First read run `5c0975bb2b9e3ff6` refused unsafe ancestry before advancing a
  session or sending a request. Metadata-only inspection found `/run` root1777.
- Explicit `TemporaryFileSystem` mode0755 correction `b3ea33a` passed20 synthetic
  tests but did **not** fix the actual namespace. Second run `945b2658d5921106`
  refused identically. `read-first/` and `read-temporary_mount/` retain both.
- Systemd's parsed property contained mode0755, yet actual mountinfo showed a
  default tmpfs with no mode option. Upstream [v249 namespace implementation](https://raw.githubusercontent.com/systemd/systemd/v249/src/core/namespace.c)
  explains this: automatic API-VFS entries omit their options during copying;
  duplicate mount ordering can keep RUN and discard TMPFS. This source explanation
  is consistent with host observations, not a claim of binary-level systemd audit.
- Final correction `ffbb71a` explicitly binds separate root:root0755 directories
  `/etc/velocita-swap/runtime-roots/{gateway,signer}` read-only at `/run`. The actual
  service namespace was checked: `/run` root0755/read-only ext4; its role child
  remains a writable private tmpfs bind. No host `/run` permissions changed; no
  C++ safety check was relaxed. Both dormant production units use this arrangement.
- **21 final focused tests pass on Mac;3/3 mutations fail named assertions.**
  Native qualification is the actual successful UID-separated read above; no
  claim that all Python tests were rerun natively is made.
- Promtool validated the new config and all57 existing rules before monitoring
  replacement. Grafana was checked through terminal HTTP health and mount
  inspection, not browser rendering or Safari screen access.

## Operational notes and limits

Pi's first start spent several minutes in Podman's `storage-chown-by-maps` while
remapping ownership on its SD card. I/O progressed and the voter eventually became
ready; it was not killed or rebuilt. This is an image-start cost, not a C++ test
failure. Systemd verification also emitted the existing unrelated
`/lib/systemd/system/snapd.service:23: Unknown key name 'RestartMode'` warning.
Monitoring daemon-reload logged the old running command being replaced; the two
monitoring services were then deliberately restarted and checked. Keep these
warnings visible for the owner's later warning-cleanup iteration.

Initial terminal health lookups used the wrong bind addresses and were corrected:
voter exporters listen on each Tailscale IP:11410; Prometheus on127.0.0.1:9090.
An unprivileged attempt to include the protected public manifest in a health
report was refused; its permissions were preserved and the already approved
root export supplied only fixed public result/manifest/unit fields. No credentials
are present in this evidence. Password input occurred only at the sudo prompt.

This closes deployed authenticated state reads and the replacement-cluster
identity check. It does not close real TM transaction economics, protected real
custody, fresh owner-reviewed live account/funding pins, deployed held-position
recovery drills, missing external heartbeat or separate live-test approval. The
exact net-output/rounding and sponsor-repayment issue remains deferred/open in the
tracker. No paper balance was adopted as live money and no funds moved.
