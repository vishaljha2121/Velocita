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
    "d349e211c21de8c4d07b1a59da7f06f95a139a53dcc38ee7e11319e25483b222")
set(VELOCITA_RAFT_EXPECTED_SERVER_SHA256
    "447eec995dc31639de1059b36f2c384a8a4be3217df6b1daeac45ae94215bb57")
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
    # where the review put them. Zero-context hunks are position-sensitive;
    # verify exact resulting content against the reviewed source hashes too.
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

# The patch also changes raft_log.c (async suffix hook), raft_private.h
# (pre-vote state) and raft_server_properties.c (compacted last-log term). Check
# them against the reviewed content independently of any stamp supplied with a
# prepatched dependency.
foreach(VELOCITA_RAFT_PINNED
        "src/raft_log.c=8aa916dd1fbf41b800e85a7a732878bf24aa3c51506a3e7e20d530751cddb386"
        "include/raft_private.h=b14235a494d786c392bee873b7477c7cba785a28a07b8ef5adb1ee0037247c6b"
        "src/raft_server_properties.c=aadbe22fbe88eaa0bb70152528932fb5b4920446667b553876d3eacd3fee7c1c")
    string(REPLACE "=" ";" VELOCITA_RAFT_PINNED "${VELOCITA_RAFT_PINNED}")
    list(GET VELOCITA_RAFT_PINNED 0 VELOCITA_RAFT_PINNED_FILE)
    list(GET VELOCITA_RAFT_PINNED 1 VELOCITA_RAFT_PINNED_SHA256)
    file(SHA256 ${willemt_raft_SOURCE_DIR}/${VELOCITA_RAFT_PINNED_FILE} VELOCITA_RAFT_FOUND_SHA256)
    if(NOT VELOCITA_RAFT_FOUND_SHA256 STREQUAL VELOCITA_RAFT_PINNED_SHA256)
        message(FATAL_ERROR
            "unreviewed pinned ${VELOCITA_RAFT_PINNED_FILE}: ${VELOCITA_RAFT_FOUND_SHA256}")
    endif()
endforeach()

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
    src/v2_local_generation.cpp
    src/v2_authority_barrier.cpp
    src/v2_static_identity.cpp
    src/v2_artifact_digest.cpp
    src/v2_digest.cpp
    src/v2_execution_catalog.cpp
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
    src/v2_venue_transport.cpp
    src/v2_venue_wire.cpp)
target_include_directories(velocita_v2_venue_simulator PUBLIC include)
target_link_libraries(velocita_v2_venue_simulator PUBLIC velocita_v2_contracts)
target_compile_options(velocita_v2_venue_simulator PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_paper_venue src/v2_paper_venue.cpp src/v2_paper_venue_store.cpp)
target_include_directories(velocita_v2_paper_venue PUBLIC include)
target_link_libraries(velocita_v2_paper_venue PUBLIC velocita_v2_contracts PRIVATE velocita_v2_venue_simulator)
target_compile_options(velocita_v2_paper_venue PRIVATE -Wall -Wextra -Wpedantic)

# ADR 0019. True Markets adapter state machine and wire encoding. All venue I/O
# and signing are injected; this library holds no network or TLS code, which
# the v2-truemarkets-no-network-symbols test checks on its object files.
add_library(velocita_v2_truemarkets_venue
    src/v2_truemarkets_venue.cpp
    src/v2_truemarkets_wire.cpp)
target_include_directories(velocita_v2_truemarkets_venue PUBLIC include)
target_link_libraries(velocita_v2_truemarkets_venue PUBLIC velocita_v2_contracts PRIVATE velocita_v2_venue_simulator)
target_compile_options(velocita_v2_truemarkets_venue PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_kernel
    src/v2_gateway_authority.cpp
    src/v2_gateway_dispatch.cpp
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
    src/v2_generation_base.cpp
    src/v2_generation_base_files.cpp
    src/v2_archive_file.cpp
    src/v2_archive_segment.cpp
    src/v2_archive_catalog.cpp
    src/v2_archive_store.cpp
    src/v2_archive_destination.cpp
    src/v2_bound_frame.cpp
    src/v2_bound_metadata.cpp
    src/v2_bound_wal.cpp
    src/v2_bound_peer.cpp
    src/v2_identity_startup.cpp
    src/v2_replication.cpp
    src/v2_result_ledger.cpp
    src/v2_snapshot_install_preflight.cpp
    src/v2_outbound_mailbox.cpp
    src/v2_retained_log.cpp
    src/v2_snapshot_store.cpp
    src/v2_wal_arena.cpp
)
target_include_directories(velocita_v2_replication PUBLIC include)
target_link_libraries(velocita_v2_replication
    PUBLIC velocita_v2_kernel velocita_core)
target_compile_options(velocita_v2_replication PRIVATE -Wall -Wextra -Wpedantic)

