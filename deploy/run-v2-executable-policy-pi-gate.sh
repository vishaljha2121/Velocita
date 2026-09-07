#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 3D executable-policy codec'
export VELOCITA_EXPECTED_SOURCE_ROOT=fcba3fbd3ece8f67ea75d0fac0bf97354f66c47e09afc1d61cdce1a4e9d8a1fe
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE3D_POLICY_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase3d-policy-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase3d-policy}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE3D_POLICY_PI_CACHE_VOLUME:-velocita-v2-phase3d-policy-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
