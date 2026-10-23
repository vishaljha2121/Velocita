# CP-021 — receiver verified-prefix commitment boundary

2026-09-23. Branch v2/phase-5-corrections, HEAD d44a4a9; remediation remains
UNCOMMITTED. No new bytes, production quota, fork patch, target gate or acceptance.

## Finding and the important qualification

Pinned `raft_recv_appendentries` advances commit to the lesser of leader_commit
and the follower's FULL current log index. Its matching loop examined only the
entries in the received message. V2 sends at most16 entries per message.

A synthetic, lineage-valid ingress fixture retains common entries1..16 and
divergent uncommitted17..20. A newer-term message verifies only1..16 but advertises
commit20. Before the fix, the receiver applies the unchecked tail: kill120 rather
than kill16. A heartbeat verifying prev16 exposes the same boundary. These are
input-boundary regression tests, NOT a proven honest-cluster execution trace.

Main and reviewer initially suspected current sender reachability. Independent
analysis corrected that: new-leader next-index starts past its own log; rejection
backtracking eagerly truncates a mismatching follower suffix. We did not establish
a public proposal/election/packet schedule preserving the divergent tail at this
point. Do not claim a reproduced live split-brain, exhaustive unreachability proof,
or that the crafted sender ran through quorum. The existing actual three-node
40-entry batching test is a healthy convergence witness, not a RED for this guard.

This receiver invariant is still necessary before the proposed capacity/header
path: a shortened request must not authorize an unverified tail merely because
its log entries are locally present. A caller's intended verified prefix should
not depend on a different component having truncated the tail first.

## Minimal fix

- Before pinned Raft, pass `min(leader_commit, prev + entry_count)` as the local
  request's commit ceiling. Empty messages cover prev only. The Raft function
  still has to validate prev before advancing commit; a lower advertised commit
  remains lower. Existing commitment never regresses on an older message.
- Bound-peer encode/decode rejects negative prev and a prev/count pair that
  cannot represent both the end index and its successor in signed64 arithmetic.
  This validates fields, not new canonical bytes. No economic proposal is
  silently clamped: this is a protocol proof boundary on replica commitment.
- V1 and the pinned fork are unchanged. The extra hot-path work is O(1) integer
  arithmetic/checks; no new ownership, queue, allocation or persisted field.
  No measured latency claim is made.

Source witnesses: `src/v2_replication.cpp` receive_peer; `src/v2_bound_peer.cpp`
validate; `tests/v2_bound_peer_tests.cpp` verified_prefix_limits_commit,
prefix_verification_boundaries and append_prefix_arithmetic_refuses.

## RED / GREEN / mutation evidence

Rebuilt pre-fix test binary:

| Selector | Original RED, exit1 | Fixed witness |
|---|---|---|
| prefix | unverified divergent suffix changed economic state | kill16, commit/apply16, exact results1..16, reply16 |
| heartbeat-prefix | same | empty request verifies only prev16 |
| prefix-arithmetic | wrong typed peer refusal | negative/overflowing prev refused; destination unchanged |

Both suffix cases then replace17..20 with the newer leader's commands, assert
kill20 and precisely their result identities, flush/checkpoint and restart to
the repaired state. Extra cases cover lower commit8 despite verified end16,
failed prev-term check with no new apply/results, and a valid empty-log heartbeat.
A decoder-only positive edge verifies a representable near-maximum prefix.

One fixture correction during GREEN: flushing the WAL does not itself update
the persisted commit checkpoint. The restart witness initially omitted the
explicit owner tick after flush. Added tick(0), as required by the existing API;
no production durability semantics changed to satisfy that test.

Isolated source-copy mutants, directly compiled with the current suite/archives:

- remove commit cap -> `unverified divergent suffix changed economic state`;
- substitute verified end without min -> `verified batch end overrode the leader's lower commit`;
- remove prefix arithmetic guard -> `wrong typed peer refusal` under the isolated
  prefix-arithmetic selector.

All exit1 at the intended assertions. Initial direct compilation used an absent
temporary-build dependency-header path; that compile failure was corrected to
the actual cached header path and is NOT mutation evidence. No production source
or shared CMake cache was mutated.

## Reproduction and limits

Work directory `/private/tmp/f5-verified-prefix.esAgZg`. Build roots unchanged:
Release `/private/tmp/velocita-full-audit.lYB4P5/release`; Debug sanitizers
`/private/tmp/aeron-phase67-audit-build`. Build velocita-all-tests before CTest.
Selectors: BOUND_PEER_CASE=prefix, heartbeat-prefix, prefix-boundaries,
prefix-arithmetic; executable v2-bound-peer-tests.

Host Release51/51 and ASan+UBSan51/51. Sanitized C++ flags remain
`-fsanitize=address,undefined -fno-omit-frame-pointer`, runtime leak detection off
and UBSan halt_on_error=1; Raft C is NOT instrumented. No new TSan/power-loss/target
claim. No new suites or inventory changes. Tools/model were not rerun this
checkpoint; their CP-020 40/109 passes remain historical, not new results.

The separate `result-ledger-layout-proposal.cpp/.txt` files record ONLY a proposed
native layout, not implemented capacity. Main inspected the worker probe and
reran the binary. Compile from repository root with C++20,
`-DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude -Ibuild/_deps/willemt_raft-src/include`.
The measured229400-byte object excludes retained-entry handles/lookup storage,
allocator overhead and all other queues. No target-layout or latency claim.

F5 result/log/outbound quotas, replacement/install/archive, F6 bound metadata
power-loss model port/async fences and F7 target gates remain open. Negative terms,
commit/reply coordinates and other peer semantic checks require their own REDs;
this increment hardens prefix arithmetic, not every signed field. The capacity
protocol itself is still a review draft, not implemented or approved here.
