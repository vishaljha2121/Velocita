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
[[ $expected_source_root =~ ^[0-9a-f]{64}$ ]] || {
    printf 'EXPECTED_SOURCE_ROOT must be an externally pinned SHA-256 root.\n' >&2
    exit 64
}
source_head=$(git -C "$source_dir" rev-parse --verify HEAD 2>/dev/null || printf unavailable)
if [[ -n $expected_source_head && $source_head != "$expected_source_head" ]]; then
    printf 'Source commit mismatch: expected %s, got %s\n' \
        "$expected_source_head" "$source_head" >&2
    exit 1
fi

source_root=$(bash "$source_dir/deploy/source-manifest.sh" \
    --root "$source_dir" --manifest "$evidence_dir/source-manifest.sha256")
printf '%s  phase2-kernel-source\n' "$source_root" >"$evidence_dir/source-root.sha256"
if [[ $source_root != "$expected_source_root" ]]; then
    printf 'Source root mismatch: expected %s, got %s\n' \
        "$expected_source_root" "$source_root" >&2
    exit 1
fi

build_root="$work_root/$source_root"
release_build="$build_root/release"
instrumented_build="$build_root/instrumented"
sanitized_build="$build_root/sanitized"
sanitizer_compile_flags=(
    -fsanitize=address,undefined
    -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer
)
sanitizer_link_flags=(-fsanitize=address,undefined)

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

# Derived from CMakeLists.txt by the `velocita-all-tests` aggregate target, so it
# cannot drift as suites are added. Only the non-test binaries the gate records
# are named individually.
release_targets=(
    velocita-all-tests
    v2-contract-benchmark
    v2-phase3-benchmark
    v2-layout-report
)
printf '%s\n' "${release_targets[@]}" >"$evidence_dir/required-targets.txt"

# This is an independent acceptance inventory, not a list derived from the
# configured tree. A newly added or accidentally omitted CTest suite therefore
# fails review instead of silently changing what the gate means.
v2_required_tests=(
    v2-contract-tests
    v2-value-contract-tests
    v2-payload-codec-tests
    v2-kernel-policy-tests
    v2-venue-wire-tests
    v2-venue-transport-tests
    v2-kernel-control-tests
    v2-kernel-source-tests
    v2-state-snapshot-tests
    v2-kernel-codec-tests
    v2-replay-tests
    v2-model-replay-tests
    v2-venue-simulator-tests
    v2-spsc-ring-tests
    v2-wal-arena-tests
    v2-replication-tests
    v2-snapshot-membership-tests
    v2-durable-quorum-tests
    v2-gateway-tests
    v2-lifecycle-reachability-tests
    v2-gateway-delivery-tests
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
    v2-execution-catalog-tests
    v2-catalog-binding-tests
    v2-authority-barrier-tests
    v2-authority-storage-failure-tests
    v2-gateway-authority-tests
    v2-authority-composition-tests
    v2-snapshot-store-bounds-tests
    v2-metadata-persistence-model-tests
    v2-bound-metadata-persistence-model-tests
    v2-static-identity-tests
    v2-identity-startup-tests
    v2-kernel-startup-tests
    v2-bound-frame-tests
    v2-bound-peer-tests
    v2-bound-metadata-tests
    v2-history-binding-tests
    v2-result-ledger-tests
    v2-result-admission-tests
    v2-result-admission-edge-tests
    v2-outbound-mailbox-tests
    v2-outbound-saturation-tests
    v2-log-saturation-tests
    v2-retained-log-tests
    v2-wal-streaming-tests
    v2-incremental-digest-tests
    v2-archive-segment-tests
    v2-archive-segment-adversarial-tests
    v2-archive-file-tests
    v2-archive-catalog-tests
    v2-archive-store-tests
    v2-wal-maintenance-tests
    v2-wal-target-switch-tests
    v2-wal-target-switch-adversarial-tests
    v2-archive-compaction-tests
    v2-compaction-fence-tests
    v2-archive-startup-tests
    v2-archive-persistence-model-tests
    v2-archive-publication-fault-tests
    v2-compaction-io-fault-tests
    v2-compaction-persistence-model-tests
    v2-async-persistence-tests
    v2-async-suffix-tests
    v2-raft-persistence-tests
    v2-persistence-completion-tests
    v2-async-persistence-model-tests
    v2-archive-file-io-tests
    v2-local-generation-tests
    v2-local-generation-adversarial-tests
    v2-generation-startup-guard-tests
    v2-generation-base-tests
    v2-generation-base-files-tests
    v2-generation-startup-tests
    v2-generation-publication-model-tests
    v2-result-obligation-tests
    v2-snapshot-install-preflight-tests
    v2-install-history-bridge-contract-tests
    v2-history-recovery-tests
    v2-two-voter-catchup-tests
    v2-auth-envelope-tests
    v2-session-fence-tests
    v2-session-epoch-persistence-model-tests
    v2-node-config-tests
    v2-deploy-template-tests
    v2-node-protocol-tests
    v2-node-tests
    v2-node-client-tests
    v2-shadow-program-tests
    v2-shadow-catalog-tests
    v2-shadow-submitter-tests
    v2-election-jitter-tests
    v2-node-files-tests
    v2-node-health-tests
    v2-node-process-tests
    v2-secrets-tool-tests
    v2-storage-benchmark-smoke-tests
    v2-node-metrics-text-tests
    v2-archive-destination-tests
    v2-archive-live-verification-tests
    v2-snapshot-install-ack-contract-tests
    v2-snapshot-install-results-contract-tests
    v2-executable-policy-kernel-tests
    v2-risk-order-outbox-tests
)
release_required_tests=(core-tests metrics-tests benchmark-tests "${v2_required_tests[@]}")
printf '%s\n' "${release_required_tests[@]}" >"$evidence_dir/required-release-tests.txt"
printf '%s\n' "${v2_required_tests[@]}" >"$evidence_dir/required-v2-tests.txt"

