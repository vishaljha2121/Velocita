# Candidate native TLS toolchain — October 12, 2026

Preparation only: this Containerfile has not been built. No host call, installation, image removal, retag, release build, deployment or commit was performed in this task. It is an isolated solution to the missing development dependency for existing readiness item 10.

## Why a separate image

The supplied image inventory at `~/.cache/velocita-build/signer-recovery-20261012/build-image-inventory-final.json` inspected the actual pinned images on nuc1 and Pi. Both contain libcurl runtime `8.18.0-1ubuntu2.7` and OpenSSL `3.5.5-1ubuntu3.7`, but lack `libcurl4-openssl-dev` and libcurl pkg-config metadata. This is different from the older host-OS curl inventory. Nuc2 has the same pinned amd64 image ID; its attempted disposable metadata container failed because the CPU cgroup controller was unavailable. Do not call that a successful nuc2 runtime test.

[Containerfile.readiness-tls](../../../deploy/v2/Containerfile.readiness-tls) derives from the exact existing local parent ID and installs `libcurl4-openssl-dev=8.18.0-1ubuntu2.7`. There is no fallback version or dependency upgrade. A build fails if the exact package is unavailable or an existing parent package's version changes. Newly installed dependency packages, if any, are retained in the full manifest and additions list; their versions are fixed by the resulting candidate image, not independently preselected by this recipe. Review those additions before choosing its qualification manifest.

| Native architecture | Required `BASE` argument |
|---|---|
| amd64 / x86_64 | `sha256:2a79f82cce5de80ab5afc832af64acba549c963e89df509acfaeb5565cb232ee` |
| arm64 / aarch64 | `sha256:7796e188c717e65a9d4793a3b8673dde0124b3514a82b5ff3a060fe68728e97b` |

The recipe requires Ubuntu 26.04/resolute and verifies the installed parent libcurl runtime version. `SOURCE_COMMIT` must be a full lowercase 40-hex commit. The build command must obtain the Containerfile from that commit; passing a commit label alone is not source provenance.

It never changes `deploy/v2/TOOLCHAIN.env`, `Containerfile.dev`, `localhost/velocita-dev:current`, or any release/application tag. The output role is `readiness-tls-candidate`, explicitly `production-qualified=false`.

## Public package retrieval boundary

Only these explicitly written Ubuntu package sources are used:

- amd64: `https://archive.ubuntu.com/ubuntu` for resolute/resolute-updates; `https://security.ubuntu.com/ubuntu` for resolute-security.
- arm64: `https://ports.ubuntu.com/ubuntu-ports` for those three suites.
- Component: main; package signatures use the parent image's Ubuntu archive keyring. HTTPS peer/host verification stays on.

A dedicated `APT_CONFIG` file disables inherited configuration fragments, main configuration, package source directories, authentication files/directories and preferences. Its source list has no credentials or non-HTTPS transports. Proxies are explicitly DIRECT, redirects disabled, retries zero, and archive timeouts 30 seconds. The effective APT configuration is retained. Podman must also use `--http-proxy=false`; no secret/environment mount is supplied.

APT's documented load order reads `APT_CONFIG` before its fragment/main configuration, which permits this isolated configuration. The HTTP/HTTPS acquire transports retrieve public archive metadata and packages; no venue, wallet, RPC or order service is involved. Package retrieval is separated from `--no-download` installation. [APT configuration](https://manpages.debian.org/bookworm/apt/apt.conf.5.en.html), [HTTP transport](https://manpages.debian.org/bookworm/apt/apt-transport-http.1.en.html), [HTTPS transport](https://manpages.debian.org/bookworm/apt/apt-transport-https.1.en.html).

These are source/configuration controls, not a claim that a packet capture proved the unexecuted build's network methods. Installing official signed packages executes their maintainer scripts inside the disposable image build; `--no-download` prevents APT fetching, but is not a general syscall sandbox for those scripts. There are no production files or credentials mounted into the build. If stronger network isolation of installation is required, split the downloaded candidate stage from a `--network=none` installation build explicitly; do not claim that property for this recipe.

## Bounded build sequence, after the source checkpoint

Main agent/operator runs this only after the relevant source is committed. Use a separate durable workspace, not the production source directory. The following is a template for Linux bash/zsh; fill the reviewed commit, unique run ID and an available CPU selected from actual host affinity/cgroup information. No commands below ran in this preparation task.

