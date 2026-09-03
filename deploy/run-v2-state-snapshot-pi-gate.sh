#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 state-snapshot'
export VELOCITA_EXPECTED_SOURCE_ROOT=17bf519d09f4b4e3f87fcf399d0fe18999b5358112736c38c2e7262be6afba75
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_STATE_SNAPSHOT_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-state-snapshot-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-state-snapshot}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_STATE_SNAPSHOT_PI_CACHE_VOLUME:-velocita-v2-state-snapshot-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
