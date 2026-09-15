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
#       LC_ALL=C sort -z | xargs -0 sha256sum; } | sha256sum
#
# LC_ALL=C is required, not optional. Manifest order decides the root and
# collation decides manifest order, so recomputing under a UTF-8 locale yields a
# different value for identical content. On macOS use `shasum -a 256` in place of
# `sha256sum`; the output format is the same.
#
# A mismatch means the tree moved, not that the gate is broken. Re-pin
# deliberately; never edit the root to make a run pass.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 5 gateway/simulator acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
export VELOCITA_EXPECTED_SOURCE_ROOT=681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE5_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase5-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase5-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE5_CONTAINER_CACHE_VOLUME:-velocita-v2-phase5-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
