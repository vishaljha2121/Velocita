# Gateway and protected-signer process drills — October 13

Status: **all eight local process scenarios pass after the current-term Raft
commitment fix**. The earlier F07 failure is preserved below. JSON artifacts are
now independently parsed and checked against integer oracles. This is working-tree
Mac evidence, not committed/native NUC/Pi release qualification or permission to
trade. Final isolated mutation verification is recorded below.

## What is composed

`tests/v2_swap_process_drill.cpp` runs the production `SwapGatewayRuntime`,
`SwapDurableSigningPort`, signing connection/client/server, signer session/key,
submission/prepare/reconciliation journals, real authenticated `GatewayLink`
frames and three real `V2Node` instances. The gateway and protected signer are
separate fresh `exec` processes on every restart; they communicate through the
actual Unix signer protocol. Each signer independently reads the real fixture
quorum and obtains the existing committed signing authority. P256 fixture keys
are freshly generated per case; no venue credentials or real transaction bytes
are used.

The parent pumps three V2Node objects through the existing deterministic local
transport. They are not three independent voter processes. Gateway/voter and
signer/voter transport is a bounded socketpair frame bridge; it does not qualify
Aeron or native network latency. Unix peer-UID checks run with the current test
UID; this does not prove production separation between gateway and signer UIDs.
Generated key loading, exact signature verification and durable replay journals
are real. The isolated target enables the explicit synthetic signer/settlement
fixture macros; those are not enabled in the shipped gateway library.

Venue prepare/execute and finalized settlement authority are injected fixture
ports. An append-only, fsynced `venue.log` records create/signature/execute and
read-reconciliation events. A complete REST response is not treated as production
settlement proof: this fixture explicitly supplies synthetic admitted settlement.
The real transaction safety/finality contract and TLS route qualification remain
separate gates.

Each case keeps its journals, voter directories, `processes.jsonl` with gateway
and signer PIDs plus OS signals/exit codes and transcript. Passing cases also
write `result.json` with independent integer financial/count assertions; the
failing leader case retains `replicas.jsonl` and the exact stderr assertion. SIGKILL is an actual process death,
not reconstruction of an object. Output paths are durable under the owner's
cache and never reused or deleted by the test.

## Passing matrix

| Case | Drill | Actual interruption | Restart oracle |
|---|---|---|---|
| 0 | F06 | Gateway SIGKILL after durable execute intent, before fake venue request | Zero execute calls; one signature; full $1 hold retained. No resend. |
| 1 | F06/F08 | Gateway SIGKILL after fake venue accepts request, before reply | Exactly one execute; unknown remains held; later admitted receipt settles once. |
| 2 | F08 | Same accepted-send death, then gateway SIGKILL after transaction identity is persisted before Settlement proposal | No early hold release; recovery settles once. |
| 3 | F08 | Same accepted-send death, then parent kills gateway after leader applies Settlement, before sending the acknowledgment | Recovered journal resolves from authenticated committed state. Settlement and fees are not applied twice. |
| 4 | F03 | Gateway SIGKILL after durable signing-request publication, before signer IPC | Zero signatures and executes. Production-equivalent recovery-only startup blocks the prepared effect and preserves the hold. |
| 5 | F05 | Gateway SIGKILL after a durable valid signature, before Attempt | Exactly one signature; zero execute. Recovery-only startup does not reuse the retained signature to start a trade. |
| 6 | F07/F08 | Accepted-send gateway SIGKILL, then stop actual V2Node leader before followers apply the committed tail; elect replacement quorum | New leader commits/applies its current-term marker before serving state, recovers the existing effect and settles once without another sign/execute. |
| 7 | F04 | Protected signer SIGKILL after signing, before signature-result temporary fsync | Gateway exposes no executable request. Signer recovery sees `temporary_present`, stays quarantined; no second signature or execute. |

Every successful-settlement case starts with exactly 5,000,000 quote atoms and
zero base, reserves 1,000,000 quote atoms, and finishes with exactly:

- quote: **4,000,000 USDC atoms**;
- base: **9,000,000 SOL atoms**;
- fees: **2,000 USDC atoms**;
- total purchase debit: **1,000,000 USDC atoms**;
- reserved quote: **0**;
- settlements: **1**, including another fresh gateway/signer restart.

Unknown and pre-Attempt cases preserve starting balances, the full hold, and zero
recorded fees/purchases/settlements. Opening cost basis and funding are synthetic.

## Leader-loss regression and correction (F07)