# M2 node-process pieces: authenticated transport envelope, session fence,
# configuration and the node owner loop. Never links a venue library.
add_library(velocita_v2_node
    src/v2_auth_envelope.cpp
    src/v2_session_fence.cpp
    src/v2_node_config.cpp
    src/v2_node_protocol.cpp
    src/v2_node.cpp
    src/v2_node_client.cpp
    src/v2_gateway_link.cpp
    src/v2_shadow_program.cpp
    src/v2_shadow_submitter.cpp
    src/v2_election_jitter.cpp
    src/v2_node_files.cpp
    src/v2_node_health.cpp
    src/v2_node_metrics_text.cpp
)
target_include_directories(velocita_v2_node PUBLIC include)
target_link_libraries(velocita_v2_node PUBLIC velocita_v2_replication)
target_compile_options(velocita_v2_node PRIVATE -Wall -Wextra -Wpedantic)

# M2 node binaries. The node must never link a venue library: checked below.
add_library(velocita_v2_node_aeron src/v2_aeron_node_transport.cpp)
target_include_directories(velocita_v2_node_aeron PUBLIC include)
target_link_libraries(velocita_v2_node_aeron PUBLIC velocita_v2_node velocita_aeron)
target_compile_options(velocita_v2_node_aeron PRIVATE -Wall -Wextra -Wpedantic)

# Phase 2 gateway: a separate process that links the venue seam. The node
# interlock below keeps every venue library out of the voter binaries.
add_library(velocita_v2_gateway_loop src/v2_gateway_loop.cpp)
target_include_directories(velocita_v2_gateway_loop PUBLIC include)
target_link_libraries(velocita_v2_gateway_loop PUBLIC velocita_v2_node velocita_v2_venue_simulator)
target_compile_options(velocita_v2_gateway_loop PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-v2-gateway src/v2_gateway_main.cpp)
target_link_libraries(velocita-v2-gateway PRIVATE velocita_v2_gateway_loop velocita_v2_paper_venue
    velocita_v2_truemarkets_venue velocita_v2_node_aeron Threads::Threads)
target_compile_options(velocita-v2-gateway PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-v2-shadow-submitter src/v2_shadow_submitter_main.cpp)
target_link_libraries(velocita-v2-shadow-submitter PRIVATE velocita_v2_node_aeron Threads::Threads)
target_compile_options(velocita-v2-shadow-submitter PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-v2-node src/v2_node_main.cpp)
target_link_libraries(velocita-v2-node PRIVATE velocita_v2_node_aeron Threads::Threads)
target_compile_options(velocita-v2-node PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-v2-client src/v2_client_main.cpp)
target_link_libraries(velocita-v2-client PRIVATE velocita_v2_node_aeron Threads::Threads)
target_compile_options(velocita-v2-client PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-v2-status src/v2_status_main.cpp)
target_link_libraries(velocita-v2-status PRIVATE velocita_v2_node)
target_compile_options(velocita-v2-status PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-v2-exporter src/v2_exporter_main.cpp)
target_link_libraries(velocita-v2-exporter PRIVATE velocita_v2_node)
target_compile_options(velocita-v2-exporter PRIVATE -Wall -Wextra -Wpedantic)

# Non-trading M3 shadow catalog writer (test fixture catalog; not a venue contract).
add_executable(velocita-v2-shadow-catalog tests/v2_shadow_catalog_main.cpp)
target_include_directories(velocita-v2-shadow-catalog PRIVATE tests)
target_link_libraries(velocita-v2-shadow-catalog PRIVATE velocita_v2_node)
target_compile_options(velocita-v2-shadow-catalog PRIVATE -Wall -Wextra -Wpedantic)

# Structural interlock: no venue simulator/transport code may reach the node.
foreach(velocita_node_target IN ITEMS velocita_v2_node velocita_v2_node_aeron velocita-v2-node velocita-v2-client
        velocita-v2-shadow-submitter velocita-v2-status velocita-v2-exporter velocita-v2-shadow-catalog)
    get_target_property(velocita_node_links ${velocita_node_target} LINK_LIBRARIES)
    foreach(velocita_node_link IN LISTS velocita_node_links)
        if(velocita_node_link MATCHES "venue")
            message(FATAL_ERROR "${velocita_node_target} links venue library ${velocita_node_link}")
        endif()
    endforeach()
