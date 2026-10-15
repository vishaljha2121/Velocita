# Build warnings retained for the cleanup iteration

Observed warnings are preexisting Aeron C++ wrapper unused parameters and the shared gateway-test fixture missing its `injected` initializer. This batch does not modify those shared/dependency files or disable warnings. No new warning remains in the drill.

```text
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:114:89: warning: unused parameter 'keyBuffer' [-Wunused-parameter]
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:114:119: warning: unused parameter 'label' [-Wunused-parameter]
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:130:54: warning: unused parameter 'counterTypeId' [-Wunused-parameter]
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:130:89: warning: unused parameter 'keyBuffer' [-Wunused-parameter]
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:130:119: warning: unused parameter 'label' [-Wunused-parameter]
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/FragmentAssembler.h:53:57: warning: unused parameter 'initialBufferLength' [-Wunused-parameter]
/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src/aeron-client/src/main/cpp_wrapper/FragmentAssembler.h:89:43: warning: unused parameter 'sessionId' [-Wunused-parameter]
/Users/vishaljha/.cache/velocita-work/focused-mvp-20261015/tests/v2_gateway_node_tests.cpp:688:36: warning: missing field 'injected' initializer [-Wmissing-field-initializers]
```
