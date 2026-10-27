# Velocita

A replicated trading system built from scratch on home hardware: a deterministic C++20 trading kernel
replicated by Raft over Aeron across two NUC mini PCs and a Raspberry Pi 5, with a separate execution gateway
and a signer that holds the only venue key. It traded real money on Solana, unattended, and its ledger matched
the chain to the atom. **[Results →](RESULTS.md)**

## At a glance

| | |
|---|---|
| Real money, unattended | **16 swaps** settled on Solana mainnet, **0** manual interventions |
| Ledger vs blockchain | **16 / 16** settlements equal to the wallet's on-chain change, to 10⁻⁹ SOL |
| Intent → committed on 3 machines | **p50 126 ms · p90 218 ms** (max 556 ms), durable on 2 disks including a Raspberry Pi SD card |
| Intent → transaction in a Solana block | **p50 2 s**, max 3 s, through the venue and the signer |
| Kernel decision | **p50 208 ns · p99 250 ns** |
| Raft thread cost per log record | **42 ns** median: **490×** less than `fsync` on the thread |
| One machine down | the other two keep committing; it rejoins from its own log ([crash safety](#crash-safety)) |
| Duplicate orders, unexplained balances | **0** across 17 h of paper trading and the live run |
| Tests | **206** C++ and **478** Python, mutation-checked, built natively on x86_64 and aarch64 |

<sub>Commit and block latencies: the 25 live intents of 2026-10-15/16 ([data](archive/evidence/benchmarks_2026-10-27/live-commit-latency.txt)).
Kernel and Raft-thread figures: in-memory benchmarks on an Apple M5 Pro ([data](archive/evidence/benchmarks_2026-10-27)).</sub>

## The problem

An automated strategy that touches real money has to survive things a script ignores:

- **Crashes mid-send.** A process can die after an order leaves and before it records that it did. Restarting
  must neither lose the order nor send it twice.
- **An unreliable outside world.** Requests time out, venues answer late or ambiguously, and "failed" does not
  always mean "did not happen".
- **A strategy that should not be trusted.** Bugs in signal code must not be able to drain the account.
- **Cheap hardware.** Three small machines on a home network, any of which can stall, lose its disk or drop off.

The goal: run a real strategy against a real venue with $25, where every cent can be explained afterwards from
the record.

## The intuition

**Separate deciding from doing.** Money and decisions live in one deterministic state machine that three
machines replicate with Raft. Anything that touches the outside world runs in separate processes that may act
only on committed state, and must bring back evidence before the books change.

| Decision | Why |
|---|---|
| Deterministic kernel behind Raft ([ADR 0005](docs/adr/0005-reducer-behind-consensus.md)) | Every voter computes the same cents from the same log; the log is the audit trail |
| Raft over Aeron with storage outside the library, on a pinned fork ([0006](docs/adr/0006-asynchronous-durability-and-the-pinned-raft-fork.md)) | Upstream counted a leader's vote before its log was on disk; durability had to be exact |
| Integer atoms; the kernel owns cash, P&L and the loss limit ([0021](docs/adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md)) | No rounding to reconcile, and no limit a strategy can bypass |
| Callers have roles: strategy proposes, operator grants, gateway reports ([0016](docs/adr/0016-strategy-and-operator-callers-are-separate.md), [0020](docs/adr/0020-commands-are-authorized-by-caller-role.md)) | A compromised strategy can ask, never authorize |
| The gateway is a separate process with per-send authority ([0013](docs/adr/0013-request-correlated-execution-authority.md), [0017](docs/adr/0017-the-gateway-is-a-separate-process-reading-committed-state.md)) | A stale or deposed gateway cannot send |
| A signer holds the only key and checks the exact transaction ([0019](docs/adr/0019-live-execution-and-key-custody.md), [0029](docs/adr/0029-the-signer-authenticates-the-gateway-for-a-day.md)) | The gateway cannot sign anything the cluster did not commit |
| "Unknown" is a state; settle only from finalized chain evidence ([0025](docs/adr/0025-bounded-swap-verification-and-policy-bound-recovery.md), [0027](docs/adr/0027-unsent-effect-release-and-reviewed-baseline.md)) | A timeout or a venue's word never moves money |

All 29 decisions are in [`docs/adr`](docs/adr).

### Subsystems

| Subsystem | Role | Read more |
|---|---|---|
| Kernel | Pure `apply(state, command)` over one fixed-size struct: integer accounting, caller roles, risk limits, the swap lifecycle. No clocks, floats, heap or hash maps, so every voter computes the same bytes | [kernel.md](docs/guide/kernel.md) |
| Gateway | Single-threaded loop that reads committed state, journals each step, talks to the venue and the chain, and reports evidence back to Raft. Cannot sign | [gateway.md](docs/guide/gateway.md) |
| Signer | Only holder of the venue key. Re-reads Raft, decodes and verifies the exact transaction, gets its own authority proof, journals, then signs | [signer.md](docs/guide/signer.md) |
| Edge | Python driver: Binance 1 s bars → stochastic signals → at most one swap intent at a time, as the strategy caller | [edge.md](docs/guide/edge.md) |
| Raft and storage | willemt/raft on a pinned fork over Aeron UDP; asynchronous WAL, snapshots, archive and compaction | [storage.md](docs/guide/storage.md), [fix-durable-quorum.md](docs/guide/fix-durable-quorum.md) |

### The three machines

```mermaid
flowchart LR
  subgraph NUC1["NUC1 · x86_64"]
    V1["voter 1<br/>kernel + Raft"]
    DRV["strategy driver"]
    GW["execution gateway"]
    SG["signer<br/>(only key holder)"]
    MON["Prometheus · Grafana<br/>Alertmanager"]
  end
  subgraph PI["Raspberry Pi 5 · aarch64"]
    V2["voter 2<br/>kernel + Raft"]
  end
  subgraph NUC2["NUC2 · x86_64"]
    V3["voter 3<br/>kernel + Raft"]
  end
  V1 <-->|"Aeron UDP<br/>over Tailscale"| V2
  V2 <-->|"Aeron UDP"| V3
  V1 <-->|"Aeron UDP"| V3
  DRV -->|"intents"| V1
  GW <-->|"committed state,<br/>authority, reports"| V1
  GW <-->|"Unix socket"| SG
  BIN(["Binance<br/>1 s bars"]) --> DRV
  GW <-->|"HTTPS"| TM(["True Markets"])
  TM --> SOL(["Solana"])
  GW -->|"finalized reads"| SOL
  MON -.->|"scrape"| V1 & V2 & V3
```

Any two voters form a quorum, so the cluster keeps committing if one machine fails. Every release that ran
live was built and tested natively on both CPU architectures.

### Happy path: one swap

```mermaid
sequenceDiagram
  participant D as Driver
  participant R as Raft (3 voters)
  participant G as Gateway
  participant S as Signer
  participant T as True Markets
  participant C as Solana
  D->>R: spot-swap-intent (strategy role)
  R->>R: commit on 2 of 3, kernel reserves cash
  G->>R: read committed intent, request send authority
  R-->>G: authority proof (current term)
  G->>T: create unsigned swap
  T-->>G: exact transaction payload
  G->>R: Prepare (payload digest), Attempt
  G->>S: sign this payload
  S->>R: is it the committed intent?
  S-->>G: signature
  G->>T: execute
  T->>C: submit
  G->>R: mark unknown (sent, outcome not yet proven)
  G->>C: getTransaction (finalized)
  C-->>G: owner SOL and USDC deltas, fee
  G->>R: Settlement (exact atoms)
  R->>R: commit, release reservation, book P&L
```

### Negative paths

The venue refuses the route before anything is signed, so the effect is released and the signal re-sent:

```mermaid
sequenceDiagram
  participant D as Driver
  participant R as Raft
  participant G as Gateway
  participant T as True Markets
  D->>R: intent, effect N
  G->>T: create unsigned swap
  T-->>G: unsupported route
  G->>R: ReleaseUnsent(N) after its sign-by time
  R->>R: commit, return the reserved cash
  D->>R: next bar still signals: intent, effect N+1
```

The venue says "failed" but gives no transaction hash (effect 25 in the live run), so the money stays held:

```mermaid
sequenceDiagram
  participant D as Driver
  participant R as Raft
  participant G as Gateway
  participant T as True Markets
  participant C as Solana
  G->>T: execute
  T->>C: submit (lands, fails on chain)
  T-->>G: failed, executed 0, no tx hash
  G->>G: nothing to verify on chain
  Note over G,R: no Settlement command: effect 25 stays reserved
  Note over G: after 180 s: stop, settlement_unproven
  Note over D: no fresh account, so no new decisions
  Note over R,C: later, a history proof finds the landed transaction<br/>and refuses "never executed": the hold was right
```

### Crash safety

Any two of the three voters are a quorum, and "committed" means durable on two disks
([why that needed a Raft fix](docs/guide/fix-durable-quorum.md)). So a machine can drop out and come back
without anyone noticing in the books:

```mermaid
sequenceDiagram
  participant N1 as NUC1 (leader)
  participant N2 as NUC2
  participant P as Raspberry Pi
  Note over P: power cut, SD-card stall or upgrade
  N1->>N2: AppendEntries 17, 18, ...
  N2-->>N1: durable
  Note over N1,N2: 2 of 3 durable: keep committing
  Note over P: restarts, replays its own WAL and snapshot
  P->>N1: back at index 16
  N1->>P: missing entries from the in-memory retained log
  P-->>N1: durable through the current index
  Note over N1,P: 3 of 3 again, same state digest on every voter
```

This happened for real. During the 2026-10-15 upgrade the Pi's new container took minutes to start; NUC1
and NUC2 elected a leader in term 6 and converged at index 17 without it, and the Pi then recovered from its
own data at index 17 ([record](archive/evidence/automated_launch_2026-10-15/strategy-grant/deploy/PI_STARTUP.md)).
On 2026-10-16 every voter was stopped and upgraded in turn, Pi first and the leader last, each rejoining at
the same index with its data unchanged ([record](archive/evidence/automated_launch_2026-10-15/effect25-recovery/RUN_2026-10-16.md)).
The paper run crossed a 20-second loss of one voter and an election burst with no unknown outcome.

What makes a restart safe: a torn WAL write is always a bad block checksum, never a valid-looking record; a
snapshot is replayed before it is trusted; a commit index past the end of the log refuses recovery instead
of clamping; and pre-vote stops a returning voter from deposing a healthy leader. Limits: losing NUC1 keeps
the cluster up but stops trading, because the gateway and signer run there; and a voter that falls behind the
leader's last compaction (2,048+ entries, days at this system's rate) needs an operator repair from another
voter's archive, because online snapshot install is designed but not built.

### Secrets and how they are shared

Voters and callers authenticate every message with 32-byte HMAC keys that are created in the owner's Keychain,
pushed over SSH into RAM-only tmpfs on each host, and mounted read-only into exactly the process that needs
them. Keys are derived per direction and per cluster incarnation, so a captured frame can't be reflected or
replayed into another cluster. The venue's P-256 key, which can move money, lives only in the signer, behind
its own Unix user and a network allowlist of the three voters; the gateway gets a short-lived token through
the signer and never sees the key.

```mermaid
flowchart LR
  KC["Mac Keychain"] -->|"SSH stdin"| TF["host tmpfs (RAM only)"]
  TF -->|"read-only mount"| P["voter / driver / gateway"]
  VK["venue key"] -->|"owner install"| SG["signer only"]
  SG -->|"signs a 47-byte auth message"| GW["gateway gets a token"]
```

Why this way, and what it doesn't protect: [secrets.md](docs/guide/secrets.md).

### Storage: databases and caches

No general-purpose database touches money. Each voter keeps a checksummed block WAL, atomically replaced Raft
metadata, a canonical snapshot that is replayed before it is trusted, and immutable archive segments for
compacted history. The gateway and signer keep small journals written *before* every external act. SQLite holds
recorded market data and Prometheus holds metrics, both beside the money path. Every cache (the in-memory
log, the gateway's state copy, the driver's account file) can only make the system wait when stale, never act
on old data. Details: [storage.md](docs/guide/storage.md).

### Memory: budgets, atlas and arenas

Every long-lived object has a size fixed at compile time: the whole replicated state is 78,512 of an
81,920-byte budget, and its canonical snapshot 61,586 of 65,536 bytes, each enforced by `static_assert`.
Memory comes from arenas allocated once at start-up: a 16 MiB ring for the retained log, 64 × 4 KiB WAL
blocks handed to a writer thread over lock-free queues, and fixed tables scanned linearly. Full means a typed
refusal, never an allocation or a stall. Details: [memory.md](docs/guide/memory.md) and the field-by-field
[memory atlas](docs/V2_MESSAGE_MEMORY_ATLAS.md).

## How it was built

Nearly 400 commits over eight weeks, in small steps; read them in order with `git log --reverse`. Each V2
phase was accepted on the target hardware before the next began.

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

### Progress, stage by stage

![Before and after: Raft thread time per record, compaction pause, commit latency](docs/img/progress.svg)

| When | What was measured | Change | Result |
|---|---|---|---|
| Aug 29 | 3 voters on one Pi SD card: commit p50 396 ms, p99 828 ms at 5 cmd/s; leaderless at 20 cmd/s (~9 fsyncs per command) | V2 storage: one writer thread per voter, three machines | Raft thread median per record 20.6 µs → 0.042 µs |
| Sep 15 | A power-cut sweep at every write offset found a commit index past the log end, and 64 KiB written per 200-byte command | Refuse instead of clamping; 4 KiB blocks | Then the benchmark refused 4 of 5 records, fixed with a 64-block arena |
| Sep 15 | Upstream Raft counted the leader's vote before its entry was durable | [Durable-quorum fork](docs/guide/fix-durable-quorum.md) | Committed = on 2 of 3 disks |
| Oct 1 | Wi-Fi drops inflated terms: 49 → 191 in 6 hours | Pre-vote | A returning voter no longer deposes a healthy leader |
| Oct 3 | Compaction pause grew 0.8 s → 4.8 s; term 1 → 65 in 48 hours | Rotating two-segment scrub | Pauses 0.1–0.4 s, below the election timeout |
| Oct 7–8 | Paper: 17.2 h, 58 fills | – | 0 unknown outcomes across an election burst and a lost voter |
| Oct 9 | Gateway read state one entry stale and stopped itself | [Read fence](archive/evidence/gateway_stop_2026-10-09/README.md) | Stale readings discarded |
| Oct 15 | One failed read during an election stopped the gateway | [Bounded retry](docs/guide/fix-read-outage-retry.md) | Live run crossed term 6 → 8 without stopping |
| Oct 15–16 | Live: 25 intents, 16 swaps | – | 16 / 16 exact; commit p50 126 ms |
| Oct 16 | Venue reported a failure with no transaction hash | On-chain proof before "never executed" | Proof found the landed transaction and refused: correct |

Practices: test-first changes, an ADR per boundary, crash-injection and multi-process drills, mutation runs
that prove each mutant was rebuilt and killed, and native qualification on x86_64 and aarch64 before each
live deploy.

## What we observed

**In testing.** The tests found real defects before money did:
- With an asynchronous log, upstream Raft counted the leader's vote before its entry was on disk, so a leader
  crash could lose an acknowledged entry: [fixed with a pinned fork](docs/guide/fix-durable-quorum.md).
- A gateway could read state one entry stale from a leader whose disk lagged:
  [fixed with a read fence](archive/evidence/gateway_stop_2026-10-09/README.md).
- A single failed read during a leader election stopped the gateway for good:
  [fixed with a bounded retry](docs/guide/fix-read-outage-retry.md).
- V1 drills showed failover in 1.5 s and no commit without quorum ([results](docs/V1_FAILURE_DRILL_RESULTS.md)).

**In paper.** 17.2 hours, 29 round trips and $804 of volume through the full cluster, with every fill
reconciled and no unknown outcome across an election burst and a lost voter. The rule was flat before fees
(+$0.0036); replays over 7 and 30 days confirmed it has no edge before costs.

**Live.** 25 intents, 16 swaps and $202 of volume over three hours, unattended. The ledger matched the chain
on every settlement. It lost $0.95, almost all of it the venue's spread and fee, and stopped correctly on an
order the venue reported without a hash.

**[Full results, round trips and evidence →](RESULTS.md)**

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

## License

[MIT](LICENSE). Aeron (Apache 2.0) and willemt/raft (BSD) are fetched at build time under their own licenses.

Development was AI-assisted: Claude and Codex sessions wrote code and documents under my direction and review.
Their handoff notes are in [`archive/handoffs`](archive/handoffs).
