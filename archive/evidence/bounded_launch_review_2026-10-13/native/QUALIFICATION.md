# Exact-commit native qualification

Source: `fe54001ce7bbd36c25947cc29ecf70833d161e37`.
Owner approved scoped checkpoint and offline qualification. NUC1 (x86_64) and
Pi (aarch64) runners started in parallel from verified Git bundles and clean,
isolated qualification checkouts. Status: **qualified on both architectures**. Completed October14 01:04UTC
(October13 evening in New York).

Host evidence directory (both):
`~/.cache/velocita-build/bounded-native-20261013-fe54001ce7bbd36c25947cc29ecf70833d161e37`.
Container: `velocita-bounded-qualification-fe54001ce7bb`.

One worker, CPU3, 2GiB memory, offline container; existing build/ccache reused.
Pinned TLS image IDs are enforced by `launch.py`. No production source tree,
services, credentials, journals or funding were changed. No live orders.

Follow from the Mac:

```sh
ssh velocitas@100.72.127.102 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-fe54001ce7bbd36c25947cc29ecf70833d161e37/driver.log" "$HOME/.cache/velocita-build/bounded-native-20261013-fe54001ce7bbd36c25947cc29ecf70833d161e37/build.log"'
ssh velocitas@100.84.151.116 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-fe54001ce7bbd36c25947cc29ecf70833d161e37/driver.log" "$HOME/.cache/velocita-build/bounded-native-20261013-fe54001ce7bbd36c25947cc29ecf70833d161e37/build.log"'
```

## Results

| Host | Native CTests | Additional Python tests | Process-drill cases | Build seconds | CTest seconds |
|---|---:|---:|---:|---:|---:|
| nuc1 | 54/54 | 222 passed | 8/8 | 418 | 106.75 |
| pi | 54/54 | 222 passed | 8/8 | 316 | 189.91 |

No failed or skipped CTest, failed runner stage, retry, OOM or capacity stop.
Schema goldens, TLS fixtures and signer-isolation checks passed. The collector
exported103 artifacts per host; every artifact hash was verified locally.
Exact source/toolchain/bundle/driver and binary hashes are in each host's
`before.json` and `status.json`. Full logs, JUnit and synthetic drill evidence:
`~/.cache/velocita-build/byreal-native-fe54001ce7bbd36c25947cc29ecf70833d161e37/{nuc1,pi}`.

Both containers exited0. Post-run read at01:04:16UTC found NUC1 gateway/signer
inactive, PID0 and RefuseManualStart=yes. Each host retained one stochastic
voter/exporter pair; no production service/tree or runtime state was changed.
The Pi has no active swap gateway/signer. No deployment or money movement.

Compiler warnings are retained in `WARNINGS.md` and each host's `warnings.json`.
Passing qualification is not a claim of warning-free compilation, a new live
quote/account review, settlement verification or continuous-strategy readiness.

Next: install the qualified executable while starts stay inhibited, apply the
reviewed locked route transition, refresh account/session and current-route
review, then separately review the bounded one-$1 launch. The old installed
69679c0 executable was not replaced by this qualification.
