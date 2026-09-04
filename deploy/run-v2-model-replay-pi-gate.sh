#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 model-replay'
export VELOCITA_EXPECTED_SOURCE_ROOT=d6eb59adecac3a486c2eee83151df02d66e7425865dd5076ad34ac0beeb96668
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_MODEL_REPLAY_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-model-replay-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-model-replay}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_MODEL_REPLAY_PI_CACHE_VOLUME:-velocita-v2-model-replay-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
