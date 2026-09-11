#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: run-v2-phase2-container-gate.sh [--yes]

Build the pinned ARM64 Ubuntu 26.04 development image and test committed V2
source under 4 CPU, 4 GiB memory, and 256 MiB /dev/shm limits. Environment
variables may specialize the label, evidence path, source root, image, and cache.
This is compatibility evidence, not Raspberry Pi acceptance or benchmarking.
USAGE
}

confirmed=0
case "${1:-}" in
    --yes) confirmed=1 ;;
    --help|-h) usage; exit 0 ;;
    "") ;;
    *) usage >&2; exit 64 ;;
esac

if [[ $confirmed -ne 1 ]]; then
    printf 'Build and run the isolated ARM64 Phase 2 development gate? [y/N] '
    read -r answer
    [[ $answer == y || $answer == Y ]] || exit 1
fi

# Engine selection. This gate used to hardcode docker while the physical Pi gate
# hardcodes podman - two gates attesting the same candidate through different
# runtimes, with different security defaults, different rootless behaviour and
# different exec semantics for a bind-mounted entrypoint. That is a difference
# the evidence should not depend on, and it produced a gate that passed under
# podman on the Pi and could not start under docker on the same machine.
#
# Podman is preferred because it is what the physical target gate already
# requires, so the two agree by default. Override with VELOCITA_CONTAINER_ENGINE
# when a specific runtime is the thing under test.
container_engine=${VELOCITA_CONTAINER_ENGINE:-}
if [[ -z $container_engine ]]; then
    for candidate in podman docker; do
        if command -v "$candidate" >/dev/null; then
            container_engine=$candidate
            break
        fi
    done
fi
if [[ -z $container_engine ]] || ! command -v "$container_engine" >/dev/null; then
    printf 'podman or docker is required; install one with an ARM64 Linux runtime.\n' >&2
    exit 69
fi
"$container_engine" info >/dev/null

# Rootless podman needs no platform flag on a native ARM64 host, and passing one
# to docker preserves this gate's previous behaviour exactly.
platform_args=()
if [[ $container_engine == docker ]]; then
    platform_args=(--platform linux/arm64)
fi

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -n $(git -C "$root_dir" status --porcelain -- \
    CMakeLists.txt include/velocita/v2 src/v2_\* tests/v2_\*) ]]; then
    printf 'Commit the V2 kernel source before creating cross-platform evidence.\n' >&2
    exit 65
fi

source_head=$(git -C "$root_dir" rev-parse --verify HEAD)
run_id=$(date -u +%Y%m%dT%H%M%SZ)
evidence_root=${VELOCITA_V2_PHASE2_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase2-container-gates"}
evidence_dir="$evidence_root/$run_id"
image=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase2-dev}
cache_volume=${VELOCITA_V2_PHASE2_CI_CACHE_VOLUME:-velocita-v2-phase2-ci-cache}
build_jobs=${BUILD_JOBS:-2}
expected_source_root=${VELOCITA_EXPECTED_SOURCE_ROOT:-}
gate_label=${VELOCITA_V2_CONTAINER_GATE_LABEL:-V2 Phase 2 development}
gate_environment=${VELOCITA_V2_CONTAINER_ENVIRONMENT:-arm64-linux-container}

aeron_commit=$(sed -n 's/^set(AERON_COMMIT "\([0-9a-f]*\)".*/\1/p' "$root_dir/CMakeLists.txt")
raft_commit=$(sed -n 's/^set(RAFT_COMMIT "\([0-9a-f]*\)".*/\1/p' "$root_dir/CMakeLists.txt")
[[ ${#aeron_commit} -eq 40 && ${#raft_commit} -eq 40 ]] || {
    printf 'Could not extract pinned dependency commits from CMakeLists.txt\n' >&2
    exit 65
}

mkdir -p "$evidence_dir"

"$container_engine" build \
    "${platform_args[@]}" \
    --build-arg "AERON_COMMIT=$aeron_commit" \
    --build-arg "RAFT_COMMIT=$raft_commit" \
    --tag "$image" \
    --file "$root_dir/deploy/pi-container/Containerfile" \
    "$root_dir"

"$container_engine" run --rm \
    "${platform_args[@]}" \
    --cpus 4 \
    --memory 4g \
    --pids-limit 4096 \
    --shm-size 256m \
    --cap-drop ALL \
    --security-opt no-new-privileges \
    --read-only \
    --tmpfs /tmp:rw,nosuid,nodev,size=1g \
    --volume "$root_dir:/src:ro" \
    --volume "$cache_volume:/work" \
    --volume "$evidence_dir:/evidence" \
    --volume "$root_dir/deploy/pi-container/run-phase2-kernel-gate-inside.sh:/usr/local/bin/run-v2-phase2-kernel-gate:ro" \
    --env "BUILD_JOBS=$build_jobs" \
    --env "EXPECTED_SOURCE_HEAD=$source_head" \
    --env "EXPECTED_SOURCE_ROOT=$expected_source_root" \
    --env "GATE_LABEL=$gate_label" \
    --env "GATE_ENVIRONMENT=$gate_environment" \
    --entrypoint /usr/local/bin/run-v2-phase2-kernel-gate \
    "$image"

printf '%s container evidence: %s\n' "$gate_label" "$evidence_dir"
