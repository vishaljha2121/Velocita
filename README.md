# Velocita

A replicated trading system on home hardware: a deterministic C++20 trading kernel replicated by Raft over
Aeron UDP across three voters (two x86-64 NUC mini PCs and a Raspberry Pi 5), a separate execution gateway, a separate
signer that holds the only venue key, and a Python strategy edge. It traded real money unattended on
Solana through True Markets in October 2026.

The point of the project is the engineering, not the strategy: every order is a committed, replicated
command before anything leaves the machine, every external effect is reconciled against the chain, and
the system stops rather than guesses when the evidence is incomplete.

## What it did

On 2026-10-15/16 the cluster ran a one-second stochastic SOL/USDC rule with $25 of managed capital:

| | |
|---|---|
| Strategy intents committed | 25 (effects 1 to 25), after a one-time operator grant |
| Swaps settled | 16 (8 round trips), $202.13 executed volume |
| Released before signing | 8 (venue refused the route or the route lookup failed; the driver re-sent) |
| Realized P&L | -$0.952 including $0.405 of venue fees |
| Intent to on-chain block | 2-3 s |
| Raft vs chain | every settlement equals the owner wallet's on-chain SOL and USDC change to the atom |
| Stop | effect 25: the venue reported "failed" with no transaction hash; the gateway refused to settle it without proof |

The loss is almost entirely execution cost: buys filled about 59 bp above the Binance reference and sells
about 33 bp below it (20 bp of each is the venue fee), while the reference itself moved about -2 bp per
round trip. Earlier replays had already shown the rule has no edge before costs; the run measured the cost
precisely. Effect 25 later turned out to have landed on chain and failed there, which is exactly why the
gateway would not mark it "never executed" from the venue's word alone.

## Architecture

```text
 Binance 1 s bars ──> strategy driver (Python) ── spot-swap-intent ──┐
                                                                     v
          ┌──────────── Raft over Aeron UDP (willemt/raft, pinned fork) ────────────┐
          │  voter 1 (NUC1)          voter 2 (Raspberry Pi 5)        voter 3 (NUC2) │
          │  deterministic kernel: integer accounting, caller roles, risk limits,   │
          │  swap lifecycle Intent -> Prepare -> Attempt -> Settlement             │
          └──────────────────────────────┬──────────────────────────────────────────┘
                                         │ committed state reads + authority proofs
                                         v
                   execution gateway (C++) ── create unsigned swap ──> True Markets
                         │       ^                                       │
          signing request│       │signature                              v
                         v       │                                    Solana
                   signer (C++, holds the only API key; checks the exact transaction)
                         │
          finalized reads│ (getTransaction, isBlockhashValid, history)
                         v
                   settlement admission -> Settlement command back through Raft
```

- **Kernel**: one deterministic reducer behind consensus. Cash, positions, fees and realized P&L are
  integers in atoms; the loss limit and caller permissions are enforced on apply, identically on every voter.
- **Raft over Aeron**: Aeron carries client, peer and response traffic; storage, snapshots and
  compaction are owned outside the Raft library. The pinned fork counts a leader's vote only once its own
  log bytes are durable ([ADR 0006](docs/adr/0006-asynchronous-durability-and-the-pinned-raft-fork.md)).
- **Gateway**: a separate process that reads committed state, asks the cluster for request-correlated
  authority before each send, and journals every step so a crash at any point recovers without a duplicate
  order ([ADR 0013](docs/adr/0013-request-correlated-execution-authority.md),
  [ADR 0017](docs/adr/0017-the-gateway-is-a-separate-process-reading-committed-state.md)).
- **Signer**: the only holder of the venue key; it independently checks that the transaction it is asked
  to sign is the committed intent ([ADR 0019](docs/adr/0019-live-execution-and-key-custody.md),
  [ADR 0029](docs/adr/0029-the-signer-authenticates-the-gateway-for-a-day.md)).
- **Settlement**: a swap settles only from finalized chain evidence that matches the retained payload;
  fees, principal and the owner's net receipt are attributed exactly
  ([ADR 0025](docs/adr/0025-bounded-swap-verification-and-policy-bound-recovery.md)).
- **Edge**: the Python model computes indicators and proposes; it cannot author an order or a transaction.

The 29 architecture decisions are in [`docs/adr/`](docs/adr), and [`docs/README.md`](docs/README.md) maps the rest.

## Repository layout

| Path | Contents |
|---|---|
| [`src/`](src), [`include/`](include) | C++20 kernel, Raft node, storage, gateway, signer, venue adapters (about 52k lines) |
| [`tests/`](tests) | C++ unit, property, crash-injection and multi-process drills; Python tests and mutation harnesses |
| [`model/`](model) | Python strategy research, the shadow edge and the live driver, with 478 tests |
| [`deploy/`](deploy) | Containerfiles, systemd units, Prometheus/Grafana/Alertmanager rules, install and launch tooling |
| [`tools/`](tools) | Operator tools: secrets, soak tests, market-data probes |
| [`docs/`](docs) | ADRs, architecture contracts, design reviews and runbooks |
| [`archive/`](archive) | The evidence trail: qualification runs, deployment records and session handoffs, kept as written |

## Build and test

Prerequisites: CMake 3.30+, a C++20 compiler, OpenSSL 3 and Git. Aeron and willemt/raft are fetched at pinned
commits; the Raft patch is applied at configure time and refused if it does not apply.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The swap runtime and its HTTPS and signer drills build with `-DVELOCITA_BUILD_SWAP_RUNTIME=ON` and the
`VELOCITA_TEST_*` options; the default build runs 182 CTest targets and the full set 206. The model tests run with
`cd model && PYTHONPATH=src python3 -m unittest discover -s tests`.

Releases that ran on the cluster were qualified natively on both architectures (x86_64 NUC and aarch64 Pi)
inside pinned containers, and safety-critical changes carry mutation runs that prove each mutant was
rebuilt and killed. Those records are under [`archive/evidence/`](archive/evidence).

## Limits

- The trading rule is a test load, not a strategy with an edge; the costs above dominate it.
- Effect 25 is still pending: the fix that settles a landed-and-failed swap from chain evidence is designed
  but not built.
- One operator, home network, one venue, one pair. Two NUCs and a Pi are separate machines but share a
  house, a router and an ISP.
- This is a personal engineering project, not financial software, and it is not offered as such.

## History

The repository starts in August 2026 as V1, an educational Raft counter on a single Raspberry Pi
([`CHANGELOG.md`](CHANGELOG.md)), and grows into V2 through the phases recorded in the ADRs and the
archive. Development was AI-assisted: Claude and Codex sessions wrote code and documents under the
owner's direction and review, and their handoff notes are kept in [`archive/handoffs/`](archive/handoffs).
