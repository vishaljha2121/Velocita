#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 6/7 replication and durability acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
: "${VELOCITA_EXPECTED_SOURCE_ROOT:?set the externally pinned candidate source root}"
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE7_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase7-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase7-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE7_CONTAINER_CACHE_VOLUME:-velocita-v2-phase7-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