endforeach()

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

    add_executable(v2-venue-wire-tests tests/v2_venue_wire_tests.cpp)
    target_link_libraries(v2-venue-wire-tests PRIVATE velocita_v2_venue_simulator)
    target_compile_options(v2-venue-wire-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-venue-wire-tests COMMAND v2-venue-wire-tests)

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

    add_executable(v2-execution-catalog-tests
        tests/v2_execution_catalog_tests.cpp
        tests/v2_execution_catalog_adversarial.cpp)
    target_link_libraries(v2-execution-catalog-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-execution-catalog-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-execution-catalog-tests COMMAND v2-execution-catalog-tests)

    add_executable(v2-catalog-binding-tests tests/v2_catalog_binding_tests.cpp
        tests/v2_catalog_replication_tests.cpp)
    target_link_libraries(v2-catalog-binding-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-catalog-binding-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-catalog-binding-tests COMMAND v2-catalog-binding-tests)

    add_executable(v2-authority-barrier-tests tests/v2_authority_barrier_tests.cpp)
    target_link_libraries(v2-authority-barrier-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-authority-barrier-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-authority-barrier-tests COMMAND v2-authority-barrier-tests)

    add_executable(v2-authority-storage-failure-tests tests/v2_authority_storage_failure_tests.cpp)
    target_link_libraries(v2-authority-storage-failure-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-authority-storage-failure-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-authority-storage-failure-tests COMMAND v2-authority-storage-failure-tests)

    add_executable(v2-gateway-authority-tests tests/v2_gateway_authority_tests.cpp)
    target_link_libraries(v2-gateway-authority-tests PRIVATE velocita_v2_kernel)
    target_compile_options(v2-gateway-authority-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-gateway-authority-tests COMMAND v2-gateway-authority-tests)

    add_executable(v2-authority-composition-tests tests/v2_authority_composition_tests.cpp)
    target_link_libraries(v2-authority-composition-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-authority-composition-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-authority-composition-tests COMMAND v2-authority-composition-tests)

    add_executable(v2-snapshot-store-bounds-tests tests/v2_snapshot_store_bounds_tests.cpp)
    target_link_libraries(v2-snapshot-store-bounds-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-snapshot-store-bounds-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-snapshot-store-bounds-tests COMMAND v2-snapshot-store-bounds-tests)

    add_executable(v2-metadata-persistence-model-tests tests/v2_metadata_persistence_model_tests.cpp)
    target_include_directories(v2-metadata-persistence-model-tests PRIVATE include)
    target_compile_options(v2-metadata-persistence-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-metadata-persistence-model-tests COMMAND v2-metadata-persistence-model-tests)

    add_executable(v2-bound-metadata-persistence-model-tests tests/v2_bound_metadata_persistence_model_tests.cpp)
    target_link_libraries(v2-bound-metadata-persistence-model-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-bound-metadata-persistence-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-bound-metadata-persistence-model-tests COMMAND v2-bound-metadata-persistence-model-tests)

    add_executable(v2-static-identity-tests tests/v2_static_identity_tests.cpp)
    target_link_libraries(v2-static-identity-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-static-identity-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-static-identity-tests COMMAND v2-static-identity-tests)

    add_executable(v2-identity-startup-tests tests/v2_identity_startup_tests.cpp)
    target_link_libraries(v2-identity-startup-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-identity-startup-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-identity-startup-tests COMMAND v2-identity-startup-tests)

    add_executable(v2-kernel-startup-tests tests/v2_kernel_startup_tests.cpp)
    target_link_libraries(v2-kernel-startup-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-kernel-startup-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-kernel-startup-tests COMMAND v2-kernel-startup-tests)

    add_executable(v2-bound-frame-tests tests/v2_bound_frame_tests.cpp)
    target_link_libraries(v2-bound-frame-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-bound-frame-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-bound-frame-tests COMMAND v2-bound-frame-tests)

    add_executable(v2-bound-peer-tests tests/v2_bound_peer_tests.cpp)
    target_link_libraries(v2-bound-peer-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-bound-peer-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-bound-peer-tests COMMAND v2-bound-peer-tests)

    add_executable(v2-bound-metadata-tests tests/v2_bound_metadata_tests.cpp)
    target_link_libraries(v2-bound-metadata-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-bound-metadata-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-bound-metadata-tests COMMAND v2-bound-metadata-tests)

    add_executable(v2-history-binding-tests tests/v2_history_binding_tests.cpp)
    target_link_libraries(v2-history-binding-tests PRIVATE velocita_v2_replication Threads::Threads)
    target_compile_options(v2-history-binding-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-history-binding-tests COMMAND v2-history-binding-tests)

    add_executable(v2-result-ledger-tests tests/v2_result_ledger_tests.cpp)
    target_link_libraries(v2-result-ledger-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-result-ledger-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-result-ledger-tests COMMAND v2-result-ledger-tests)

    add_executable(v2-result-admission-tests tests/v2_result_admission_red.cpp)
    target_link_libraries(v2-result-admission-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-result-admission-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-result-admission-tests COMMAND v2-result-admission-tests)

    add_executable(v2-result-admission-edge-tests tests/v2_result_admission_edges.cpp)
    target_link_libraries(v2-result-admission-edge-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-result-admission-edge-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-result-admission-edge-tests COMMAND v2-result-admission-edge-tests)

    add_executable(v2-outbound-mailbox-tests tests/v2_outbound_mailbox_tests.cpp)
    target_link_libraries(v2-outbound-mailbox-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-outbound-mailbox-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-outbound-mailbox-tests COMMAND v2-outbound-mailbox-tests)

    add_executable(v2-outbound-saturation-tests tests/v2_outbound_log_capacity_red.cpp)
    target_link_libraries(v2-outbound-saturation-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-outbound-saturation-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-outbound-saturation-tests COMMAND v2-outbound-saturation-tests)
    add_test(NAME v2-log-saturation-tests COMMAND ${CMAKE_COMMAND} -E env
        CAPACITY_REVIEW_CASE=log $<TARGET_FILE:v2-outbound-saturation-tests>)

    add_executable(v2-retained-log-tests tests/v2_retained_log_tests.cpp)
    target_link_libraries(v2-retained-log-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-retained-log-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-retained-log-tests COMMAND v2-retained-log-tests)

    add_executable(v2-wal-streaming-tests tests/v2_wal_streaming_tests.cpp)
    target_link_libraries(v2-wal-streaming-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-wal-streaming-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-wal-streaming-tests COMMAND v2-wal-streaming-tests)

    add_executable(v2-incremental-digest-tests tests/v2_incremental_digest_tests.cpp)
    target_link_libraries(v2-incremental-digest-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-incremental-digest-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-incremental-digest-tests COMMAND v2-incremental-digest-tests)

    add_executable(v2-archive-segment-tests tests/v2_archive_segment_tests.cpp)
    foreach(archive_suite IN ITEMS archive-catalog archive-store wal-maintenance archive-compaction compaction-fence archive-startup archive-persistence-model archive-publication-fault compaction-io-fault)
        string(REPLACE "-" "_" archive_suite_file "${archive_suite}")
        add_executable(v2-${archive_suite}-tests tests/v2_${archive_suite_file}_tests.cpp)
        target_link_libraries(v2-${archive_suite}-tests PRIVATE velocita_v2_replication)
        target_compile_options(v2-${archive_suite}-tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME v2-${archive_suite}-tests COMMAND v2-${archive_suite}-tests)
    endforeach()
    target_link_libraries(v2-archive-segment-tests PRIVATE velocita_v2_replication)
    add_library(v2-compaction-fault-io OBJECT src/v2_snapshot_store.cpp src/v2_wal_arena.cpp)
    target_link_libraries(v2-compaction-fault-io PRIVATE velocita_v2_replication)
    target_compile_options(v2-compaction-fault-io PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_compaction_fault_hooks.hpp"
        -Wall -Wextra -Wpedantic)
    target_sources(v2-compaction-io-fault-tests PRIVATE $<TARGET_OBJECTS:v2-compaction-fault-io>)
    add_library(v2-compaction-model-archive OBJECT src/v2_archive_store.cpp)
    target_compile_definitions(v2-compaction-model-archive PRIVATE COMPACTION_MODEL_ARCHIVE=1)
    add_library(v2-compaction-model-io OBJECT src/v2_snapshot_store.cpp src/v2_wal_arena.cpp)
    foreach(model_io_target IN ITEMS v2-compaction-model-archive v2-compaction-model-io)
        target_link_libraries(${model_io_target} PRIVATE velocita_v2_replication)
        target_compile_options(${model_io_target} PRIVATE
            -include "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_compaction_persistence_model_hooks.hpp"
            -Wall -Wextra -Wpedantic)
    endforeach()
    add_executable(v2-compaction-persistence-model-tests
        tests/v2_compaction_persistence_model_tests.cpp
        $<TARGET_OBJECTS:v2-compaction-model-archive>
        $<TARGET_OBJECTS:v2-compaction-model-io>)
    target_link_libraries(v2-compaction-persistence-model-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-compaction-persistence-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-compaction-persistence-model-tests COMMAND v2-compaction-persistence-model-tests)
    add_executable(v2-async-persistence-tests tests/v2_async_persistence_tests.cpp)
    target_link_libraries(v2-async-persistence-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-async-persistence-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-async-persistence-tests COMMAND v2-async-persistence-tests)
    set_tests_properties(v2-async-persistence-tests PROPERTIES TIMEOUT 60)
    add_executable(v2-async-suffix-tests tests/v2_async_suffix_tests.cpp)
    target_link_libraries(v2-async-suffix-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-async-suffix-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-async-suffix-tests COMMAND v2-async-suffix-tests)
    set_tests_properties(v2-async-suffix-tests PROPERTIES TIMEOUT 60)
    add_executable(v2-raft-persistence-tests tests/v2_raft_persistence_tests.cpp)
    target_link_libraries(v2-raft-persistence-tests PRIVATE willemt_raft)
    target_compile_options(v2-raft-persistence-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-raft-persistence-tests COMMAND v2-raft-persistence-tests)
    add_executable(v2-persistence-completion-tests tests/v2_persistence_completion_tests.cpp)
    target_link_libraries(v2-persistence-completion-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-persistence-completion-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-persistence-completion-tests COMMAND v2-persistence-completion-tests)
    set_tests_properties(v2-persistence-completion-tests PROPERTIES TIMEOUT 60)
    add_library(v2-async-model-io OBJECT src/v2_bound_metadata.cpp src/v2_wal_arena.cpp)
    target_link_libraries(v2-async-model-io PRIVATE velocita_v2_replication)
    target_compile_options(v2-async-model-io PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/tests/v2_compaction_persistence_model_hooks.hpp"
        -Wall -Wextra -Wpedantic)
    add_executable(v2-async-persistence-model-tests
        tests/v2_async_persistence_model_tests.cpp
        $<TARGET_OBJECTS:v2-async-model-io>)
    target_link_libraries(v2-async-persistence-model-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-async-persistence-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-async-persistence-model-tests COMMAND v2-async-persistence-model-tests)
    set_tests_properties(v2-async-persistence-model-tests PROPERTIES TIMEOUT 180)
    target_compile_options(v2-archive-segment-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-archive-segment-tests COMMAND v2-archive-segment-tests)

    add_executable(v2-archive-segment-adversarial-tests tests/v2_archive_segment_adversarial_tests.cpp)
    target_link_libraries(v2-archive-segment-adversarial-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-archive-segment-adversarial-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-archive-segment-adversarial-tests COMMAND v2-archive-segment-adversarial-tests)

    add_executable(v2-archive-file-tests tests/v2_archive_file_tests.cpp)
    target_link_libraries(v2-archive-file-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-archive-file-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-archive-file-tests COMMAND v2-archive-file-tests)

    add_executable(v2-archive-file-io-tests tests/v2_archive_file_io_tests.cpp)
    target_link_libraries(v2-archive-file-io-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-archive-file-io-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-archive-file-io-tests COMMAND v2-archive-file-io-tests)

    add_executable(v2-local-generation-tests tests/v2_local_generation_tests.cpp)
    target_link_libraries(v2-local-generation-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-local-generation-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-local-generation-tests COMMAND v2-local-generation-tests)

    add_executable(v2-local-generation-adversarial-tests tests/v2_local_generation_adversarial_tests.cpp)
    target_link_libraries(v2-local-generation-adversarial-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-local-generation-adversarial-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-local-generation-adversarial-tests COMMAND v2-local-generation-adversarial-tests)

    add_executable(v2-generation-startup-guard-tests tests/v2_generation_startup_guard_tests.cpp)
    target_link_libraries(v2-generation-startup-guard-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-generation-startup-guard-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-generation-startup-guard-tests COMMAND v2-generation-startup-guard-tests)

    add_executable(v2-generation-base-tests tests/v2_generation_base_tests.cpp)
    target_link_libraries(v2-generation-base-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-generation-base-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-generation-base-tests COMMAND v2-generation-base-tests)

    add_executable(v2-generation-base-files-tests tests/v2_generation_base_files_tests.cpp)
    target_link_libraries(v2-generation-base-files-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-generation-base-files-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-generation-base-files-tests COMMAND v2-generation-base-files-tests)

    add_executable(v2-generation-startup-tests tests/v2_generation_startup_tests.cpp)
    target_link_libraries(v2-generation-startup-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-generation-startup-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-generation-startup-tests COMMAND v2-generation-startup-tests)

    add_executable(v2-generation-publication-model-tests tests/v2_generation_publication_model_tests.cpp)
    target_link_libraries(v2-generation-publication-model-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-generation-publication-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-generation-publication-model-tests COMMAND v2-generation-publication-model-tests)

    add_executable(v2-result-obligation-tests tests/v2_result_obligation_tests.cpp)
    target_link_libraries(v2-result-obligation-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-result-obligation-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-result-obligation-tests COMMAND v2-result-obligation-tests)

    add_executable(v2-snapshot-install-preflight-tests tests/v2_snapshot_install_preflight_tests.cpp)
    target_link_libraries(v2-snapshot-install-preflight-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-snapshot-install-preflight-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-snapshot-install-preflight-tests COMMAND v2-snapshot-install-preflight-tests)

    add_executable(v2-install-history-bridge-contract-tests tests/v2_install_history_bridge_contract_tests.cpp)
    target_link_libraries(v2-install-history-bridge-contract-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-install-history-bridge-contract-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-install-history-bridge-contract-tests COMMAND v2-install-history-bridge-contract-tests)

    foreach(switch_suite IN ITEMS wal-target-switch wal-target-switch-adversarial)
        string(REPLACE "-" "_" switch_source ${switch_suite})
        add_executable(v2-${switch_suite}-tests tests/v2_${switch_source}_tests.cpp)
        target_link_libraries(v2-${switch_suite}-tests PRIVATE velocita_v2_replication)
        target_compile_options(v2-${switch_suite}-tests PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME v2-${switch_suite}-tests COMMAND v2-${switch_suite}-tests)
        set_tests_properties(v2-${switch_suite}-tests PROPERTIES TIMEOUT 30)
    endforeach()

    add_executable(v2-history-recovery-tests tests/v2_history_recovery_tests.cpp)
    target_link_libraries(v2-history-recovery-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-history-recovery-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-history-recovery-tests COMMAND v2-history-recovery-tests)
    set_tests_properties(v2-history-recovery-tests PROPERTIES TIMEOUT 120)

    add_executable(v2-auth-envelope-tests tests/v2_auth_envelope_tests.cpp)
    target_link_libraries(v2-auth-envelope-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-auth-envelope-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-auth-envelope-tests COMMAND v2-auth-envelope-tests)

    add_executable(v2-session-fence-tests tests/v2_session_fence_tests.cpp)
    target_link_libraries(v2-session-fence-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-session-fence-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-session-fence-tests COMMAND v2-session-fence-tests)

    # Compiles the production source itself with persistence syscalls redirected.
    add_executable(v2-session-epoch-persistence-model-tests
        tests/v2_session_epoch_persistence_model_tests.cpp)
    target_link_libraries(v2-session-epoch-persistence-model-tests PRIVATE velocita_v2_contracts)
    target_compile_options(v2-session-epoch-persistence-model-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-session-epoch-persistence-model-tests COMMAND v2-session-epoch-persistence-model-tests)

    add_executable(v2-node-config-tests tests/v2_node_config_tests.cpp)
    target_link_libraries(v2-node-config-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-node-config-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-config-tests COMMAND v2-node-config-tests)

    add_executable(v2-deploy-template-tests tests/v2_deploy_template_tests.cpp)
    target_link_libraries(v2-deploy-template-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-deploy-template-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(v2-deploy-template-tests PRIVATE VELOCITA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME v2-deploy-template-tests COMMAND v2-deploy-template-tests)

    add_executable(v2-node-protocol-tests tests/v2_node_protocol_tests.cpp)
    target_link_libraries(v2-node-protocol-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-node-protocol-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-protocol-tests COMMAND v2-node-protocol-tests)

    add_executable(v2-election-jitter-tests tests/v2_election_jitter_tests.cpp)
    target_link_libraries(v2-election-jitter-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-election-jitter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-election-jitter-tests COMMAND v2-election-jitter-tests)

    add_executable(v2-shadow-submitter-tests tests/v2_shadow_submitter_tests.cpp)
    target_link_libraries(v2-shadow-submitter-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-shadow-submitter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-shadow-submitter-tests COMMAND v2-shadow-submitter-tests)

    add_executable(v2-shadow-program-tests tests/v2_shadow_program_tests.cpp)
    target_link_libraries(v2-shadow-program-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-shadow-program-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-shadow-program-tests COMMAND v2-shadow-program-tests)

    add_executable(v2-follower-ack-tests tests/v2_follower_ack_tests.cpp)
    target_link_libraries(v2-follower-ack-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-follower-ack-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-follower-ack-tests COMMAND v2-follower-ack-tests)

    add_executable(v2-shadow-catalog-tests tests/v2_shadow_catalog_tests.cpp)
    target_include_directories(v2-shadow-catalog-tests PRIVATE tests)
    target_link_libraries(v2-shadow-catalog-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-shadow-catalog-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-shadow-catalog-tests COMMAND v2-shadow-catalog-tests)

    add_executable(v2-node-client-tests tests/v2_node_client_tests.cpp)
    target_link_libraries(v2-node-client-tests PRIVATE velocita_v2_node Threads::Threads)
    target_compile_options(v2-node-client-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-client-tests COMMAND v2-node-client-tests)
    set_tests_properties(v2-node-client-tests PROPERTIES TIMEOUT 180)

    add_executable(v2-paper-venue-tests tests/v2_paper_venue_tests.cpp)
    target_link_libraries(v2-paper-venue-tests PRIVATE velocita_v2_kernel velocita_v2_paper_venue)
    target_compile_options(v2-paper-venue-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-paper-venue-tests COMMAND v2-paper-venue-tests)

    add_executable(v2-paper-venue-state-tests tests/v2_paper_venue_state_tests.cpp)
    target_link_libraries(v2-paper-venue-state-tests PRIVATE velocita_v2_paper_venue)
    target_compile_options(v2-paper-venue-state-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-paper-venue-state-tests COMMAND v2-paper-venue-state-tests)

    add_executable(v2-gateway-reconciliation-tests tests/v2_gateway_reconciliation_tests.cpp)
    target_link_libraries(v2-gateway-reconciliation-tests PRIVATE velocita_v2_gateway_loop)
    target_compile_options(v2-gateway-reconciliation-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-gateway-reconciliation-tests COMMAND v2-gateway-reconciliation-tests)

    add_executable(v2-truemarkets-wire-tests tests/v2_truemarkets_wire_tests.cpp)
    target_link_libraries(v2-truemarkets-wire-tests PRIVATE velocita_v2_truemarkets_venue)
    target_compile_options(v2-truemarkets-wire-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-truemarkets-wire-tests COMMAND v2-truemarkets-wire-tests)

    add_executable(v2-truemarkets-venue-tests tests/v2_truemarkets_venue_tests.cpp)
    target_link_libraries(v2-truemarkets-venue-tests PRIVATE velocita_v2_kernel velocita_v2_truemarkets_venue)
    target_compile_options(v2-truemarkets-venue-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-truemarkets-venue-tests COMMAND v2-truemarkets-venue-tests)

    # The adapter library must contain no path to a network: no socket, name
    # resolution or TLS symbol may be referenced by its objects.
    add_test(NAME v2-truemarkets-no-network-symbols
        COMMAND ${CMAKE_COMMAND} -DLIBRARY=$<TARGET_FILE:velocita_v2_truemarkets_venue>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_no_network_symbols.cmake)

    # --venue truemarkets must refuse to start: without arming, without a
    # signer, and (in every build today) without a compiled live transport.
    foreach(velocita_refusal IN ITEMS
            "unarmed|not armed|--venue;truemarkets"
            "signer-only|not armed|--venue;truemarkets;--signer-endpoint;/run/velocita/signer.sock"
            "armed-no-signer|no signer|--venue;truemarkets;--arm-live-venue"
            "armed-with-signer|no live venue transport|--venue;truemarkets;--arm-live-venue;--signer-endpoint;/run/velocita/signer.sock")
        string(REPLACE "|" ";" velocita_refusal_parts "${velocita_refusal}")
        list(GET velocita_refusal_parts 0 velocita_refusal_name)
        list(GET velocita_refusal_parts 1 velocita_refusal_expect)
        list(SUBLIST velocita_refusal_parts 2 -1 velocita_refusal_args)
        string(REPLACE ";" "," velocita_refusal_args "${velocita_refusal_args}")
        add_test(NAME v2-gateway-truemarkets-refuses-${velocita_refusal_name}
            COMMAND ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:velocita-v2-gateway>
                    "-DARGS=${velocita_refusal_args}" "-DEXPECT=${velocita_refusal_expect}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gateway_refusal.cmake)
    endforeach()

    # Paper-state refusals: decided from the arguments and the file alone, before
    # any key, Aeron directory or network; a refused file is never rewritten.
    set(velocita_paper_garbage "${CMAKE_CURRENT_BINARY_DIR}/paper-state-garbage.bin")
    foreach(velocita_usage IN ITEMS
            "no-state-file|--venue paper needs --paper-state-file||--venue;paper"
            "base-lots|replaced by the persisted paper state||--venue;paper;--paper-base-lots;25"
            "missing-file|the first start needs --paper-state-init||--venue;paper;--paper-state-file;${CMAKE_CURRENT_BINARY_DIR}/absent-paper.state"
            "corrupt-file|not a paper state file|1|--venue;paper;--paper-state-file;${velocita_paper_garbage}"
            "init-over-file|never replace a file|1|--venue;paper;--paper-state-file;${velocita_paper_garbage};--paper-state-init;--paper-quote-usd;25"
            "quote-on-restart|applies only to the first start||--venue;paper;--paper-state-file;${velocita_paper_garbage};--paper-quote-usd;3"
            "adopt-without-cost|needs --paper-adopt-cost-micros||--venue;paper;--paper-state-file;${velocita_paper_garbage};--paper-adopt-position;--paper-quote-usd;25")
        string(REPLACE "|" ";" velocita_usage_parts "${velocita_usage}")
        list(GET velocita_usage_parts 0 velocita_usage_name)
        list(GET velocita_usage_parts 1 velocita_usage_expect)
        list(GET velocita_usage_parts 2 velocita_usage_garbage)
        list(SUBLIST velocita_usage_parts 3 -1 velocita_usage_args)
        string(REPLACE ";" "," velocita_usage_args "${velocita_usage_args}")
        set(velocita_usage_garbage_arg "")
        if(velocita_usage_garbage)
            set(velocita_usage_garbage_arg "-DGARBAGE_FILE=${velocita_paper_garbage}")
        endif()
        add_test(NAME v2-gateway-paper-state-refuses-${velocita_usage_name}
            COMMAND ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:velocita-v2-gateway>
                    "-DARGS=${velocita_usage_args}" "-DEXPECT=${velocita_usage_expect}" ${velocita_usage_garbage_arg}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gateway_usage.cmake)
        set_tests_properties(v2-gateway-paper-state-refuses-${velocita_usage_name} PROPERTIES RESOURCE_LOCK paper-garbage)
    endforeach()

    add_executable(v2-gateway-protocol-tests tests/v2_gateway_protocol_tests.cpp)
    target_link_libraries(v2-gateway-protocol-tests PRIVATE velocita_v2_gateway_loop)
    target_compile_options(v2-gateway-protocol-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-gateway-protocol-tests COMMAND v2-gateway-protocol-tests)

    add_executable(v2-gateway-node-tests tests/v2_gateway_node_tests.cpp)
    target_link_libraries(v2-gateway-node-tests PRIVATE velocita_v2_gateway_loop velocita_v2_paper_venue Threads::Threads)
    target_compile_options(v2-gateway-node-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-gateway-node-tests COMMAND v2-gateway-node-tests)
    set_tests_properties(v2-gateway-node-tests PROPERTIES TIMEOUT 600)

    add_executable(v2-node-tests tests/v2_node_tests.cpp)
    target_link_libraries(v2-node-tests PRIVATE velocita_v2_node Threads::Threads)
    target_compile_options(v2-node-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-tests COMMAND v2-node-tests)
    set_tests_properties(v2-node-tests PROPERTIES TIMEOUT 180)

    add_executable(v2-node-files-tests tests/v2_node_files_tests.cpp)
    target_link_libraries(v2-node-files-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-node-files-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-files-tests COMMAND v2-node-files-tests)

    add_executable(v2-node-health-tests tests/v2_node_health_tests.cpp)
    target_link_libraries(v2-node-health-tests PRIVATE velocita_v2_node Threads::Threads)
    target_compile_options(v2-node-health-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-health-tests COMMAND v2-node-health-tests)

    # M2 exit: real node/client/status processes over loopback UDP.
    add_executable(v2-node-process-tests tests/v2_node_process_tests.cpp)
    target_link_libraries(v2-node-process-tests PRIVATE velocita_v2_node Threads::Threads)
    target_compile_options(v2-node-process-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_dependencies(v2-node-process-tests velocita-v2-node velocita-v2-client velocita-v2-status)
    add_test(NAME v2-node-process-tests COMMAND v2-node-process-tests
        $<TARGET_FILE:velocita-v2-node> $<TARGET_FILE:velocita-v2-client> $<TARGET_FILE:velocita-v2-status>
        $<TARGET_FILE:velocita-v2-shadow-submitter>)
    set_tests_properties(v2-node-process-tests PROPERTIES TIMEOUT 300)

    # The Mac-side secrets tool (Keychain + SSH are faked; no real secret is used).
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME v2-secrets-presence-tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests/test_secrets_presence.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME v2-observability-layout-tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests/test_observability_layout.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Alert rule unit tests, only where promtool is installed (not on the Mac by default).
    find_program(VELOCITA_PROMTOOL promtool)
    if(VELOCITA_PROMTOOL)
        add_test(NAME v2-observability-rule-tests
            COMMAND ${VELOCITA_PROMTOOL} test rules velocita-v2-alerts.test.yml
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deploy/v2/observability/rule-tests)
    endif()

    add_test(NAME v2-secrets-tool-tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests/test_secrets_tool.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # Keeps the storage benchmark runnable: it silently rotted once (M3 finding 11).
    add_test(NAME v2-storage-benchmark-smoke-tests COMMAND v2-storage-benchmark)
    set_tests_properties(v2-storage-benchmark-smoke-tests PROPERTIES TIMEOUT 120)

    add_executable(v2-node-metrics-text-tests tests/v2_node_metrics_text_tests.cpp)
    target_link_libraries(v2-node-metrics-text-tests PRIVATE velocita_v2_node)
    target_compile_options(v2-node-metrics-text-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-node-metrics-text-tests COMMAND v2-node-metrics-text-tests)

    add_executable(v2-two-voter-catchup-tests tests/v2_two_voter_catchup_tests.cpp)
    target_link_libraries(v2-two-voter-catchup-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-two-voter-catchup-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-two-voter-catchup-tests COMMAND v2-two-voter-catchup-tests)
    set_tests_properties(v2-two-voter-catchup-tests PROPERTIES TIMEOUT 120)

    add_executable(v2-archive-destination-tests tests/v2_archive_destination_tests.cpp)
    target_link_libraries(v2-archive-destination-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-archive-destination-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-archive-destination-tests COMMAND v2-archive-destination-tests)

    add_executable(v2-archive-live-verification-tests tests/v2_archive_live_verification_tests.cpp)
    target_link_libraries(v2-archive-live-verification-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-archive-live-verification-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-archive-live-verification-tests COMMAND v2-archive-live-verification-tests)

    add_executable(v2-snapshot-install-ack-contract-tests tests/v2_snapshot_install_ack_contract_tests.cpp)
    target_link_libraries(v2-snapshot-install-ack-contract-tests PRIVATE willemt_raft)
    target_compile_options(v2-snapshot-install-ack-contract-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-snapshot-install-ack-contract-tests COMMAND v2-snapshot-install-ack-contract-tests)

    add_executable(v2-snapshot-install-results-contract-tests tests/v2_snapshot_install_results_contract_tests.cpp)
    target_link_libraries(v2-snapshot-install-results-contract-tests PRIVATE velocita_v2_replication)
    target_compile_options(v2-snapshot-install-results-contract-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-snapshot-install-results-contract-tests COMMAND v2-snapshot-install-results-contract-tests)

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
