#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 3C authoritative-decision'
export VELOCITA_EXPECTED_SOURCE_ROOT=d3a45742b260562abb987c31092d04bc2abe8b162f71e0008af14f3be4942b75
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE3C_DECISION_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase3c-decision-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase3c-decision}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE3C_DECISION_PI_CACHE_VOLUME:-velocita-v2-phase3c-decision-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
