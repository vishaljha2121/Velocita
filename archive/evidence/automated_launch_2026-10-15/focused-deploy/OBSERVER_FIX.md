# Observer authorization-window alignment

Status: implemented and locally tested; not installed by this task. No trading service was started and no credentials were read or changed.

The installed observer rejects account reviews longer than 15 minutes. The approved account-review contract in `deploy/v2/swap-runtime/service_launcher.py` permits up to 24 hours. Consequently, an otherwise valid current review stops `snapshot()` before it publishes `strategy-settlements.json`; the driver correctly refuses the missing or stale snapshot.

## Narrow correction

`deploy/v2/observability/live_observer.py` now uses the launcher's 86,400,000 ms maximum. Review timestamps must be unsigned ASCII decimal strings within uint64; capture and observation times must be positive integers. Capture must precede review, review must not be in the future, and both review duration and capture age at review must be at most 24 hours.

A review is current only before its explicit expiry and while the original capture is at most 24 hours old. Valid expired reviews remain visible with `review_current=0`, preserving dashboard diagnostics; they never become current by observing them again. Existing 15-minute reviews retain their explicit expiry. Malformed, future, overlong, or originally overage reviews are rejected.

Balance capture time, gateway observation time, and current observer execution time remain distinct. The observer preserves the gateway producer timestamp. The strategy's independent 12-second observation-freshness requirement is unchanged; increasing an authorization window does not freshen a stalled producer.

## Validation

- RED before implementation: 23 tests, four assertion failures and no test errors, recorded in `~/.cache/velocita-build/focused-mvp-20261015/driver/observer-red.log`.
- GREEN: `python3 -B -m unittest discover -s tests -p test_live_observer.py -v`: 24 tests pass. `observer-green.log` records the result.
- Seven isolated named mutants killed with assertion failures, zero test errors: `old-fifteen-minute-window`, `capture-age-ignored`, `review-expiry-ignored`, `overlong-review`, `old-history-review`, `future-review`, `malformed-review`. Evidence: `observer-mutations.json` and per-mutant logs in the same durable scratch directory. Mutation copies never replaced the shared source.
- `git diff --check` passes for the two source/test paths.

Exact SHA256 after this change:

| File | SHA256 |
| --- | --- |
| `deploy/v2/observability/live_observer.py` | `4d180ef32ff1a59c0677cbc7fdca53b03a15114fc739f8c58749aa672229b58f` |
| `tests/test_live_observer.py` | `b905e73135f1a4dd67f9263fabbbd827f3015c5eb8390748a773681def81ee25` |

The historical evidence copy `docs/evidence/automated_launch_2026-10-15/live_observer.py` and its historical `observer.sha256` remain untouched. Deployment must package this corrected canonical observer with newly generated adjacent pins, using the existing upgrade script and canonical five-second timer. No C++ rebuild is required for this observer-only change.

## Read-only deployment review

NUC1's installed observer hash was `06f9950cccb1b1a2286ec1b2978fb7604375f62c6d408fe9e4c0a4eb011aed62`; its service was failed with exit status 1 and its 15-second timer remained active. The pre-upgrade archive path was absent. The root service's supplementary `velocitas` group and textfile directory mode 0775 permit publication; the public snapshot is readable by the strategy. The service has no venue credentials or network access. These observations establish the upgrade preconditions, not successful deployment.

Independent read-only review of `upgrade_first_start.py` found no blocking defect. Its 13 local tests pass. It pins the qualified payload before loading its renderer, changes only three executable manifest fields, validates current reviewed documents, checks stopped services and empty recovery roots, installs and verifies start guards before mutation, preserves credentials and documents, and retains guards after partial failure. Success does not start, rearm, seal, or trade. This review does not substitute for the owner's privileged host execution and post-install verification.
