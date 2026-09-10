#!/usr/bin/env bash
set -Eeuo pipefail

source_dir=/src
work_root=/work
evidence_dir=/evidence
build_jobs=${BUILD_JOBS:-2}
expected_source_head=${EXPECTED_SOURCE_HEAD:-}
expected_source_root=${EXPECTED_SOURCE_ROOT:-}
gate_label=${GATE_LABEL:-V2 deterministic-kernel}
expected_machine=${EXPECTED_MACHINE:-aarch64}
expected_dpkg_arch=${EXPECTED_DPKG_ARCH:-arm64}
gate_environment=${GATE_ENVIRONMENT:-arm64-linux-container}

exec > >(tee "$evidence_dir/gate.log") 2>&1

result=FAIL
failure_step=initialization
finish() {
    local exit_status=$?
    {
        printf 'result\t%s\n' "$result"
        printf 'exit_status\t%s\n' "$exit_status"
        printf 'failure_step\t%s\n' "$failure_step"
        printf 'environment\t%s\n' "$gate_environment"
        printf 'evidence_dir\t%s\n' "$evidence_dir"
    } >"$evidence_dir/summary.tsv"
    printf '%s native development gate: %s\n' "$gate_label" "$result"
}
trap finish EXIT

failure_step=environment_preflight
[[ $(uname -m) == "$expected_machine" ]] || {
    printf 'Expected native %s kernel, got %s\n' \
        "$expected_machine" "$(uname -m)" >&2
    exit 1
}
[[ $(dpkg --print-architecture) == "$expected_dpkg_arch" ]] || {
    printf 'Expected %s user space, got %s\n' \
        "$expected_dpkg_arch" "$(dpkg --print-architecture)" >&2
    exit 1
}
. /etc/os-release
[[ ${VERSION_ID:-} == 26.04 ]] || {
    printf 'Expected Ubuntu 26.04, got %s %s\n' "${ID:-unknown}" "${VERSION_ID:-unknown}" >&2
    exit 1
}
runtime_l1d_cache_line_bytes=$(getconf LEVEL1_DCACHE_LINESIZE 2>/dev/null || printf unavailable)
[[ $runtime_l1d_cache_line_bytes == 64 ]] || {
    printf 'Expected a 64-byte L1 data-cache line, got %s\n' \
        "$runtime_l1d_cache_line_bytes" >&2
    exit 1
}

failure_step=source_identity
source_head=$(git -C "$source_dir" rev-parse --verify HEAD 2>/dev/null || printf unavailable)
if [[ -n $expected_source_head && $source_head != "$expected_source_head" ]]; then
    printf 'Source commit mismatch: expected %s, got %s\n' \
        "$expected_source_head" "$source_head" >&2
    exit 1
fi

(
    cd "$source_dir"
    find include/velocita/v2 src tests -maxdepth 1 -type f \
        \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
        sort -z |
        xargs -0 sha256sum
) >"$evidence_dir/source-manifest.sha256"
source_root=$(
    {
        (cd "$source_dir" && sha256sum CMakeLists.txt)
        cat "$evidence_dir/source-manifest.sha256"
    } | sha256sum | awk '{print $1}'
)
printf '%s  phase2-kernel-source\n' "$source_root" >"$evidence_dir/source-root.sha256"
if [[ -n $expected_source_root && $source_root != "$expected_source_root" ]]; then
    printf 'Source root mismatch: expected %s, got %s\n' \
        "$expected_source_root" "$source_root" >&2
    exit 1
fi

build_root="$work_root/$source_root"
release_build="$build_root/release"
instrumented_build="$build_root/instrumented"
sanitized_build="$build_root/sanitized"

{
    printf 'started_utc=%s\n' "$(date --iso-8601=seconds --utc)"
    printf 'gate_label=%s\n' "$gate_label"
    printf 'environment=%s\n' "$gate_environment"
    printf 'kernel=%s\n' "$(uname -srvm)"
    printf 'machine=%s\n' "$(uname -m)"
    printf 'distribution=%s\n' "$PRETTY_NAME"
    printf 'dpkg_architecture=%s\n' "$(dpkg --print-architecture)"
    printf 'compiler=%s\n' "$(c++ --version | head -1)"
    printf 'cmake=%s\n' "$(cmake --version | head -1)"
    printf 'cpu_count=%s\n' "$(getconf _NPROCESSORS_ONLN)"
    printf 'target_cache_line_bytes=64\n'
    printf 'runtime_l1d_cache_line_bytes=%s\n' "$runtime_l1d_cache_line_bytes"
    printf 'memory_limit_bytes=%s\n' \
        "$(cat /sys/fs/cgroup/memory.max 2>/dev/null || printf unavailable)"
    printf 'shm_bytes=%s\n' "$(df -B1 --output=size /dev/shm | tail -1 | tr -d ' ')"
    printf 'build_jobs=%s\n' "$build_jobs"
    printf 'source_head=%s\n' "$source_head"
    printf 'source_root=%s\n' "$source_root"
    printf 'expected_source_root=%s\n' "${expected_source_root:-not-pinned}"
    printf 'aeron_commit=%s\n' "$(git -C /opt/velocita-deps/aeron rev-parse HEAD)"
    printf 'raft_commit=%s\n' "$(git -C /opt/velocita-deps/willemt-raft rev-parse HEAD)"
} >"$evidence_dir/metadata.txt"

