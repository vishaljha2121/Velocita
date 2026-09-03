#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: run-v2-source-admission-pi-gate.sh [--yes]

Build and validate the committed V2 source-admission checkpoint on Raspberry Pi
ARM64. Run `sudo -v` first so power state can be captured non-interactively.
This builds test binaries only and does not stop or replace the V1/V1.1 cluster.
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
    printf 'Run the non-destructive V2 source-admission Pi gate? [y/N] '
    read -r answer
    [[ $answer == y || $answer == Y ]] || exit 1
fi

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run_id=$(date -u +%Y%m%dT%H%M%SZ)
evidence_root=${VELOCITA_V2_SOURCE_PI_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-source-admission-pi-gates"}
evidence_dir="$evidence_root/$run_id"
release_build=${VELOCITA_V2_SOURCE_RELEASE_BUILD:-"$root_dir/build-v2-source-release"}
instrumented_build=${VELOCITA_V2_SOURCE_INSTRUMENTED_BUILD:-"$root_dir/build-v2-source-instrumented"}
sanitized_build=${VELOCITA_V2_SOURCE_SANITIZED_BUILD:-"$root_dir/build-v2-source-sanitized"}
build_jobs=${BUILD_JOBS:-2}
expected_source_root=0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8

mkdir -p "$evidence_dir"
exec > >(tee "$evidence_dir/gate.log") 2>&1

result=FAIL
failure_step=initialization
finish() {
    local exit_status=$?
    {
        printf 'result\t%s\n' "$result"
        printf 'exit_status\t%s\n' "$exit_status"
        printf 'failure_step\t%s\n' "$failure_step"
        printf 'evidence_dir\t%s\n' "$evidence_dir"
    } >"$evidence_dir/summary.tsv"
    printf 'V2 source-admission Raspberry Pi gate: %s\nEvidence: %s\n' \
        "$result" "$evidence_dir"
}
trap finish EXIT

for command_name in cmake ctest git sha256sum uname; do
    command -v "$command_name" >/dev/null || {
        printf 'Missing required command: %s\n' "$command_name" >&2
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

failure_step=preflight
[[ $(uname -m) == aarch64 ]] || {
    printf 'Expected Raspberry Pi aarch64, got %s\n' "$(uname -m)" >&2
    exit 1
}

power_before=$(read_throttled)
temperature_before=$(read_temperature)
if [[ $power_before != throttled=0x0 ]]; then
    printf 'Power preflight failed: expected throttled=0x0, got %s\n' "$power_before" >&2
    printf 'Run sudo -v first; fix power and reboot if the value is nonzero.\n' >&2
    exit 1
fi

{
    printf 'started_utc=%s\n' "$(date --iso-8601=seconds --utc)"
    printf 'source_head=%s\n' "$(git -C "$root_dir" rev-parse --verify HEAD 2>/dev/null || printf unavailable)"
    printf 'source_branch=%s\n' "$(git -C "$root_dir" branch --show-current 2>/dev/null || printf unavailable)"
    printf 'kernel=%s\n' "$(uname -srvm)"
    printf 'architecture=%s\n' "$(uname -m)"
    printf 'compiler=%s\n' "$(c++ --version | head -1)"
    printf 'cmake=%s\n' "$(cmake --version | head -1)"
    printf 'cpu_count=%s\n' "$(getconf _NPROCESSORS_ONLN)"
    printf 'memory_bytes=%s\n' "$(awk '/^MemTotal:/ {print $2 * 1024}' /proc/meminfo)"
    printf 'build_jobs=%s\n' "$build_jobs"
    printf 'cpu_governor=%s\n' "$(read_governor)"
    printf 'power_before=%s\n' "$power_before"
    printf 'temperature_before_millicelsius=%s\n' "$temperature_before"
} >"$evidence_dir/metadata.txt"

(
    cd "$root_dir"
    find include/velocita/v2 src tests -maxdepth 1 -type f \
        \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
        sort -z |
        xargs -0 sha256sum
) >"$evidence_dir/source-manifest.sha256"
actual_source_root=$(
    {
        (cd "$root_dir" && sha256sum CMakeLists.txt)
        cat "$evidence_dir/source-manifest.sha256"
    } | sha256sum | awk '{print $1}'
)
printf '%s  phase2-source-admission-source\n' "$actual_source_root" \
    >"$evidence_dir/source-root.sha256"
printf 'expected_source_root=%s\nactual_source_root=%s\n' \
    "$expected_source_root" "$actual_source_root" >>"$evidence_dir/metadata.txt"
if [[ $actual_source_root != "$expected_source_root" ]]; then
    printf 'Source identity mismatch: expected %s, got %s\n' \
        "$expected_source_root" "$actual_source_root" >&2
    exit 1
fi

cmake_source_args=()
if [[ -d "$root_dir/build/_deps/aeron-src" ]]; then
    cmake_source_args+=("-DFETCHCONTENT_SOURCE_DIR_AERON=$root_dir/build/_deps/aeron-src")
fi
if [[ -d "$root_dir/build/_deps/willemt_raft-src" ]]; then
    cmake_source_args+=("-DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT=$root_dir/build/_deps/willemt_raft-src")
fi

v2_targets=(
    v2-contract-tests
    v2-value-contract-tests
    v2-payload-codec-tests
    v2-kernel-policy-tests
    v2-kernel-control-tests
    v2-kernel-source-tests
)

failure_step=release_configure
cmake -S "$root_dir" -B "$release_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAERON_HIDE_DEPRECATION_MESSAGE=ON \
    "${cmake_source_args[@]}"

failure_step=release_build
cmake --build "$release_build" \
    --target core-tests metrics-tests benchmark-tests "${v2_targets[@]}" \
             v2-contract-benchmark \
    --parallel "$build_jobs"

failure_step=release_tests
ctest --test-dir "$release_build" --output-on-failure |
    tee "$evidence_dir/release-tests.txt"

failure_step=instrumented_configure
cmake -S "$root_dir" -B "$instrumented_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DVELOCITA_BENCHMARK_INSTRUMENTATION=ON \
    -DAERON_HIDE_DEPRECATION_MESSAGE=ON \
    "${cmake_source_args[@]}"

failure_step=instrumented_build
cmake --build "$instrumented_build" --target "${v2_targets[@]}" \
    --parallel "$build_jobs"

failure_step=instrumented_tests
ctest --test-dir "$instrumented_build" -R '^v2-.*-tests$' --output-on-failure |
    tee "$evidence_dir/instrumented-tests.txt"

failure_step=sanitized_configure
cmake -S "$root_dir" -B "$sanitized_build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
    -DAERON_HIDE_DEPRECATION_MESSAGE=ON \
    "${cmake_source_args[@]}"

failure_step=sanitized_build
cmake --build "$sanitized_build" --target "${v2_targets[@]}" \
    --parallel "$build_jobs"

failure_step=sanitized_tests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    ctest --test-dir "$sanitized_build" -R '^v2-.*-tests$' --output-on-failure |
    tee "$evidence_dir/sanitized-tests.txt"

failure_step=codec_benchmark
"$release_build/v2-contract-benchmark" | tee "$evidence_dir/codec-benchmark.txt"

failure_step=power_postflight
power_after=$(read_throttled)
temperature_after=$(read_temperature)
{
    printf 'finished_utc=%s\n' "$(date --iso-8601=seconds --utc)"
    printf 'power_after=%s\n' "$power_after"
    printf 'temperature_after_millicelsius=%s\n' "$temperature_after"
} >>"$evidence_dir/metadata.txt"

if [[ $power_after != throttled=0x0 ]]; then
    printf 'Power postflight failed: expected throttled=0x0, got %s\n' "$power_after" >&2
    exit 1
fi

failure_step=none
result=PASS
