if(TARGET velocita_v2_swap_runtime)
    add_executable(v2-swap-continuous-drill tests/v2_swap_continuous_drill.cpp)
    target_compile_options(v2-swap-continuous-drill PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(v2-swap-continuous-drill PRIVATE velocita_v2_swap_runtime
        velocita_v2_swap_paper_gateway velocita_v2_gateway_loop velocita_v2_paper_venue
        Threads::Threads OpenSSL::Crypto)
    if(TARGET velocita_v2_resting_paper_venue)
        target_link_libraries(v2-swap-continuous-drill PRIVATE velocita_v2_resting_paper_venue)
    endif()
    add_test(NAME v2-swap-continuous-drill COMMAND v2-swap-continuous-drill)
    set_tests_properties(v2-swap-continuous-drill PROPERTIES TIMEOUT 240)
endif()
