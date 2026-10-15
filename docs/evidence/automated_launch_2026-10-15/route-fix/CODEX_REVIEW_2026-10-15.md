# Codex continuation review — 2026-10-15

Reviewed executable checkpoint: `a835deb24a001adc19ffca5adad83a94d3b4ccad`, compared with `0fed1b0`. Review covers the changed feed/telemetry path, pre-Prepare admission, unsent-release continuation, state-read outage handling and owner upgrade helper. This is a scoped correctness review, not a claim of exhaustive review of the entire repository.

## Conclusion and reasons

No deployment-blocking defect found in the inspected changes. Proceed with the requested exact-commit native qualification and owner-controlled upgrade; do not use the previously staged `a665615` upgrade. Qualification passed on NUC1 and Pi; installation has not occurred.

- **Feed:** Claude chose stateless catch-up for any contiguous stale candle rather than the earlier suggested startup-only flag. This is a reasonable smaller design: `Driver.check` runs first; future/gap checks remain; stale bars call the existing non-trading warm path and return before request allocation. Source timestamps and the independent pre-submit age check remain. Continuous stale data therefore suppresses trading rather than repeatedly re-bootstrapping. The existing expiry/inflight checks still run. This is a deliberate difference from the earlier proposal, not its exact implementation.
- **Refused routes:** `SwapPrepareSession::poll` first durably records create intent. After the unsigned response, it asks the production port to run the existing transaction verifier, including finalized lookup-table resolution, before persisting a prepared payload. Production main passes the adopted transaction policy. The signer still verifies independently; the gateway precheck does not grant signing authority. A refused/lost unsigned create stays locally `create_intent` and in Raft `reserved`, so it can be released after `sign_by` without ever signing or executing it. It does not release prepared/attempted/unknown effects or retry create/execute.
- **Raft outage:** `SwapGatewayRuntime::step` retries failed top-level state-read outcomes while the configured outage window remains. Successful domain-matching reads clear the outage clock; failed commands retain the stop path. Tests cover survival, cutoff and reset. Production config sets 120,000 ms. Read-retry does not replay an execution request or change journal contents. The runtime remains single-threaded, with existing session/link ownership checks and owned pending records.
- **Upgrade:** the helper pins the old sealed manifest and executable, verifies documents/units/guards, requires the driver stopped and at least an hour of review remaining, archives old files, locks recovery directories after stopping services, preserves journal/credential/document bytes, switches executable pins only, starts signer then gateway and restores guards. It requires a fresh flat producer observation after startup. Its old flat observation is deliberately allowed to be stale when the gateway is down; that is not evidence of a fresh wallet read. Gateway startup must still pass authenticated account admission.
- **Telemetry:** the added price/indicator/fill markers observe existing strategy/account data. Metrics write errors do not propagate into trading. Marker prices are effective quote/base amounts; they are display evidence, not proof of profitability.

## Limits to state accurately

The outage window begins at the first failed read outcome, not the instant a network failure starts, and is checked on subsequent outcomes. It is not a precise 120-second wall-clock service-stop SLA. The retry code is enabled by `read_outage` outside recovery-only mode; production supplies it with continuous mode. It is not blanket resilience for prepare/submission/resolution/signing failures: nested sessions and failed commands can still stop safely with state retained. Do not claim every election at every phase is automatically recovered.

The native runner uses fake venue transport for process drills. It cannot prove current TM route acceptance, actual fills, fees, or successful live reconciliation. The helper's health metrics are not themselves authenticated Raft authority. Review lifetime, protected signer checks, signed transaction bounds and account admission remain required at runtime. No new ADR, kernel schema, economic cap or release command was introduced by this continuation.

## Current host readback

At 2026-10-15 19:56 UTC: NUC1 driver inactive/PID0; gateway failed; signer active; manual-start guards retained. Driver configuration is max_orders 240, sequence 0, no inflight request or persistent stop. Producer file records index 11, managed $25, no SOL/effects/fees/pending/settlements, but its observation timestamp is from before the gateway failure and is stale. Do not present it as current authenticated wallet evidence. NUC1 has 43 GB free, Pi 4.7 GB; launchers independently enforce capacity limits. One existing voter/exporter pair remains on each inspected host; no production service changes made.

## Qualification / continuation

Both completed native jobs used the committed launcher and runner, exact bundle HEAD `a835deb`, pinned architecture-specific toolchain images, network-disabled containers, one compiler/core 3 and 2 GiB RAM. Existing incremental build objects are reused. Logs are under `~/.cache/velocita-build/bounded-native-20261013-a835deb24a001adc19ffca5adad83a94d3b4ccad/` on each host. Stop at the first failed qualification; retain all outputs.

After both qualify: retarget only helper COMMIT/ARCHIVE, run its tests, commit explicit owned paths as requested in the takeover plan, stage the exact helper commit and qualified NUC1 binary, verify every file hash, then give the owner the read-only preflight. Only after its result is reviewed provide the `--run` command. Sudo stays owner-run. Keep driver stopped and alert silence unchanged until successful installation and fresh account observation. Do not reset or re-stage driver state. Review entry drain starts 2026-10-16 13:41:34.459 UTC; expiry is 13:48:34.459 UTC.

## Independent local checks

A clean `git archive a835deb` (no dirty overlays) passed 84 strategy/driver unittest executions and three owner-helper tests. Logs are in [codex-qualification-a835deb](codex-qualification-a835deb/). Claude reports 206 Mac CTest targets and mutation checking; this continuation has not independently rerun those mutation experiments. Native qualification below supplies fresh exact-build evidence rather than treating the report as a substitute.

## Native result — completed 20:07:59 UTC

Both architectures qualified the exact `a835deb` executable. Each passed59 selected CTest targets,404 Python/CLI unittest executions, eight process-drill cases and nine continuous cases (54 fixture effects across three repetitions). All151 retained artifact hashes per host verified. Counts overlap: process drills are part of CTest, and the continuous cases include its initial run. [Summary and actual per-log counts](codex-qualification-a835deb/SUMMARY.json). The runner retains a historical hard-coded `focused_python_tests=104`; use the actual logs/counts, not that stale field, for this checkpoint.

Each incremental build retained24 unique warning lines. Full diagnostics and resources are archived per host. The GCC bounds/overflow diagnostics resolve to the existing RouteFixture vector builder in `tests/v2_swap_transaction_verifier_test_support.hpp`, already tracked by `focused-mvp/BUILD_WARNINGS.md`. No warnings were suppressed.

The helper now targets `a835deb` and fresh archive `/var/lib/velocita-route-fix-upgrade-a835deb`; old installed commit/manifest pins remain unchanged. This is a helper-only retarget, not a gateway rebuild or a live start. Three helper tests pass after the retarget.
