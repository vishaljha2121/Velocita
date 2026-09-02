#!/usr/bin/env bash
set -Eeuo pipefail

source_dir=/src
work_root=/work
evidence_dir=/evidence
build_jobs=${BUILD_JOBS:-2}
expected_source_head=${EXPECTED_SOURCE_HEAD:-}

exec > >(tee "$evidence_dir/gate.log") 2>&1

result=FAIL
failure_step=initialization
finish() {
    local exit_status=$?
    {
        printf 'result\t%s\n' "$result"
        printf 'exit_status\t%s\n' "$exit_status"
        printf 'failure_step\t%s\n' "$failure_step"
        printf 'environment\tarm64-linux-container\n'
        printf 'evidence_dir\t%s\n' "$evidence_dir"
    } >"$evidence_dir/summary.tsv"
    printf 'V2 Phase 2 ARM64 development gate: %s\n' "$result"
}
trap finish EXIT

failure_step=environment_preflight
[[ $(uname -m) == aarch64 ]] || {
    printf 'Expected native aarch64 kernel, got %s\n' "$(uname -m)" >&2
    exit 1
}
[[ $(dpkg --print-architecture) == arm64 ]] || {
    printf 'Expected arm64 user space, got %s\n' "$(dpkg --print-architecture)" >&2
    exit 1
}
. /etc/os-release
[[ ${VERSION_ID:-} == 26.04 ]] || {
    printf 'Expected Ubuntu 26.04, got %s %s\n' "${ID:-unknown}" "${VERSION_ID:-unknown}" >&2
    exit 1
}

failure_step=source_identity
source_head=$(git -C "$source_dir" rev-parse --verify HEAD)
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

build_root="$work_root/$source_root"
release_build="$build_root/release"
instrumented_build="$build_root/instrumented"
sanitized_build="$build_root/sanitized"

{
    printf 'started_utc=%s\n' "$(date --iso-8601=seconds --utc)"
    printf 'environment=arm64-linux-container\n'
    printf 'kernel=%s\n' "$(uname -srvm)"
    printf 'machine=%s\n' "$(uname -m)"
    printf 'distribution=%s\n' "$PRETTY_NAME"
    printf 'dpkg_architecture=%s\n' "$(dpkg --print-architecture)"
    printf 'compiler=%s\n' "$(c++ --version | head -1)"
    printf 'cmake=%s\n' "$(cmake --version | head -1)"
    printf 'cpu_count=%s\n' "$(getconf _NPROCESSORS_ONLN)"
    printf 'memory_limit_bytes=%s\n' \
        "$(cat /sys/fs/cgroup/memory.max 2>/dev/null || printf unavailable)"
    printf 'shm_bytes=%s\n' "$(df -B1 --output=size /dev/shm | tail -1 | tr -d ' ')"
    printf 'build_jobs=%s\n' "$build_jobs"
    printf 'source_head=%s\n' "$source_head"
    printf 'source_root=%s\n' "$source_root"
    printf 'aeron_commit=%s\n' "$(git -C /opt/velocita-deps/aeron rev-parse HEAD)"
    printf 'raft_commit=%s\n' "$(git -C /opt/velocita-deps/willemt-raft rev-parse HEAD)"
} >"$evidence_dir/metadata.txt"

common_cmake_args=(
    -G Ninja
    -DAERON_HIDE_DEPRECATION_MESSAGE=ON
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
    v2-contract-benchmark
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
    --parallel "$build_jobs"

failure_step=sanitized_tests
ctest --test-dir "$sanitized_build" -R '^v2-.*-tests$' --output-on-failure |
    tee "$evidence_dir/sanitized-tests.txt"

failure_step=codec_benchmark
"$release_build/v2-contract-benchmark" | tee "$evidence_dir/codec-benchmark.txt"

{
    printf 'finished_utc=%s\n' "$(date --iso-8601=seconds --utc)"
    printf 'container_note=development compatibility evidence; not Raspberry Pi acceptance or capacity evidence\n'
} >>"$evidence_dir/metadata.txt"

failure_step=none
result=PASS
