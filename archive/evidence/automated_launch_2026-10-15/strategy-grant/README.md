# Explicit strategy intent grant — 2026-10-15

Status: grant source `b490d68` plus test correction `ba299ff` is now natively qualified on NUC1
and Pi. Not staged, installed or activated. [Final native qualification](native/ba299ff/RESULTS.md).
Both architectures passed 59 CTest targets, 407 Python/CLI checks and 54 continuous fixture
settlements. The earlier Pi failure remains preserved and its exact failed operation is unknown.

## Problem and correction

Caller 21/key 12 was correctly a strategy, but the outer kernel admitted direct spot-swap
intents only from operators. The stopped live run logged two unauthorized requests, sequences
1/2 at indices 13/14, without an admitted effect or venue trade. The prior six-trade drill used
operator identity for intents. See the [incident](../route-fix/live-run-a835deb/AUTHORIZATION_FINDING.md).

`GrantStrategy` records an operator decision in Raft, default absent. It binds the opening,
review evidence, one existing strategy caller, a request sequence floor, and both control epochs.
A grant requires a halted, initialized account with no effect history, no pending work and its
original balances. Caller 21 can then submit future intents, subject to the unchanged swap checks.
The strategy cannot mint funds, change bounds or controls, grant itself authority, or forge gateway
evidence. Advancing either epoch disables the one-time grant. No automatic grant renewal exists.
The strategy signal is computed by the Python driver; this does not move indicator evaluation into
the kernel. [ADR 0016 amendment](../../../../docs/adr/0016-strategy-and-operator-callers-are-separate.md).

## Validation

Final local result: 11 CTest targets passed, eight CLI tests passed, 15 fresh-compiled mutants
killed at the named assertions, and three six-trade process scenarios passed (18 settlements).
The first Pi run failed; corrected checkpoint `ba299ff` subsequently qualified on both architectures.
See the final native report and preserved investigation.

- New test first failed at `grant_operator_commits` with a refusal stub, before implementation.
- Kernel integration includes pre-grant refusals, caller and sequence binding, owner-only authority,
  halted/pristine admission, price/account checks, buy/sell settlement, epoch revocation,
  snapshots, corruption, canonical command/state encoding and a frozen legacy snapshot hash.
- Independent old/new executable replay of the two refusal requests produced identical code 71
  and identical 4261-byte snapshots, SHA-256
  `1c54cf2eb7c87f4449dc69d34b851ca523dd55cd4497df5c9fd3f735c61fdb6c`.
  This is a deterministic fixture replay comparison, not a replay of private production WAL.
- The process drill now authenticates every intent as strategy caller 21/key 12, following an
  operator grant. Three cases each completed six unique creates, signatures, submissions and
  settlements, including gateway crash recovery and delayed settlement. All replicas ended flat
  at 25000000 quote atoms, zero base, 12000 fixture fee atoms. Fixture profits offset the fixture
  fees: these synthetic prices are a recovery oracle, not a profitability prediction.
- CLI dry runs cover the new grant and existing funding/intent/price-amendment forms without keys
  or network use. Golden schema checks pass. Native runner now executes the grant CLI suite and
  requires caller-21 evidence in each continuous case.

The first process drill was blocked by sandbox Unix-socket bind permission, confirmed with an
isolated bind probe. It passed with local sockets allowed; no external venue was used. During
test construction the sell-fee expectation initially used floor instead of ceiling. Correct
independent arithmetic: buy 9259259 lamports, sell gross floor(9259259*108000000/1e9)=999999 USDC
atoms, fee ceil(999999*2000/1e6)=2000, receipt 997999; final cash 4995999, total fees 4000.

`swap::State` adds 72 bytes (4088 to 4160), exceeding the former 4096-byte local assertion; it is
explicitly raised to 4160. The full trading state remains 78512 of its unchanged 81920-byte budget.
No dynamic allocation is added to the reducer. The wire record is 148 bytes; state extension is
73 bytes, with or without the existing price amendment. No-grant state bytes remain unchanged.

## Next deployment sequence — proposal only

1. Review and commit the explicit owned paths. Native-qualify that exact commit on NUC/Pi,
   including all voter/runtime binaries, replay/snapshot checks and the updated strategy drill.
2. Reverify driver sequence 2/next 3, account flat/no pending, review expiry, and next operator
   sequence. Owner halt first (expected operator sequence 10, subject to verification).
3. Perform a coordinated upgrade of the single existing cluster and gateway/signer/client,
   preserving incarnation, WAL, journals, funding and driver state. All readers must understand
   the new state extension before the grant. The previous gateway-only upgrade helper is not
   sufficient for this kernel change. Do not launch a second cluster or reset anything.
4. Prepare and review the one-time grant against the original opening and a review-evidence
   digest, caller 21, floor 3, epochs 1/1 if still current. Owner commits it using
   `spot-swap-record GRANT_RECORD ORIGINAL_OPENING_RECORD`; inspect the typed committed result.
   Neither the grant nor its owner sequence has been prepared or sent in this implementation step.
5. Owner normal-mode transition; resume the existing driver without resetting sequences. Observe
   the first admitted intent through signing/submission/settlement and reconcile the balances.
   Stop at a refusal, discrepancy, or expired review; never repeat a partial ceremony.

Latest readback at 2026-10-15 21:02:26 UTC: driver inactive, gateway/signer active, producer index14,
25000000 quote atoms, zero base/effects/pending/fees/P&L, settlements empty. This is fresh producer
state, not a fresh TM wallet request. The 240-order/$25-capital/$10-realized-loss run approval
persists within its review window; no trade occurred in this implementation step. The realized
loss stop is not a guarantee of maximum total loss. Entry drains October16 13:41:34.459 UTC;
review expires 13:48:34.459 UTC. Owner renewal is needed if qualification passes that window.

## Evidence and reproduction

See `results.json`, `candidate-files.json`, `mutations.json`, `legacy-replay.json`, `continuous-results.json`,
`warnings.json` and CTest XML files beside this document. Local build uses HEAD 968f18f plus only the
listed candidate source files in an isolated archive checkout; shared dirty files were not copied.
All scratch artifacts remain under `$HOME/.cache/velocita-{work,build}/strategy-grant-20261015`.
This source was subsequently committed as `b490d68`. No installed images or services were changed.
