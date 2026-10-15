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

## Native GCC 15 inventory — October 15

The earlier paragraph describes Apple Clang's local build only. Native GCC reports additional
misleading-indentation warnings, including two in the new continuous test bridge (lines 80/82)
and per-effect latch statements in production runtime. Inspection confirms the statement after
the guard is deliberately unconditional; keep these in the owner's formatting cleanup iteration.
Do not describe this checkpoint as warning-free.

The first NUC native build has 29 unique diagnostic lines; exact output is retained in
`native/attempt-c19a675/nuc1/warnings.json` and `build.log`. Categories: Aeron unused parameters,
project one-line control-flow formatting, a shared gateway test's missing `injected` initializer,
and GCC bounds/overflow diagnostics in the existing RouteFixture vector construction.
The latter arise in inlined `std::vector::resize`/`push_back` at
`tests/v2_swap_transaction_verifier_test_support.hpp:94/103`, not a production raw-buffer write.
The actual qualified fixture uses two required signatures (three in the extra-signer refusal test);
its explicitly signed-payload case uses the default two. Preserve the diagnostics and inspect the
fixture simplification in the cleanup iteration rather than disabling compiler checks.

The test-boundary correction in `887d6f5` changes no C++ sources. Reusing existing objects in an incremental build
therefore does not make the retained warnings disappear; use both attempts' inventories.

## Final native checkpoint `1f8d7b6`

Both architectures qualified, with seven unique warning lines in each incremental compilation.
See `native/qualified-1f8d7b6/{nuc1,pi}/warnings.json` and `build.log`; prior inventories still apply
to reused objects. No warning was disabled. Fix formatting, test-fixture initialization and the
recorded vector diagnostics in the requested cleanup iteration, then qualify that changed source.
