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
    src/tape_voter.cpp src/tape_head_file.cpp)
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

add_executable(velocita-tape-exporter src/tape_exporter_main.cpp)
target_compile_options(velocita-tape-exporter PRIVATE -Wall -Wextra -Wpedantic)

add_executable(velocita-tape-verify src/tape_verify_main.cpp)
target_link_libraries(velocita-tape-verify PRIVATE velocita_tape_voter)
target_compile_options(velocita-tape-verify PRIVATE -Wall -Wextra -Wpedantic)

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
    set(VELOCITA_TAPE_FIXTURE_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH "Python with the websockets library, for recorder tests")
    option(VELOCITA_TAPE_REQUIRE_FIXTURE "Fail, rather than skip, the recorder tests when the fixture cannot run" ON)
    add_test(NAME tape-recorder-tests
        COMMAND ${CMAKE_COMMAND} -E env TAPE_RECORDER=$<TARGET_FILE:velocita-tape-recorder>
            TAPE_FIXTURE_PYTHON=${VELOCITA_TAPE_FIXTURE_PYTHON}
            TAPE_REQUIRE_FIXTURE=$<BOOL:${VELOCITA_TAPE_REQUIRE_FIXTURE}>
            ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_recorder.py)
    set_tests_properties(tape-recorder-tests PROPERTIES TIMEOUT 300)
    add_test(NAME tape-reducer-is-pure
        COMMAND ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:velocita_tape_core> -DUNDEFINED_ONLY=ON
            -DCHECK=ATT-6 "-DFORBIDDEN=^(clock_gettime|gettimeofday|time|mach_absolute_time|open|openat|fopen|read|write|close|socket|connect|recv|send|pthread_.*|getentropy|rand|srand|.*chrono.*clock.*now.*)$"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_tape_symbols.cmake)
    foreach(tape_binary IN ITEMS velocita-tape-voter velocita-tape-recorder velocita-tape-verify velocita-tape-keys)
        add_test(NAME tape-never-trades-${tape_binary}
            COMMAND ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:${tape_binary}> -DCHECK=X-1
                "-DFORBIDDEN=(N8velocita2v2[0-9]+(swap|truemarkets|gateway|signer|paper|venue|kernel|Swap|TrueMarkets|Gateway|Signer|Paper|Venue|Kernel)|^curl_)"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_tape_symbols.cmake)
    endforeach()
    add_test(NAME tape-exporter-links-no-tape-code
        COMMAND ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:velocita-tape-exporter> -DCHECK=MET-1
            "-DFORBIDDEN=velocita" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_tape_symbols.cmake)
    add_test(NAME tape-watchdog-tests COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_watchdog.py)
    add_test(NAME tape-rotate-tests COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_rotate.py)
    add_test(NAME tape-report-tests
        COMMAND ${CMAKE_COMMAND} -E env TAPE_VERIFY=$<TARGET_FILE:velocita-tape-verify> TAPE_LOG_APPLY=$<TARGET_FILE:tape-log-apply>
            TAPE_KEYS=$<TARGET_FILE:velocita-tape-keys>
            ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_report.py)
    set_tests_properties(tape-report-tests PROPERTIES TIMEOUT 300)
    add_test(NAME tape-proof-tests
        COMMAND ${CMAKE_COMMAND} -E env TAPE_VERIFY=$<TARGET_FILE:velocita-tape-verify> TAPE_LOG_APPLY=$<TARGET_FILE:tape-log-apply>
            TAPE_KEYS=$<TARGET_FILE:velocita-tape-keys>
            ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_proof.py)
    set_tests_properties(tape-proof-tests PROPERTIES TIMEOUT 300)
    set_tests_properties(tape-rotate-tests PROPERTIES TIMEOUT 120)
    set_tests_properties(tape-watchdog-tests PROPERTIES TIMEOUT 120)
    add_executable(tape-submitter-tests tests/tape_submitter_tests.cpp)
    target_link_libraries(tape-submitter-tests PRIVATE velocita_tape_recorder)
    target_compile_options(tape-submitter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tape-submitter-tests COMMAND tape-submitter-tests)
    add_executable(tape-voter-tests tests/tape_voter_tests.cpp)
    target_link_libraries(tape-voter-tests PRIVATE velocita_tape_voter)
    target_compile_options(tape-voter-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tape-voter-tests COMMAND tape-voter-tests)
    set_tests_properties(tape-voter-tests PROPERTIES TIMEOUT 300)
endif()
