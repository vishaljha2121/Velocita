# Confirmed fill telemetry and initialized-run review renewal

Work requested October 13 to converge on bounded live execution and eventually
continuous stochastic trading. No order, signing, mode change, ledger reset or
service activation is authorized by this evidence. Current host read: all three
voters up, globally halted (mode5); gateway and signer stopped, review expired.

## Implemented and native-qualified; deployment pending

The gateway copies only an accepted authenticated committed state read into a
bounded observation (at most16 settlements). Quotes, signature responses and
submission acknowledgments cannot create a fill. The snapshot contains aggregate
atom balances and successful finalized settlement amounts; it excludes wallet,
transaction, credential and venue-request identifiers. Output is atomic JSON in
the existing private metrics directory, at most once per second while unchanged,
and on a newly observed committed index or terminal result. Reporting failures
warn without causing an order retry. No Raft schema, consensus or rule changes.

The local observer validates snapshot identity, shape, finite integer ranges,
committed-index/time monotonicity and replayed economics. A private root-owned
StateDirectory retains first-confirmed-observation timestamps across restarts.
The point chart uses those times, not repeated Prometheus scrape times. It is NOT
exact chain execution time; no historical execution timestamp is invented.
All retained successful effects are shown; the current kernel stops at16 records,
it does not evict records. Missing/stale/invalid feed states are explicit. Ledger
balances exclude unreconciled external deposits and P&L is the ledger's realized
figure against the reviewed opening basis, not marked-to-market equity.

`refresh-idle` validates proposed account evidence only when both services are stopped and
all five initialized journals are pristine and identically bound to the original
funded state. It preserves funding, opening, credentials, cost limits and launch
flags. It cannot renew an expired transaction policy or modify journals.

## Real renewal issue found

The preserved installed funding-start transaction review expires at
2026-10-13T22:17:27.845Z, the same time as its account review. Expiry participates
in the protected signer journal domain. Changing the expiry alone breaks recovery.
An expiry-only renewal therefore requires a reviewed, locked journal transition:
retain the original immutable record; advance the unused protected-signing record
ordinal and domain; preserve every gateway journal and the original funding;
update dependent document hashes; authenticate current Raft no-effect/no-pending
state; and fail closed on partial application. A proposal-only helper is tested. It cannot grant itself live authority or treat supplied JSON as an
authenticated voter read. This is a restart correctness issue, not another fee
experiment or permission to discard journals.

## Continuous strategy is distinct from the bounded test

The production predicate `swap_mvp_buy_scope` explicitly caps purchase total at
$1, effect ID at1 and settlement count at1. The runtime completes after one effect.
The stochastic edge still emits the existing limit-order decision flow; no
`ApplySpotSwap` producer is present in its model path. Therefore full live strategy
operation requires a reviewed signal-to-swap intent bridge, repeated buy/sell
recovery/authorization, budget/loss controls and bounded settlement-history
handling. Do not remove the MVP predicate as a substitute for that work. The
current chart work and single-buy test can finish without redesigning Raft.

First execute and reconcile the separately reviewed bounded buy. Then use a
registered cost-adjusted paper experiment before promoting a continuous strategy.
20bp explicit fees and native SOL output deductions remain separate; the accepted
2,039,280-lamport maximum is a worst-case allowance, not an expected per-fill fee.

## Local validation

- C++ gateway executable and runtime test target built successfully in the local
  development cache. This is not committed-build/native qualification.
- All seven runtime drill modes passed, including lost response, refused signing,
  uncertain settlement, restart recovery and late settlement. Added observation
  assertions ensure unproven outcomes never produce markers. Hand-computed
  partial-sell fixture uses 0.005 SOL debit /0.60 USDC net receipt /0.001 fee.
- Two C++ mutations fail their named assertions: fee-as-debit and failed-as-fill.
- 16 collector,21 dashboard and17 review-transition tests passed. Agent also ran
  6 adoption and16 launcher tests. Full unrelated discovery requires a synthetic
  drill executable environment variable; missing configuration was not counted
  as a passing test or mutation kill.
- The first new sell fixture incorrectly used a21-second sign window against
  its20-second test limit; named intent assertion caught it. Corrected the
  fixture to20seconds and reran the complete seven-mode test successfully.
- Existing C++ warnings are retained in warnings.json: test fixture missing-field
  initializer and upstream Aeron unused parameters. No warning was suppressed.

The real Grafana13.1.3 transformation library passed four chart cases and one
fill-table case. This caught and fixed exclusion of the converted event timestamp
and the Prometheus `Value #A`/`Value #B` multi-query field names. No browser or
Safari access was used; this is data transformation validation, not a screenshot.
All38 proposed panel queries parsed on the running Prometheus. New settlement
series are not expected until the producer is deployed; empty data is not a fill.

The offline expiry-only renewal proposal passes6 tests plus fixed native C++
hash parity for gateway signing domain, protected signer domain and runtime
identity. Its output is always non-applyable until the authenticated proof and
locked crash-safe transition are qualified. It cannot start services or trade.

## Checkpoint boundary

Owner approved the scoped checkpoint and offline NUC/Pi qualification. Renewal
remains proposal-only: `refresh-idle --apply` rejects before touching host files
because double-reading journals does not prevent a service-start race. Actual
renewal needs exclusive locks and start inhibition across the entire transition.
The current native read probe can establish exact original snapshot-digest
equality; a new state schema or Raft feature is not required for this narrow
quiesced owner procedure. Fresh authenticated snapshot equality is not described
as a generic new per-request quorum ReadIndex guarantee.

The native runner here reuses the prior exact-commit protocol/cache and adds the
four new Python suites. Original 52-plus CTests, process drills, TLS tests, schema
goldens and signer-isolation checks remain required. NUC free space was54.5GB,
Pi3.01GB; launcher requires2.5GB initially and stops below1.8GB. One worker/CPU3,
2GiB memory, no container network or credentials, no production restart.

Exact committed native qualification has passed on both architectures; see
[results and follow commands](QUALIFICATION.md). The approved checkpoint is
`69679c081ce2d0b8939a89c3c846948c460d8d3e`. No live service was started.
