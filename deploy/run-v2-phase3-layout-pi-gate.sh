#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 3 layout-guardrail'
export VELOCITA_EXPECTED_SOURCE_ROOT=1d9e44e20c495ee935b58b0723351cd5282642d0cdc098c4c35eba25d29bb577
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE3_LAYOUT_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase3-layout-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase3-layout}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE3_LAYOUT_PI_CACHE_VOLUME:-velocita-v2-phase3-layout-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
