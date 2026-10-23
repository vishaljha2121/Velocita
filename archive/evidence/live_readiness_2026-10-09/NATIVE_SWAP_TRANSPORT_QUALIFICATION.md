# Native transport qualification preparation, 2026-10-12

Status: preparation and read-only inventory complete; native transport builds and native fake-TLS tests **not run**. No deployment, service restart, package installation, image execution, venue request, credential access or signer operation occurred. This continues existing live-promotion item 10; it does not add a new promotion requirement.

## Actual host inventory

The metadata reads completed at 18:40:26–28 UTC. Exact bounded outputs are retained in [NATIVE_TRANSPORT_INVENTORY_2026-10-12.json](NATIVE_TRANSPORT_INVENTORY_2026-10-12.json).

| Host / verified login | CPU | Host compiler / CMake | Host libcurl | Host OpenSSL | Available disk |
|---|---|---|---|---|---|
| nuc1: `velocitas@100.72.127.102`, `bulbazar-NUC` | x86_64 | GCC 11.4 / 3.22.1 | development + runtime 7.81.0, Ubuntu package `7.81.0-1ubuntu1.29` | 3.0.2, package `3.0.2-0ubuntu1.30` | 64.18 GiB |
| nuc2: `velocitas@100.85.36.22`, `nuc2` | x86_64 | GCC 11.4 / 3.22.1 | development + runtime 7.81.0, same package | 3.0.2, package `3.0.2-0ubuntu1.20` | 245.06 GiB |
| Pi: `velocitas@100.84.151.116`, `vishalkapi` | aarch64 | GCC 15.2 / 4.2.3 | runtime 8.18.0, package `8.18.0-1ubuntu2.7`; `curl-config` and libcurl pkg-config metadata unavailable | 3.5.5, package `3.5.5-1ubuntu3.7` | 6.36 GiB |

Both NUC host environments fail the new transport's libcurl 7.85 minimum API gate. Pi's installed CLI/runtime is recent enough, but its development environment is incomplete. A runtime version alone does not prove headers, link inputs or TLS backend compatibility. Version comparisons here are API prerequisites, not a vulnerability assessment of distribution backports.

The known dev image metadata matches `deploy/v2/TOOLCHAIN.env`:

- Both NUCs: `localhost/velocita-dev:current`, amd64, ID `2a79f82cce5de80ab5afc832af64acba549c963e89df509acfaeb5565cb232ee`.
- Pi: same tag, arm64, ID `7796e188c717e65a9d4793a3b8673dde0124b3514a82b5ff3a060fe68728e97b`.

No container was started or entered. Its actual curl headers/runtime remain unverified. `deploy/v2/Containerfile.dev` does not explicitly install the curl development package; transitive installation cannot be assumed either way. A reviewed toolchain amendment may be needed. Do not install dependencies into running production hosts as a shortcut.

All three source trees report `SOURCE_COMMIT=0680b08a5bbfbcaba32a77fbdf5cbe5d0162f40c`. That is a source marker, **not evidence of the active voter image, health or gateway state**. This task deliberately did not query processes or modify cluster services.

SSH used `BatchMode=yes`, `ConnectTimeout=10`, `StrictHostKeyChecking=yes`, `UpdateHostKeys=no`, `ControlPath=none`. A Python script supplied on standard input ran individual installed-metadata commands with 8-second timeouts, bounded retained stdout/stderr, and no writes on the target. Commands were `hostname`, `id -un`, `uname`, `/etc/os-release`, compiler/CMake/curl/pkg-config/OpenSSL version reads, `df`, explicit-package `dpkg-query`, known-image `podman image inspect`/filtered image metadata, and reading the source marker. Local scratch retains `inventory.py` and individual JSON files under `~/.cache/velocita-build/native-transport-20261012/`.

## Prepared qualification command

New tool: [tools/qualify-swap-transport.py](../../../tools/qualify-swap-transport.py).

Its two modes are local-only. It has no install, SSH, deployment, remote HTTP, account, or order capability. The only network execution in `run` is the existing loopback TLS fixture, whose C++ probe independently rejects other origins. The transport target remains test-only and excluded from release.

`fingerprint` inventories installed Linux dependencies; it is not approval or qualification. `run` requires:

1. Full explicit 40-hex source commit and expected native architecture.
2. Clean isolated checkout at that commit, including no ignored/untracked files. Required source paths must be regular committed blobs, and the running qualifier must match that commit.
3. Explicit reviewed manifest and its SHA-256. A matching hash alone is insufficient: every captured fingerprint field must equal the reviewed content before compilation.
4. Installed curl development version at least 7.85; structured, restricted pkg-config compiler/linker flags.

The fingerprint retains compiler, assembler, linker, compiler frontend, Python, pkg-config and OpenSSL executable identities; versions where applicable; installed curl public headers; libcurl shared library; dynamic-library dependency paths/hashes; Python `_ssl` module/dependencies; OS and architecture. Commands receive a minimal environment, excluding inherited compiler/loader/Python override variables. Read-only Git calls disable hooks, fsmonitor, replacement objects, optional locks and global/system configuration.

After preflight, the tool copies only the exact committed transport header/source/probe/fake-TLS test/qualifier blobs into a new output tree. It compiles those bytes with ASan/UBSan and the test-only macro, records the command and log, verifies the linked libcurl file, checks runtime curl header/runtime agreement plus TLS/async-DNS capability, and runs all ten existing fake-TLS tests. Fingerprints are checked again after build and tests. Failure leaves `qualified:false`; success explicitly says `scope: native isolated fake TLS only` and `production_qualified:false`.

