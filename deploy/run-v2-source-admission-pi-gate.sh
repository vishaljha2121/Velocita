#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: run-v2-source-admission-pi-gate.sh [--yes]

Build and validate the committed V2 source-admission checkpoint in a pinned
Ubuntu/GCC container on Raspberry Pi ARM64. Run `sudo -v` first so firmware
power state can be captured non-interactively. The running V1/V1.1 cluster is
not stopped or replaced.
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
    printf 'Run the containerized V2 source-admission Pi gate? [y/N] '
    read -r answer
    [[ $answer == y || $answer == Y ]] || exit 1
fi

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run_id=$(date -u +%Y%m%dT%H%M%SZ)
evidence_root=${VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-source-admission-pi-gates"}
evidence_dir="$evidence_root/$run_id"
image=${VELOCITA_PI_CI_IMAGE:-localhost/velocita-pi-ci:v2-source-admission}
cache_volume=${VELOCITA_V2_SOURCE_PI_CACHE_VOLUME:-velocita-v2-source-pi-ci-cache}
build_jobs=${BUILD_JOBS:-2}
expected_source_root=0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8

mkdir -p "$evidence_dir"
exec > >(tee "$evidence_dir/wrapper.log") 2>&1

result=FAIL
failure_step=initialization
finish() {
    local exit_status=$?
    {
        printf 'result\t%s\n' "$result"
        printf 'exit_status\t%s\n' "$exit_status"
        printf 'failure_step\t%s\n' "$failure_step"
        printf 'environment\traspberry-pi-arm64-container\n'
        printf 'evidence_dir\t%s\n' "$evidence_dir"
    } >"$evidence_dir/summary.tsv"
    printf 'V2 source-admission Raspberry Pi gate: %s\nEvidence: %s\n' \
        "$result" "$evidence_dir"
}
trap finish EXIT

for command_name in podman git sed uname; do
    command -v "$command_name" >/dev/null || {
        printf 'Missing required command: %s\n' "$command_name" >&2
        printf 'This gate requires Podman, not a host C++/CMake toolchain.\n' >&2
        exit 69
    }
done

read_throttled() {
    if command -v vcgencmd >/dev/null && sudo -n true 2>/dev/null; then
        sudo -n vcgencmd get_throttled
    else
        printf 'unavailable'
    fi
}

read_temperature() {
    if [[ -r /sys/class/thermal/thermal_zone0/temp ]]; then
        tr -d '\n' </sys/class/thermal/thermal_zone0/temp
    else
        printf 'unavailable'
    fi
}

read_governor() {
    if [[ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
        tr -d '\n' </sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    else
        printf 'unavailable'
    fi
}

failure_step=host_preflight
[[ $(uname -m) == aarch64 ]] || {
    printf 'Expected Raspberry Pi aarch64, got %s\n' "$(uname -m)" >&2
    exit 1
}

podman info >/dev/null
power_before=$(read_throttled)
temperature_before=$(read_temperature)
governor_before=$(read_governor)
if [[ $power_before != throttled=0x0 ]]; then
    printf 'Power preflight failed: expected throttled=0x0, got %s\n' "$power_before" >&2
    printf 'Run sudo -v first; fix power and reboot if the value is nonzero.\n' >&2
    exit 1
fi

aeron_commit=$(sed -n 's/^set(AERON_COMMIT "\([0-9a-f]*\)".*/\1/p' "$root_dir/CMakeLists.txt")
raft_commit=$(sed -n 's/^set(RAFT_COMMIT "\([0-9a-f]*\)".*/\1/p' "$root_dir/CMakeLists.txt")
[[ ${#aeron_commit} -eq 40 && ${#raft_commit} -eq 40 ]] || {
    printf 'Could not extract pinned dependency commits from CMakeLists.txt\n' >&2
    exit 65
}

failure_step=image_build
podman build \
    --build-arg "AERON_COMMIT=$aeron_commit" \
    --build-arg "RAFT_COMMIT=$raft_commit" \
    --tag "$image" \
    --file "$root_dir/deploy/pi-container/Containerfile" \
    "$root_dir"

failure_step=container_gate
set +e
podman run --rm \
    --cpus 4 \
    --memory 2g \
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
    --env "EXPECTED_SOURCE_ROOT=$expected_source_root" \
    --entrypoint /usr/local/bin/run-v2-phase2-kernel-gate \
    "$image"
gate_status=$?
set -e

power_after=$(read_throttled)
temperature_after=$(read_temperature)
{
    printf 'target_environment=raspberry-pi-arm64-container\n'
    printf 'host_kernel=%s\n' "$(uname -srvm)"
    printf 'host_governor_before=%s\n' "$governor_before"
    printf 'host_power_before=%s\n' "$power_before"
    printf 'host_temperature_before_millicelsius=%s\n' "$temperature_before"
    printf 'host_power_after=%s\n' "$power_after"
    printf 'host_temperature_after_millicelsius=%s\n' "$temperature_after"
} >>"$evidence_dir/metadata.txt"

if [[ -f "$evidence_dir/summary.tsv" ]]; then
    mv "$evidence_dir/summary.tsv" "$evidence_dir/container-summary.tsv"
fi

if [[ $gate_status -ne 0 ]]; then
    printf 'The containerized build/test gate exited with status %s.\n' "$gate_status" >&2
    exit "$gate_status"
fi

failure_step=host_postflight
if [[ $power_after != throttled=0x0 ]]; then
    printf 'Power postflight failed: expected throttled=0x0, got %s\n' "$power_after" >&2
    exit 1
fi

failure_step=none
result=PASS
