# Compiler warning inventory — checkpoint 86f4408

Owner requested October 13: fix all warnings in the next iteration so future
build logs are easy to follow. This inventory does not alter the qualified source
or suppress diagnostics. Completion of compilation is not completion of tests.

| Host | Unique diagnostics | Occurrences | Capture status |
|---|---:|---:|---|
| nuc1 | 109 | 245 | complete build log |
| pi | 112 | 251 | complete build log |

Exact messages and per-host occurrence counts are in [warnings.json](warnings.json),
including diagnostics from retained earlier attempts (identical log copies counted once), with log hashes in [NUC1](warnings-nuc1.json) and [Pi](warnings-pi.json).
Rows below are deduplicated by source location and full diagnostic text; long
messages are shortened here only. Project warnings should be corrected in source.
Diagnostics located in system headers may originate in project callers; inspect
the retained inlining context before assigning ownership. Dependency/compiler
diagnostics need individual review and, where appropriate,
a pinned upstream fix. Do not hide them using blanket warning suppression.

The command-client enum warning is currently unreachable: its decoder rejects the
swap-authority response before the command client's switch. The new swap code also
has misleading-indentation warnings from multiple statements on one line. Neither
observation excuses skipping review; preserve behavior with tests when cleaning up.

| File:line | Warning | Hosts | Diagnostic |
|---|---|---|---|
| `/usr/include/aarch64-linux-gnu/bits/unistd.h:46` | `-Wstringop-overflow=` | pi | ‘ssize_t __pread_alias(int, void*, size_t, __off_t)’ specified size between 18446744073709547522 and 18446744073709551615 exceeds maximum object size 9223372036854775807 [-Wstringop-overflow=] |
| `/usr/include/c++/15/bits/new_allocator.h:172` | `-Wmismatched-new-delete` | nuc1, pi | ‘void operator delete(void*, std::size_t)’ called on pointer returned from a mismatched allocation function [-Wmismatched-new-delete] |
| `/usr/include/c++/15/bits/stl_algobase.h:407` | `-Wstringop-overflow=` | nuc1, pi | writing 1 byte into a region of size 0 [-Wstringop-overflow=] |
| `/usr/include/c++/15/bits/stl_algobase.h:426` | `-Warray-bounds=` | nuc1, pi | ‘void* __builtin_memmove(void*, const void*, long unsigned int)’ offset [1064, 1065] is out of the bounds [0, 1064] of object ‘result’ with type ‘velocita::v2::SignerResult’ [-Warray-bounds=] |
| `/usr/include/c++/15/bits/stl_construct.h:88` | `-Warray-bounds=` | nuc1, pi | array subscript -1 is outside array bounds of ‘std::byte []’ [-Warray-bounds=] |
| `/usr/include/c++/15/bits/stl_uninitialized.h:273` | `-Wstringop-overflow=` | nuc1, pi | writing 5 bytes into a region of size 0 [-Wstringop-overflow=] |
| `/usr/include/c++/15/bits/stl_uninitialized.h:553` | `-Warray-bounds=` | nuc1, pi | ‘void* __builtin_memset(void*, int, long unsigned int)’ offset 1 is out of the bounds [0, 1] [-Warray-bounds=] |
| `/usr/include/c++/15/bits/stl_uninitialized.h:553` | `-Wstringop-overflow=` | nuc1, pi | ‘void* __builtin_memset(void*, int, long unsigned int)’ writing between 1 and 129 bytes into a region of size 0 overflows the destination [-Wstringop-overflow=] |
| `/usr/include/x86_64-linux-gnu/bits/unistd.h:46` | `-Wstringop-overflow=` | nuc1 | ‘ssize_t __pread_alias(int, void*, size_t, __off_t)’ specified size between 18446744073709547522 and 18446744073709551615 exceeds maximum object size 9223372036854775807 [-Wstringop-overflow=] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/FragmentAssembler.h:53` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘initialBufferLength’ [-Wunused-parameter] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/FragmentAssembler.h:89` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘sessionId’ [-Wunused-parameter] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:114` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘keyBuffer’ [-Wunused-parameter] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:114` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘label’ [-Wunused-parameter] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:130` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘counterTypeId’ [-Wunused-parameter] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:130` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘keyBuffer’ [-Wunused-parameter] |
| `/work/deps/aeron/aeron-client/src/main/cpp_wrapper/concurrent/CountersReader.h:130` | `-Wunused-parameter` | nuc1, pi | unused parameter ‘label’ [-Wunused-parameter] |
| `include/velocita/wire.hpp:82` | `-Wmaybe-uninitialized` | nuc1, pi | ‘*(velocita::AppendEntries*)((char*)&result + offsetof(velocita::PeerMessage, velocita::PeerMessage::payload.std::variant<velocita::RequestVote, velocita::RequestVoteResponse, velocita::AppendEntries, velocita::AppendEntriesResponse>::<unna… |
| `src/v2_execution_catalog.cpp:130` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘scale’ [-Wpedantic] |
| `src/v2_execution_catalog.cpp:133` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘product’ [-Wpedantic] |
| `src/v2_execution_catalog.cpp:134` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘type name’ [-Wpedantic] |
| `src/v2_identity_startup.cpp:108` | `-Wignored-attributes` | nuc1, pi | ignoring attributes on template argument ‘int (*)(DIR*)’ [-Wignored-attributes] |
| `src/v2_kernel.cpp:233` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘limit’ [-Wpedantic] |
| `src/v2_kernel.cpp:234` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘quoted’ [-Wpedantic] |
| `src/v2_kernel.cpp:234` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘type name’ [-Wpedantic] |
| `src/v2_kernel.cpp:239` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘per_tick_lot’ [-Wpedantic] |
| `src/v2_kernel.cpp:240` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘per_lot’ [-Wpedantic] |
| `src/v2_kernel.cpp:242` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘value’ [-Wpedantic] |
| `src/v2_kernel.cpp:250` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘limit’ [-Wpedantic] |
| `src/v2_kernel.cpp:251` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘position’ [-Wpedantic] |
| `src/v2_kernel.cpp:252` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘held’ [-Wpedantic] |
| `src/v2_kernel.cpp:253` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘filled’ [-Wpedantic] |
| `src/v2_kernel.cpp:254` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘gross’ [-Wpedantic] |
| `src/v2_kernel.cpp:255` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘charged’ [-Wpedantic] |
| `src/v2_kernel.cpp:258` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘closed’ [-Wpedantic] |
| `src/v2_kernel.cpp:259` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘closed_gross’ [-Wpedantic] |
| `src/v2_kernel.cpp:260` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘basis’ [-Wpedantic] |
| `src/v2_kernel.cpp:261` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘basis_out’ [-Wpedantic] |
| `src/v2_kernel.cpp:262` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘traded’ [-Wpedantic] |
| `src/v2_kernel.cpp:263` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘pnl’ [-Wpedantic] |
| `src/v2_kernel.cpp:264` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘values’ [-Wpedantic] |
| `src/v2_kernel.cpp:274` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘value’ [-Wpedantic] |
| `src/v2_node_client.cpp:96` | `-Wswitch` | nuc1, pi | enumeration value ‘swap_authority_granted’ not handled in switch [-Wswitch] |
| `src/v2_swap_account_history.cpp:17` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_account_review.cpp:50` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_gateway_runtime.cpp:32` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_gateway_runtime.cpp:54` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_gateway_runtime.cpp:57` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_prepare_session.cpp:32` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_rest_codec.cpp:49` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_https.cpp:41` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_https.cpp:57` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_production.cpp:15` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_production.cpp:91` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_production.cpp:99` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_production.cpp:104` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_production.cpp:109` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_runtime_production.cpp:120` | `-Wmisleading-indentation` | nuc1, pi | this ‘for’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_evidence.cpp:18` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_evidence.cpp:24` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_evidence.cpp:43` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_evidence.cpp:49` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_evidence.cpp:58` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_lifecycle.cpp:25` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_lifecycle.cpp:103` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_lifecycle.cpp:144` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_lifecycle.cpp:153` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_lifecycle.cpp:224` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_settlement_lifecycle.cpp:225` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_protocol.cpp:28` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_protocol.cpp:41` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:28` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:57` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:61` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:62` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:113` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:114` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_service.cpp:135` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_session.cpp:53` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `src/v2_swap_signer_session.cpp:131` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/../src/v2_identity_startup.cpp:108` | `-Wignored-attributes` | nuc1, pi | ignoring attributes on template argument ‘int (*)(DIR*)’ [-Wignored-attributes] |
| `tests/v2_archive_store_tests.cpp:32` | `-Wmisleading-indentation` | nuc1, pi | this ‘for’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_bound_peer_tests.cpp:28` | `-Wmismatched-new-delete` | nuc1, pi | ‘void free(void*)’ called on pointer returned from a mismatched allocation function [-Wmismatched-new-delete] |
| `tests/v2_gateway_node_tests.cpp:688` | `-Wmissing-field-initializers` | nuc1, pi | missing initializer for member ‘{anonymous}::CountingVenue::injected’ [-Wmissing-field-initializers] |
| `tests/v2_risk_order_outbox_tests.cpp:4127` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘m’ [-Wpedantic] |
| `tests/v2_risk_order_outbox_tests.cpp:4127` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘p’ [-Wpedantic] |
| `tests/v2_risk_order_outbox_tests.cpp:4127` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘q’ [-Wpedantic] |
| `tests/v2_risk_order_outbox_tests.cpp:4127` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘r’ [-Wpedantic] |
| `tests/v2_risk_order_outbox_tests.cpp:4129` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘after’ [-Wpedantic] |
| `tests/v2_risk_order_outbox_tests.cpp:4133` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘after’ [-Wpedantic] |
| `tests/v2_risk_order_outbox_tests.cpp:4135` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘floor’ [-Wpedantic] |
| `tests/v2_snapshot_store_bounds_tests.cpp:30` | `-Wsized-deallocation` | nuc1, pi | the program should also define ‘void operator delete(void*, std::size_t)’ [-Wsized-deallocation] |
| `tests/v2_snapshot_store_bounds_tests.cpp:32` | `-Wsized-deallocation` | nuc1, pi | the program should also define ‘void operator delete [](void*, std::size_t)’ [-Wsized-deallocation] |
| `tests/v2_static_identity_tests.cpp:265` | `-Wsign-compare` | nuc1, pi | comparison of integer expressions of different signedness: ‘const std::size_t’ {aka ‘const long unsigned int’} and ‘int’ [-Wsign-compare] |
| `tests/v2_swap_gateway_runtime_tests.cpp:32` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_prepare_store_tests.cpp:53` | `-Wmisleading-indentation` | nuc1, pi | this ‘while’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:44` | `-Wmisleading-indentation` | pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:45` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:46` | `-Wmisleading-indentation` | pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:47` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:52` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:54` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:68` | `-Wmisleading-indentation` | pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:69` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:70` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:90` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_process_drill.cpp:91` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_signer_session_tests.cpp:25` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_signer_session_tests.cpp:102` | `-Wmisleading-indentation` | nuc1, pi | this ‘if’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_swap_submission_store_tests.cpp:42` | `-Wmisleading-indentation` | nuc1, pi | this ‘while’ clause does not guard... [-Wmisleading-indentation] |
| `tests/v2_truemarkets_test_support.hpp:210` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘type name’ [-Wpedantic] |
| `tests/v2_truemarkets_test_support.hpp:211` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘scale’ [-Wpedantic] |
| `tests/v2_truemarkets_test_support.hpp:211` | `-Wpedantic` | nuc1, pi | ISO C++ does not support ‘__int128’ for ‘s’ [-Wpedantic] |
| `tests/v2_wal_streaming_tests.cpp:27` | `-Wmismatched-new-delete` | nuc1, pi | ‘void free(void*)’ called on pointer returned from a mismatched allocation function [-Wmismatched-new-delete] |