The `--case 6` regression stops the old V2Node leader after the accepted
execute, elects a replacement quorum, and restarts gateway and signer. Before the fix, funds stayed
held and no new request was sent. However, the new gateway remained running at its
bounded deadline instead of reaching the reconciliation refusal:

```
FAIL unknown restart blocks status=0 cause=0 phase=2 seen=10 prepared=7 submitted=10
```

The observed state is still `prepared`, below the gateway's durable result index
10. The main review confirmed a missing current-term commitment fence: state reads
serve the new leader's applied snapshot without appending/committing a current-term
marker, while the runtime correctly refuses to go below its persisted result
watermark. The earlier single-process failover test waited for every voter to
apply Attempt before leader loss, then directly proposed MarkUnknown; it did not
cover this window. The production fix adds the reviewed current-term commitment marker and gates
state reads on its durable commitment/application. The harness does not weaken
the watermark or inject an artificial MarkUnknown to hide the issue. Evidence is retained in
`~/.cache/velocita-build/process-drill-20261013/run-45816/case-6/`.
Before stopping leader node 2 in term 1, it had current/commit/applied index 10;
both followers had current index 10 but commit/applied index 9. After election,
leader node 3 in term 2 still had current 10 and commit/applied 9. Thus the tail
was replicated; the replacement leader had not committed/applied it.
The default harness still runs all eight cases. On the corrected build it exits
zero; F07 reaches the same exact final balances and single-settlement oracle as
the other success cases. This closes the local parent-hosted V2Node leader-loss
regression. Full separate-voter-process native failure qualification remains a
separate release check.

## Verification and commands

The isolated CMake target is `v2-swap-process-drill`; it shares the existing gateway
runtime fixture's production sources and adds the synthetic signer macro only to
this target. Run from the repository:

```sh
BUILD="$HOME/.cache/velocita-build/readiness-20261011/cmake-root"
cmake --build "$BUILD" --target v2-swap-process-drill -j 3
ctest --test-dir "$BUILD" -R '^v2-swap-process-drill$' --output-on-failure
```

This sandbox requires permission to use local Unix sockets. An ordinary
unrestricted host terminal should not need that sandbox exception. No Internet
connection, cluster deployment, venue call or money movement is performed.

Current verification artifacts:

- fixed eight-case run: `~/.cache/velocita-build/process-drill-20261013/raft-fixed/`;
- full output: `~/.cache/velocita-build/process-drill-20261013/raft-fixed.log` — exit 0;
- independent evidence parser: **8/8** JSON/type/economic/transcript/process checks;
- final frozen linked-input and mutation evidence:
  `~/.cache/velocita-build/process-drill-20261013/raft-fixed-mutations/`;
- `raft-fixed-mutations/results.json`: **8/8 independently parsed baselines,
  3/3 designated named production mutation kills**.

Historical evidence is preserved without rewriting: `final-ctest.log` and
`run-45816/` show the pre-fix F07 failure (CTest exit 8, 47.25 seconds).
**The old `run-45816` result.json files are malformed:** a uint8 settlement count
was streamed as a character. C++ financial assertions still ran, but those files
must not be treated as valid JSON. An explicit unsigned conversion fixes output.
`json-repaired-red/` is the corrected-JSON pre-Raft-fix run: seven cases pass and
F07 fails. The independent parser rejects the old malformed file and three
separate wrong-type/wrong-value mutations; these checks are retained under
`json-oracles/validation.json`.

Validate the current full-run artifacts independently:

```sh
python3 tests/test_swap_process_drill.py --verify-evidence \
  "$HOME/.cache/velocita-build/process-drill-20261013/raft-fixed"
```

Mutation runner (choose a new output directory):

```sh
python3 tests/test_swap_process_drill.py --build "$BUILD" \
  --out "$HOME/.cache/velocita-build/process-drill-20261013/new-mutation-run"
```

The runner snapshots linked objects/archives and headers, compiles changed copies
outside the repository, and requires separately passing baselines and parsed
financial/process artifacts for all eight cases before running mutants. Mutations
bypass the V2Node current-term read gate, disable recovery-only pre-Attempt guards,
and bypass Settlement commit. The first must fail the designated F07 recovery
assertion; a generic process timeout is not counted. The explicit
`--expect-open-f07` option exists only for historical pre-fix validation; it is
not used for the final corrected-build verification. Neither a compiler error
nor a timeout counts as a kill.
An earlier mutation attempt had a fixture compile error; it was not counted.

No commit, deployment, operator ceremony, live enablement or real signing was
performed. Exact native build qualification and deferred TM economic safety gates
remain required.
