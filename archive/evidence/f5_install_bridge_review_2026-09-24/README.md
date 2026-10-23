# CP-045: E1 publication-boundary review and contract witnesses

2026-09-24, `v2/phase-5-corrections` at `d44a4a9`, uncommitted remediation.
No production changes or new accepted byte/economic contract. This work enables
E1 design; it does not satisfy E1 actual lagger installation.

## Evidence and classification

The unchanged `tests/v2_snapshot_catchup_red.cpp` was freshly compiled against
current Release libraries. It fails as intended: healthy quorum compacted12,
lagger applied0/commit0, seven actual failed AppendEntries replies. The failure
is absent live installation, not a fabricated snapshot-corruption trace.

Existing rebuilt archive-startup, generation-startup and install-preflight suites
pass3/3. Their refusal rules independently show why publishing a root archive
ahead of selected commitment is invalid and why a snapshot cannot replace the
missing historical prefix. The two-head integration argument is source/contract
analysis, NOT a reproduction of an existing installer (none exists).

New `v2-install-history-bridge-contract-tests` constructs cold disk images:

1. Snapshot3/commit3 with neither archive nor retained prefix refuses specifically
   for missing verified history, leaving bytes unchanged.
2. Snapshot3 with authored bound WAL1..3 recovers exact independently reduced
   state, exact authored WAL tuples and unchanged evidence. No cold-start result
   notification or online install promise is invented.
3. Snapshot8193 with8193 legitimate duplicate commands in exact retained history
   still reaches the current deployment-archive-quota refusal. This is a CURRENT
   limitation, not desired final E1 behavior. The test tells the implementer to
   replace this expectation after the accounted bridge contract is approved.

The third image is a constructed valid history layout, not evidence that the
current live8192-entry admission can generate it without compaction. It explains
why a proposed streamed imported history alone will not bypass current recovery
accounting. These fixtures do not publish a new generation selector, authenticate
a peer, preserve an online ledger or demonstrate a delayed-write activation cut.

One Sol agent audited ownership and archive ordering; one implemented bounded
test-only witnesses. Main found the additional8192 admission limitation, reviewed
the tests and changed their WAL oracle from fixture readback to authored records.
No result under test is used as its own expected WAL answer.

## Negative controls

Compile modified copies of actual `src/v2_replication.cpp` outside the repo;
production remains unchanged. Link each object before the normal library:

- Remove only missing-prefix startup refusal: exit1 at
  `snapshot without archive or retained prefix did not refuse for missing history`.
- Remove only `pending > kRetainedLogEntries` rejection: exit1 at
  `snapshot-covered streaming history did not reach the current archive quota refusal`.

The second mutation establishes which CURRENT guard constrains the proposal; it
does not argue that this coarse guard must remain the final install contract.
Do not remove it globally: the proposal requires separate verified debt accounting.

## Proposed next step

Read `../../F5_INSTALL_HISTORY_BRIDGE_REVIEW.md`. Review the WAL-bridge ordering,
candidate disk budget, exact future archive reservation and nonserving recovery
before implementation. Previously approved result/ACK/authority/durability rules
stand. No new transport or authentication bypass is approved. All broad exits,
target durability, E2 independent copy and live launch remain open.

Final rebuilt regressions and commands are in `verification.txt`; raw logs are
retained alongside this file. No hardware, passive model corpus, throughput,
RSS, TSan or real power-loss result is claimed in this checkpoint.
