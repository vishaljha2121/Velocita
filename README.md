# Velocita: orderbook baseline

A replicated trading system on home hardware: a deterministic C++20 trading kernel replicated by Raft over
Aeron across two NUC mini PCs and a Raspberry Pi 5, a separate execution gateway, and a signer that holds the
only venue key. The published version traded real money on Solana unattended and its ledger matched the
chain to the atom; its story, results and evidence are at
[github.com/vishaljha2121/Velocita](https://github.com/vishaljha2121/Velocita).

This branch is the baseline for the orderbook version. It keeps the code that ran live and the limit-order
machinery from the paper runs, and drops the V1 counter, the evidence archive and finished history.
**Read [`docs/ORDERBOOK_BASELINE.md`](docs/ORDERBOOK_BASELINE.md) first**: what is here, what was removed,
and the gaps to orderbook trading.

## Design in one table

| Decision | Why |
|---|---|
| Deterministic kernel behind Raft ([ADR 0005](docs/adr/0005-reducer-behind-consensus.md)) | Every voter computes the same state from the same log; the log is the audit trail |
| Raft over Aeron, storage outside the library, on a pinned fork ([0006](docs/adr/0006-asynchronous-durability-and-the-pinned-raft-fork.md)) | "Committed" means durable on two of three disks ([why](docs/guide/fix-durable-quorum.md)) |
| Integer atoms; the kernel owns cash, P&L and the loss limit ([0021](docs/adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md)) | No rounding to reconcile, no limit a strategy can bypass |
| Callers have roles: strategy proposes, operator grants, gateway reports ([0016](docs/adr/0016-strategy-and-operator-callers-are-separate.md), [0020](docs/adr/0020-commands-are-authorized-by-caller-role.md)) | A compromised strategy can ask, never authorize |
| The gateway is a separate process with per-send authority ([0013](docs/adr/0013-request-correlated-execution-authority.md), [0017](docs/adr/0017-the-gateway-is-a-separate-process-reading-committed-state.md)) | A stale or deposed gateway cannot send |
| A signer holds the only key and checks the exact transaction ([0019](docs/adr/0019-live-execution-and-key-custody.md), [0029](docs/adr/0029-the-signer-authenticates-the-gateway-for-a-day.md)) | The gateway cannot sign anything the cluster did not commit |
| "Unknown" is a state; settle only from evidence ([0008](docs/adr/0008-resting-orders-are-confirmed-not-assumed.md), [0025](docs/adr/0025-bounded-swap-verification-and-policy-bound-recovery.md)) | A timeout or a venue's word never moves money |

## Subsystems

| Subsystem | Role | Read more |
|---|---|---|
| Kernel | Pure `apply(state, command)` over one fixed-size struct: integer accounting, caller roles, risk limits, the order and swap lifecycles. No clocks, floats, heap or hash maps | [kernel.md](docs/guide/kernel.md) |
| Gateway | Single-threaded loop that reads committed state, journals each step, talks to the venue, and reports evidence back to Raft. Cannot sign | [gateway.md](docs/guide/gateway.md) |
| Signer | Only holder of the venue key. Re-reads Raft, verifies the exact payload, journals, then signs | [signer.md](docs/guide/signer.md) |
| Edge | Python strategy driver and research: market data, signals, the live swap driver, a market-making simulator | [edge.md](docs/guide/edge.md) |
| Raft and storage | willemt/raft on a pinned fork over Aeron UDP; asynchronous WAL, snapshots, archive and compaction | [storage.md](docs/guide/storage.md) |

## Build

Requires CMake 3.30+, a C++20 compiler, OpenSSL 3 and Git. Aeron and willemt/raft are fetched at pinned
commits; the Raft durability patch is in `deploy/patches`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
cd model && PYTHONPATH=src python3 -m unittest discover -s tests
```

The swap runtime and its HTTPS, TLS and signer tests are behind `-DVELOCITA_BUILD_SWAP_RUNTIME=ON` and the
`VELOCITA_TEST_*` options; the full command and the current test counts are in
[`docs/ORDERBOOK_BASELINE.md`](docs/ORDERBOOK_BASELINE.md#build-and-test). Release images are built natively
on each host in a pinned toolchain image ([`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md)).

| Path | Contents |
|---|---|
| [`src/`](src), [`include/`](include) | Kernel, Raft node, storage, gateway, signer, venue adapters |
| [`tests/`](tests) | Unit, property, crash-injection and multi-process tests; mutation harnesses |
| [`model/`](model) | Python strategy research, simulation and the live driver |
| [`deploy/`](deploy) | Containers, systemd units, monitoring and alerting, install tooling |
| [`tools/`](tools) | Read-only venue and chain probes, log replay, operator helpers |
| [`docs/`](docs) | ADRs, subsystem guides, architecture contracts, runbooks ([map](docs/README.md)) |

## License

[MIT](LICENSE). Aeron (Apache 2.0) and willemt/raft (BSD) are fetched at build time under their own licenses.

Development was AI-assisted: Claude and Codex sessions wrote code and documents under my direction and review.
