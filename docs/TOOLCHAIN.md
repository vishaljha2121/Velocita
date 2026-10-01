# The pinned build toolchain (velocita-dev)

This follows book ch29 §"Compile cache and hermetic containers" and ch56 §"release identity":
*"The image is the build machine. Pin its reference and digest, update it in a reviewed change,
and make the same image run local builds, CI, lint, and test."*

## Why

Before 2026-10-01, every release build started from `ubuntu:26.04` (a **tag**, which can move).
It then ran `apt-get install g++ cmake …`, getting whatever versions the archive served that day,
and cloned Aeron from GitHub. That had three problems:

1. **Toolchain drift.** Builds made on different days, or on different hosts, could use
   different compiler versions without anyone noticing. For a replicated cluster, "same version"
   must mean the same verified bytes (book ch56).
2. **Network dependency.** Every build needed GitHub and the Ubuntu archive.
3. **Speed.** Package installs and the Aeron clone ran on every build.

## How it works

```
deploy/v2/Containerfile.dev             deploy/v2/Containerfile
(built rarely, reviewed)                (built for every change)

velocita-dev:<date>                     FROM velocita-dev   (checked by build-image.sh)
├─ ubuntu@sha256:da6fc2be… (index)       COPY our source
├─ g++, cmake, ninja, ccache, git, …     cmake: FETCHCONTENT_FULLY_DISCONNECTED=ON,
├─ /opt/velocita-deps/aeron @5b62f21d           sources = /opt/velocita-deps/*
├─ /opt/velocita-deps/willemt-raft @e428eeb    (CMake verifies both commits)
└─ /opt/velocita-dev/MANIFEST            compile (ccache) + FULL ctest (parallel)
                                         runtime stages FROM ubuntu@sha256:da6fc2be…
                                         LABEL source_commit, toolchain_image_id
```

**Identity at three levels:**

| What | Pinned by | Checked by |
|---|---|---|
| Ubuntu base | multi-arch **index digest** `sha256:da6fc2be…`: one digest that resolves to the amd64 image on the NUCs and the arm64 image on the Pi | podman: a digest names exact bytes |
| Compiler, tools, library sources | the **dev image ID**, one per architecture, recorded in `deploy/v2/TOOLCHAIN.env` | `build-image.sh` refuses to build if the local dev image differs (exit 64) |
| Aeron and willemt/raft commits | `AERON_COMMIT` and `RAFT_COMMIT` in `CMakeLists.txt` | `velocita_require_pinned_checkout`: CMake refuses a local copy at any other commit |

**Every release image carries its provenance** in its labels:

    podman image inspect localhost/velocita-v2:m3 --format '{{index .Labels "org.velocita.source_commit"}} {{index .Labels "org.velocita.toolchain_image_id"}}'

This is the minimum "release manifest" of book ch56: what source, built by what toolchain.

## One toolchain per architecture

The NUCs are x86-64 and the Pi is ARM64, so their compilers produce different machine code, and
each needs its own dev image. **Within an architecture there is exactly one:**
- the amd64 dev image is built **once, on nuc1**;
- it is **copied** to nuc2 (`podman save | ssh nuc2 podman load`). A copy keeps the image ID,
  while a second build would not.
- So both NUCs compile with byte-identical toolchains, and the Pi uses its own recorded arm64
  toolchain.

## Everyday use

    # ship the source as usual (CLEAN_RESET_RUNBOOK.md step 1), then on each host:
    cd ~/src/aeron-raft-pi
    deploy/v2/build-image.sh voter  m3-new        # all hosts
    deploy/v2/build-image.sh shadow m3-new        # nuc1 only (the edge image)

A change to our own code never touches the dev image. Builds use the compiler cache (ccache, at
`~/.cache/velocita-ccache`) and run the full test suite in parallel.

## Changing the toolchain (rare, deliberate)

Do this to upgrade the compiler or Ubuntu, or to move the Aeron or Raft pins.

1. Edit `deploy/v2/Containerfile.dev`: the base digest, the packages, or the commit ARGs. If the
   commits change, also change the pins in `CMakeLists.txt`; CMake enforces that they match.
2. Build it on nuc1 (amd64) and on the Pi (arm64):
   `podman build -f deploy/v2/Containerfile.dev -t localhost/velocita-dev:<DATE> .`
3. **Review the MANIFEST diff** against the old toolchain:
   `podman run --rm localhost/velocita-dev:<DATE> cat /opt/velocita-dev/MANIFEST`
   It shows every package version, the compiler, cmake and the commits.
4. Copy the amd64 image from nuc1 to nuc2:
   `podman save localhost/velocita-dev:<DATE> | ssh velocitas@nuc2 podman load`
5. Tag it `localhost/velocita-dev:current` on every host, record the two image IDs in
   `deploy/v2/TOOLCHAIN.env`, and commit that change. It is the reviewable record.
6. Rebuild and test the release images. Expect a cold compiler cache once: the compiler changed,
   so `compiler_check=content` correctly invalidates every cached object (book ch29: *"cache
   correctness over cache hit rate"*).

## What is still not pinned

- **Runtime images** still `apt-get install` a few runtime libraries (libstdc++, libbsd, libuuid,
  python3) at build time. Their base is digest-pinned, but those packages can update. The next
  step, if needed, is a `velocita-runtime` base image built the same way.
- **The dev image's own packages** are whatever the archive served on the day it was built. They
  are frozen from then on, and MANIFEST records them.
