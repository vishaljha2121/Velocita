#!/usr/bin/env bash
set -Eeuo pipefail

# V2 Phase 5 gateway/simulator acceptance on the physical Raspberry Pi 5.
#
# Run only after explicit source synchronization and `sudo -v`, exactly as the
# Phase 4 gate required. Clean power evidence - throttled=0x0 before and after,
# plus temperature - is part of the acceptance record, not a nicety: a throttled
# run measures a different machine than the one that will trade.
#
# See run-v2-phase5-container-gate.sh for how to recompute the pinned root.

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 5 gateway/simulator acceptance'
export VELOCITA_EXPECTED_SOURCE_ROOT=681e222d7b6a6b314d1bf4e73bc24f2ce9685faa39a68a7832cf7abb5ec6976f
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE5_ACCEPTANCE_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase5-acceptance-pi-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase5-acceptance}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE5_ACCEPTANCE_PI_CACHE_VOLUME:-velocita-v2-phase5-acceptance-pi-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
