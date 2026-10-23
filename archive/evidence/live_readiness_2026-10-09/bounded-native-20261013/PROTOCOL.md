# Exact-commit bounded-MVP native qualification

Prepared before the next committed qualification. No host has been contacted by
these files' preparation, and no qualification result is claimed here. Supply the
full 40-character committed revision to the launcher; never build a dirty source
tree or infer qualification from the earlier b1c6650 results.

## Host and capacity contract

Use the existing unprivileged `velocitas` build account on NUC1 and Pi. Reuse
`$HOME/.cache/velocita-build/integrated-a890307-20261013/source`, its
`qualification` directory and `ccache`; do not touch service source mounts.
`launch.py` takes an exclusive launcher lock, refuses dirty source and running
qualification/cache users, verifies a local bundle's exact HEAD, fetches it and
checks out that commit detached. It never resets, cleans, stashes or deletes
prior evidence. New output uses the full commit; the container name uses its
first 12 characters. Existing output/container names refuse rather than retry.

Pinned TLS toolchain images:

| Architecture | Image ID |
|---|---|
| x86_64 | `076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f` |
| aarch64 | `3f3cfb3403a2644b27bf21351e26683e62f6ad90fde1f1b1567c8d2279d4fb43` |

Each offline container uses one build worker, CPU3 affinity, 2 GiB memory with
no additional swap, 512 PIDs, 256 MiB shared memory and `--network none`. Require
more than 2.5 GB free initially; terminate the active process group below 1.8 GB
or at its timeout. Source mounts read-only; no credential mounts or production
state directories are present. Loopback-only test fixtures remain possible.

## Qualification scope

Build Release production `velocita-v2-*` binaries and executable targets selected
by `v2-swap-*`, `v2-truemarkets-*`, and `v2-operational-recovery-tests`. This is
incremental qualification of affected components, not a fresh run of every
unrelated kernel test.

The recorded CTest inventory must contain at least 52 tests, explicitly including
transaction verifier, transaction review, verified signer, runtime production,
TLS fixtures and combined process drills. All selected tests must run without
failures or skips. Run the account Python suite, launcher/profile/install suites,
signer isolation audit and actual executable startup-gate tests. Check schema12 /
replication epoch2 goldens, then independently validate all eight process-drill
financial/transcript/OS-exit oracles.

The process drills use generated test keys and a synthetic venue. Gateway and
signer are child processes; voters are parent-hosted V2Node instances. Passing
them does not establish three-host live execution or venue settlement.

Record commit, toolchain ID, bundle/driver hashes, target and test inventories,
logs, warnings, cgroup resource counters and production binary hashes. Preserve
failures; do not suppress warnings. `collect.py` exports only completed or failed
logs and allowlisted synthetic drill evidence with a SHA-256 manifest.

The build may contain the narrow reviewed-profile admission predicate. Build
qualification does not authorize a launch. These scripts never edit a production
manifest or enable `launch_authorized`; `live_admitted=false` in their results
describes their actions, not a claim that the compiled gate is constant false.

## Invocation after the checkpoint exists

Upload the three Python files and the exact bundle to a durable build directory.
On each host, supply actual reviewed paths and the actual full revision:

```sh
python3 -B launch.py --commit FULL_COMMIT --bundle /absolute/path/checkpoint.bundle --runner /absolute/path/native-focused.py
```

The launcher prints the evidence directory and container identity. Follow
`driver.log`, then `build.log` or `tests.log` within the printed directory. Collect
only after its `status.json` is terminal:

```sh
python3 -B collect.py --commit FULL_COMMIT > /absolute/path/native-evidence.tar.gz
```

The main agent owns bundle preparation, transfers, actual launches and artifact
verification. No automatic cleanup or restart is included.
