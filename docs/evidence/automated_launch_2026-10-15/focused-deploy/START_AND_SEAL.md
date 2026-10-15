# Guarded first start and seal

Requested by owner after successful guarded installation. Source is committed as `f30e6ccbba0735f60ebacf3ed2dcf637656662bd`, qualified on NUC1 and staged for the owner. Startup has not run; sudo stays with the owner. No new C++ build is required. Qualified binary remains `1f8d7b6`.

## Current evidence

Owner output confirms manifest `bf8c44952fef8c6f5e2e01b63e1e65ebad17e0dc780802e3ec126f3ba61a797f`, restored caller keys, empty recovery roots and retained start guards. Fresh read-only health endpoints on all three voters agree: ready, healthy, mode 5 (halted), term 1, leader 3, commit/applied index 7. Health metrics are operational evidence, not an authenticated state proof. The installed native processes authenticate state themselves at startup, before creating their first-use journals. Gateway admission also verifies TM account/main-wallet identity and the current balance against the reviewed account partition.

Review expiry remains October 16, 2026 at 13:48:34.459 UTC. The helper validates installed current review bytes at execution time; this document neither refreshes nor extends them.

## What the helper does

`start_first_run.py` uses the already-qualified installer, caller verifier and seal-transition helpers. It checks exact installed manifest, executable/helper/unit pins, current document bindings, protected credential identities, empty recovery roots, both stopped services, stopped driver and three healthy halted voters at funding index 7. It takes the installer lock and records one create-only attempt. It temporarily moves only the two exact upgrade guards into the attempt directory; other guards are never cleared.

It starts signer once, then gateway once. It restores the start guards while those processes remain active. All five journals must have first-use ordinals, valid checksums and matching policy/incarnation/read-proof bindings. The authenticated gateway observation must be at most 12 seconds old, at index 7, with exactly $25 managed cash, zero managed SOL/fees/P&L/effects, and no pending effect or settlements. The gateway's native admission owns fresh TM balance verification; the Python helper does not connect to TM.

Only then it applies the existing seal bundle `0ddf2b8191ba2a19337719554707d13ef7dba9a9181e3461d1c7214f690d0eab`. The expected sealed manifest is `f3df32529bc2e3bfd08cb4a82ddecd8fe9b128160b2417ecb7850252cd0668c4`. Sealing disables first-use initialization; it does not change trading mode, funding, budget, credentials or journal contents. The driver remains stopped. No strategy activation is part of this command.

Any failure after the attempt starts restores guards and attempts to stop both services, without restarting, removing journals or overwriting state. A failed stop is surfaced for owner intervention. Partial state is retained and another attempt is refused. Concurrent owner commands changing mode/funding are prohibited during this procedure. The wrapper does not add a new Raft command or alter kernel authority.

## Qualification

Eight new orchestration/validation tests passed; 13 installer and 19 existing review-transition tests also passed. Six named mutations were killed by assertions (no import/runtime-error kills): bypassed preflight, gateway started first, omitted failure guard restoration, ignored halted mode, ignored observation freshness and overwritten existing guard. Raw initial RED, GREEN and mutation results are in `qualification/start/`.

Tests use synthetic process/credential interfaces and real guard-file rename checks. NUC1/Python 3.10.12 qualification passed from the exact committed archive: 21 startup/installer tests, 19 seal-transition tests and 24 observer tests (64 total). All 65 archive files and the owner copy verified against SHA-256 pins. No production helper was run, no live process was started, and no venue request was sent during preparation. The owner approved this checkpoint. Exact source is staged at `/home/vishal/.cache/velocita-build/focused-start-20261015/source`; qualification logs and staging/hash manifests are in `qualification/start/`. Post-staging readback still shows gateway inactive, signer failed, both PID 0 and RefuseManualStart=yes; driver inactive, PID 0. Existing runtime and units remain unchanged until the owner command runs.

## Ready owner command

On NUC1 as `vishal`:

```sh
sh "$HOME/.cache/velocita-build/focused-start-20261015/source/docs/evidence/automated_launch_2026-10-15/focused-deploy/owner-start.sh"
```

It prompts for sudo locally and performs one startup/seal attempt. Expect about one minute for service initialization and checks. Share the output; do not rerun on failure. The existing review must still be current when executed. No mode-normal command or strategy-driver start is included.
