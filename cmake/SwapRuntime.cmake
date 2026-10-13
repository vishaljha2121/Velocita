# Opt-in executable composition. This does not admit a real transaction route.
option(VELOCITA_BUILD_SWAP_RUNTIME "Compile gated exact-input swap runtime into gateway" OFF)
if(VELOCITA_BUILD_SWAP_RUNTIME)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    find_package(CURL 7.85 REQUIRED)
    add_library(velocita_v2_swap_runtime
        src/v2_swap_gateway_runtime.cpp
        src/v2_swap_runtime_config.cpp
        src/v2_swap_runtime_production.cpp
        src/v2_swap_account_setup.cpp
        src/v2_swap_account_review.cpp
        src/v2_swap_account_history.cpp
        src/v2_swap_reconcile.cpp
        src/v2_swap_balance.cpp
        src/v2_spot_balance.cpp
        src/v2_swap_runtime_https.cpp
        src/v2_swap_prepare_store.cpp
        src/v2_swap_prepare_session.cpp
        src/v2_swap_prepare_https.cpp
        src/v2_swap_signing_connection.cpp
        src/v2_swap_signer_session.cpp
        src/v2_swap_signer_service.cpp
        src/v2_swap_signer_protocol.cpp
        src/v2_swap_signing_stamp.cpp
        src/v2_swap_submission_session.cpp
        src/v2_swap_submission_store.cpp
        src/v2_swap_submission_resolution.cpp
        src/v2_swap_order_status.cpp
        src/v2_swap_order_https.cpp
        src/v2_swap_settlement_connection.cpp
        src/v2_swap_settlement_evidence.cpp
        src/v2_swap_settlement_admission.cpp
        src/v2_swap_settlement_lifecycle.cpp
        src/v2_swap_owner_accounting.cpp
        src/v2_solana_instruction_decode.cpp
        src/v2_jupiter_route_decode.cpp
        src/v2_solana_payload_view.cpp
        src/v2_solana_resolved_accounts.cpp
        src/v2_swap_rest_codec.cpp
        src/v2_swap_https.cpp
        src/v2_truemarkets_wire.cpp
        src/v2_truemarkets_rest.cpp)
    target_compile_definitions(velocita_v2_swap_runtime PRIVATE VELOCITA_SWAP_RUNTIME_WIRING=1)
    target_compile_options(velocita_v2_swap_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(velocita_v2_swap_runtime PUBLIC velocita_v2_swap_recovery velocita_v2_node velocita_v2_swap_transaction_verifier OpenSSL::Crypto CURL::libcurl)
    target_link_libraries(velocita-v2-gateway PRIVATE velocita_v2_swap_runtime)
    target_compile_definitions(velocita-v2-gateway PRIVATE VELOCITA_SWAP_RUNTIME_WIRING=1)
endif()
