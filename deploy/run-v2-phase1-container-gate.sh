#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: run-v2-phase1-container-gate.sh [--yes]

Build the pinned ARM64 Ubuntu 26.04 CI image and run the V2 Phase 1 compatibility
gate under 4 CPU, 4 GiB memory, and 256 MiB /dev/shm limits.
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
    printf 'Build and run the isolated ARM64 Linux compatibility gate? [y/N] '
    read -r answer
    [[ $answer == y || $answer == Y ]] || exit 1
fi

command -v docker >/dev/null || {
    printf 'docker CLI is required; install Docker with an ARM64 Linux runtime.\n' >&2
    exit 69
}
docker info >/dev/null

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run_id=$(date -u +%Y%m%dT%H%M%SZ)
evidence_root=${VELOCITA_V2_CONTAINER_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase1-container-gates"}
evidence_dir="$evidence_root/$run_id"
image=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-phase1}
cache_volume=${VELOCITA_PI_CI_CACHE_VOLUME:-velocita-v2-phase1-ci-cache}
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
    --env "BUILD_JOBS=$build_jobs" \
    "$image"

printf 'Container evidence: %s\n' "$evidence_dir"
