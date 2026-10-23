# Native qualification of the focused correction checkpoint

Owner approved the scoped checkpoint and native qualification on October 15.
Initial source commit: `c19a6755c0135286843c7e06f41cb753a4cbf252`.
Intermediate checkpoint: `887d6f51373eae6642391fe5ae0a42307aa3df09` corrected the test-target packaging allowlist and added negative boundary checks. NUC qualified it; Pi stopped in the continuous drill.
Intermediate checkpoint `26b1e51` added fresh Raft deadlines after blocking ports and deterministic regression tests. NUC completed all checks; Pi exposed a separate signer accept/request deadline defect. See [TIMING_CORRECTION.md](TIMING_CORRECTION.md) and [SIGNER_ACCEPT_DEADLINE.md](SIGNER_ACCEPT_DEADLINE.md).
Current qualification checkpoint: `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc`, with a fresh fixed deadline after accepting a signer IPC connection. Both architectures completed qualification; see [RESULTS.md](RESULTS.md) and [QUALIFICATION.json](QUALIFICATION.json).
The committed source hashes match the locally qualified overlay; gateway main includes only the continuous-mode argument, not the shared resting-paper work.

The native runner reuses the prior qualified, pinned TLS-capable toolchain image and incremental build cache. This is an offline qualification, not a release-image deployment. It uses a clean detached committed checkout in the dedicated build cache, never the deployed source tree. The existing `launch.py` pins the image ID per architecture, refuses competing cache users, records bundle/source identity, checks disk capacity, and runs a network-disabled container as the unprivileged deployment user. CPU affinity is core 3, one compiler job, memory cap 2 GiB, no swap. No production credentials are mounted.

Fresh host identity checks:

| Host | SSH | Architecture |
|---|---|---|
| bulbazar-NUC | velocitas@100.72.127.102 | x86_64 |
| vishalkapi | velocitas@100.84.151.116 | aarch64 |

The old address `100.85.36.22` resolved to NUC2/x86_64; no build or mutation was attempted there. Do not infer architecture from older address notes.

Qualification includes at least 59 selected CTest targets, existing eight process-drill cases, three fixed repetitions of the three new six-effect production-path fake-venue cases (54 fixture effects per architecture), existing service/account/observer/CLI checks and the 104 focused Python checks. The runner validates case outputs and records executable hashes. Failure stops the runner; no automatic retry or deployment occurs. Prior failed evidence remains intact.

Both hosts retain the single existing cluster. Nothing in this procedure restores caller keys, arms or seals launch, changes funding or review documents, stages a live driver, starts gateway/signer services, or contacts TM/Solana. Same-UID fake-venue drills do not qualify production UID separation or external venue connectivity.

## Commands

Local bundle creation (from the approved commit):

```sh
git bundle create "$HOME/.cache/velocita-build/focused-native-c19a6755c0135286843c7e06f41cb753a4cbf252/source.bundle" 54a5fcb..HEAD
```

Copy only that bundle and the three committed native runner scripts into a new host cache directory of the same name. Run from each host:

```sh
python3 -B "$HOME/.cache/velocita-build/focused-native-c19a6755c0135286843c7e06f41cb753a4cbf252/launch.py" \
  --commit c19a6755c0135286843c7e06f41cb753a4cbf252 \
  --bundle "$HOME/.cache/velocita-build/focused-native-c19a6755c0135286843c7e06f41cb753a4cbf252/source.bundle"
```

Follow `driver.log`, `build.log` and `tests.log` under
`~/.cache/velocita-build/bounded-native-20261013-c19a6755c0135286843c7e06f41cb753a4cbf252/`.
The launcher refuses reuse of that evidence directory. Collect only allowlisted logs/results using `collect.py`; generated keys and fixture bearer files remain private in host scratch.

## Pi capacity

Initial free space was only about 1.7 GiB, below the existing 2.5 GB preflight guard. No guard was lowered. Inactive diagnostic scratch and object files excluded from this qualification were archived with per-file SHA-256 verification. The verified archive contains 2,503 files, occupies 129,189,825 bytes, and increased available disk from 1,793,003,520 to 2,696,900,608 bytes. Sparse diagnostic files explain why logical original size is larger than space recovered. Production WAL, journals, images and qualification reports are excluded. The first archival preflight correctly refused an apparent cache mount overlap; it was the node exporter's standard read-only `/` mount. That read-only metrics mount was explicitly excluded from cache-user detection; no files were removed by the refused attempt. The second attempt retains a restorable private archive and its manifest.

## Test-boundary correction

Both initial attempts passed all 59 CTest targets, then stopped at `signer-isolation` because
the newly added test executable was absent from the exact target allowlist. Initial logs are
preserved under `attempt-c19a675/`. The correction allows only that test target and adds checks
that fail if its binary is shipped, its source is added to production, its target is linked into
production, or its real verifier is replaced with a synthetic macro. Local RED had four failures;
GREEN passed all 24 boundary tests. Production trust and code are unchanged.

The intermediate checkpoint used the same commands above with
`887d6f51373eae6642391fe5ae0a42307aa3df09` replacing the initial commit. Its successful NUC
evidence is retained under `qualified-887d6f5/nuc1/`; its failed Pi evidence is under
`attempt-887d6f5/pi/`. Reconfiguration reused C++ objects, with Ninja dependency-scanning work.
The runner also executes all 24 boundary tests.

The current qualification uses those commands with `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc`.
It adds two predetermined continuous-suite repetitions after the first run in CTest.
Any failure stops the run. These are fixed repetitions, not retries until success.

The private Pi archive preserves original bytes and metadata, not a ready-to-overwrite runtime
backup. Its original logical size is 3,543,256,690 bytes; restoring sparse diagnostic files must
allow sufficient space or preserve their sparse representation. Keep the archive and manifest;
production state and prior qualification reports were never included in the cleanup.
