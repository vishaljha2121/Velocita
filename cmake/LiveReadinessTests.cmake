set(velocita_readiness_root "${CMAKE_CURRENT_LIST_DIR}/..")

foreach(solana_check IN ITEMS resolved_accounts instruction_decode inspection_pipeline)
    string(REPLACE "_" "-" solana_target "v2-solana-${solana_check}-tests")
    add_executable(${solana_target}
        "${velocita_readiness_root}/tests/v2_solana_${solana_check}_tests.cpp")
    if(NOT solana_check STREQUAL "instruction_decode")
        target_sources(${solana_target} PRIVATE
            "${velocita_readiness_root}/src/v2_solana_resolved_accounts.cpp"
            "${velocita_readiness_root}/src/v2_solana_payload_view.cpp"
            "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
            "${velocita_readiness_root}/src/v2_digest.cpp")
    endif()
    if(NOT solana_check STREQUAL "resolved_accounts")
        target_sources(${solana_target} PRIVATE
            "${velocita_readiness_root}/src/v2_solana_instruction_decode.cpp")
    endif()
    target_include_directories(${solana_target} PRIVATE "${velocita_readiness_root}/include")
    target_compile_definitions(${solana_target} PRIVATE
        VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
    target_compile_features(${solana_target} PRIVATE cxx_std_20)
    target_compile_options(${solana_target} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${solana_target} COMMAND ${solana_target})
endforeach()

add_executable(v2-spot-balance-tests
    "${velocita_readiness_root}/tests/v2_spot_balance_tests.cpp"
    "${velocita_readiness_root}/src/v2_spot_balance.cpp")
target_include_directories(v2-spot-balance-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-spot-balance-tests PRIVATE
    VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-spot-balance-tests PRIVATE cxx_std_20)
target_compile_options(v2-spot-balance-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-spot-balance-tests COMMAND v2-spot-balance-tests)

add_executable(v2-swap-balance-tests
    "${velocita_readiness_root}/tests/v2_swap_balance_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_balance.cpp"
    "${velocita_readiness_root}/src/v2_spot_balance.cpp"
    "${velocita_readiness_root}/src/v2_swap.cpp"
    "${velocita_readiness_root}/src/v2_swap_codec.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-balance-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-balance-tests PRIVATE
    VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-swap-balance-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-balance-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-balance-tests COMMAND v2-swap-balance-tests)

foreach(swap_target IN ITEMS v2-swap-paper-tests v2-swap-paper-drill)
    if(swap_target STREQUAL "v2-swap-paper-tests")
        set(swap_main tests/v2_swap_paper_tests.cpp)
    else()
        set(swap_main tests/v2_swap_paper_drill.cpp)
    endif()
    add_executable(${swap_target}
        "${velocita_readiness_root}/${swap_main}"
        "${velocita_readiness_root}/src/v2_swap_paper.cpp"
        "${velocita_readiness_root}/src/v2_swap.cpp"
        "${velocita_readiness_root}/src/v2_swap_codec.cpp"
        "${velocita_readiness_root}/src/v2_swap_paper_codec.cpp"
        "${velocita_readiness_root}/src/v2_digest.cpp")
    target_include_directories(${swap_target} PRIVATE "${velocita_readiness_root}/include")
    target_compile_definitions(${swap_target} PRIVATE
        VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
    target_compile_features(${swap_target} PRIVATE cxx_std_20)
    target_compile_options(${swap_target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
add_test(NAME v2-swap-paper-tests COMMAND v2-swap-paper-tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME v2-swap-paper-process-drill
    COMMAND "${CMAKE_COMMAND}" -E env
        "VELOCITA_SWAP_PAPER_DRILL=$<TARGET_FILE:v2-swap-paper-drill>"
        "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_swap_paper_drill.py")
set_tests_properties(v2-swap-paper-process-drill PROPERTIES TIMEOUT 30)
add_test(NAME v2-readiness-build-boundary
    COMMAND "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_readiness_build_boundary.py")
add_test(NAME v2-sol-limit-probe-offline
    COMMAND "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_tm_sol_limit_contract_probe.py")

option(VELOCITA_TEST_HTTPS "Build local-fixture HTTPS tests; does not enable a live transport" OFF)
if(VELOCITA_TEST_HTTPS)
    find_package(CURL 7.85 REQUIRED)
    find_program(VELOCITA_TEST_OPENSSL NAMES openssl REQUIRED)
    add_executable(v2-https-read-probe
        "${velocita_readiness_root}/tests/v2_https_read_probe.cpp"
        "${velocita_readiness_root}/src/v2_https_read.cpp")
    target_include_directories(v2-https-read-probe PRIVATE "${velocita_readiness_root}/include")
    target_compile_features(v2-https-read-probe PRIVATE cxx_std_20)
    target_compile_options(v2-https-read-probe PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-https-read-probe PRIVATE CURL::libcurl)
    add_test(NAME v2-https-read-local-fixtures
        COMMAND "${CMAKE_COMMAND}" -E env
            "VELOCITA_HTTPS_PROBE=$<TARGET_FILE:v2-https-read-probe>"
            "VELOCITA_TEST_OPENSSL=${VELOCITA_TEST_OPENSSL}"
            "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_https_read.py")
    set_tests_properties(v2-https-read-local-fixtures PROPERTIES TIMEOUT 30)
endif()

add_executable(v2-swap-reconcile-tests
    "${velocita_readiness_root}/tests/v2_swap_reconcile_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_reconcile.cpp"
    "${velocita_readiness_root}/src/v2_swap_balance.cpp"
    "${velocita_readiness_root}/src/v2_spot_balance.cpp"
    "${velocita_readiness_root}/src/v2_swap.cpp"
    "${velocita_readiness_root}/src/v2_swap_codec.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-reconcile-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-reconcile-tests PRIVATE
    VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-swap-reconcile-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-reconcile-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-reconcile-tests COMMAND v2-swap-reconcile-tests)
add_test(NAME v2-jupiter-idl-reader-offline
    COMMAND "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_read_jupiter_idl.py")

option(VELOCITA_TEST_SWAP_HTTPS "Build isolated swap HTTPS fixtures; no production transport" OFF)
if(VELOCITA_TEST_SWAP_HTTPS)
    find_package(CURL 7.85 REQUIRED)
    find_program(VELOCITA_TEST_OPENSSL NAMES openssl REQUIRED)
    add_executable(v2-swap-https-probe
        "${velocita_readiness_root}/tests/v2_swap_https_probe.cpp"
        "${velocita_readiness_root}/src/v2_swap_https.cpp")
    target_include_directories(v2-swap-https-probe PRIVATE "${velocita_readiness_root}/include")
    target_compile_definitions(v2-swap-https-probe PRIVATE VELOCITA_SWAP_HTTPS_TEST_ONLY=1)
    target_compile_features(v2-swap-https-probe PRIVATE cxx_std_20)
    target_compile_options(v2-swap-https-probe PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-https-probe PRIVATE CURL::libcurl)
    add_test(NAME v2-swap-https-local-fixtures
        COMMAND "${CMAKE_COMMAND}" -E env
            "VELOCITA_SWAP_HTTPS_PROBE=$<TARGET_FILE:v2-swap-https-probe>"
            "VELOCITA_TEST_OPENSSL=${VELOCITA_TEST_OPENSSL}"
            "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_swap_https.py")
    set_tests_properties(v2-swap-https-local-fixtures PROPERTIES TIMEOUT 60)
endif()

add_executable(v2-jupiter-route-decode-tests
    "${velocita_readiness_root}/tests/v2_jupiter_route_decode_tests.cpp"
    "${velocita_readiness_root}/src/v2_jupiter_route_decode.cpp")
target_include_directories(v2-jupiter-route-decode-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-jupiter-route-decode-tests PRIVATE cxx_std_20)
target_compile_options(v2-jupiter-route-decode-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-jupiter-route-decode-tests COMMAND v2-jupiter-route-decode-tests)

add_executable(v2-swap-owner-accounting-tests
    "${velocita_readiness_root}/tests/v2_swap_owner_accounting_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_owner_accounting.cpp")
target_include_directories(v2-swap-owner-accounting-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-owner-accounting-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-swap-owner-accounting-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-owner-accounting-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-owner-accounting-tests COMMAND v2-swap-owner-accounting-tests)
add_test(NAME v2-owner-settlement-reader-offline COMMAND "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_read_owner_settlement.py")
add_test(NAME v2-native-transport-qualification-gates COMMAND "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_qualify_swap_transport.py")

option(VELOCITA_TEST_SWAP_GATEWAY_TLS "Build isolated authenticated reconciliation and gateway TLS fixtures" OFF)
if(VELOCITA_TEST_SWAP_GATEWAY_TLS)
    if(NOT TARGET velocita_v2_node)
        message(FATAL_ERROR "Swap gateway TLS fixtures require the root project node libraries")
    endif()
    find_package(CURL 7.85 REQUIRED)
    find_program(VELOCITA_TEST_OPENSSL NAMES openssl REQUIRED)
    foreach(swap_tls_target IN ITEMS v2-swap-reconcile-session-tests v2-swap-gateway-tls-drill v2-swap-reconcile-store-tests)
        if(swap_tls_target STREQUAL "v2-swap-reconcile-session-tests")
            set(swap_tls_main tests/v2_swap_reconcile_session_tests.cpp)
        elseif(swap_tls_target STREQUAL "v2-swap-reconcile-store-tests")
            set(swap_tls_main tests/v2_swap_reconcile_store_tests.cpp)
        else()
            set(swap_tls_main tests/v2_gateway_node_tests.cpp)
        endif()
        add_executable(${swap_tls_target} "${velocita_readiness_root}/${swap_tls_main}"
            "${velocita_readiness_root}/src/v2_swap_reconcile_session.cpp"
            "${velocita_readiness_root}/src/v2_swap_https.cpp"
            "${velocita_readiness_root}/src/v2_swap_reconcile.cpp"
            "${velocita_readiness_root}/src/v2_swap_balance.cpp"
            "${velocita_readiness_root}/src/v2_spot_balance.cpp"
            "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp")
        target_compile_definitions(${swap_tls_target} PRIVATE VELOCITA_SWAP_RECONCILE_SESSION_TEST_ONLY=1
            VELOCITA_SWAP_HTTPS_TEST_ONLY=1 VELOCITA_SWAP_TLS_DRILL=1)
        target_compile_options(${swap_tls_target} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(${swap_tls_target} PRIVATE velocita_v2_swap_paper_gateway velocita_v2_gateway_loop
            velocita_v2_paper_venue CURL::libcurl Threads::Threads)
    endforeach()
    add_test(NAME v2-swap-reconcile-session-tests COMMAND v2-swap-reconcile-session-tests)
    add_test(NAME v2-swap-reconcile-store-tests COMMAND v2-swap-reconcile-store-tests)
    add_test(NAME v2-swap-gateway-tls-drill COMMAND "${CMAKE_COMMAND}" -E env
        "VELOCITA_SWAP_GATEWAY_TLS_DRILL=$<TARGET_FILE:v2-swap-gateway-tls-drill>"
        "VELOCITA_TEST_OPENSSL=${VELOCITA_TEST_OPENSSL}" "${Python3_EXECUTABLE}" -B
        "${velocita_readiness_root}/tests/test_swap_gateway_tls_drill.py")
    set_tests_properties(v2-swap-gateway-tls-drill PROPERTIES TIMEOUT 75)
endif()

add_executable(v2-swap-transaction-inspection-tests
    "${velocita_readiness_root}/tests/v2_swap_transaction_inspection_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_transaction_inspection.cpp"
    "${velocita_readiness_root}/src/v2_solana_payload_view.cpp"
    "${velocita_readiness_root}/src/v2_solana_resolved_accounts.cpp"
    "${velocita_readiness_root}/src/v2_solana_instruction_decode.cpp"
    "${velocita_readiness_root}/src/v2_jupiter_route_decode.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-transaction-inspection-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-transaction-inspection-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_INSPECTION_TEST_ONLY=1)
target_compile_features(v2-swap-transaction-inspection-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-transaction-inspection-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-transaction-inspection-tests COMMAND v2-swap-transaction-inspection-tests)

add_executable(v2-swap-submission-store-tests
    "${velocita_readiness_root}/tests/v2_swap_submission_store_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-submission-store-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-submission-store-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-swap-submission-store-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-submission-store-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-submission-store-tests COMMAND v2-swap-submission-store-tests)

if(TARGET velocita_v2_node)
    add_executable(v2-swap-authority-tests "${velocita_readiness_root}/tests/v2_swap_authority_tests.cpp")
    target_link_libraries(v2-swap-authority-tests PRIVATE velocita_v2_node)
    add_test(NAME v2-swap-authority-tests COMMAND v2-swap-authority-tests)
    add_executable(v2-swap-authority-storage-tests "${velocita_readiness_root}/tests/v2_swap_authority_storage_tests.cpp")
    target_link_libraries(v2-swap-authority-storage-tests PRIVATE velocita_v2_node)
    add_test(NAME v2-swap-authority-storage-tests COMMAND v2-swap-authority-storage-tests)
    add_executable(v2-swap-submission-resolution-tests
        "${velocita_readiness_root}/tests/v2_swap_submission_resolution_tests.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_resolution.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_store.cpp")
    target_compile_definitions(v2-swap-submission-resolution-tests PRIVATE VELOCITA_SWAP_SUBMISSION_RESOLUTION_TEST_ONLY=1)
    target_compile_options(v2-swap-submission-resolution-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-submission-resolution-tests PRIVATE velocita_v2_node)
    add_test(NAME v2-swap-submission-resolution-tests COMMAND v2-swap-submission-resolution-tests)
    add_executable(v2-swap-submission-session-tests
        "${velocita_readiness_root}/tests/v2_swap_submission_session_tests.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_session.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_store.cpp")
    target_compile_definitions(v2-swap-submission-session-tests PRIVATE VELOCITA_SWAP_SUBMISSION_SESSION_TEST_ONLY=1)
    target_compile_options(v2-swap-submission-session-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-submission-session-tests PRIVATE velocita_v2_node)
    add_test(NAME v2-swap-submission-session-tests COMMAND v2-swap-submission-session-tests)
    add_executable(v2-swap-submission-gateway-drill
        "${velocita_readiness_root}/tests/v2_gateway_node_tests.cpp"
        "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
        "${velocita_readiness_root}/src/v2_swap_prepare_session.cpp"
        "${velocita_readiness_root}/src/v2_swap_order_status.cpp"
        "${velocita_readiness_root}/src/v2_truemarkets_rest.cpp"
        "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
        "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
        "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_resolution.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_session.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_store.cpp")
    target_compile_definitions(v2-swap-submission-gateway-drill PRIVATE
        VELOCITA_SWAP_PREPARE_STORE_TEST_ONLY=1 VELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY=1 VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY=1
        VELOCITA_SWAP_SUBMISSION_SESSION_TEST_ONLY=1 VELOCITA_SWAP_SUBMISSION_DRILL=1 VELOCITA_SWAP_SUBMISSION_RESOLUTION_TEST_ONLY=1)
    target_compile_options(v2-swap-submission-gateway-drill PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-submission-gateway-drill PRIVATE velocita_v2_swap_paper_gateway
        velocita_v2_gateway_loop velocita_v2_paper_venue Threads::Threads)
    add_test(NAME v2-swap-submission-gateway-drill COMMAND "${CMAKE_COMMAND}" -E env
        GATEWAY_TEST=swap_submission_lost_reply_leader_loss_restart $<TARGET_FILE:v2-swap-submission-gateway-drill>)
    set_tests_properties(v2-swap-submission-gateway-drill PROPERTIES TIMEOUT 60)
    add_test(NAME v2-swap-authority-quorum-drill COMMAND "${CMAKE_COMMAND}" -E env
        GATEWAY_TEST=swap_authority_without_quorum_or_after_kill $<TARGET_FILE:v2-swap-submission-gateway-drill>)
    set_tests_properties(v2-swap-authority-quorum-drill PROPERTIES TIMEOUT 30)
endif()

add_executable(v2-swap-signing-stamp-tests
    "${velocita_readiness_root}/tests/v2_swap_signing_stamp_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
target_include_directories(v2-swap-signing-stamp-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-signing-stamp-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-signing-stamp-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-signing-stamp-tests COMMAND v2-swap-signing-stamp-tests)

add_executable(v2-swap-rest-codec-tests
    "${velocita_readiness_root}/tests/v2_swap_rest_codec_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-rest-codec-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-rest-codec-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-swap-rest-codec-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-rest-codec-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-rest-codec-tests COMMAND v2-swap-rest-codec-tests)

add_executable(v2-swap-prepare-store-tests
    "${velocita_readiness_root}/tests/v2_swap_prepare_store_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-prepare-store-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-prepare-store-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-prepare-store-tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(v2-swap-prepare-store-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_PREPARE_STORE_TEST_ONLY=1)
add_test(NAME v2-swap-prepare-store-tests COMMAND v2-swap-prepare-store-tests)

if(TARGET velocita_v2_node)
add_executable(v2-swap-prepare-session-tests
    "${velocita_readiness_root}/tests/v2_swap_prepare_session_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp")
target_include_directories(v2-swap-prepare-session-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-prepare-session-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-prepare-session-tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(v2-swap-prepare-session-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_PREPARE_STORE_TEST_ONLY=1 VELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY=1)
target_link_libraries(v2-swap-prepare-session-tests PRIVATE velocita_v2_node)
add_test(NAME v2-swap-prepare-session-tests COMMAND v2-swap-prepare-session-tests)
option(VELOCITA_TEST_SWAP_SIGNER "Build isolated synthetic signer tests; no live signing" OFF)
if(VELOCITA_TEST_SWAP_SIGNER)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
target_sources(v2-swap-submission-gateway-drill PRIVATE "${velocita_readiness_root}/src/v2_swap_signer_session.cpp")
target_compile_definitions(v2-swap-submission-gateway-drill PRIVATE VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)
target_link_libraries(v2-swap-submission-gateway-drill PRIVATE OpenSSL::Crypto)
add_executable(v2-swap-signer-session-tests
    "${velocita_readiness_root}/tests/v2_swap_signer_session_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
target_include_directories(v2-swap-signer-session-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-signer-session-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-signer-session-tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(v2-swap-signer-session-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)
target_link_libraries(v2-swap-signer-session-tests PRIVATE velocita_v2_node OpenSSL::Crypto)
add_test(NAME v2-swap-signer-session-tests COMMAND v2-swap-signer-session-tests)
add_executable(v2-swap-signer-production-gate
    "${velocita_readiness_root}/tests/v2_swap_signer_session_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
target_include_directories(v2-swap-signer-production-gate PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-signer-production-gate PRIVATE cxx_std_20)
target_compile_options(v2-swap-signer-production-gate PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(v2-swap-signer-production-gate PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} )
target_link_libraries(v2-swap-signer-production-gate PRIVATE velocita_v2_node OpenSSL::Crypto)
add_test(NAME v2-swap-signer-production-gate COMMAND v2-swap-signer-production-gate production_gate)
add_executable(v2-swap-signer-protocol-tests
    "${velocita_readiness_root}/tests/v2_swap_signer_protocol_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_protocol.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
target_link_libraries(v2-swap-signer-protocol-tests PRIVATE velocita_v2_node)
add_test(NAME v2-swap-signer-protocol-tests COMMAND v2-swap-signer-protocol-tests)
add_executable(v2-swap-signer-service-tests
    "${velocita_readiness_root}/tests/v2_swap_signer_service_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_protocol.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_service.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
target_compile_definitions(v2-swap-signer-service-tests PRIVATE VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)
target_link_libraries(v2-swap-signer-service-tests PRIVATE velocita_v2_node OpenSSL::Crypto)
add_test(NAME v2-swap-signer-service-tests COMMAND v2-swap-signer-service-tests)
set_tests_properties(v2-swap-signer-service-tests PROPERTIES TIMEOUT 30)
endif()
endif()
add_executable(v2-swap-order-status-tests
    "${velocita_readiness_root}/tests/v2_swap_order_status_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_order_status.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_rest.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-order-status-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-order-status-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-order-status-tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(v2-swap-order-status-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_PREPARE_STORE_TEST_ONLY=1 VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY=1)
add_test(NAME v2-swap-order-status-tests COMMAND v2-swap-order-status-tests)

if(VELOCITA_TEST_SWAP_HTTPS)
add_executable(v2-swap-order-https-probe
    "${velocita_readiness_root}/tests/v2_swap_order_https_probe.cpp"
    "${velocita_readiness_root}/src/v2_swap_order_https.cpp"
    "${velocita_readiness_root}/src/v2_swap_https.cpp"
    "${velocita_readiness_root}/src/v2_swap_order_status.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_rest.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-order-https-probe PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-order-https-probe PRIVATE cxx_std_20)
target_compile_options(v2-swap-order-https-probe PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(v2-swap-order-https-probe PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_PREPARE_STORE_TEST_ONLY=1 VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY=1 VELOCITA_SWAP_HTTPS_TEST_ONLY=1)
target_link_libraries(v2-swap-order-https-probe PRIVATE CURL::libcurl)
add_test(NAME v2-swap-order-https-local-fixtures COMMAND "${CMAKE_COMMAND}" -E env
    "VELOCITA_SWAP_ORDER_HTTPS_PROBE=$<TARGET_FILE:v2-swap-order-https-probe>"
    "VELOCITA_TEST_OPENSSL=${VELOCITA_TEST_OPENSSL}" "${Python3_EXECUTABLE}" -B
    "${velocita_readiness_root}/tests/test_swap_order_https.py")
set_tests_properties(v2-swap-order-https-local-fixtures PROPERTIES TIMEOUT 45)
endif()


add_executable(v2-swap-settlement-evidence-tests
    "${velocita_readiness_root}/tests/v2_swap_settlement_evidence_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_settlement_evidence.cpp"
    "${velocita_readiness_root}/src/v2_solana_payload_view.cpp"
    "${velocita_readiness_root}/src/v2_solana_resolved_accounts.cpp"
    "${velocita_readiness_root}/src/v2_swap_owner_accounting.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-settlement-evidence-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-settlement-evidence-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_features(v2-swap-settlement-evidence-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-settlement-evidence-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-settlement-evidence-tests COMMAND v2-swap-settlement-evidence-tests)

# Unsigned create port qualification only; never linked into the release gateway.
if(VELOCITA_TEST_SWAP_HTTPS AND TARGET velocita_v2_node)
    add_executable(v2-swap-prepare-https-tests
        "${velocita_readiness_root}/tests/v2_swap_prepare_session_tests.cpp"
        "${velocita_readiness_root}/src/v2_swap_prepare_https.cpp"
        "${velocita_readiness_root}/src/v2_swap_prepare_session.cpp"
        "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
        "${velocita_readiness_root}/src/v2_swap_https.cpp"
        "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
        "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
        "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp")
    target_compile_definitions(v2-swap-prepare-https-tests PRIVATE
        VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY=1 VELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY=1
        VELOCITA_SWAP_HTTPS_TEST_ONLY=1)
    target_compile_options(v2-swap-prepare-https-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-prepare-https-tests PRIVATE velocita_v2_node CURL::libcurl)
    add_test(NAME v2-swap-prepare-https-local-fixtures COMMAND "${CMAKE_COMMAND}" -E env
        "VELOCITA_SWAP_PREPARE_HTTPS_TESTS=$<TARGET_FILE:v2-swap-prepare-https-tests>"
        "VELOCITA_TEST_OPENSSL=${VELOCITA_TEST_OPENSSL}"
        "${Python3_EXECUTABLE}" -B "${velocita_readiness_root}/tests/test_swap_prepare_https.py")
    set_tests_properties(v2-swap-prepare-https-local-fixtures PROPERTIES TIMEOUT 60)
endif()

if(TARGET v2-swap-signer-service-tests)
    add_executable(v2-swap-signing-connection-tests
        "${velocita_readiness_root}/tests/v2_swap_signing_connection_tests.cpp"
        "${velocita_readiness_root}/src/v2_swap_signing_connection.cpp"
        "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
        "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
        "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
        "${velocita_readiness_root}/src/v2_swap_signer_protocol.cpp"
        "${velocita_readiness_root}/src/v2_swap_signer_service.cpp"
        "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
        "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
        "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
    target_compile_definitions(v2-swap-signing-connection-tests PRIVATE VELOCITA_SWAP_SIGNING_CONNECTION_TEST_ONLY=1 VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)
    target_compile_options(v2-swap-signing-connection-tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-signing-connection-tests PRIVATE velocita_v2_node OpenSSL::Crypto)
    add_test(NAME v2-swap-signing-connection-tests COMMAND v2-swap-signing-connection-tests)
    set_tests_properties(v2-swap-signing-connection-tests PROPERTIES TIMEOUT 30)
endif()

add_executable(v2-swap-settlement-connection-tests
    "${velocita_readiness_root}/tests/v2_swap_settlement_evidence_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_settlement_connection.cpp"
    "${velocita_readiness_root}/src/v2_swap_settlement_evidence.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_solana_payload_view.cpp"
    "${velocita_readiness_root}/src/v2_solana_resolved_accounts.cpp"
    "${velocita_readiness_root}/src/v2_swap_owner_accounting.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_digest.cpp")
target_include_directories(v2-swap-settlement-connection-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_definitions(v2-swap-settlement-connection-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE} VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY=1)
target_compile_features(v2-swap-settlement-connection-tests PRIVATE cxx_std_20)
target_compile_options(v2-swap-settlement-connection-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-settlement-connection-tests COMMAND v2-swap-settlement-connection-tests)

if(TARGET v2-swap-signing-connection-tests)
add_executable(v2-swap-runtime-config-tests
    "${velocita_readiness_root}/tests/v2_swap_runtime_config_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_runtime_config.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_protocol.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_service.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp")
target_link_libraries(v2-swap-runtime-config-tests PRIVATE velocita_v2_node OpenSSL::Crypto)
add_test(NAME v2-swap-runtime-config-tests COMMAND v2-swap-runtime-config-tests)
set_tests_properties(v2-swap-runtime-config-tests PROPERTIES TIMEOUT 30)

add_executable(v2-swap-gateway-runtime-tests
    "${velocita_readiness_root}/tests/v2_swap_gateway_runtime_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_gateway_runtime.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_connection.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_protocol.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_service.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_resolution.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp")
target_compile_definitions(v2-swap-gateway-runtime-tests PRIVATE
    VELOCITA_SWAP_SIGNING_CONNECTION_TEST_ONLY=1 VELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY=1
    VELOCITA_SWAP_SUBMISSION_SESSION_TEST_ONLY=1 VELOCITA_SWAP_SUBMISSION_RESOLUTION_TEST_ONLY=1
    VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE=1)
target_compile_options(v2-swap-gateway-runtime-tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(v2-swap-gateway-runtime-tests PRIVATE velocita_v2_swap_paper_gateway
    velocita_v2_gateway_loop velocita_v2_paper_venue Threads::Threads OpenSSL::Crypto)
add_test(NAME v2-swap-gateway-runtime-tests COMMAND v2-swap-gateway-runtime-tests)
set_tests_properties(v2-swap-gateway-runtime-tests PROPERTIES TIMEOUT 120)
add_executable(v2-swap-process-drill
    "${velocita_readiness_root}/tests/v2_swap_process_drill.cpp"
    "${velocita_readiness_root}/src/v2_swap_gateway_runtime.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_connection.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_protocol.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_service.cpp"
    "${velocita_readiness_root}/src/v2_swap_signer_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_prepare_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_store.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_session.cpp"
    "${velocita_readiness_root}/src/v2_swap_submission_resolution.cpp"
    "${velocita_readiness_root}/src/v2_swap_rest_codec.cpp"
    "${velocita_readiness_root}/src/v2_swap_signing_stamp.cpp"
    "${velocita_readiness_root}/src/v2_truemarkets_wire.cpp")
target_compile_definitions(v2-swap-process-drill PRIVATE
    VELOCITA_SWAP_SIGNING_CONNECTION_TEST_ONLY=1 VELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY=1
    VELOCITA_SWAP_SUBMISSION_SESSION_TEST_ONLY=1 VELOCITA_SWAP_SUBMISSION_RESOLUTION_TEST_ONLY=1
    VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE=1 VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)
target_compile_options(v2-swap-process-drill PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(v2-swap-process-drill PRIVATE velocita_v2_swap_paper_gateway
    velocita_v2_gateway_loop velocita_v2_paper_venue Threads::Threads OpenSSL::Crypto)
add_test(NAME v2-swap-process-drill COMMAND v2-swap-process-drill)
set_tests_properties(v2-swap-process-drill PROPERTIES TIMEOUT 120)
endif()

if(TARGET velocita_v2_swap_runtime)
    add_executable(v2-swap-runtime-production-tests "${velocita_readiness_root}/tests/v2_swap_runtime_production_tests.cpp")
    target_link_libraries(v2-swap-runtime-production-tests PRIVATE velocita_v2_swap_runtime)
    target_compile_options(v2-swap-runtime-production-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME v2-swap-runtime-production-tests COMMAND v2-swap-runtime-production-tests)
endif()

if(TARGET velocita_v2_swap_runtime)
    add_executable(v2-swap-settlement-lifecycle-tests "${velocita_readiness_root}/tests/v2_swap_settlement_lifecycle_tests.cpp")
    target_link_libraries(v2-swap-settlement-lifecycle-tests PRIVATE velocita_v2_swap_runtime)
    add_test(NAME v2-swap-settlement-lifecycle-tests COMMAND v2-swap-settlement-lifecycle-tests)
    # The proof factory exists only in this isolated copy, never in the runtime library.
    get_target_property(swap_runtime_fixture_sources velocita_v2_swap_runtime SOURCES)
    list(TRANSFORM swap_runtime_fixture_sources PREPEND "${velocita_readiness_root}/")
    add_executable(v2-swap-settlement-admission-tests
        "${velocita_readiness_root}/tests/v2_swap_settlement_admission_tests.cpp" ${swap_runtime_fixture_sources})
    target_compile_definitions(v2-swap-settlement-admission-tests PRIVATE VELOCITA_SWAP_RUNTIME_WIRING=1 VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE=1 VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY=1)
    target_link_libraries(v2-swap-settlement-admission-tests PRIVATE velocita_v2_swap_recovery velocita_v2_node OpenSSL::Crypto CURL::libcurl)
    add_test(NAME v2-swap-settlement-admission-tests COMMAND v2-swap-settlement-admission-tests)
    add_executable(v2-swap-runtime-https-probe
        "${velocita_readiness_root}/tests/v2_swap_runtime_https_probe.cpp" ${swap_runtime_fixture_sources})
    target_compile_definitions(v2-swap-runtime-https-probe PRIVATE VELOCITA_SWAP_RUNTIME_WIRING=1 VELOCITA_SWAP_RUNTIME_HTTPS_TEST_ONLY=1)
    target_link_libraries(v2-swap-runtime-https-probe PRIVATE velocita_v2_swap_recovery velocita_v2_node OpenSSL::Crypto CURL::libcurl)
    add_test(NAME v2-swap-runtime-https-fixtures COMMAND "${CMAKE_COMMAND}" -E env
        "VELOCITA_SWAP_RUNTIME_HTTPS_PROBE=$<TARGET_FILE:v2-swap-runtime-https-probe>"
        "VELOCITA_TEST_OPENSSL=${VELOCITA_TEST_OPENSSL}" "${Python3_EXECUTABLE}" -B
        "${velocita_readiness_root}/tests/test_swap_runtime_https.py")
    set_tests_properties(v2-swap-runtime-https-fixtures PROPERTIES TIMEOUT 60)
endif()
if(TARGET velocita_v2_swap_runtime)
    add_test(NAME v2-swap-runtime-startup-gate COMMAND "${CMAKE_COMMAND}" -E env
        "VELOCITA_SWAP_RUNTIME_GATEWAY=$<TARGET_FILE:velocita-v2-gateway>" "${Python3_EXECUTABLE}" -B
        "${velocita_readiness_root}/tests/test_swap_runtime_gate.py")
endif()

if(TARGET velocita_v2_swap_runtime)
    add_executable(v2-swap-account-setup-tests "${velocita_readiness_root}/tests/v2_swap_account_setup_tests.cpp")
    target_link_libraries(v2-swap-account-setup-tests PRIVATE velocita_v2_swap_runtime)
    add_test(NAME v2-swap-account-setup-tests COMMAND v2-swap-account-setup-tests)
endif()

if(TARGET velocita_v2_swap_runtime)
    add_executable(v2-swap-failed-settlement-tests "${velocita_readiness_root}/tests/v2_swap_failed_settlement_tests.cpp")
    target_link_libraries(v2-swap-failed-settlement-tests PRIVATE velocita_v2_swap_runtime)
    add_test(NAME v2-swap-failed-settlement-tests COMMAND v2-swap-failed-settlement-tests)
endif()
if(TARGET velocita_v2_node)
    add_executable(v2-operational-recovery-tests "${velocita_readiness_root}/tests/v2_operational_recovery_tests.cpp")
    target_link_libraries(v2-operational-recovery-tests PRIVATE velocita_v2_node)
    add_test(NAME v2-operational-recovery-tests COMMAND v2-operational-recovery-tests)
endif()


# Accepted output-cost arithmetic has no signing or replicated-state authority.
add_executable(v2-swap-cost-policy-tests
    "${velocita_readiness_root}/tests/v2_swap_cost_policy_tests.cpp"
    "${velocita_readiness_root}/src/v2_swap_owner_accounting.cpp")
target_include_directories(v2-swap-cost-policy-tests PRIVATE "${velocita_readiness_root}/include")
target_compile_features(v2-swap-cost-policy-tests PRIVATE cxx_std_20)
target_compile_definitions(v2-swap-cost-policy-tests PRIVATE VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_options(v2-swap-cost-policy-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME v2-swap-cost-policy-tests COMMAND v2-swap-cost-policy-tests)