common_cmake_args=(
    -G Ninja
    -DAERON_HIDE_DEPRECATION_MESSAGE=ON
    -DVELOCITA_TARGET_CACHE_LINE_SIZE=64
    -DFETCHCONTENT_SOURCE_DIR_AERON=/opt/velocita-deps/aeron
    -DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT=/opt/velocita-deps/willemt-raft
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON
)

release_targets=(
    core-tests
    metrics-tests
    benchmark-tests
    v2-contract-tests
    v2-value-contract-tests
    v2-payload-codec-tests
    v2-kernel-policy-tests
    v2-kernel-control-tests
    v2-kernel-source-tests
    v2-state-snapshot-tests
    v2-kernel-codec-tests
    v2-replay-tests
    v2-model-replay-tests
    v2-metrics-tests
    v2-phase4-property-tests
    v2-rule-evaluator-tests
    v2-layout-tests
    v2-decision-kernel-tests
    v2-action-template-contract-tests
    v2-action-resolver-tests
    v2-executable-policy-contract-tests
    v2-executable-policy-codec-tests
    v2-artifact-digest-tests
    v2-executable-policy-kernel-tests
    v2-risk-order-outbox-tests
    v2-contract-benchmark
    v2-phase3-benchmark
    v2-layout-report
)

failure_step=release_configure
cmake -S "$source_dir" -B "$release_build" \
    -DCMAKE_BUILD_TYPE=Release "${common_cmake_args[@]}"

failure_step=release_build
cmake --build "$release_build" --target "${release_targets[@]}" \
    --parallel "$build_jobs"

failure_step=release_tests
ctest --test-dir "$release_build" --output-on-failure |
    tee "$evidence_dir/release-tests.txt"

failure_step=instrumented_configure
cmake -S "$source_dir" -B "$instrumented_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DVELOCITA_BENCHMARK_INSTRUMENTATION=ON \
    "${common_cmake_args[@]}"

failure_step=instrumented_build
cmake --build "$instrumented_build" \
    --target v2-contract-tests v2-value-contract-tests \
             v2-payload-codec-tests v2-kernel-policy-tests \
             v2-kernel-control-tests v2-kernel-source-tests \
             v2-state-snapshot-tests v2-kernel-codec-tests v2-replay-tests \
             v2-model-replay-tests v2-metrics-tests \
             v2-phase4-property-tests v2-rule-evaluator-tests v2-layout-tests \
             v2-decision-kernel-tests v2-action-template-contract-tests \
             v2-action-resolver-tests \
             v2-executable-policy-contract-tests \
             v2-executable-policy-codec-tests \
             v2-artifact-digest-tests v2-executable-policy-kernel-tests \
             v2-risk-order-outbox-tests \
    --parallel "$build_jobs"

failure_step=instrumented_tests
ctest --test-dir "$instrumented_build" -R '^v2-.*-tests$' --output-on-failure |
    tee "$evidence_dir/instrumented-tests.txt"

failure_step=sanitized_configure
cmake -S "$source_dir" -B "$sanitized_build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
    "${common_cmake_args[@]}"

failure_step=sanitized_build
cmake --build "$sanitized_build" \
    --target v2-contract-tests v2-value-contract-tests \
             v2-payload-codec-tests v2-kernel-policy-tests \
             v2-kernel-control-tests v2-kernel-source-tests \
             v2-state-snapshot-tests v2-kernel-codec-tests v2-replay-tests \
             v2-model-replay-tests v2-metrics-tests \
             v2-phase4-property-tests v2-rule-evaluator-tests v2-layout-tests \
             v2-decision-kernel-tests v2-action-template-contract-tests \
             v2-action-resolver-tests \
             v2-executable-policy-contract-tests \
             v2-executable-policy-codec-tests \
             v2-artifact-digest-tests v2-executable-policy-kernel-tests \
             v2-risk-order-outbox-tests \
    --parallel "$build_jobs"

failure_step=sanitized_tests
ctest --test-dir "$sanitized_build" -R '^v2-.*-tests$' --output-on-failure |
    tee "$evidence_dir/sanitized-tests.txt"

failure_step=codec_benchmark
"$release_build/v2-contract-benchmark" | tee "$evidence_dir/codec-benchmark.txt"

failure_step=phase3_benchmark
"$release_build/v2-phase3-benchmark" | tee "$evidence_dir/phase3-benchmark.txt"

failure_step=layout_report
"$release_build/v2-layout-report" | tee "$evidence_dir/layout-report.txt"
cat "$evidence_dir/layout-report.txt" >>"$evidence_dir/metadata.txt"

{
    printf 'finished_utc=%s\n' "$(date --iso-8601=seconds --utc)"
    printf 'container_note=native target compatibility and candidate-capacity evidence; promotion follows the phase-specific reviewed target gates\n'
} >>"$evidence_dir/metadata.txt"

failure_step=none
result=PASS
