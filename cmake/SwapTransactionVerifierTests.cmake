add_executable(v2-swap-transaction-review-tests tests/v2_swap_transaction_review_tests.cpp)
target_link_libraries(v2-swap-transaction-review-tests PRIVATE velocita_v2_swap_transaction_verifier)
add_test(NAME v2-swap-transaction-review-tests COMMAND v2-swap-transaction-review-tests)
add_executable(v2-swap-transaction-verifier-tests tests/v2_swap_transaction_verifier_tests.cpp)
target_link_libraries(v2-swap-transaction-verifier-tests PRIVATE velocita_v2_swap_transaction_verifier)
add_test(NAME v2-swap-transaction-verifier-tests COMMAND v2-swap-transaction-verifier-tests)
if(TARGET v2-swap-signer-production-gate)
    add_executable(v2-swap-signer-verified-tests
        tests/v2_swap_signer_verified_tests.cpp
        src/v2_swap_signer_session.cpp
        src/v2_swap_submission_store.cpp
        src/v2_swap_signing_stamp.cpp)
    target_link_libraries(v2-swap-signer-verified-tests PRIVATE velocita_v2_node OpenSSL::Crypto)
    add_test(NAME v2-swap-signer-verified-tests COMMAND v2-swap-signer-verified-tests)
endif()
# Every test that compiles the session directly needs its pure verifier dependency.
get_property(verifier_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(verifier_target IN LISTS verifier_targets)
    get_target_property(verifier_sources ${verifier_target} SOURCES)
    foreach(verifier_source IN LISTS verifier_sources)
        if(verifier_source MATCHES "(^|/)v2_swap_signer_session\\.cpp$")
            target_link_libraries(${verifier_target} PRIVATE velocita_v2_swap_transaction_verifier)
            break()
        endif()
    endforeach()
endforeach()
