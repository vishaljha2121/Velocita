# cmake -DBINARY=<gateway> -DARGS=a,b,c -DEXPECT=<text> [-DGARBAGE_FILE=<path>] -P check_gateway_usage.cmake
#
# Runs the gateway with ARGS and requires the usage exit (64) with EXPECT in
# its diagnostic, before READY. GARBAGE_FILE, when given, is first written with
# bytes that are not a paper state file (and is left byte-identical after).

if(NOT BINARY OR NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "BINARY not found: ${BINARY}")
endif()
if(GARBAGE_FILE)
    file(WRITE "${GARBAGE_FILE}" "not a paper state file, 0123456789abcdef0123456789abcdef")
    file(SHA256 "${GARBAGE_FILE}" before)
endif()
string(REPLACE "," ";" arguments "${ARGS}")
execute_process(COMMAND "${BINARY}" ${arguments}
    RESULT_VARIABLE exit_code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
if(NOT exit_code EQUAL 64)
    message(FATAL_ERROR "expected exit 64, got '${exit_code}'\nstdout: ${out}\nstderr: ${err}")
endif()
string(FIND "${err}" "${EXPECT}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "expected '${EXPECT}', got stderr: ${err}")
endif()
if(out MATCHES "READY")
    message(FATAL_ERROR "the gateway reported READY: ${out}")
endif()
if(GARBAGE_FILE)
    file(SHA256 "${GARBAGE_FILE}" after)
    if(NOT before STREQUAL after)
        message(FATAL_ERROR "the refused paper state file was rewritten")
    endif()
    file(REMOVE "${GARBAGE_FILE}")
endif()
message(STATUS "refused as expected: ${EXPECT}")
