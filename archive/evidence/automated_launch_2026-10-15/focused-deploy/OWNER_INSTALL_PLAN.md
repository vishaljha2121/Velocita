# Armed first-start executable update — owner plan

Prepared from qualified source `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc` and qualification evidence checkpoint `fa495a1`. This document records preparation, not an installed upgrade or permission to repeat a failed operation. The main agent stages public artifacts; sudo, TM account/session access and Keychain remain owner-only. Preserve the single cluster, all funding/history, retired originals and every journal. Do not repeat retirement, arm, funding or driver staging.

## Exact pins and scope

| Item | Value |
|---|---|
| Incarnation | `aae119b95ddcea5a206f03ab5ca12c15` |
| Expected armed installed manifest | `f139936f89cae8de526f82c02b41aa3be1b2ec668efdfb7492281bb8d5d2568a` |
| New executable source | `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc` |
| NUC gateway ELF SHA-256 | `8d0fab33246d85628cac6471c3926205fd5710f96a55731106324a101cc9abcf` |
| Release directory | `/opt/velocita/releases/1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc` |
| Owner staging base | `/home/vishal/.cache/velocita-build/focused-deploy-1f8d7b6` |
| Existing runtime image | `076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f` |
| Unchanged runtime profile | `b9734e53cfb37ee38344a5cf9fcdcb4905e248818dbc151c5e18a9f01d0208b2` |
| Review expiry | `1792158514459` ms; October 16, 2026, 13:48:34.459 UTC |

The expected old manifest pin was derived from retained adoption documents and the applied arm flags, matching the historical handoff prefix. The owner helper must verify it against installed bytes; it is not a fresh privileged readback. Both swap roles use the qualified `velocita-v2-gateway` executable with separate role arguments. Do not substitute the standalone generic `velocita-v2-signer` binary.

The qualified launcher, profile renderer and FD helper hashes are fixed in `upgrade_first_start.py` and [staged artifacts](STAGED_ARTIFACTS.json). The executable update changes exactly `source_commit`, `binary` and `binary_sha256` in the manifest; both unit files receive corresponding paths and manifest pins. Account/transaction documents, launch flags, credentials, recovery roots and the existing cluster stay intact.

## Why old commands do not fit

- `install_disabled.py` accepts dormant old/new manifests and requires venue credentials absent for replacement. The current installation is armed with credentials installed.
- Historical telemetry/Byreal upgrade helpers require a sealed installation and existing initialized journals. This installation is unsealed with empty recovery roots.
- `review_transition.py` cannot change executable pins.
- `verify_installed.py` main requires a dormant manifest and inactive/dead services. Its individual protected caller-key functions remain reusable; do not change the armed manifest merely to make that verifier pass.

The narrow new `upgrade_first_start.py` handles this specific first-start state. It installs persistent manual/dependency start guards, takes an exclusive installer lock and locks both empty recovery roots. It validates old/new artifact pins, current reviewed account/transaction bindings and stopped services. It archives old manifest/units, preserves credential bytes in place, and prepares a root-private seal bundle without applying it. Any refusal requires inspection; never automatically retry.

## Owner commands and sequence

The following assumes the reviewed helper checkpoint is staged under `$B/source` with its repository-relative dependencies, and the four pinned release artifacts under `$B/payload`. Main must establish those paths before providing a final owner launcher. No placeholder SHA should be passed as an approval pin.

Optional read-only diagnosis, from the Mac; prints no credential bytes or their hashes:

```zsh
ssh -t vishal@100.72.127.102 '
sudo systemctl show velocita-swap-gateway.service velocita-swap-signer.service -p ActiveState -p MainPID -p ControlPID -p UnitFileState -p DropInPaths -p RefuseManualStart -p ExecCondition
sudo sha256sum /etc/velocita-swap/public/manifest.json
sudo find /var/lib/velocita-swap-gateway/recovery /var/lib/velocita-swap-signer/recovery -mindepth 1 -maxdepth 1 -printf "%p %y\n"
sudo stat -c "%n uid=%u gid=%g mode=%a links=%h size=%s" /run/velocita-retired-credentials-b79e65be-20261015/gateway/caller.key /run/velocita-retired-credentials-b79e65be-20261015/signer/caller.key
'
```

