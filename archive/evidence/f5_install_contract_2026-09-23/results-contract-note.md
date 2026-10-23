# Result-contract prerequisite witness

Scope: `tests/v2_snapshot_install_results_contract_tests.cpp` only. This is a
cold-constructor contract witness, not a live snapshot installer test and not an
F5 acceptance result.

The production-valid persisted fixture contains a reducer-derived snapshot at
S=2, a durable WAL through index 5, and hard-state commit C=4. Recovery installs
the snapshot and applies exactly 3..4, reaching independently reduced literal
`kill_epoch=4`, `state_version=4`, and the expected canonical state digest. It
intentionally emits no process-local results for those cold-replayed committed
entries. Index 5 remains RESERVED; a later ordinary commit publishes its exact
identity, term, index, and every `ApplyResult` field once, then releases the
reservation.

This does **not** establish a reachable live replacement defect. Current normal
public `propose`, peer receive, and `tick` paths call `raft_apply_all` before a
successful return, so no healthy usable old-owner `commit > applied` interval
was established. More importantly, the approved safe-install precondition
requires incoming S to be greater than the old owner's applied index. At a
normal quiescent owner turn, applied equals commit, so the `S < i <= C*` cohort
is empty. That cohort remains a defensive future/intermediate-state rule; this
test only records why reusing cold restoration would suppress outcomes if such a
cohort ever becomes admissible.

A genuine live-install RED still requires a production test seam that retains
the same live `ResultLedger` and exact `ResultHandle` bindings while a staged
owner captures replay outcomes. Required activation semantics remain:

- READY: preserve exact value and FIFO position; never replay or republish.
- RESERVED `i <= S`: verified historical outcome publishes once to the exact
  original handle, if this cohort is admitted by the integrated contract.
- RESERVED `S < i <= C*`: staged suffix outcome publishes once to the exact
  original handle, if a future safe boundary makes this cohort reachable.
- RESERVED retained `i > C*`: preserve exact handle, binding, and payload for
  ordinary Raft processing.
- Divergent locally uncommitted: release only after durable selection excludes
  the entry; divergence at or below known commit refuses installation.

No fake install stub, result-ledger injection constructor, production API, byte
contract, or acceptance claim was added.