Future commands, **only after reviewed commit and toolchain selection**; none were run on a host in this task:

```sh
python3 -I -B /path/to/clean-committed-source/tools/qualify-swap-transport.py \
  --mode fingerprint --source /path/to/clean-committed-source \
  --output "$HOME/.cache/velocita-build/transport-fingerprint-UNIQUE"
```

Review `candidate-manifest.json` and its actual dependency contents. Preserve that exact file as the approved toolchain manifest, recording its SHA-256 independently. Then, in the **same native toolchain environment**:

```sh
python3 -I -B /path/to/clean-committed-source/tools/qualify-swap-transport.py \
  --mode run --source /path/to/clean-committed-source \
  --output "$HOME/.cache/velocita-build/transport-qualification-UNIQUE" \
  --commit FULL_REVIEWED_40_HEX_COMMIT --expected-arch aarch64 \
  --toolchain-manifest /path/to/reviewed-toolchain-manifest.json \
  --toolchain-manifest-sha256 REVIEWED_64_HEX_SHA256
```

Use `x86_64` for a NUC. Output paths must be new and outside the checkout. The tool never fetches dependencies or resets a checkout. The strict source gate intentionally refuses the current shared dirty tree. Native testing should use an isolated checkout/export arranged by the main agent after checkpoint approval, not a production source-tree swap.

This is a bounded dependency identity check, not a complete reproducible-build or operating-system supply-chain proof. It does not hash every C++ standard header, compiler specification or system configuration file. Loopback tests use fixture certificates and do not qualify production CA-store packaging, venue TLS interoperability, real endpoint schemas, signing, order submission or economic correctness. Retain the image ID and dev-image manifest alongside qualification output when running inside the selected build image; this script does not invent or inspect container identity.

## Local validation of the command

`python3 -B -m unittest discover -s tests -p test_qualify_swap_transport.py -v`: **5 tests passed**. Tests use disposable local Git fixtures and mock preflight environments; they do not build the transport or access remote hosts. Explicit tests cover manifest mismatch despite correct hash, omitted fingerprint fields, wrong hash, source mismatch, tracked/untracked/ignored dirt, runtime mismatch/TLS/async-DNS requirements, and gate ordering before compilation.

`python3 -B tests/mutation_qualify_swap_transport.py`: **5/5 named-assertion mutation kills**, covering manifest content, manifest hash, checkout cleanliness, runtime SSL and async-DNS enforcement. Evidence: `~/.cache/velocita-build/native-transport-20261012/mutation-cmsme5vv/results.json`.

TDD evidence: `red.log` has real named-assertion failures for initial no-op manifest and runtime checks. Its separate initial missing-constant setup error is **not counted as RED evidence**. Final `green.log` records all five tests. No native-positive build result is claimed.

## Proposed combined gateway / lost-ack drill

This is a test design for main-agent integration, not a production execution adapter.

The existing `FakeSwapTlsServer` in `tests/test_swap_https.py` records one POST and can close the socket before replying. Existing `unknown_kill_and_late_settlement` in `tests/v2_swap_integration_tests.cpp` verifies unknown reservations survive kill and block normal resume. `swap_paper_raft_gateway_restart` in `tests/v2_gateway_node_tests.cpp` drives real `GatewayLink` and three voters through committed attempted state and WAL restart, but deliberately resolves using deterministic **paper** simulation. That successful paper recovery must not be reused to interpret an ambiguous external send as a fill or as unsent.

Connect these existing boundaries through an opt-in test-only driver:

1. Reserve the fixed synthetic account budget, commit Prepare and Attempt via `GatewayLink`, and wait for the committed state before the single fake POST. Assert the retained hold and canonical request identity.
2. Have the loopback server durably record accepting that request, then withhold/lose the reply. Its current in-memory request list suffices for a client-only restart; a server restart scenario needs a fixture journal so the request counter survives. This journal is test evidence, not a proposed production settlement authority.
3. Observe transport `possibly_sent`, commit MarkUnknown through `GatewayLink`, and assert no hold release, resubmit, simulated settlement or buyback. Assert fake-server accept count exactly one.
4. Kill/restart the gateway and voters from WAL, change leader, and inspect every voter's committed reservation. Test the separate window where MarkUnknown's Raft acknowledgement is lost: persisted Attempt alone must also recover by reconciliation, never by resend or paper simulation.
5. Feed stale pre-attempt state and a repeated link reply; neither may authorize another send. Test unavailable status/read failures: the hold stays.
6. Supply one explicit synthetic final reconciliation result with exact known debit, credit and fee. Assert reservation release and owner accounting once; a duplicate is idempotent, contradictory evidence freezes further activity.

Also exercise definite local refusal before curl starts as a distinct branch. Do not conclude an arbitrary TLS/timeout failure means no charge: this transport intentionally returns conservative exposure once `curl_easy_perform` is entered. Keep all fake endpoint names and settlement fixtures explicit, with no guessed True Markets execution route.

Separate Oct 5 historical evidence reported by the main agent identifies a sponsored account whose SPL `SetAuthority` changes its close authority to the sponsor. It reinforces the pending ownership/rent review: sponsored creation does not establish that rent is reclaimable by the token owner. That decoder/economic-policy work is outside this transport qualification and is not closed by TLS or lost-ack tests.
