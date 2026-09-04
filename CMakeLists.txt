cmake_minimum_required(VERSION 3.30)

project(velocita_raft VERSION 0.1.0 LANGUAGES C CXX)

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
    src/v2_digest.cpp
    src/v2_wire.cpp
    src/v2_payload.cpp
)
target_include_directories(velocita_v2_contracts PUBLIC include)
target_compile_definitions(velocita_v2_contracts PUBLIC
    VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_options(velocita_v2_contracts PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_kernel
    src/v2_kernel.cpp
    src/v2_kernel_codec.cpp
    src/v2_replay.cpp
    src/v2_state_snapshot.cpp
)
target_include_directories(velocita_v2_kernel PUBLIC include)
target_link_libraries(velocita_v2_kernel PUBLIC velocita_v2_contracts)
target_compile_options(velocita_v2_kernel PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_v2_evaluator
    src/v2_rule_evaluator.cpp
)
target_include_directories(velocita_v2_evaluator PUBLIC include)
target_link_libraries(velocita_v2_evaluator PUBLIC velocita_v2_contracts)
target_compile_options(velocita_v2_evaluator PRIVATE -Wall -Wextra -Wpedantic)

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

    add_executable(v2-rule-evaluator-tests tests/v2_rule_evaluator_tests.cpp)
    target_link_libraries(v2-rule-evaluator-tests PRIVATE velocita_v2_evaluator)
    target_compile_options(v2-rule-evaluator-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-rule-evaluator-tests COMMAND v2-rule-evaluator-tests)

    add_executable(v2-layout-tests tests/v2_layout_tests.cpp)
    target_link_libraries(v2-layout-tests PRIVATE
        velocita_v2_contracts velocita_v2_kernel velocita_v2_evaluator)
    target_compile_options(v2-layout-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-layout-tests COMMAND v2-layout-tests)

    add_executable(v2-layout-report src/v2_layout_report.cpp)
    target_link_libraries(v2-layout-report PRIVATE
        velocita_v2_contracts velocita_v2_kernel velocita_v2_evaluator)
    target_compile_options(v2-layout-report PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(v2-contract-benchmark src/v2_contract_benchmark.cpp)
    target_link_libraries(v2-contract-benchmark PRIVATE velocita_v2_contracts)
    target_compile_options(v2-contract-benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()
