#!/usr/bin/env bash
set -Eeuo pipefail

# V2 Phase 6/7 replication and durability acceptance, ARM64 Linux container.
#
# WHAT THIS GATE ADDS OVER PHASE 5. Phase 5 was a layout and capacity gate. This
# one is about DURABILITY AND THREADS, which is the part most likely to differ
# off the development host: fdatasync rather than fsync, a real page cache, a
# real scheduler behind the writer thread, and directory syncs that actually
# mean something. `v2-replication-tests` includes a power-cut sweep that
# truncates the segment at every structurally distinct offset, and a parked
# writer racing a segment rewrite; neither is interesting on a machine where the
# filesystem lies.
#
# Compute the candidate with deploy/source-manifest.sh and place its expected
# root in the invocation environment from an independently held review record.
# This script intentionally carries no default root. A PASS here is only the
# container leg; F7 remains open until the same root passes every required host,
# NUC, Pi, and mixed-replay leg.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 6/7 replication and durability acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
: "${VELOCITA_EXPECTED_SOURCE_ROOT:?set the externally pinned candidate source root}"
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE7_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase7-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase7-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE7_CONTAINER_CACHE_VOLUME:-velocita-v2-phase7-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
