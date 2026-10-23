#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 5 gateway/simulator acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
export VELOCITA_EXPECTED_SOURCE_ROOT=681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE5_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase5-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase5-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE5_CONTAINER_CACHE_VOLUME:-velocita-v2-phase5-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
