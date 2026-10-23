add_library(velocita_v2_swap_transaction_verifier
    src/v2_swap_transaction_verifier.cpp
    src/v2_swap_transaction_review.cpp
    src/v2_solana_payload_view.cpp
    src/v2_solana_resolved_accounts.cpp
    src/v2_solana_instruction_decode.cpp
    src/v2_jupiter_route_decode.cpp
    src/v2_swap_owner_accounting.cpp
    src/v2_truemarkets_wire.cpp
    src/v2_digest.cpp)
target_include_directories(velocita_v2_swap_transaction_verifier PUBLIC include $<TARGET_PROPERTY:willemt_raft,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_features(velocita_v2_swap_transaction_verifier PUBLIC cxx_std_20)
target_compile_definitions(velocita_v2_swap_transaction_verifier PUBLIC VELOCITA_TARGET_CACHE_LINE_SIZE=${VELOCITA_TARGET_CACHE_LINE_SIZE})
target_compile_options(velocita_v2_swap_transaction_verifier PRIVATE -Wall -Wextra -Wpedantic)
