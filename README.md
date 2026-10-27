# Velocita

A replicated trading system built from scratch on home hardware: a deterministic C++20 trading kernel,
replicated by Raft over Aeron UDP across two NUC mini PCs and a Raspberry Pi 5, with a separate execution
gateway and a separate signer that holds the only venue key. It traded real money on Solana, unattended.

Every order is a committed Raft command before it leaves the machine. Every fill is settled from finalized
chain data. When the evidence is incomplete, the system stops instead of guessing.

## Live run, 15–16 October 2026

| | |
|---|---|
| Capital | $25 USDC, 0.12 SOL per trade, $10 loss stop |
| Strategy intents | 25, all committed through Raft |
| Swaps settled | 16 (8 round trips), $202.13 volume |
| Intent to on-chain block | 2–3 s |
| Ledger vs chain | 16 of 16 settlements equal to the wallet's on-chain change, to the atom |
| Realized P&L | −$0.95, including $0.40 of venue fees |
| Stop | The venue reported effect 25 as failed with no transaction hash. The gateway would not settle it without proof; the chain later showed it had landed and failed. |

The loss was execution cost: fills averaged 59 bp above the reference on buys and 33 bp below it on sells,
while the reference itself moved −2 bp per round trip. The strategy was a test load; the system was the point.
Evidence: [`archive/evidence/live_run_2026-10-15`](archive/evidence/live_run_2026-10-15).

## Architecture

```text
strategy driver ──intent──> Raft over Aeron: 3 voters, one deterministic kernel
                                       │ committed state + per-send authority proof
                                       v
            signer <──sign request── execution gateway ──> True Markets ──> Solana
       (only key holder,                     ^                                │
        checks the exact tx)                 └──── finalized transaction ─────┘
                                       │
                                       └──Settlement command back through Raft
```

- **Kernel**: integer accounting, caller roles, risk limits and the swap lifecycle, applied identically on
  every voter ([ADR 0005](docs/adr/0005-reducer-behind-consensus.md), [0021](docs/adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md)).
- **Consensus**: willemt/raft on a pinned fork that counts a leader's vote only once its log is durable;
  storage, snapshots and compaction live outside the library ([ADR 0006](docs/adr/0006-asynchronous-durability-and-the-pinned-raft-fork.md)).
- **Gateway**: a separate process that journals every step, so a crash at any point recovers without a
  duplicate order ([ADR 0013](docs/adr/0013-request-correlated-execution-authority.md), [0017](docs/adr/0017-the-gateway-is-a-separate-process-reading-committed-state.md)).
- **Signer**: holds the key and signs only the transaction the cluster committed ([ADR 0019](docs/adr/0019-live-execution-and-key-custody.md), [0029](docs/adr/0029-the-signer-authenticates-the-gateway-for-a-day.md)).
- **Settlement**: only from finalized chain evidence matching the signed payload ([ADR 0025](docs/adr/0025-bounded-swap-verification-and-policy-bound-recovery.md)).

All 29 decisions are in [`docs/adr`](docs/adr); [`docs/README.md`](docs/README.md) maps the rest.

## How it was built

Nearly 400 commits over eight weeks, in small steps; read them in order with `git log --reverse`.
Each V2 phase was accepted on the target hardware before the next began.

| Date (2026) | Step | Ref |
|---|---|---|
| Aug 26 | V1: Raft counter over Aeron on a Raspberry Pi 5, with WAL, replay and deduplication | [`v1.0.0`](https://github.com/vishaljha2121/Velocita/tree/v1.0.0) |
| Aug 27–29 | Leader-aware client routing and bounded recovery | [`v1.1-phase2`](https://github.com/vishaljha2121/Velocita/tree/v1.1-phase2) |
| Sep 1–7 | V2 contracts, deterministic kernel, authoritative decision evaluation | [`v2-phase3-accepted`](https://github.com/vishaljha2121/Velocita/tree/v2-phase3-accepted) |
| Sep 10 | Risk reservations, orders and a transactional outbox | [`v2-phase4-accepted`](https://github.com/vishaljha2121/Velocita/tree/v2-phase4-accepted) |
| Sep 11–15 | Venue simulator and execution gateway | [`v2-phase5-corrected-accepted`](https://github.com/vishaljha2121/Velocita/tree/v2-phase5-corrected-accepted) |
| Sep 16 | Kernel behind Raft; asynchronous durable WAL and snapshots | [`v2-phase7-complete`](https://github.com/vishaljha2121/Velocita/tree/v2-phase7-complete) |
| Sep 17 | Execution authority, fencing and emergency control | [`v2-phase8-mechanisms`](https://github.com/vishaljha2121/Velocita/tree/v2-phase8-mechanisms) |
| Sep 30–Oct 1 | Paper trading on the home cluster; pre-vote; full architecture review | [`v2-baseline-2026-10-01`](https://github.com/vishaljha2121/Velocita/tree/v2-baseline-2026-10-01) |
| Oct 7 | One-second stochastic strategy in paper | [`v2-paper-stochastic-2026-10-07`](https://github.com/vishaljha2121/Velocita/tree/v2-paper-stochastic-2026-10-07) |
| Oct 9–13 | Inspecting signer, swap runtime, finalized settlement | [`a890307`](https://github.com/vishaljha2121/Velocita/commit/a890307) |
| Oct 14 | First live swap from the cluster ($1) | [`6c54f3f`](https://github.com/vishaljha2121/Velocita/commit/6c54f3f) |
| Oct 15 | Unattended driver and the release that ran live | [`ba299ff`](https://github.com/vishaljha2121/Velocita/commit/ba299ff) |
| Oct 16 | Recovery for a venue-failed swap, qualified on both architectures | [`64e6f9f`](https://github.com/vishaljha2121/Velocita/commit/64e6f9f) |

Practices: test-first changes, an ADR per boundary, crash-injection and multi-process drills, mutation runs
that prove each mutant was rebuilt and killed, and native qualification on x86_64 and aarch64 before each
live deploy. The records are in [`archive/`](archive).

## Build

Requires CMake 3.30+, a C++20 compiler, OpenSSL 3 and Git. Aeron and willemt/raft are fetched at pinned commits.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure    # 182 tests; 206 with the swap runtime and VELOCITA_TEST_* options
cd model && PYTHONPATH=src python3 -m unittest discover -s tests    # 478 tests
```

| Path | Contents |
|---|---|
| [`src/`](src), [`include/`](include) | Kernel, Raft node, storage, gateway, signer, venue adapters (C++20, ~52k lines) |
| [`tests/`](tests) | Unit, property, crash-injection and multi-process tests; mutation harnesses |
| [`model/`](model) | Python strategy research and the live driver |
| [`deploy/`](deploy) | Containers, systemd units, monitoring and alerting, install tooling |
| [`docs/`](docs) | ADRs, architecture contracts, design reviews, runbooks |
| [`archive/`](archive) | Qualification, deployment and live-run evidence, kept as written |

## Limits

- One operator, one venue, one pair; the three machines share a house and a network.
- Effect 25 is still reserved: settling a landed-and-failed swap from the transaction the proof finds is designed, not built.
- A personal engineering project, not financial software.

Development was AI-assisted: Claude and Codex sessions wrote code and documents under my direction and review.
Their handoff notes are in [`archive/handoffs`](archive/handoffs).
