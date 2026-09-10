#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 4 risk/orders/outbox acceptance'
export VELOCITA_EXPECTED_SOURCE_ROOT=a84609b54b2efe408f34151b91bbd10a217d8cdadbd7b18aba6a54c6183ac853
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE4_ACCEPTANCE_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase4-acceptance-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase4-acceptance}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE4_ACCEPTANCE_PI_CACHE_VOLUME:-velocita-v2-phase4-acceptance-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
