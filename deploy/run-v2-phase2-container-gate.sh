#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: run-v2-phase2-container-gate.sh [--yes]

Build the pinned ARM64 Ubuntu 26.04 development image and test the committed V2
Phase 2 kernel source under 4 CPU, 4 GiB memory, and 256 MiB /dev/shm limits.
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

command -v docker >/dev/null || {
    printf 'docker CLI is required; install Docker with an ARM64 Linux runtime.\n' >&2
    exit 69
}
docker info >/dev/null

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

aeron_commit=$(sed -n 's/^set(AERON_COMMIT "\([0-9a-f]*\)".*/\1/p' "$root_dir/CMakeLists.txt")
raft_commit=$(sed -n 's/^set(RAFT_COMMIT "\([0-9a-f]*\)".*/\1/p' "$root_dir/CMakeLists.txt")
[[ ${#aeron_commit} -eq 40 && ${#raft_commit} -eq 40 ]] || {
    printf 'Could not extract pinned dependency commits from CMakeLists.txt\n' >&2
    exit 65
}

mkdir -p "$evidence_dir"

docker build \
    --platform linux/arm64 \
    --build-arg "AERON_COMMIT=$aeron_commit" \
    --build-arg "RAFT_COMMIT=$raft_commit" \
    --tag "$image" \
    --file "$root_dir/deploy/pi-container/Containerfile" \
    "$root_dir"

docker run --rm \
    --platform linux/arm64 \
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
    --entrypoint /usr/local/bin/run-v2-phase2-kernel-gate \
    "$image"

printf 'Phase 2 container evidence: %s\n' "$evidence_dir"
