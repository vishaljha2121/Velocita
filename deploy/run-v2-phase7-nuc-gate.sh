#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export VELOCITA_V2_PI_GATE_LABEL='V2 Phase 6/7 replication and durability acceptance'
: "${VELOCITA_EXPECTED_SOURCE_ROOT:?set the externally pinned candidate source root}"
export VELOCITA_EXPECTED_HOST_MACHINE=x86_64
export VELOCITA_EXPECTED_CONTAINER_ARCH=amd64
export VELOCITA_V2_HOST_ENVIRONMENT=nuc-x86_64-linux-container
export VELOCITA_REQUIRE_CLEAN_PI_POWER=0
export VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT=${VELOCITA_V2_PHASE7_NUC_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase7-nuc-gates"}
export VELOCITA_PI_CI_IMAGE=${VELOCITA_NUC_CI_IMAGE:-localhost/velocita-nuc-ci:v2-phase7-acceptance}
export VELOCITA_V2_SOURCE_PI_CACHE_VOLUME=${VELOCITA_V2_PHASE7_NUC_CACHE_VOLUME:-velocita-v2-phase7-nuc-ci-cache}

exec "$script_dir/run-v2-source-admission-pi-gate.sh" "$@"
