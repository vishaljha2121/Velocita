add_library(velocita_tape_core
    src/tape_frame.cpp
    src/tape_entry.cpp
    src/tape_merkle.cpp
    src/tape_reducer.cpp)
target_include_directories(velocita_tape_core PUBLIC include)
target_link_libraries(velocita_tape_core PUBLIC velocita_v2_contracts)
target_compile_options(velocita_tape_core PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(tape-log-apply tests/tape_log_apply.cpp)
    target_link_libraries(tape-log-apply PRIVATE velocita_tape_core)
    target_compile_options(tape-log-apply PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tape-core-tests
        COMMAND ${CMAKE_COMMAND} -E env TAPE_LOG_APPLY=$<TARGET_FILE:tape-log-apply>
            ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tape_core.py)
    set_tests_properties(tape-core-tests PROPERTIES TIMEOUT 300)
endif()
