# Orderbook baseline

Branch `orderbook/baseline`, cut 2026-11-09 from the published tree (`github/main`, commit `710677f`). It is
the starting point for an orderbook version of Velocita: the code that ran live, the limit-order machinery
the paper runs used, and nothing else.

## What the review found

**The core is sound and stays unchanged.** The replicated kernel, Raft over Aeron with the durable-quorum
fork, the asynchronous WAL, snapshots and archive, caller roles, kill epoch and executor generation, the
gateway's per-send authority, and the signer's key custody all ran on three machines with real money and
settled 16 of 16 swaps to the atom. No byte format, kernel rule or protocol was changed for this branch.

**Two execution models live side by side in the kernel**, and the orderbook work has to choose between them:

| | Order lifecycle (paper runs) | Spot swap (live run) |
|---|---|---|
| Commands | `AuthorizeDecision`, `ObserveFill`, `ObserveOrderEvent`, `RequestCancel`, `RequestReplace`, `RequestMassCancel`, `ResolveOrderConflict` | `ApplySpotSwap` (intent, prepare, attempt, unknown, settlement, release) |
| Who authors an order | Nobody outside the kernel: an executable policy turns a `DecisionFrame` into a proposal ([edge design](V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md) §"cannot author an `OrderEffect`") | The strategy caller, under an operator `StrategyGrant` |
| Order types | limit price; GTC, IOC, FOK, post-only; submit, cancel, replace | exact-input market swap |
| Concurrency | 16 order slots, about 9 usable: each live order reserves worst-case venue-event receipts (`venue_completion_capacity_available`) | one pending effect (`swap::State::pending`) |
| Accounting | `RiskAccount`: lots, micros, margin and fee holds | `swap::State`: base and quote atoms, exact chain settlement |
| Venue | paper venue (fill-or-kill only, [ADR 0018](adr/0018-paper-venue-fills-fill-or-kill-at-measured-costs.md)), simulator, a mock-only True Markets adapter | True Markets unsigned swap, Solana finalized reads |

**Orderbook trading needs pieces neither model has yet** (most important first):

1. **A strategy-authored limit order.** Quoting needs the strategy to name price, size, side and
   time-in-force directly, with the kernel enforcing risk. The swap path's intent plus `StrategyGrant` is the
   precedent; the policy evaluator path cannot express a quoting strategy.
2. **Many concurrent orders.** Two-sided quoting with replaces needs tens of live orders. The binding budget
   is the snapshot, 61,586 of 65,536 bytes, with the state at 78,512 of 81,920 (`v2-layout-report`). The
   largest cost is 256 request receipts at 160 bytes each; see
   [memory.md](guide/memory.md) and the [memory atlas](V2_MESSAGE_MEMORY_ATLAS.md) before resizing anything.
   Receipts also hold the venue-event evidence that bounds concurrent orders, so shrinking them tightens
   that limit rather than freeing room for orders.
3. **Latency per action.** Every order, cancel and fill is a Raft commit (live p50 126 ms, p90 218 ms on
   NUC + Pi), and a swap took about five commits. Competitive cancels probably need a committed quoting
   envelope (prices, sizes and a risk budget) that the gateway can act inside, plus cancel-on-halt, rather
   than one consensus round per cancel. This is the main design decision for the new version.
4. **One accounting model.** Integer atoms with exact settlement (the swap model) should carry over to
   orders; `RiskAccount` micros and lots would otherwise be a second ledger to reconcile.
