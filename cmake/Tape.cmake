add_library(velocita_tape_core
    src/tape_frame.cpp
    src/tape_entry.cpp
    src/tape_merkle.cpp
    src/tape_reducer.cpp)
target_include_directories(velocita_tape_core PUBLIC include)
target_link_libraries(velocita_tape_core PUBLIC velocita_v2_contracts)
target_compile_options(velocita_tape_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_tape_replica src/tape_wal.cpp src/tape_replica.cpp)
target_include_directories(velocita_tape_replica PUBLIC include)
target_link_libraries(velocita_tape_replica PUBLIC velocita_tape_core velocita_core)
target_compile_options(velocita_tape_replica PRIVATE -Wall -Wextra -Wpedantic)

find_package(OpenSSL 3 REQUIRED)
add_library(velocita_tape_voter src/tape_auth.cpp src/tape_config.cpp src/tape_signing.cpp src/tape_peer_codec.cpp
    src/tape_voter.cpp)
target_include_directories(velocita_tape_voter PUBLIC include)
target_link_libraries(velocita_tape_voter PUBLIC velocita_tape_replica OpenSSL::Crypto)
target_compile_options(velocita_tape_voter PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_tape_link src/tape_link.cpp)
target_include_directories(velocita_tape_link PUBLIC include)
target_link_libraries(velocita_tape_link PUBLIC velocita_tape_voter velocita_aeron)
target_compile_options(velocita_tape_link PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-tape-voter src/tape_voter_main.cpp)
target_link_libraries(velocita-tape-voter PRIVATE velocita_tape_link Threads::Threads)
target_compile_options(velocita-tape-voter PRIVATE -Wall -Wextra -Wpedantic)

add_library(velocita_tape_recorder src/tape_websocket.cpp src/tape_recorder.cpp)
target_include_directories(velocita_tape_recorder PUBLIC include)
target_link_libraries(velocita_tape_recorder PUBLIC velocita_tape_core OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_options(velocita_tape_recorder PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-tape-recorder src/tape_recorder_main.cpp)
target_link_libraries(velocita-tape-recorder PRIVATE velocita_tape_recorder velocita_tape_link)
target_compile_options(velocita-tape-recorder PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-tape-keys src/tape_keys_main.cpp)
target_link_libraries(velocita-tape-keys PRIVATE velocita_tape_voter)
target_compile_options(velocita-tape-keys PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(tape-log-apply tests/tape_log_apply.cpp)
    target_link_libraries(tape-log-apply PRIVATE velocita_tape_core)
    target_compile_options(tape-log-apply PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tape-core-tests
        COMMAND ${CMAKE_COMMAND} -E env TAPE_LOG_APPLY=$<TARGET_FILE:tape-log-apply>
            ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_core.py)
    set_tests_properties(tape-core-tests PROPERTIES TIMEOUT 300)
    add_executable(tape-replica-tests tests/tape_replica_tests.cpp)
    target_link_libraries(tape-replica-tests PRIVATE velocita_tape_replica)
    target_compile_options(tape-replica-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tape-replica-tests COMMAND tape-replica-tests)
    set_tests_properties(tape-replica-tests PROPERTIES TIMEOUT 300)
    add_executable(tape-voter-tests tests/tape_voter_tests.cpp)
    target_link_libraries(tape-voter-tests PRIVATE velocita_tape_voter)
    target_compile_options(tape-voter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tape-voter-tests COMMAND tape-voter-tests)
    set_tests_properties(tape-voter-tests PROPERTIES TIMEOUT 300)
endif()