validate_ctest_inventory() {
    local build_dir=$1
    local evidence_name=$2
    local expected_name=$3
    shift 3
    local inventory="$evidence_dir/$evidence_name"
    ctest --test-dir "$build_dir" --show-only=json-v1 "$@" >"$inventory"
    local -n expected_tests=$expected_name
    python3 "$source_dir/deploy/validate-ctest-inventory.py" \
        --inventory "$inventory" --expected "${expected_tests[@]}"
}

failure_step=release_configure
cmake -S "$source_dir" -B "$release_build" \
    -DCMAKE_BUILD_TYPE=Release "${common_cmake_args[@]}"

failure_step=release_inventory
validate_ctest_inventory "$release_build" release-ctest-inventory.json \
    release_required_tests

failure_step=release_build
cmake --build "$release_build" --target "${release_targets[@]}" \
    --parallel "$build_jobs"

failure_step=release_tests
ctest --test-dir "$release_build" --no-tests=error --output-on-failure |
    tee "$evidence_dir/release-tests.txt"

failure_step=instrumented_configure
cmake -S "$source_dir" -B "$instrumented_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DVELOCITA_BENCHMARK_INSTRUMENTATION=ON \
    "${common_cmake_args[@]}"

failure_step=instrumented_inventory
validate_ctest_inventory "$instrumented_build" instrumented-ctest-inventory.json \
    v2_required_tests -R '^v2-.*-tests$'

failure_step=instrumented_build
cmake --build "$instrumented_build" \
    --target velocita-v2-tests \
    --parallel "$build_jobs"

failure_step=instrumented_tests
ctest --test-dir "$instrumented_build" --no-tests=error \
    -R '^v2-.*-tests$' --output-on-failure |
    tee "$evidence_dir/instrumented-tests.txt"

failure_step=sanitized_configure
cmake -S "$source_dir" -B "$sanitized_build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="${sanitizer_compile_flags[*]}" \
    -DCMAKE_EXE_LINKER_FLAGS="${sanitizer_link_flags[*]}" \
    "${common_cmake_args[@]}"

