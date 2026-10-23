# Snapshot-install ACK boundary contract witness

Date: 2026-09-23

Scope: a permanent low-level witness for the eventual integrated installer. No
production install API exists yet, so this does not claim a currently reachable
live-install defect. It exercises the pinned Raft leader response path that the
future installer must feed.

Scenario: two pinned Raft servers share a term-5 prefix through `S=12`. The
installed receiver retains divergent term-6 entries 13..20 while the current
term-7 leader owns replacement entries 13..20. The fixture asserts this actual
term/identity divergence. A successful install response carrying 20
immediately makes the leader count the unverified suffix toward quorum and apply
through 20. Carrying only 12 commits/applies only the independently verified
prefix; an actual `raft_recv_appendentries` exchange then replaces the receiver
suffix and its returned success legitimately advances leader commit/state to 20.
A delayed duplicate boundary ACK is also checked not to regress match/commit or
reapply results.

Direct compile (unique output outside repository build directories):

```text
c++ -std=c++20 -Wall -Wextra -Wpedantic \
  -I build/_deps/willemt_raft-src/include \
  tests/v2_snapshot_install_ack_contract_tests.cpp \
  build-v2-release/libwillemt_raft.a \
  -o /private/tmp/v2_snapshot_install_ack_contract_tests
exit 0; no output
```

GREEN contract:

```text
/private/tmp/v2_snapshot_install_ack_contract_tests
exit 0; no output
```

Rejected overclaim control:

```text
/private/tmp/v2_snapshot_install_ack_contract_tests --overclaim
snapshot install ACK contract failed: install ACK committed/applied unverified retained suffix: commit=20 applied=20
exit 1
```

Independent state oracle: entry IDs are 1..20. Correct boundary application is
`sum(1..12)=78`; only after ordinary verified-prefix acknowledgement may state
reach `sum(1..20)=210`. The test checks peer match index, commit index, applied
index, and this applied-state sum.

Limitations: host-only low-level policy witness against the already built pinned
Raft archive. The common prefix/commit is initialized directly as an honest-state
fixture, not produced by a full election/network trace. No production installer,
generation selection, durability, transport/authentication, sanitizer or target
coverage is claimed here. It supports the integrated install exit; it is not that
exit and is not counted as safe lagger catch-up completion.
