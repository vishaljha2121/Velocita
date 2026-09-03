#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 canonical-replay'
export VELOCITA_EXPECTED_SOURCE_ROOT=810f8c4db00d6ae37061694ab3be6f775b3312f0b837c09b00c589903d5e04aa
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_CANONICAL_REPLAY_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-canonical-replay-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-canonical-replay}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_CANONICAL_REPLAY_PI_CACHE_VOLUME:-velocita-v2-canonical-replay-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
