#!/usr/bin/env bash
set -Eeuo pipefail

# V2 Phase 6/7 replication and durability acceptance on the x86-64 NUC.
#
# WHY THIS RUNS ON THE NUC AND NOT ONLY IN A CONTAINER. Phase 5's NUC gate asked
# a layout question - whether the bounded structures still fit under x86-64
# padding. This one asks a different one, and it is the reason the NUC is the
# target machine for Phases 6 and 7: the write path now has a THREAD in it. The
# writer drains off the owner loop, truncation and compaction quiesce it, and a
# parked writer racing a segment rewrite is a real interleaving on real cores.
# Container and development-host runs exercise the same code on a different
# scheduler and a different filesystem; agreement between them is the evidence.
#
# The layout bounds still apply and are still checked: `TradingState` must fit
# 81,920 bytes and the canonical snapshot 65,536.
#
# No firmware power interface exists on a NUC, so the Pi's throttled=0x0
# precondition is disabled rather than fabricated.
#
# Requires podman, git, sed and uname. It does NOT require a host C++ toolchain:
# everything is built inside the container.
#
# Compute the candidate with deploy/source-manifest.sh and supply its expected
# root from an independently held review record. This NUC leg cannot close F7 by
# itself; the same root still needs host, container, Pi, and mixed-replay proof.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 6/7 replication and durability acceptance'
: "${VELOCITA_EXPECTED_SOURCE_ROOT:?set the externally pinned candidate source root}"
export VELOCITA_EXPECTED_HOST_MACHINE=x86_64
export VELOCITA_EXPECTED_CONTAINER_ARCH=amd64
export VELOCITA_V2_HOST_ENVIRONMENT=nuc-x86_64-linux-container
export VELOCITA_REQUIRE_CLEAN_PI_POWER=0
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE7_NUC_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase7-nuc-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_NUC_CI_IMAGE:-localhost/velocita-nuc-ci:v2-phase7-acceptance}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE7_NUC_CACHE_VOLUME:-velocita-v2-phase7-nuc-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
