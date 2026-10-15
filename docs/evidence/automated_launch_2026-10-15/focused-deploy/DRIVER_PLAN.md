# Six-effect driver deployment plan — 2026-10-15

**Preparation only. No driver was staged or started, no remote file was written, and no trading command was sent by this review.** Install the qualified gateway/signer and complete the main agent's stopped-service, credential, authorization and halted-start checks before activating this driver. Existing funding, incarnation, journals and request sequences remain in place.

## Pinned inputs

| Input | Reviewed value |
|---|---|
| Qualified executable/model source | `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc` |
| Native evidence | [RESULTS](../focused-mvp/native/RESULTS.md), [qualification JSON](../focused-mvp/native/QUALIFICATION.json); evidence commit `fa495a1` |
| Incarnation | `aae119b95ddcea5a206f03ab5ca12c15` |
| Model tar SHA-256 | `10457cc0e0029779ca7f214f22f250ac0c8b63f1f4201758eb631ce9add40bbe` |
| Committed staging script SHA-256 | `61f3c64b1f17260592559606a977b84b998344e4626bed05f07a50e249634336` |
| Opening record SHA-256 | `2f329e441584fbda843187ffd251f8ac000ea383506b0ba4623586ef604d9b95` (340 bytes) |
| Opening source on Mac | `$HOME/.velocita-credentials/truemarkets/automated-launch-aae119b9/documents/opening/opening.record` |
| Exact reviewed expiry | **`1792158514459` ms = 2026-10-16 13:48:34.459 UTC** |
| Driver ceiling | **6 attempted Raft effects**, including failed/released effects; never effect 7 |
| Epochs | Kill epoch 1, executor generation 1; verify these against fresh authenticated state before use |
| Managed opening | $25 USDC; existing wallet SOL remains outside this managed ledger |

The exact expiry and opening digest were read from the prepared public `documents/summary.json`; the opening's hash was independently recomputed. The older handoff rounds expiry to seconds. Do not derive the flag from that rounded timestamp. If reviewed documents change, refresh this expiry from the newly installed reviewed document rather than extending it independently.

## Current read-only NUC1 inventory

Read at `1792080720589` ms (2026-10-15 16:12:00.589 UTC), as `velocitas`, without sudo or credential contents:

- User UID 1003; home `/home/velocitas`.
- `/home/velocitas/velocita-strategy/aae119b95ddcea5a206f03ab5ca12c15` does not exist. No driver configuration or driver state exists at that path.
- `velocita-strategy-driver.service`: `LoadState=not-found`, `ActiveState=inactive`, `SubState=dead`.
- `/home/velocitas/velocita-v2/textfile/strategy-settlements.json` does not exist. The observer must publish a fresh, correct-incarnation snapshot before the driver starts.
- Caller-12 key metadata: UID 1003, GID 1004, mode 0600, size 32 bytes, one link. Its contents were not read; this is not proof of authenticated caller access.
- Existing client image `localhost/velocita-v2:qualified-54a5fcb`: image ID `ddf59cea34198ca4a130dbdbd31b7eee5aeec5ccb499c86cc831a8dfb6a263f6`, created 2026-10-15 12:59:57 UTC.
- One `velocita-paper-stochastic-node` and one matching exporter container run on that image. Their names are historical; no new cluster is needed.

This inventory is a snapshot, not an authenticated proof of Raft mode, next sequence, balances or pending effects. Recheck it immediately before applying the plan.

## Existing client image is sufficient

The driver invokes only `/usr/local/bin/velocita-v2-client` inside the existing image. Between `54a5fcb` and qualified `1f8d7b6`, the client source, swap reducer, command codec, kernel protocol and core state schema are unchanged. The changed C++ files are gateway/signing runtime components; the corrected Python model comes from its separate committed tar.

Therefore the existing qualified `54a5fcb` image is sufficient for the strategy's **client process**. It is not sufficient for the gateway/signer services, which need the new qualified binaries. Do not rebuild/restart voters solely to run this driver. Pin/check the existing image ID below so an unexpectedly retagged image causes a stop rather than a silent substitution.

## Staging checks and activation dependencies

1. Before **activation**, main deployment installs the exact qualified gateway/signer, restores caller keys with the corrected helper, verifies protected state and finishes the halted startup/observer steps. No repeated arm/funding/retirement. These steps may proceed in parallel with stopped driver staging.
2. Before **activation**, fresh authenticated state still identifies this incarnation, reviewed opening and epochs; mode is halted, no pending effect or conflict, and the managed account/outside partition matches fresh wallet reconciliation.
3. Before the **first driver start**, verify caller 21's committed request history before treating a missing driver state as a fresh sequence. A missing file is not permission to reuse a previously consumed sequence. The historical next operator sequence is 7, but must also be rechecked; this document does not authorize guessing it.
4. Confirm the driver root still does not exist. `stage_driver.py` deliberately refuses an existing root. If anything has already been staged or sent, preserve the directory, driver state and client session; prepare an in-place reviewed update instead of rerunning staging or deleting state.
5. The installed review expiry remains exactly the value above. Staging requires more than 420 seconds remaining. No local flag grants authority beyond the protected review.

## Prepared commands — not run

These commands create/copy public deployment artifacts and install a stopped manual-start driver unit. They do not issue orders. Run staging only after the artifact, root, stopped-unit and expiry checks above; account/observer/installer dependencies still gate activation. Stop at the first refusal. Scratch directories are create-only so prior attempts remain available.