5. **A real orderbook venue adapter.** `truemarkets_venue` is shaped around the market-order MCP tools
   (`get_quote`, `trade_prepare`, `trade_execute`) and never ran against the real API. It needs replacing
   once the orderbook API (order entry, cancel, open orders, fills, book feed, session and rate limits)
   is documented in [`MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md).
6. **A resting-order paper venue.** The paper venue fills fill-or-kill only. Passive quoting needs queue
   position, partial fills and fills after cancel. `model/src/velocita_model/simulation/` (fill model,
   inventory, fair value, quote shadowing) is kept as a starting point for that.

**Smaller findings:**

- The internal "V2" naming (`velocita::v2`, `src/v2_*.cpp`, `velocita-v2-*` binaries) now collides with the
  new version's name. Renaming touches deploy units, images and scripts, so it is better done once, with the
  first new incarnation.
- The last live cluster was left halted on `64e6f9f` with effect 25 reserved (see the
  [published README](https://github.com/vishaljha2121/Velocita#limits)). Kernel changes for orders will need a
  new incarnation anyway, opened flat.
- This branch has been built and tested on the Mac only. Native qualification on the NUCs and the Pi
  ([TOOLCHAIN.md](TOOLCHAIN.md)) is needed before anything from it is deployed.

## What was removed, and where it is now

Everything removed is unchanged on `github/main`.

| Removed | Why |
|---|---|
| `archive/` (4,631 files of qualification, deploy and live-run evidence, session handoffs) | Evidence, not code. Links from kept docs now point to GitHub |
| The V1 Raft counter: `raft_node`, `counter_fsm`, `client_*`, benchmarks, metrics exporter, dump tool, their 3 tests, the root `Containerfile` | Replaced by the V2 node; nothing deployed used it |
| V1 deploy: `deploy/quadlet`, `pi-container`, `systemd-user`, `systemd-system`, `observability`, and 32 top-level scripts (V1 cluster control and the per-phase acceptance gates) | Superseded by `deploy/v2` and `deploy/v2/build-image.sh` |
| `deploy/v2/nuc-rehearsal` | Untested templates from a rehearsal |
| 6 C++ test files no build or script compiled (`*_red.cpp` drafts, a reachability sketch) | Dead; their tests live in the compiled suites |
| 43 docs: phase reviews (`F3`–`F6`, `M2`, `M3`), V1 plans, dated baselines and plans, the live-trading journey, the 2026-10-01 architecture review, the Pi runbook | History; the ADRs record the decisions |
| 4 one-off research report renderers in `model/` | Not imported by anything deployed or tested |
| `RESULTS.md` | Describes the published run; stays on GitHub |
| 3 Python tests of archived scripts (`test_gate_manifest`, `test_roles_stage`, `test_venue_basis_timing`) | Their subjects were removed |

The V1 pieces V2 still uses stay: `storage.hpp` (WAL records, metadata) and the Raft peer messages, now
`peer_wire.hpp` with the counter's client messages removed. The peer bytes are unchanged.

## Changed to keep it working

- Phase and draft names: `v2-phase4-property-tests` is `v2-order-property-tests`, `v2-phase3-benchmark` is
  `v2-decision-benchmark`, two `*_red.cpp` suites dropped the suffix, and
  `V2_PHASE3_DECISION_EVALUATOR_DESIGN.md` is `V2_DECISION_EVALUATOR_DESIGN.md`.
- The log replay tool from the 2026-10-09 gateway stop is `tools/replay-log.cpp`, built as `v2-replay-log`,
  so it now compiles with every build.
- The independent replay oracle for the live rule moved from the archive to
  `model/tests/oracles/stochastic_replay.py`.
- The measured True Markets quote set moved to `model/data/`. `research/costs.py` was already pointing at a
  stale `docs/evidence` path on `main`; it and the edge image now read the new location.
- Mutation harness results go to `build/mutations/` instead of the archive.
- The NUC runbook keeps host preparation and drops the old gate procedure.

Three tests failed on `main` when the Python suite ran on its own, and are fixed here:

- `test_token_tools` passed alone but failed after scripts that set `umask 077`. Its "public" directory is
  now `chmod 0755` explicitly.
- The dashboard scoping test predated the live dashboard, which scopes by run instead of by strategy. It now
  accepts either scope; removing a query's scope still fails it.
- `test_swap_paper_drill` now skips without its drill binary, and the signer packaging check is
  `check_signer_not_shipped.py`, since it is a script CTest runs with arguments, not a unittest module.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVELOCITA_BUILD_SWAP_RUNTIME=ON \
      -DVELOCITA_TEST_HTTPS=ON -DVELOCITA_TEST_SWAP_GATEWAY_TLS=ON \
      -DVELOCITA_TEST_SWAP_HTTPS=ON -DVELOCITA_TEST_SWAP_SIGNER=ON
cmake --build build --parallel
ctest --test-dir build -j 8 --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
cd model && PYTHONPATH=src python3 -m unittest discover -s tests
```

| Suite | `main` (`710677f`) | This branch |
|---|---|---|
| CTest, with the options above | 206 / 206 | 203 / 203 (the 3 V1 tests removed) |
| CTest, default options | 182 (published README) | 179 / 179 |
| Root Python suite, run directly | 569 run: 2 failures, 5 errors | 544 run, 0 failures, 44 skipped |
| `model/` Python suite | 478 / 478 | 478 / 478 |
| Compiler warnings | 54 | 42, all in Aeron headers and existing tests |
