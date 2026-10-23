# Exact-commit offline qualification

Source: `69679c081ce2d0b8939a89c3c846948c460d8d3e`.
Owner approved this checkpoint and offline NUC/Pi qualification. Both launched
successfully in parallel using the existing clean qualification source/cache at
`~/.cache/velocita-build/integrated-a890307-20261013`. Production services,
credentials, journals, voters and source mounts were not changed.

Container: `velocita-bounded-qualification-69679c081ce2`.
Evidence on each host:
`~/.cache/velocita-build/bounded-native-20261013-69679c081ce2d0b8939a89c3c846948c460d8d3e`.

Status: **qualified on x86_64 NUC1 and aarch64 Pi**. Each passed 52 selected
native CTests, 193 Python tests across the recorded suites, eight independently
checked synthetic process-crash cases, schema goldens, TLS fixtures and signer
isolation checks. Binary hashes and exact toolchain/source identities are in
`native/{nuc1,pi}/status.json` and `before.json`.

Incremental compilation took about 116 seconds on NUC and 86 seconds on Pi.
The native suites took 106.05 seconds and 183.04 seconds respectively. No test
failed or was skipped. Both full collectors' artifact manifests were hash-checked
locally. Full logs and synthetic drill artifacts remain on each host and in
`~/.cache/velocita-build/live-fill-69679c081ce2d0b8939a89c3c846948c460d8d3e/{nuc1,pi}`.

Warnings are preserved in each `native/*/warnings.json`: misleading indentation,
a test-fixture missing initializer and upstream Aeron unused parameters. This is
an incremental build warning inventory, not a claim that every unchanged source
was rebuilt without other warnings.

This qualifies the scoped build/test suite, not venue profitability, actual live
settlement, continuous stochastic trading, or journal-renewal application. The
new producer/observer/dashboard has not been deployed by this qualification.

Follow the build from separate Mac terminals:

```sh
ssh velocitas@100.72.127.102 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-69679c081ce2d0b8939a89c3c846948c460d8d3e/driver.log" "$HOME/.cache/velocita-build/bounded-native-20261013-69679c081ce2d0b8939a89c3c846948c460d8d3e/build.log"'
ssh velocitas@100.84.151.116 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-69679c081ce2d0b8939a89c3c846948c460d8d3e/driver.log" "$HOME/.cache/velocita-build/bounded-native-20261013-69679c081ce2d0b8939a89c3c846948c460d8d3e/build.log"'
```

The launcher validates the exact bundle HEAD, cache cleanliness, pinned toolchain,
architecture and disk headroom before starting an unprivileged offline container.
Build/test status is recorded in `status.json`; all warnings and test results are
retained separately. No live activation follows automatically.

Final read-only production check: NUC1 still has one stochastic voter/exporter
pair. The signer is inactive. The gateway retains systemd `failed/exit-code64`
from its earlier stop at18:17:27EDT (22:17:27UTC), matching review expiry;
`NRestarts=0`. This predates the23:04UTC qualification and is not a new build
failure. No failed-state clearing or restart was performed.