Mac, from the repository:

```zsh
set -euo pipefail
cd "$HOME/aeron-raft-pi"
DRIVER_SOURCE=1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc
DRIVER_BUNDLE="$HOME/.cache/velocita-build/focused-deploy-1f8d7b6"
test ! -e "$DRIVER_BUNDLE"
mkdir -m 700 -p "$DRIVER_BUNDLE"
git archive --format=tar --output="$DRIVER_BUNDLE/model.tar" "$DRIVER_SOURCE" model/src/velocita_model
git show "$DRIVER_SOURCE:deploy/v2/strategy/stage_driver.py" > "$DRIVER_BUNDLE/stage_driver.py"
shasum -a 256 "$DRIVER_BUNDLE/model.tar" "$DRIVER_BUNDLE/stage_driver.py" \
  "$HOME/.velocita-credentials/truemarkets/automated-launch-aae119b9/documents/opening/opening.record"
```

Check all three hashes against the table. Then prepare the public bundle on NUC1:

```zsh
ssh velocitas@100.72.127.102 'test ! -e "$HOME/.cache/velocita-build/focused-deploy-1f8d7b6" && install -d -m 700 "$HOME/.cache/velocita-build/focused-deploy-1f8d7b6"'
scp "$DRIVER_BUNDLE/model.tar" "$DRIVER_BUNDLE/stage_driver.py" \
  "$HOME/.velocita-credentials/truemarkets/automated-launch-aae119b9/documents/opening/opening.record" \
  velocitas@100.72.127.102:/home/velocitas/.cache/velocita-build/focused-deploy-1f8d7b6/
```

As `velocitas` on NUC1, stage once:

```sh
set -eu
DRIVER_BUNDLE="$HOME/.cache/velocita-build/focused-deploy-1f8d7b6"
test "$(podman image inspect localhost/velocita-v2:qualified-54a5fcb --format '{{.Id}}')" = ddf59cea34198ca4a130dbdbd31b7eee5aeec5ccb499c86cc831a8dfb6a263f6
test "$(sha256sum "$DRIVER_BUNDLE/stage_driver.py" | cut -d ' ' -f 1)" = 61f3c64b1f17260592559606a977b84b998344e4626bed05f07a50e249634336
python3 -B "$DRIVER_BUNDLE/stage_driver.py" \
  --archive "$DRIVER_BUNDLE/model.tar" \
  --archive-sha256 10457cc0e0029779ca7f214f22f250ac0c8b63f1f4201758eb631ce9add40bbe \
  --opening "$DRIVER_BUNDLE/opening.record" \
  --opening-sha256 2f329e441584fbda843187ffd251f8ac000ea383506b0ba4623586ef604d9b95 \
  --image localhost/velocita-v2:qualified-54a5fcb \
  --incarnation aae119b95ddcea5a206f03ab5ca12c15 \
  --kill-epoch 1 --executor-generation 1 --max-orders 6 \
  --review-expires-ms 1792158514459
systemctl --user show velocita-strategy-driver.service -p LoadState -p ActiveState -p SubState
```

Expected stage output includes `started:false`, `max_orders:6` and the exact expiry. Expected unit state is loaded/inactive. Inspect the generated `driver.json`; confirm the incarnation, ceiling, expiry, opening path and caller 21/key slot 12. Staging does not create a zeroed replacement for a prior state; the first driver start may create state only after the fresh-caller-history precondition is proven.

## Supervised activation boundary

The main agent/owner must first verify the newly installed gateway, signer and observer while halted, including the first protected token mint and authenticated state reads. Require a matching-incarnation observer snapshot no more than 12 seconds old and no pending effect.

Only then use the owner-run `operator.zsh … mode normal <verified-next-sequence> --run`, followed by this separate NUC1 command as `velocitas`:

```sh
systemctl --user start velocita-strategy-driver.service
journalctl --user -u velocita-strategy-driver.service -f -o cat
```

Reconcile every settlement against the wallet. Preserve the driver state/client session through all stops. A clean alternating run produces six effects and ends flat; six is a hard maximum, not a promise to submit six. Released/failed attempts consume effect capacity. A failed final exit can leave exposure and produces a durable stop reason; do not request a seventh effect to conceal that outcome.

The driver stops new entries 420 seconds before expiry and stops initiating exits 210 seconds before expiry. It attempts a timely exit with fresh data; this cannot guarantee flatness or finality when the feed, venue or network fails. At expiry it records pending/exposure/unknown state for recovery. It never extends signer authorization. An ambiguous client outcome stops as `submission_unconfirmed` with its sequence/effect retained; do not restart with fresh state or automatically resend.

After the supervised run, halt using the freshly verified next operator sequence, inspect pending/managed exposure and retain every journal. **No `--extend 240` is included or authorized by this plan.** Extension remains a separate owner decision and must retain the same driver state and request history.

## Remaining blockers

- Corrected gateway/signer installation and caller-key restoration are still main-deployment work.
- Fresh authenticated account/authorization/sequence checks and halted startup have not been performed by this driver review.
- Observer snapshot was missing at the recorded inventory; driver startup must wait for a fresh valid snapshot.
- Exact account review expiry must remain current through the run; after renewal use new reviewed documents/expiry, not an arbitrary CLI increase.
- Native fake-venue qualification establishes the six-effect lifecycle, not live profitability or guaranteed drain. Current create/prepared crashes may still need supervised recovery, as recorded in the native qualification limits.
