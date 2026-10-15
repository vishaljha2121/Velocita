# MVP deployment preparation — October 15

**Executable `1f8d7b6` is qualified on NUC/Pi. Its binary payload is staged, not installed. The six-order driver is staged on NUC1, inactive with no driver/client state. Deployment tools are committed as `24ecbec`; all 37 tests pass on NUC1, and the owner source/observer payload is staged and hash-verified. Installation is pending owner sudo. No trading service has started; no venue call, signing or money movement occurred in this preparation.**

## Necessary corrections

The installation is armed, unsealed and has empty recovery roots. Existing upgrade tools assume dormant credentials or initialized journals, so neither fits. `upgrade_first_start.py` handles exactly this state: pinned executable replacement, unchanged account/transaction documents and launch flags, stopped services, exclusive installer/recovery locks, retained start guards and archived old units/manifest. It prepares a new seal bundle; it does not apply it. The existing tested caller-key restoration helper creates missing active keys and retains retired originals.

The observer still enforced 15-minute review/capture lifetimes, whereas the runtime permits the approved 24-hour review. It rejected the review before publishing the strategy snapshot. The narrow canonical observer correction matches the runtime's 24-hour maximum while preserving expiry, capture-age, timestamp validation and producer-data freshness. Expired review metrics remain visible as not-current; old data is never given a new observation timestamp. Historical evidence copies are unchanged.

| Item | Evidence / current state |
|---|---|
| Qualified executable and model archive | [Hash-verified remote inputs](STAGED_ARTIFACTS.json) |
| Driver | [Stage result](DRIVER_STAGE_RESULT.json); static/inactive, PID 0; no client session or driver state created |
| Existing client image | `qualified-54a5fcb`; client/codec/kernel source unchanged; no voter/image rebuild needed |
| Guarded install helper | 13 unit tests passed; 7 named mutation kills; [plan](OWNER_INSTALL_PLAN.md) |
| Observer | 24 unit tests passed; 7 named mutation kills; [fix](OBSERVER_FIX.md) |
| Local review preview | [Exact old/new/seal pins](REVIEWED_UPGRADE_PREVIEW.json); not a fresh venue or privileged host read |
| Owner entry point | [owner-install.sh](owner-install.sh), staged from `24ecbec`; ready for owner-run installation |

## Next

1. Completed: owner-approved source checkpoint `24ecbec` and NUC1 qualification (13 installer + 24 observer tests).
2. Completed: exact committed source, pinned binary and observer files staged; see [owner-stage result](OWNER_STAGE_RESULT.json) and [qualification](QUALIFICATION.md). No new C++ build.
3. Owner runs the prepared installation command: guarded binary replacement, create-only caller restoration and read-only observer update. Trading services and driver remain stopped; share output before proceeding.
4. Review installed pins and current authorization, verify halted authenticated Raft state, release only the exact new start guards, start signer then gateway while halted and apply the newly pinned seal. The old arm/seal script is superseded.
5. Verify fresh observer/account/sequence state, then run the supervised six-effect strategy. Preserve its state for any separately approved extension; never re-stage or retry an ambiguous trade.

The recorded review expires `1792158514459` ms (October 16, 13:48:34.459 UTC).
Actual installation/startup remains subject to current-state checks; the preview is not settlement or authorization renewal evidence.
