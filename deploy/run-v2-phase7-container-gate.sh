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
#
# AND THE ROOT IN THIS FILE PROVES NOTHING ON ITS OWN. It travels inside the
# tree, so a stale tree carries its own stale pin and they agree. Compare the
# recomputed value against the handoff or the acceptance record, from outside
# this tree, before trusting a PASS.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 6/7 replication and durability acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
export VELOCITA_EXPECTED_SOURCE_ROOT=4c9265268598bf5f56b65efeebd761d005fcf23400d49f7371281c146babe26f
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE7_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase7-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase7-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE7_CONTAINER_CACHE_VOLUME:-velocita-v2-phase7-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
