cmake_minimum_required(VERSION 3.30)

project(velocita_raft VERSION 0.1.0 LANGUAGES C CXX)

find_package(Threads REQUIRED)

option(VELOCITA_BENCHMARK_INSTRUMENTATION
       "Compile benchmark-only Raft stage timestamps into the node and response wire format" OFF)
set(VELOCITA_TARGET_CACHE_LINE_SIZE "64" CACHE STRING
    "Target CPU cache-line size in bytes; part of the V2 arena-layout ABI")

# willemt/raft is source-only and has no CMakeLists.txt; populate it so this
# project can define the target and keep the upstream commit pinned.
cmake_policy(SET CMP0169 OLD)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(CTest)

set(AERON_COMMIT "5b62f21d917af027cdf5a3241aa5f355149b04fa" CACHE STRING "Pinned Aeron commit")
set(RAFT_COMMIT "e428eeb921a014192d1d703dd317f3f29f5916c5" CACHE STRING "Pinned willemt/raft commit")

set(AERON_TESTS OFF CACHE BOOL "" FORCE)
set(AERON_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(AERON_SYSTEM_TESTS OFF CACHE BOOL "" FORCE)
set(AERON_SLOW_SYSTEM_TESTS OFF CACHE BOOL "" FORCE)
set(AERON_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(AERON_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(AERON_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
set(BUILD_AERON_ARCHIVE_API OFF CACHE BOOL "" FORCE)
set(BUILD_AERON_DRIVER ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    aeron
    GIT_REPOSITORY https://github.com/aeron-io/aeron.git
    GIT_TAG ${AERON_COMMIT}
)
FetchContent_MakeAvailable(aeron)

FetchContent_Declare(
    willemt_raft
    GIT_REPOSITORY https://github.com/willemt/raft.git
    GIT_TAG ${RAFT_COMMIT}
)
FetchContent_GetProperties(willemt_raft)
if(NOT willemt_raft_POPULATED)
    FetchContent_Populate(willemt_raft)
endif()

# PINNED FORK. Phase 7 splits "appended" from "durable", and upstream counts a
# leader's own vote before its bytes are on disk - see
# deploy/patches/0001-raft-durable-quorum.patch for why that cannot be corrected
# from outside the library.
#
# Applied here rather than vendored so the upstream pin above stays the source of
# truth and the delta stays reviewable as a diff. A sentinel makes reconfiguring
# idempotent, and a failure to apply is FATAL: silently building unpatched raft
# would silently restore the defect the patch exists to remove.
set(VELOCITA_RAFT_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/deploy/patches/0001-raft-durable-quorum.patch)
set(VELOCITA_RAFT_PATCH_STAMP ${willemt_raft_SOURCE_DIR}/.velocita-durable-quorum-applied)
file(SHA256 ${VELOCITA_RAFT_PATCH} VELOCITA_RAFT_PATCH_SHA256)
set(VELOCITA_RAFT_EXPECTED_H_SHA256
    "63b950cc7aa6dd10af41f0a291804d4b85cd3a25de43973aa1a8a9b446fc360b")
set(VELOCITA_RAFT_EXPECTED_SERVER_SHA256
    "3604fff1269195814b5f58ff7d2d90bc6589a86f58e8b5553e70fb78781d3e8c")
if(EXISTS ${VELOCITA_RAFT_PATCH_STAMP})
    file(READ ${VELOCITA_RAFT_PATCH_STAMP} VELOCITA_RAFT_PATCH_STAMP_CONTENT)
    string(FIND "${VELOCITA_RAFT_PATCH_STAMP_CONTENT}" "RAFT_COMMIT=${RAFT_COMMIT}\n" VELOCITA_RAFT_STAMP_COMMIT)
    string(FIND "${VELOCITA_RAFT_PATCH_STAMP_CONTENT}" "PATCH_SHA256=${VELOCITA_RAFT_PATCH_SHA256}\n" VELOCITA_RAFT_STAMP_PATCH)
    string(REGEX MATCH "RAFT_H_SHA256=([0-9a-f]+)" _ "${VELOCITA_RAFT_PATCH_STAMP_CONTENT}")
    set(VELOCITA_RAFT_STAMP_H_SHA256 "${CMAKE_MATCH_1}")
    string(REGEX MATCH "RAFT_SERVER_SHA256=([0-9a-f]+)" _ "${VELOCITA_RAFT_PATCH_STAMP_CONTENT}")
    set(VELOCITA_RAFT_STAMP_SERVER_SHA256 "${CMAKE_MATCH_1}")
    file(SHA256 ${willemt_raft_SOURCE_DIR}/include/raft.h VELOCITA_RAFT_H_SHA256)
    file(SHA256 ${willemt_raft_SOURCE_DIR}/src/raft_server.c VELOCITA_RAFT_SERVER_SHA256)
    if(VELOCITA_RAFT_STAMP_COMMIT GREATER_EQUAL 0 AND
       VELOCITA_RAFT_H_SHA256 STREQUAL VELOCITA_RAFT_EXPECTED_H_SHA256 AND
       VELOCITA_RAFT_SERVER_SHA256 STREQUAL VELOCITA_RAFT_EXPECTED_SERVER_SHA256 AND
       VELOCITA_RAFT_STAMP_PATCH LESS 0)
        # Migrate the pre-digest stamp only when the patched source content is
        # exactly the content produced by this patch and pinned commit.
        file(WRITE ${VELOCITA_RAFT_PATCH_STAMP}
            "RAFT_COMMIT=${RAFT_COMMIT}\n"
            "PATCH_SHA256=${VELOCITA_RAFT_PATCH_SHA256}\n"
            "RAFT_H_SHA256=${VELOCITA_RAFT_H_SHA256}\n"
            "RAFT_SERVER_SHA256=${VELOCITA_RAFT_SERVER_SHA256}\n")
    elseif(VELOCITA_RAFT_STAMP_COMMIT LESS 0 OR
           VELOCITA_RAFT_STAMP_PATCH LESS 0 OR
           NOT VELOCITA_RAFT_STAMP_H_SHA256 STREQUAL VELOCITA_RAFT_H_SHA256 OR
           NOT VELOCITA_RAFT_STAMP_SERVER_SHA256 STREQUAL VELOCITA_RAFT_SERVER_SHA256 OR
           NOT VELOCITA_RAFT_H_SHA256 STREQUAL VELOCITA_RAFT_EXPECTED_H_SHA256 OR
           NOT VELOCITA_RAFT_SERVER_SHA256 STREQUAL VELOCITA_RAFT_EXPECTED_SERVER_SHA256)
        # The last two conditions are the ones that matter when the stamp was
        # written somewhere else - the container image applies the patch at BUILD
        # time, because the gate runs read-only and cannot patch anything. Without
        # them this branch only checks the stamp against itself, so any image
        # could declare its own content correct. The reviewed digests live here
        # and nowhere else, so that declaration is always checked against them.
        message(FATAL_ERROR
            "stale or invalid pinned raft durability patch stamp; remove the "
            "willemt_raft dependency and reconfigure against RAFT_COMMIT=${RAFT_COMMIT}. "
            "Expected raft.h ${VELOCITA_RAFT_EXPECTED_H_SHA256} and raft_server.c "
            "${VELOCITA_RAFT_EXPECTED_SERVER_SHA256}; found ${VELOCITA_RAFT_H_SHA256} "
            "and ${VELOCITA_RAFT_SERVER_SHA256}.")
    endif()
elseif(NOT EXISTS ${VELOCITA_RAFT_PATCH_STAMP})
    # Resolve the tool separately so a MISSING PATCH and a REJECTED HUNK do not
    # produce the same message. They did: `execute_process` reports a program it
    # cannot find through RESULT_VARIABLE and writes nothing to the output, so a
    # container image without `patch` failed configure with a FATAL_ERROR whose
    # body was blank - and the obvious reading of a blank patch error is that the
    # patch did not apply, which sends you to the diff instead of the image.
    find_program(VELOCITA_PATCH_EXECUTABLE patch)
    if(NOT VELOCITA_PATCH_EXECUTABLE)
        message(FATAL_ERROR
            "`patch` was not found on PATH, and the pinned raft durability fork "
            "is applied at configure time rather than vendored. Install it "
            "(Debian/Ubuntu: `patch`) and reconfigure.")
    endif()
    execute_process(
        COMMAND ${VELOCITA_PATCH_EXECUTABLE} -p1 --forward --input=${VELOCITA_RAFT_PATCH}
        WORKING_DIRECTORY ${willemt_raft_SOURCE_DIR}
        RESULT_VARIABLE VELOCITA_RAFT_PATCH_RESULT
        OUTPUT_VARIABLE VELOCITA_RAFT_PATCH_OUTPUT
        ERROR_VARIABLE VELOCITA_RAFT_PATCH_OUTPUT)
    if(NOT VELOCITA_RAFT_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR
            "failed to apply the pinned raft durability patch against "
            "${RAFT_COMMIT} (patch exited ${VELOCITA_RAFT_PATCH_RESULT}):\n"
            "${VELOCITA_RAFT_PATCH_OUTPUT}")
    endif()
    file(SHA256 ${willemt_raft_SOURCE_DIR}/include/raft.h VELOCITA_RAFT_H_SHA256)
    file(SHA256 ${willemt_raft_SOURCE_DIR}/src/raft_server.c VELOCITA_RAFT_SERVER_SHA256)
    # CHECK THE RESULT, do not merely record it.
    #
    # `patch` exiting zero says the hunks were placed, not that they were placed
    # where the review put them. The patch carries ZERO CONTEXT, so every hunk
    # applies by line number alone and a source that differs from the pinned
    # commit by even one line can absorb it silently into working, wrong code.
    #
    # This is the branch that matters: a first configure on a clean machine -
    # every container gate, and both physical targets - takes it. Recording the
    # digests here without comparing them made the stamp a record of whatever
    # happened rather than a check that the right thing happened, and left the
    # expected values guarding only the migration path they were added for.
    if(NOT VELOCITA_RAFT_H_SHA256 STREQUAL VELOCITA_RAFT_EXPECTED_H_SHA256 OR
       NOT VELOCITA_RAFT_SERVER_SHA256 STREQUAL VELOCITA_RAFT_EXPECTED_SERVER_SHA256)
        message(FATAL_ERROR
            "the pinned raft durability patch applied but produced unreviewed "
            "content against ${RAFT_COMMIT}. Expected raft.h "
            "${VELOCITA_RAFT_EXPECTED_H_SHA256} and raft_server.c "
            "${VELOCITA_RAFT_EXPECTED_SERVER_SHA256}; got "
            "${VELOCITA_RAFT_H_SHA256} and ${VELOCITA_RAFT_SERVER_SHA256}. "
            "Do not update the expected digests to make this pass - regenerate "
            "the patch against the pinned commit and review the diff.")
    endif()
    file(WRITE ${VELOCITA_RAFT_PATCH_STAMP}
        "RAFT_COMMIT=${RAFT_COMMIT}\n"
        "PATCH_SHA256=${VELOCITA_RAFT_PATCH_SHA256}\n"
        "RAFT_H_SHA256=${VELOCITA_RAFT_H_SHA256}\n"
        "RAFT_SERVER_SHA256=${VELOCITA_RAFT_SERVER_SHA256}\n")
    message(STATUS "Applied the pinned raft durability patch")
endif()

add_library(willemt_raft STATIC
    ${willemt_raft_SOURCE_DIR}/src/raft_log.c
    ${willemt_raft_SOURCE_DIR}/src/raft_node.c
    ${willemt_raft_SOURCE_DIR}/src/raft_server.c
    ${willemt_raft_SOURCE_DIR}/src/raft_server_properties.c
)
target_include_directories(willemt_raft PUBLIC ${willemt_raft_SOURCE_DIR}/include)

add_library(velocita_core
    src/benchmark_timing.cpp
    src/client_recovery.cpp
    src/client_routing.cpp
    src/counter_fsm.cpp
    src/raft_node.cpp
    src/storage.cpp
    src/wire.cpp
)
target_include_directories(velocita_core PUBLIC include)
target_link_libraries(velocita_core PUBLIC willemt_raft)
target_compile_options(velocita_core PRIVATE -Wall -Wextra -Wpedantic)
if(VELOCITA_BENCHMARK_INSTRUMENTATION)
    target_compile_definitions(velocita_core PUBLIC VELOCITA_BENCHMARK_INSTRUMENTATION=1)
endif()

add_library(velocita_benchmark
    src/benchmark.cpp
)
target_include_directories(velocita_benchmark PUBLIC include)
target_compile_options(velocita_benchmark PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_aeron
    src/aeron_runtime.cpp
    src/client_session.cpp
)
target_include_directories(velocita_aeron PUBLIC include)
target_link_libraries(velocita_aeron PUBLIC
    velocita_core
    aeron_client_wrapper
    aeron::aeron_driver_static
)
target_compile_options(velocita_aeron PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_metrics
    src/shared_metrics.cpp
)
target_include_directories(velocita_metrics PUBLIC include)
target_link_libraries(velocita_metrics PUBLIC velocita_core)
target_compile_options(velocita_metrics PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_contracts
    src/v2_artifact_digest.cpp
    src/v2_digest.cpp
    src/v2_wire.cpp
    src/v2_payload.cpp
)
target_include_directories(velocita_v2_contracts PUBLIC include)
target_compile_definitions(velocita_v2_contracts PUBLIC
    VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_options(velocita_v2_contracts PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_evaluator
    src/v2_rule_evaluator.cpp
    src/v2_action_resolver.cpp
)
target_include_directories(velocita_v2_evaluator PUBLIC include)
target_link_libraries(velocita_v2_evaluator PUBLIC velocita_v2_contracts)
target_compile_options(velocita_v2_evaluator PRIVATE -Wall -Wextra -Wpedantic)

# Test-only by construction: no production target links this, so a venue
# simulator cannot reach a shipping binary by accident (architecture 9.1.6).
add_library(velocita_v2_venue_simulator
    src/v2_venue_simulator.cpp
    src/v2_venue_transport.cpp)
target_include_directories(velocita_v2_venue_simulator PUBLIC include)
target_link_libraries(velocita_v2_venue_simulator PUBLIC velocita_v2_contracts)
target_compile_options(velocita_v2_venue_simulator PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_kernel
    src/v2_gateway.cpp
    src/v2_kernel.cpp
    src/v2_kernel_codec.cpp
    src/v2_metrics.cpp
    src/v2_replay.cpp
    src/v2_state_snapshot.cpp
)
target_include_directories(velocita_v2_kernel PUBLIC include)
target_link_libraries(velocita_v2_kernel PUBLIC velocita_v2_contracts
    PRIVATE velocita_v2_evaluator)
target_compile_options(velocita_v2_kernel PRIVATE -Wall -Wextra -Wpedantic)

# Phase 6. The deterministic reducer behind consensus. Links willemt_raft and
# velocita_core's payload-generic storage/wire layers; V1's RaftNode is left
# untouched so its accepted gates stay valid.
add_library(velocita_v2_replication
    src/v2_replication.cpp
    src/v2_snapshot_store.cpp
    src/v2_wal_arena.cpp
)
target_include_directories(velocita_v2_replication PUBLIC include)
target_link_libraries(velocita_v2_replication
    PUBLIC velocita_v2_kernel velocita_core)
target_compile_options(velocita_v2_replication PRIVATE -Wall -Wextra -Wpedantic)

add_executable(v2-storage-benchmark src/v2_storage_benchmark.cpp)
target_link_libraries(v2-storage-benchmark PRIVATE velocita_v2_replication Threads::Threads)
target_compile_options(v2-storage-benchmark PRIVATE -Wall -Wextra -Wpedantic)

add_executable(raft-node src/node_main.cpp)
target_link_libraries(raft-node PRIVATE velocita_aeron velocita_metrics)

add_executable(raft-metrics-exporter src/metrics_exporter_main.cpp)
target_link_libraries(raft-metrics-exporter PRIVATE velocita_metrics)

add_executable(benchmark-metrics-exporter src/benchmark_metrics_exporter_main.cpp)
target_compile_options(benchmark-metrics-exporter PRIVATE -Wall -Wextra -Wpedantic)

add_executable(raft-client src/client_main.cpp)
target_link_libraries(raft-client PRIVATE velocita_aeron)

add_executable(raft-bench src/benchmark_main.cpp)
target_link_libraries(raft-bench PRIVATE velocita_aeron velocita_benchmark)
target_compile_options(raft-bench PRIVATE -Wall -Wextra -Wpedantic)

add_executable(raft-bench-merge src/benchmark_merge_main.cpp)
target_link_libraries(raft-bench-merge PRIVATE velocita_benchmark)
target_compile_options(raft-bench-merge PRIVATE -Wall -Wextra -Wpedantic)

add_executable(raft-dump src/dump_main.cpp)
target_link_libraries(raft-dump PRIVATE velocita_core)

if(BUILD_TESTING)
    add_executable(core-tests tests/core_tests.cpp)
    target_link_libraries(core-tests PRIVATE velocita_core)
    add_test(NAME core-tests COMMAND core-tests)

    add_executable(metrics-tests tests/metrics_tests.cpp)
    target_link_libraries(metrics-tests PRIVATE velocita_metrics)
    add_test(NAME metrics-tests COMMAND metrics-tests)

    add_executable(benchmark-tests tests/benchmark_tests.cpp)
    target_link_libraries(benchmark-tests PRIVATE velocita_benchmark velocita_core)
    add_test(NAME benchmark-tests COMMAND benchmark-tests)

    add_executable(v2-contract-tests tests/v2_contract_tests.cpp)
    target_link_libraries(v2-contract-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-contract-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-contract-tests COMMAND v2-contract-tests)

    add_executable(v2-value-contract-tests tests/v2_value_contract_tests.cpp)
    target_link_libraries(v2-value-contract-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-value-contract-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-value-contract-tests COMMAND v2-value-contract-tests)

    add_executable(v2-payload-codec-tests tests/v2_payload_codec_tests.cpp)
    target_link_libraries(v2-payload-codec-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-payload-codec-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-payload-codec-tests COMMAND v2-payload-codec-tests)

    add_executable(v2-kernel-policy-tests tests/v2_kernel_policy_tests.cpp)
    target_link_libraries(v2-kernel-policy-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-kernel-policy-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-kernel-policy-tests COMMAND v2-kernel-policy-tests)

    add_executable(v2-venue-transport-tests tests/v2_venue_transport_tests.cpp)
    target_link_libraries(v2-venue-transport-tests PRIVATE velocita_v2_venue_simulator)
    target_compile_options(v2-venue-transport-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-venue-transport-tests COMMAND v2-venue-transport-tests)

    add_executable(v2-kernel-control-tests tests/v2_kernel_control_tests.cpp)
    target_link_libraries(v2-kernel-control-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-kernel-control-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-kernel-control-tests COMMAND v2-kernel-control-tests)

    add_executable(v2-kernel-source-tests tests/v2_kernel_source_tests.cpp)
    target_link_libraries(v2-kernel-source-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-kernel-source-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-kernel-source-tests COMMAND v2-kernel-source-tests)

    add_executable(v2-state-snapshot-tests tests/v2_state_snapshot_tests.cpp)
    target_link_libraries(v2-state-snapshot-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-state-snapshot-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-state-snapshot-tests COMMAND v2-state-snapshot-tests)

    add_executable(v2-kernel-codec-tests tests/v2_kernel_codec_tests.cpp)
    target_link_libraries(v2-kernel-codec-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-kernel-codec-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-kernel-codec-tests COMMAND v2-kernel-codec-tests)

    add_executable(v2-replay-tests tests/v2_replay_tests.cpp)
    target_link_libraries(v2-replay-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-replay-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-replay-tests COMMAND v2-replay-tests)

    add_executable(v2-model-replay-tests tests/v2_model_replay_tests.cpp)
    target_link_libraries(v2-model-replay-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-model-replay-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-model-replay-tests COMMAND v2-model-replay-tests)

    add_executable(v2-venue-simulator-tests tests/v2_venue_simulator_tests.cpp)
    target_link_libraries(v2-venue-simulator-tests PRIVATE
        velocita_v2_venue_simulator)
    target_compile_options(v2-venue-simulator-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-venue-simulator-tests COMMAND v2-venue-simulator-tests)

    add_executable(v2-spsc-ring-tests tests/v2_spsc_ring_tests.cpp)
    target_link_libraries(v2-spsc-ring-tests PRIVATE velocita_v2_replication Threads::Threads)
    target_compile_options(v2-spsc-ring-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-spsc-ring-tests COMMAND v2-spsc-ring-tests)

    add_executable(v2-wal-arena-tests tests/v2_wal_arena_tests.cpp)
    target_link_libraries(v2-wal-arena-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-wal-arena-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-wal-arena-tests COMMAND v2-wal-arena-tests)

    add_executable(v2-replication-tests tests/v2_replication_tests.cpp)
    target_link_libraries(v2-replication-tests PRIVATE velocita_v2_replication Threads::Threads)
    target_compile_options(v2-replication-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-replication-tests COMMAND v2-replication-tests)

    add_executable(v2-snapshot-membership-tests
        tests/v2_snapshot_membership_tests.cpp)
    target_link_libraries(v2-snapshot-membership-tests PRIVATE
        velocita_v2_replication Threads::Threads)
    target_compile_options(v2-snapshot-membership-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-snapshot-membership-tests
        COMMAND v2-snapshot-membership-tests)

    add_executable(v2-durable-quorum-tests tests/v2_durable_quorum_tests.cpp)
    target_link_libraries(v2-durable-quorum-tests PRIVATE willemt_raft)
    target_compile_options(v2-durable-quorum-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-durable-quorum-tests COMMAND v2-durable-quorum-tests)

    add_executable(v2-gateway-tests tests/v2_gateway_tests.cpp)
    target_link_libraries(v2-gateway-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-gateway-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-gateway-tests COMMAND v2-gateway-tests)

    # The Phase 5 gate: the real kernel, the real guard and planner, and the
    # adversarial venue, driven together through the G-033 fault matrix.
    # An exhaustive inventory of the reachable order lifecycle, used to review
    # the Phase 5 applicability predicates by enumeration rather than by reading.
    add_executable(v2-lifecycle-reachability-tests
        tests/v2_lifecycle_reachability_tests.cpp)
    target_link_libraries(v2-lifecycle-reachability-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-lifecycle-reachability-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-lifecycle-reachability-tests
        COMMAND v2-lifecycle-reachability-tests)

    add_executable(v2-gateway-delivery-tests tests/v2_gateway_delivery_tests.cpp)
    target_link_libraries(v2-gateway-delivery-tests PRIVATE velocita_v2_kernel
        velocita_v2_venue_simulator)
    target_compile_options(v2-gateway-delivery-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-gateway-delivery-tests COMMAND v2-gateway-delivery-tests)

    add_executable(v2-metrics-tests tests/v2_metrics_tests.cpp)
    target_link_libraries(v2-metrics-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-metrics-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-metrics-tests COMMAND v2-metrics-tests)

    add_executable(v2-phase4-property-tests tests/v2_phase4_property_tests.cpp)
    target_link_libraries(v2-phase4-property-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-phase4-property-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-phase4-property-tests COMMAND v2-phase4-property-tests)

    add_executable(v2-rule-evaluator-tests tests/v2_rule_evaluator_tests.cpp)
    target_link_libraries(v2-rule-evaluator-tests PRIVATE velocita_v2_evaluator)
    target_compile_options(v2-rule-evaluator-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-rule-evaluator-tests COMMAND v2-rule-evaluator-tests)

    add_executable(v2-layout-tests tests/v2_layout_tests.cpp)
    target_link_libraries(v2-layout-tests PRIVATE
        velocita_v2_contracts velocita_v2_kernel velocita_v2_evaluator)
    target_compile_options(v2-layout-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-layout-tests COMMAND v2-layout-tests)

    add_executable(v2-decision-kernel-tests tests/v2_decision_kernel_tests.cpp)
    target_link_libraries(v2-decision-kernel-tests PRIVATE
        velocita_v2_kernel velocita_v2_evaluator)
    target_compile_options(v2-decision-kernel-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-decision-kernel-tests COMMAND v2-decision-kernel-tests)

    add_executable(v2-action-template-contract-tests
        tests/v2_action_template_contract_tests.cpp)
    target_link_libraries(v2-action-template-contract-tests PRIVATE
        velocita_v2_contracts)
    target_compile_options(v2-action-template-contract-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-action-template-contract-tests
        COMMAND v2-action-template-contract-tests)

    add_executable(v2-action-resolver-tests tests/v2_action_resolver_tests.cpp)
    target_link_libraries(v2-action-resolver-tests PRIVATE velocita_v2_evaluator)
    target_compile_options(v2-action-resolver-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-action-resolver-tests COMMAND v2-action-resolver-tests)

    add_executable(v2-executable-policy-contract-tests
        tests/v2_executable_policy_contract_tests.cpp)
    target_link_libraries(v2-executable-policy-contract-tests PRIVATE
        velocita_v2_contracts)
    target_compile_options(v2-executable-policy-contract-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-executable-policy-contract-tests
        COMMAND v2-executable-policy-contract-tests)

    add_executable(v2-executable-policy-codec-tests
        tests/v2_executable_policy_codec_tests.cpp)
    target_link_libraries(v2-executable-policy-codec-tests PRIVATE
        velocita_v2_contracts)
    target_compile_options(v2-executable-policy-codec-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-executable-policy-codec-tests
        COMMAND v2-executable-policy-codec-tests)

    add_executable(v2-artifact-digest-tests
        tests/v2_artifact_digest_tests.cpp)
    target_link_libraries(v2-artifact-digest-tests PRIVATE
        velocita_v2_contracts)
    target_compile_options(v2-artifact-digest-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-artifact-digest-tests COMMAND v2-artifact-digest-tests)

    add_executable(v2-executable-policy-kernel-tests
        tests/v2_executable_policy_kernel_tests.cpp)
    target_link_libraries(v2-executable-policy-kernel-tests PRIVATE
        velocita_v2_kernel)
    target_compile_options(v2-executable-policy-kernel-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-executable-policy-kernel-tests
        COMMAND v2-executable-policy-kernel-tests)

    add_executable(v2-risk-order-outbox-tests
        tests/v2_risk_order_outbox_tests.cpp)
    target_link_libraries(v2-risk-order-outbox-tests PRIVATE
        velocita_v2_kernel)
    target_compile_options(v2-risk-order-outbox-tests PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME v2-risk-order-outbox-tests
        COMMAND v2-risk-order-outbox-tests)

    add_executable(v2-layout-report src/v2_layout_report.cpp)
    target_link_libraries(v2-layout-report PRIVATE
        velocita_v2_contracts velocita_v2_kernel velocita_v2_evaluator)
    target_compile_options(v2-layout-report PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(v2-contract-benchmark src/v2_contract_benchmark.cpp)
    target_link_libraries(v2-contract-benchmark PRIVATE velocita_v2_contracts)
    target_compile_options(v2-contract-benchmark PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(v2-phase3-benchmark src/v2_phase3_benchmark.cpp)
    target_link_libraries(v2-phase3-benchmark PRIVATE velocita_v2_kernel)
    target_compile_options(v2-phase3-benchmark PRIVATE -Wall -Wextra -Wpedantic)

    # Aggregate targets for the acceptance gates.
    #
    # The gates build explicit targets rather than `all`, to keep a four-core Pi
    # from compiling binaries no suite runs. A HAND-MAINTAINED list is the
    # obvious way to do that and it is wrong: it drifts from this file every time
    # a test is added, and the failure surfaces as ctest reporting "Not Run",
    # which reads like a behavioural failure and is not. Phase 5 added four
    # suites and lost a full physical-target run to exactly that.
    #
    # These are derived from the build system itself, so they cannot drift. Must
    # stay after every add_executable above: BUILDSYSTEM_TARGETS only sees what
    # this directory has already defined.
    get_property(velocita_defined_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(velocita_all_test_targets "")
    set(velocita_v2_test_targets "")
    foreach(velocita_target IN LISTS velocita_defined_targets)
        if(velocita_target MATCHES "-tests$")
            list(APPEND velocita_all_test_targets ${velocita_target})
            if(velocita_target MATCHES "^v2-")
                list(APPEND velocita_v2_test_targets ${velocita_target})
            endif()
        endif()
    endforeach()
    add_custom_target(velocita-all-tests DEPENDS ${velocita_all_test_targets})
    add_custom_target(velocita-v2-tests DEPENDS ${velocita_v2_test_targets})
endif()
