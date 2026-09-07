#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 3 acceptance'
export VELOCITA_EXPECTED_SOURCE_ROOT=f2d350757c9f2f71f0eab4e2600af982bea44d56027082fafc70764c77073f5b
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE3_ACCEPTANCE_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase3-acceptance-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase3-acceptance}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE3_ACCEPTANCE_PI_CACHE_VOLUME:-velocita-v2-phase3-acceptance-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