failure_step=sanitized_inventory
validate_ctest_inventory "$sanitized_build" sanitized-ctest-inventory.json \
    v2_required_tests -R '^v2-.*-tests$'

failure_step=sanitized_build
cmake --build "$sanitized_build" \
    --target velocita-v2-tests \
    --parallel "$build_jobs"

failure_step=sanitized_tests
# LeakSanitizer is disabled EXPLICITLY rather than by accident.
#
# GCC's AddressSanitizer enables LSan by default on x86-64 and not on aarch64.
# LSan needs ptrace to stop threads at exit, and this gate runs --cap-drop ALL,
# so on x86-64 every suite died during LSan initialisation before executing a
# single test - while the identical aarch64 run passed, because LSan was never
# active there.
#
# So "Debug ASan+UBSan" has always meant something different on the two targets,
# and the acceptance records said 26/26 for both without noting it. Pinning
# detect_leaks=0 makes the three environments test the SAME thing and makes the
# difference visible in this file instead of buried in a runtime default.
#
# Enabling leak detection uniformly - --cap-add SYS_PTRACE plus detect_leaks=1 on
# both targets - is real added coverage and is deferred deliberately: it would
# widen what the already-accepted ARM64 evidence asserts, so it belongs in a
# reviewed increment rather than smuggled into a target bring-up.
export ASAN_OPTIONS="${ASAN_OPTIONS:-}${ASAN_OPTIONS:+:}detect_leaks=0"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-}${UBSAN_OPTIONS:+:}halt_on_error=1:print_stacktrace=1"
printf 'asan_options=%s\n' "$ASAN_OPTIONS" >>"$evidence_dir/metadata.txt"
printf 'ubsan_options=%s\n' "$UBSAN_OPTIONS" >>"$evidence_dir/metadata.txt"
printf 'ubsan_compile_recovery=disabled\n' >>"$evidence_dir/metadata.txt"

failure_step=ubsan_negative_probe
ubsan_probe="$sanitized_build/ubsan-negative-probe"
c++ "${sanitizer_compile_flags[@]}" \
    "$source_dir/deploy/pi-container/ubsan-negative-probe.cpp" \
    "${sanitizer_link_flags[@]}" -o "$ubsan_probe"
set +e
"$ubsan_probe" >"$evidence_dir/ubsan-negative-probe.txt" 2>&1
ubsan_probe_status=$?
set -e
sed -n '1,20p' "$evidence_dir/ubsan-negative-probe.txt"
printf 'ubsan_negative_probe_exit_status=%s\n' "$ubsan_probe_status" \
    >>"$evidence_dir/metadata.txt"
if [[ $ubsan_probe_status -eq 0 ]]; then
    printf 'UBSan negative probe recovered and exited zero.\n' >&2
    exit 1
fi
if ! grep -q 'runtime error: signed integer overflow' \
    "$evidence_dir/ubsan-negative-probe.txt"; then
    printf 'UBSan negative probe failed without the expected diagnostic.\n' >&2
    exit 1
fi

failure_step=sanitized_tests
ctest --test-dir "$sanitized_build" --no-tests=error \
    -R '^v2-.*-tests$' --output-on-failure |
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

failure_step=evidence_inventory
for required_key in gate_label environment source_head source_root expected_source_root aeron_commit raft_commit; do
    grep -q "^${required_key}=" "$evidence_dir/metadata.txt" || {
        printf 'Required metadata key is missing: %s\n' "$required_key" >&2
        exit 1
    }
done
[[ -s "$evidence_dir/required-targets.txt" && \
   -s "$evidence_dir/required-release-tests.txt" && \
   -s "$evidence_dir/required-v2-tests.txt" && \
   -s "$evidence_dir/release-ctest-inventory.json" && \
   -s "$evidence_dir/instrumented-ctest-inventory.json" && \
   -s "$evidence_dir/sanitized-ctest-inventory.json" ]] || {
    printf 'Required target inventory is missing or empty.\n' >&2
    exit 1
}

failure_step=none
result=PASS