1. **Owner sudo, install only after the helper checkpoint and staged artifact pins are reviewed.** Both services must be inactive or failed with main/control PID 0, static units, and both recovery roots completely empty. The helper enforces these prerequisites and refuses missing/mismatched artifacts. Planned command:

```zsh
ssh -t vishal@100.72.127.102 '
set -eu
B=/home/vishal/.cache/velocita-build/focused-deploy-1f8d7b6
sudo python3 -B "$B/source/docs/evidence/automated_launch_2026-10-15/focused-deploy/upgrade_first_start.py" \
  --bundle "$B/payload" \
  --old-manifest-sha256 f139936f89cae8de526f82c02b41aa3be1b2ec668efdfb7492281bb8d5d2568a \
  --archive /var/lib/velocita-first-start-upgrade-1f8d7b6 \
  --apply
sudo -k
'
```

   The result supplies the new manifest and seal-bundle pins. The new guard is `90-first-start-upgrade-inhibit.conf` for each service. It remains installed. If the helper fails partway, retain the archive/release/guards and reconcile that exact partial state.

2. **Owner sudo, create-only caller restoration.** The corrected helper checks both stopped services, empty recovery roots, retained source metadata and missing destinations before writing either key. It creates mode-0600, single-link 32-byte keys for UID/GID 29001 and 29002, verifies own-role access/cross-role denial, and retains the retired originals. No `mv`, overwrite or credential retransmission is needed.

```zsh
ssh -t vishal@100.72.127.102 '
set -eu
B=/home/vishal/.cache/velocita-build/focused-deploy-1f8d7b6
sudo python3 -B "$B/source/docs/evidence/automated_launch_2026-10-15/restore_caller_keys.py" \
  --receiver "$B/source/deploy/v2/swap-runtime"
sudo -k
'
```

3. **Review before start.** Main/owner verifies unchanged account partition and current account authorization; existing expiry cannot be extended by this upgrade. Verify the installed new manifest/unit hashes, protected role credentials and restored caller access, halted authenticated Raft state, no pending effect/conflict and the funded opening. Release only the exact two newly installed guards under a separate reviewed owner start procedure. Neither historical `owner-start.zsh` nor its old seal pin can be reused: it repeats arm and pins the old executable manifest.

4. **First start while halted.** Start signer, then gateway; inspect authenticated admission and logs, require both active and all five expected first-use journals. Apply the new seal proposal under `/var/lib/velocita-first-start-upgrade-1f8d7b6/seal` using the helper-returned exact bundle pin. This changes only `initialize_swap_recovery` to false. A startup failure or partial journal set must be investigated, not retried or erased. Observer upgrade/readback must supply a fresh correct-incarnation strategy snapshot before driver activation.

5. **Supervised six-effect run.** Consult current [driver staging result](DRIVER_STAGE_RESULT.json) and [driver plan](DRIVER_PLAN.md); never restage an existing driver root. Fresh authenticated state must establish operator and strategy sequence histories before mode normal. The historical next operator sequence is 7, not a substitute for readback. The corrected limit counts six attempted effects, including failures/releases; 240-effect extension remains a separate owner decision.

The guarded upgrade and caller restoration are concrete supported operations. Guard release, armed-install validation and the final start/seal launcher still require the main agent's final preparation/review; this document does not imply those actions have happened. No voter replacement or fresh cluster is needed.

## Local qualification of the helper

Thirteen unittest cases passed. Seven named mutations were killed by assertions: phase validation, old-manifest pin, initial empty-recovery check, stopped-service checks, credential preservation, loaded guards and current review validation. Real temp-file/atomic replacement/flock operations exercise publication and injected partial failure; host root identity/systemctl and synthetic document fixtures are injected. No production host was modified by these tests.

Evidence is at `$HOME/.cache/velocita-build/focused-mvp-20261015/deploy-tests/{green.log,mutations-final.json,*-final.log}`. Initial pre-implementation RED import output remains at `../upgrade-first-red.log`. The first empty-root mutant survived because a later check still refused; the oracle was strengthened to require initial nonempty roots fail before guard publication, and the final mutant is killed. These Python checks do not replace native qualification, production privilege isolation, fresh account checks or owner authorization.
