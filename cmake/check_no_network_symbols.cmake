# cmake -DLIBRARY=<static library> -P check_no_network_symbols.cmake
#
# Fails if any object in LIBRARY references a socket, name-resolution or TLS
# symbol. The True Markets adapter reaches the venue only through an injected
# interface; a direct reference here would be a network path that bypasses
# live arming.

if(NOT LIBRARY OR NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "LIBRARY not found: ${LIBRARY}")
endif()

execute_process(COMMAND nm -u "${LIBRARY}"
    RESULT_VARIABLE nm_result OUTPUT_VARIABLE undefined ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed on ${LIBRARY}: ${nm_error}")
endif()

# Guard against a vacuous pass: the objects must reference something.
string(LENGTH "${undefined}" undefined_length)
if(undefined_length LESS 16)
    message(FATAL_ERROR "nm listed no undefined symbols for ${LIBRARY}; the check would be vacuous")
endif()

string(REPLACE "\n" ";" symbols "${undefined}")
set(forbidden "")
foreach(symbol IN LISTS symbols)
    string(STRIP "${symbol}" symbol)
    # Mach-O prefixes C symbols with an underscore; ELF does not.
    string(REGEX REPLACE "^_" "" bare "${symbol}")
    string(REGEX REPLACE "@.*$" "" bare "${bare}")
    if(bare MATCHES "^(socket|socketpair|connect|bind|listen|accept|send|sendto|sendmsg|recv|recvfrom|recvmsg|getaddrinfo|gethostbyname|gethostbyname2|getnameinfo|res_query|curl_.*|SSL_.*|TLS_.*|BIO_.*|nw_.*|CFSocket.*|CFStream.*)$")
        list(APPEND forbidden "${symbol}")
    endif()
endforeach()

if(forbidden)
    message(FATAL_ERROR "network symbols referenced by ${LIBRARY}: ${forbidden}")
endif()
message(STATUS "no network symbols referenced by ${LIBRARY}")
