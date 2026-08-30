#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
    cat <<'USAGE'
Usage: run-v2-phase1-gate.sh [--yes]

Build and validate the V2 Phase 1 contracts on the current ARM64 target.
Run `sudo -v` first so Raspberry Pi power state can be captured non-interactively.
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
    printf 'This builds and tests V2 Phase 1 without changing the running V1/V1.1 cluster. Continue? [y/N] '
    read -r answer
    [[ $answer == y || $answer == Y ]] || exit 1
fi

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run_id=$(date -u +%Y%m%dT%H%M%SZ)
evidence_root=${VELOCITA_V2_PHASE1_EVIDENCE_ROOT:-"$HOME/.local/share/velocita/v2-phase1-gates"}
evidence_dir="$evidence_root/$run_id"
release_build=${VELOCITA_V2_PHASE1_RELEASE_BUILD:-"$root_dir/build-v2-phase1-release"}
instrumented_build=${VELOCITA_V2_PHASE1_INSTRUMENTED_BUILD:-"$root_dir/build-v2-phase1-instrumented"}
build_jobs=${BUILD_JOBS:-2}
expected_source_root=6eafe3ffe4b78819682517e78846e8b997cc8e7adb4735cfd222397a99b52640

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
    printf 'V2 Phase 1 gate: %s\nEvidence: %s\n' "$result" "$evidence_dir"
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

failure_step=preflight
power_before=$(read_throttled)
temperature_before=$(read_temperature)
if [[ $power_before != throttled=0x0 ]]; then
    printf 'Power preflight failed: expected throttled=0x0, got %s\n' "$power_before" >&2
    printf 'Run sudo -v first; if the value is nonzero, fix power and reboot before testing.\n' >&2
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
    printf 'build_jobs=%s\n' "$build_jobs"
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
printf '%s  phase1-contract-source\n' "$actual_source_root" \
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

failure_step=release_configure
cmake -S "$root_dir" -B "$release_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAERON_HIDE_DEPRECATION_MESSAGE=ON \
    "${cmake_source_args[@]}"

failure_step=release_build
cmake --build "$release_build" \
    --target core-tests metrics-tests benchmark-tests v2-contract-tests \
             v2-value-contract-tests v2-payload-codec-tests v2-contract-benchmark \
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
cmake --build "$instrumented_build" \
    --target v2-contract-tests v2-value-contract-tests v2-payload-codec-tests \
    --parallel "$build_jobs"

failure_step=instrumented_tests
ctest --test-dir "$instrumented_build" \
    -R 'v2-.*(contract|codec)-tests' --output-on-failure |
    tee "$evidence_dir/instrumented-tests.txt"

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