```sh
set -euo pipefail
TLS_COMMIT=FULL_REVIEWED_40_HEX_COMMIT
TLS_RUN_ID=UNIQUE_RUN_ID
TLS_CPU=SELECTED_AVAILABLE_CPU
TLS_REPO="$HOME/src/readiness-checkpoint"
TLS_WORK="$HOME/.cache/velocita-build/readiness-tls-$TLS_RUN_ID"
test ! -e "$TLS_WORK"
mkdir -p "$TLS_WORK/context"
case "$(uname -m)" in
  x86_64) TLS_ARCH=amd64; TLS_PARENT=2a79f82cce5de80ab5afc832af64acba549c963e89df509acfaeb5565cb232ee ;;
  aarch64) TLS_ARCH=arm64; TLS_PARENT=7796e188c717e65a9d4793a3b8673dde0124b3514a82b5ff3a060fe68728e97b ;;
  *) exit 64 ;;
esac
TLS_TAG="localhost/velocita-readiness-tls:candidate-$TLS_RUN_ID-$TLS_ARCH"
test "$(podman image inspect --format '{{.Id}}' localhost/velocita-dev:current)" = "$TLS_PARENT"
if podman image exists "$TLS_TAG"; then exit 64; fi
GIT_OPTIONAL_LOCKS=0 git -c core.hooksPath=/dev/null -c core.fsmonitor=false \
  -C "$TLS_REPO" cat-file -e "$TLS_COMMIT^{commit}"
GIT_OPTIONAL_LOCKS=0 git -c core.hooksPath=/dev/null -c core.fsmonitor=false \
  -C "$TLS_REPO" show "$TLS_COMMIT:deploy/v2/Containerfile.readiness-tls" \
  > "$TLS_WORK/context/Containerfile"
sha256sum "$TLS_WORK/context/Containerfile" > "$TLS_WORK/containerfile.sha256"
df -Pk "$HOME" > "$TLS_WORK/disk-before.txt"
podman info --format '{{.Store.GraphRoot}}' > "$TLS_WORK/graph-root.txt"
df -Pk "$(cat "$TLS_WORK/graph-root.txt")" > "$TLS_WORK/image-disk-before.txt"
podman image inspect --format '{{.Id}} {{.Architecture}} {{.Os}} {{.Size}}' \
  "sha256:$TLS_PARENT" > "$TLS_WORK/parent-image.txt"
```

Inspect available image-store and scratch space before proceeding. Reserve at least 2 GiB free for this small dependency layer and logs; stop if space is insufficient. This is a conservative operational allowance, not a measured upper bound. Existing Pi free space was approximately 6.36 GiB at the earlier inventory and must be refreshed. Do not remove images, containers, ledger files or archives to make room automatically.

```sh
set -o pipefail
timeout --kill-after=30s 1200s taskset -c "$TLS_CPU" \
  podman build --pull=never --http-proxy=false --jobs=1 \
  --cpuset-cpus="$TLS_CPU" --memory=1g --memory-swap=1g \
  --build-arg "BASE=sha256:$TLS_PARENT" \
  --build-arg "SOURCE_COMMIT=$TLS_COMMIT" \
  -f "$TLS_WORK/context/Containerfile" -t "$TLS_TAG" \
  "$TLS_WORK/context" 2>&1 | tee "$TLS_WORK/build.log"
podman image inspect --format '{{.Id}} {{.Architecture}} {{.Os}} {{.Size}}' \
  "$TLS_TAG" > "$TLS_WORK/candidate-image.txt"
df -Pk "$HOME" > "$TLS_WORK/disk-after.txt"
df -Pk "$(cat "$TLS_WORK/graph-root.txt")" > "$TLS_WORK/image-disk-after.txt"
test "$(podman image inspect --format '{{.Id}}' localhost/velocita-dev:current)" = "$TLS_PARENT"
```

Stop on any failure; do not continue the command block after a failed build. Run it with `set -euo pipefail` in the owner-controlled script. The budget is one CPU, 1 GiB memory, one build job and a 20-minute timeout with a 30-second termination grace. The image may leave build cache on failure; preserve evidence and assess capacity, rather than automatic cleanup/retry. Cgroup restrictions must be supported. Nuc2's earlier CPU-controller error means its resource-limited path is not qualified; do not silently remove the constraints. Native amd64 qualification can begin on nuc1, with Pi separately on ARM.

## Retained image evidence and qualification

The candidate stores `/opt/velocita-readiness-tls/` containing:

- Original parent `/opt/velocita-dev/MANIFEST`, parent ID and source commit.
- Full before/after package versions and explicit new-package list.
- Controlled archive source list, effective APT configuration and downloaded `.deb` SHA-256s.
- Archive metadata hashes and retained downloaded packages.
- Compiler/CMake/Python/OpenSSL/curl versions and curl configure/features/flags.
- Curl shared-library and dependency paths/hashes, plus installed public-header hashes.

Before approving the candidate dependency fingerprint, extract/read these files using a bounded disposable container or `podman create`/`podman cp` workflow, retaining the exact candidate image ID. This is an operation for the main agent, not executed here. Compare the full package manifests: parent-package changes already make the build fail; inspect any added dependencies and the OpenSSL/libcurl backend. Do not interpret a successful package installation as transport qualification.

Then follow [NATIVE_SWAP_TRANSPORT_QUALIFICATION.md](NATIVE_SWAP_TRANSPORT_QUALIFICATION.md): execute fingerprint and fake-TLS qualification inside this exact candidate image, from a clean committed source checkout, using a new output directory outside the checkout. That script performs the more detailed actual dependency fingerprint comparison before compilation. Run the candidate with `--network=none`; its fake server/client communicate on container loopback. Bind only the explicit source and durable output/scratch paths, with source read-only. Do not mount home directories or pass venue credentials. Retain the candidate ID and both manifests beside the test output.

No switch of the production toolchain tag, rebuild of voters, release packaging change, or deployment is needed for this qualification. Candidate qualification remains narrower than production TLS/venue execution qualification.

## Preparation checks

Only local syntax validation is claimed: shell syntax for both RUN bodies and Python AST parsing of the embedded snippets. Package availability, build success, cgroup support and native fake-TLS behavior remain untested by this document. The first Ubuntu-manpage fetch timed out; Debian's upstream APT manpages supplied the configuration/transport references instead. No package GET or host command was issued during preparation.
