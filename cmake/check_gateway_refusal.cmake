# cmake -DBINARY=<gateway> -DARGS=a,b,c -DEXPECT=<text> -P check_gateway_refusal.cmake
#
# Runs the gateway with ARGS and requires the usage exit (64) with EXPECT in
# its diagnostic. The refusal happens during argument parsing, before any key
# file, Aeron directory or network is touched.

if(NOT BINARY OR NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "BINARY not found: ${BINARY}")
endif()
string(REPLACE "," ";" arguments "${ARGS}")
execute_process(COMMAND "${BINARY}" ${arguments}
    RESULT_VARIABLE exit_code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
if(NOT exit_code EQUAL 64)
    message(FATAL_ERROR "expected exit 64, got '${exit_code}'\nstdout: ${out}\nstderr: ${err}")
endif()
string(FIND "${err}" "--venue truemarkets refused: ${EXPECT}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "expected refusal '${EXPECT}', got stderr: ${err}")
endif()
if(out MATCHES "READY")
    message(FATAL_ERROR "the gateway reported READY: ${out}")
endif()
message(STATUS "refused as expected: ${EXPECT}")
