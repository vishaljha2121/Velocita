#!/usr/bin/env bash
set -Eeuo pipefail

# V2 Phase 5 gateway/simulator acceptance on the x86-64 NUC.
#
# WHAT THIS GATE IS FOR. It is a LAYOUT AND CAPACITY gate more than a functional
# one. The Pi and container gates already proved the logic on ARM64. The open
# question is whether the bounded layout holds on the other deployment
# architecture: `TradingState` must still fit 81,920 bytes and the canonical
# snapshot 65,536, with x86-64 struct padding and alignment rather than ARM64's.
#
# That matters because architecture 10.12 derives the best-case eight unresolved
# orders from the sixteen-record submit-plus-reserved-cancel outbox budget. If
# the layout is tighter here, that availability bound cannot be raised safely.
#
# No firmware power interface exists on a NUC, so the Pi's throttled=0x0
# precondition is disabled rather than fabricated.
#
# Requires podman, git, sed and uname. It does NOT require a host C++ toolchain:
# everything is built inside the container.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 5 gateway/simulator acceptance'
export VELOCITA_EXPECTED_SOURCE_ROOT=b23bdea07d60163cd05a970cf058af8e6d62665afb47f1217f5f1162f5e2d1b4
export VELOCITA_EXPECTED_HOST_MACHINE=x86_64
export VELOCITA_EXPECTED_CONTAINER_ARCH=amd64
export VELOCITA_V2_HOST_ENVIRONMENT=nuc-x86_64-linux-container
export VELOCITA_REQUIRE_CLEAN_PI_POWER=0
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE5_NUC_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase5-nuc-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_NUC_CI_IMAGE:-localhost/velocita-nuc-ci:v2-phase5-acceptance}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE5_NUC_CACHE_VOLUME:-velocita-v2-phase5-nuc-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
