#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_CONTAINER_GATE_LABEL='V2 Phase 4 risk/orders/outbox acceptance'
export VELOCITA_V2_CONTAINER_ENVIRONMENT='arm64-linux-container'
export VELOCITA_EXPECTED_SOURCE_ROOT=a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853
export VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT=${VELOCITA_V2_PHASE4_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase4-container-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase4-acceptance}
export VELOCITA_V2_PHASE2_CI_CACHE_VOLUME=${VELOCITA_V2_PHASE4_CONTAINER_CACHE_VOLUME:-velocita-v2-phase4-container-ci-cache}

exec "$script_dir/run-v2-phase2-container-gate.sh" "$@"
