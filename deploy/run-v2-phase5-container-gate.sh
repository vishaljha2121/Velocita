#!/usr/bin/env bash
set -Eeuo pipefail

# V2 Phase 5 gateway/simulator acceptance, ARM64 Linux container.
#
# The source root below pins the reviewed content: CMakeLists.txt plus the
# bounded V2 headers, sources and tests the gate manifest selects. Gate scripts
# and evidence narratives live outside that manifest and do not alter it, which
# is why this file can name its own root.
#
# Recompute it on the development host with the same recipe the container uses:
#
#   { sha256sum CMakeLists.txt;
#     find include/velocita/v2 src tests -maxdepth 1 -type f \
#       \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
#       sort -z | xargs -0 sha256sum; } | sha256sum
#
# A mismatch means the tree moved, not that the gate is broken. Re-pin
# deliberately; never edit the root to make a run pass.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 5 gateway/simulator acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
export VELOCITA_EXPECTED_SOURCE_ROOT=8d31c33e66a2345e0e8152ff42876b8eb7036f0250d9048f705cd1c6bee0bbc4
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE5_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase5-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase5-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE5_CONTAINER_CACHE_VOLUME:-velocita-v2-phase5-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
