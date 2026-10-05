# Architecture and code review — 2026-10-01

**Version reviewed:** tag `v2-review-2026-10-01` (commit `caedfb6`). This is the baseline
`v2-baseline-2026-10-01` plus the post-baseline commits: ccache, toolchain image, Pi watchdog, gross
P&L panel and the research prompt. The review read a frozen `git archive` of that tag. Uncommitted
work in the tree (the indicator research session) was **not** reviewed.

**New ADRs, all *Proposed*:**
- [0020](adr/0020-commands-are-authorized-by-caller-role.md) — authorization by role;
- [0021](adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md) — the kernel owns money;
- [0022](adr/0022-a-state-snapshot-size-budget-with-headroom.md) — snapshot size budget.

---

## 1. The short version

The core of this system — the replicated kernel, Raft integration, durability, and the
gateway's authority handshake — is **unusually careful work**. It is deterministic, bounded,
typed-refusal everywhere, and every past incident has a regression test and a written finding. I
found no correctness bug in the paths I read line by line.

The problems are at the **edges of the trust model** and in **what is still missing before real
money**:

1. **Who may say what (A1).** The kernel checks *who you are* but, for most commands, not *what
   you're allowed to say*. The strategy's key can tell the kernel "a fill happened", and so
   could anything running in the edge container.
2. **Where the money truth lives (A2).** The kernel counts lots but not dollars. Cash, P&L and
   the weekly loss stop live in a Python process that *infers* fills.
3. **No room left (A3).** The kernel's state is 16 bytes short of its 64 KiB ceiling, so the
   fixes for 1 and 2 cannot be added until space is made.

Everything else is ordinary hygiene: alert delivery, paper realism, randomized tests, file
structure, docs. None of it is urgent while trading is paper-only. **A1–A3 should be done before
the first live dollar**, and they are cheapest done together: one byte-format change, one fresh
cluster.

---

## 2. How the system fits together (plain terms)

```
 internet (True Markets WS, public data)
        │
        ▼
 ┌──────────────── nuc1 ─────────────────────────────────────────────┐
 │  EDGE (Python)            computes features from prices,          │
 │   └─ shadow-submitter     sends "here is what I see" + "please    │
 │      (C++, key 12,        trade" commands, keeps the paper P&L    │
 │       caller 21)          book and the weekly loss stop           │
 │                                                                   │
 │  GATEWAY (C++, key 13, caller 22)   reads COMMITTED state, gets   │
 │   └─ paper venue           a proof it may act, "sends" the order, │
 │                            reports fills back as commands         │
 │                                                                   │
 │  VOTER 1  ◄──── Raft over Aeron UDP ────►  VOTER 2 (Pi)           │
 │                                     └──►   VOTER 3 (nuc2)         │
 │  Prometheus, Grafana, md-stream, md-archive                       │
 └───────────────────────────────────────────────────────────────────┘
```

**The idea in one sentence:** every decision that changes money is a command that three machines
agree on *before* it has any effect. The thing that talks to the outside world (the gateway) acts
only on agreed state, with proof.

- **The kernel** (`src/v2_kernel.cpp`) is a pure function: `(state, command) → (new state,
  result)`. It reads no clock and draws no randomness, so every voter computes the same thing.
  It holds the policies, the decisions, risk limits, positions, orders and the outbox of things
  the gateway should do.
- **Replication** (`src/v2_replication.cpp`, a patched willemt Raft) makes sure a command is on
  disk on a majority before anyone applies it.
- **The edge** turns market data into numbers ("features"). The kernel runs a tiny rule program
  over those numbers: if feature 4 > 0 → buy.
- **The gateway** turns committed "please buy" outbox entries into venue sends, and venue fills
  back into commands.

### File structure at a glance

| Path | What | Size | Verdict |
|---|---|---|---|
| `include/velocita/v2/`, `src/v2_*.cpp` | V2: kernel, codecs, replication, node, gateway, venues | ~41 k lines | Well factored by concern; the kernel file is large (3.8 k) |
| `include/velocita/*.hpp`, `src/{raft_node,storage,wire,counter_fsm,client_*,benchmark*,node_main,…}.cpp` | **V1** counter lab: still compiled and tested | ~5 k lines | Legacy; see C2 |
| `tests/` | 149 files: C++ tests, Python tool tests, **plus production catalog rows and the catalog binary** | 59 k lines | Mixed purposes; see C1 |
| `model/src/velocita_model/` | Python edge, storage, research; stdlib only, no dependencies | 10.8 k lines | Good; research ships in the runtime image (C6) |
| `deploy/v2/` | systemd units, containerfiles, toolchain, observability | — | Current and well hardened |
| `deploy/` (top), root `Containerfile` | V1 quadlets, ~25 historical phase-gate scripts, unpinned V1 image | — | Legacy; see C2 |
| `docs/` | 63 docs (31 k lines) + 22 ADRs + 91 evidence folders | — | Rich but hard to navigate; see C3 |
| `tools/` | operator tools (secrets, tokens, markup probe, md backup) | 1.7 k lines | Fine |

### What I checked

| Area | Depth |
|---|---|
| Kernel `apply`, fill path, authorization, dispatch; snapshot budget; Raft apply callbacks; node ingress; gateway loop contract and venue selection; paper venue fill model; edge paper book and stop; systemd units; observability exposure | **Read line by line** |
| Codecs, WAL arena, archive, history recovery | **Skimmed.** These already have per-byte review documents (`docs/F*_REVIEW.md`) |
| Python model suite | **Run:** 342 tests OK |
| Tools suite | **Run:** 107 tests OK |
| C++ suite | **Not re-run in this review.** The last full run was 138/138 inside the velocita-dev image (`fc3629d`), and no C++ changed since |

---

## 3. What is strong (keep doing this)

- **A deterministic, bounded reducer.**
  - No heap growth: every collection is a fixed `std::array` with a count.
  - No clock, randomness, floats or unordered containers in the reducer.
  - Every refusal is a typed `ApplyCode`. That is why replay across x86 and ARM works and why
    "why didn't it trade?" is answerable from the log.
- **Durability done honestly.** The Raft fork counts a vote only once its bytes are on disk; the
  patch is pinned by SHA-256 at configure time, with a refusal that explains itself.
- **The authority barrier.** The gateway may act only on a proof minted from a barrier entry
  *committed in the current term*. A stale leader cannot authorize a send. This is the hard part
  of "a replicated OMS with one executor", and it is done right.
- **A fail-safe gateway.** Every stop cause except a fence is latched until an operator restarts
  it. Unknown outcomes stop new risk instead of guessing.
- **Supply chain.**
  - Digest-pinned base image and a pinned toolchain image.
  - CMake refuses a dependency checkout at the wrong commit.
  - The Python side has zero third-party packages.
  - Containers run with `--cap-drop all --read-only no-new-privileges`, Prometheus on
    localhost, and Grafana on Tailscale with sign-up and anonymous access off.
- **A learning loop.** Every incident becomes a numbered finding, a test, often an alert, and
  sometimes an ADR. This is the most valuable habit in the repo.

---

## 4. Findings

Each finding follows the same flow:
- **Problem:** what it is, in plain terms.
- **Why it's a problem:** what goes wrong.
- **Why it deserves your attention:** the stakes.
- **Intuition behind the fix.**
- **Trade-offs.**
- **ADR:** if one is needed.

Priorities:
- **A**: before live money.
- **B**: soon, cheap and high value.
- **C**: structure and maintainability.
- **D**: known, tracked, re-confirmed.

### A1. The kernel checks who you are, not what you're allowed to say

**Problem.** Every command is signed, and the node checks that the signature matches the
claimed caller. Good. But inside the kernel, only five *control* commands check the caller's role
(operator-only):
- kill epoch;
- executor generation;
- strategy mode;
- catalog install;
- risk limits.

Every other command is accepted from **any** signed caller. That includes "a fill happened"
(`ObserveFill`) and "replace the trading program" (`StageExecutablePolicy`,
`ActivateExecutablePolicy`). Evidence: `src/v2_kernel.cpp` `apply()` (the operator-only block),
and `src/v2_node.cpp` `receive_ingress()`, which checks only `caller_id == key's caller`.

**Why it's a problem.**
- The strategy's key (12) sits in the edge container, next to Python that parses JSON from the
  internet.
- A bug or a compromise there could commit a fake fill.
- The kernel would then believe a position that doesn't exist. Every risk limit (max position,
  long-only) is computed from that position, so the only replicated safety would be computing
  from a lie.
- It could also silently swap the rule program.

**Why it deserves your attention.** ADR 0016 split the strategy from the operator precisely so
that the strategy couldn't loosen its own limits. This is the same hole one level down:
the strategy can't change the limits, but it can change the *facts* the limits are checked
against. On paper the damage is nil. Live, it's the difference between "the edge crashed" and
"the edge bought past its cap".

**Intuition behind the fix.** Think of a bank teller.
- Authentication is checking the ID card.
- Authorization is the teller's list of what this ID may do.

We have the ID check; we need the list. Give each caller a **role** (operator, strategy,
gateway), stored in replicated state, and a fixed table of which roles may send which command:
- only the gateway reports fills;
- only the operator changes programs;
- the strategy only observes, decides and authorizes, inside the risk limits.

Put the check in the kernel, not the node, so it is replicated, deterministic, and shows up in
replay. While there, make command dispatch exhaustive (`std::visit`) so a new command kind can't
compile without someone deciding its roles.

**Trade-offs.**
- **Latency:** none worth measuring, one table lookup per command.
- **Correctness:** strictly better.
- **Cost:** a byte-format change (fresh cluster), and one more setup step (assign roles).

**ADR:** [0020](adr/0020-commands-are-authorized-by-caller-role.md).

### A2. The kernel counts lots, not dollars; the loss stop lives in Python

**Problem.** When a fill arrives, the kernel updates the position and frees the reservation.
It **ignores the fill's price and fee** (`apply_observe_fill`), and it has no cash, cost basis or
P&L.

Those live in the edge's paper book (`shadow/paper_book.py`). The paper book doesn't even see
fills: it *infers* them from position changes reported in refusals, and books them at the limit
price it asked for.

The weekly loss stop is a flag (`exits_only=1`) that this Python process attaches to its own
messages.

**Why it's a problem.**
- On paper, fills happen exactly at the limit, so inference is exact. Live, a fill's price and
  fee are whatever the venue says.
- A loss stop in the least-trusted, single-copy process isn't a breaker. It's a suggestion the
  same process makes to itself. If the edge restarts with a lost SQLite file, or has a bug, the
  stop is gone.
- The gateway also keeps its own balances, and nobody checks the two books against each other.

**Why it deserves your attention.**
- Your hard constraint is money: a $25 budget and $10–12 a week of losses. The one control
  that enforces it is the weakest link in the chain.
- Your plan already lists "loss breaker" and "fill price/fee check" as live blockers
  (`LIVE_TRADING_JOURNEY.md`). This finding says *where* they must live and *why*.

**Intuition behind the fix.** The thing that says "yes, buy" must be the thing that knows how
much money is left. Move the ledger into the kernel:
- Each fill carries price and fee. The kernel checks them: price no worse than the limit, fee no
  more than reserved.
- It keeps cash, average cost, realized P&L and a loss window per account.
- When the window's realized loss passes the operator-set limit, the kernel refuses new buys
  itself. Exits still go.
- Time comes from the fill's committed timestamp, so the kernel still reads no clock.
- The Python book becomes a *view* of committed numbers. Its early stop stays as a second line.

**Trade-offs.**
- **Latency:** a few integer operations per fill; negligible.
- **Correctness:** the money truth gets replicated and replayable, and it can be checked against
  the venue.
- **Scope:** it limits *realized* loss, not the mark-to-market loss of an open position. That is
  fine for quick long-only swaps; say it in the runbook.
- **Cost:** a byte-format change, and it needs A3 first.

**ADR:** [0021](adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md).

### A3. The kernel's state is 16 bytes from its ceiling

**Problem.** The worst-case state snapshot is 65,519 bytes, and the wire envelope that carries it
is 64 KiB (65,536). That leaves 16 bytes free; ADR 0015 already noted "17 left". **Half of the
snapshot (34 KB) is request receipts:** the memory of "I already processed request #N from
caller X", kept so retries aren't executed twice. 256 receipts × 133 bytes.

**Why it's a problem.** A1 and A2 both add state (about 72 and 192 bytes), and so will anything
after them. Today every feature has to win a fight over bytes, and the easy way to win is to
shave a bound that was chosen for a safety reason.

**Why it deserves your attention.** It is the gate on the two live blockers above. It is also a
cheap decision now and an expensive one later.

**Intuition behind the fix.**
- **Spend the space where it's used.** The receipt count is already exported as a metric. Measure
  its peak for a week, then size the table to 2× that (128 is likely). That frees ~17 KB.
- **Trim unused capacity.** The system uses 2–4 policies, not 16; trimming frees ~4 KB more.
- **Hold headroom.** Write a rule into the build that the snapshot stays ≤ 90 % of the ceiling,
  so this never sneaks up again.
- **Not now:** chunked snapshot transfer, which would lift the ceiling. It changes the
  most-reviewed and least-forgiving code path to solve a problem right-sizing already solves.

**Trade-offs.**
- **Latency:** slightly *faster*; smaller snapshots and smaller receipt scans.
- **Correctness risk:** a burst could hit `receipt_capacity_exceeded`. It is typed, refused and
  alerted, never silent, and the 2× margin covers it. Add a 75 %-full alert.

**ADR:** [0022](adr/0022-a-state-snapshot-size-budget-with-headroom.md).

### B1. 33 alert rules, zero places they go

**Problem.** The alert rules are good and promtool-tested. But they fire into Grafana, and
nobody is looking at Grafana at 3 a.m. md-stream recorded nothing for 43 hours before anyone
noticed (finding 50).

**Why it deserves your attention.** Every safety mechanism in this system *stops* rather than
guesses: the gateway latches, the edge exits. That is correct, but a silent stop costs you
the volume you're trying to earn, and you don't find out.

**Intuition behind the fix.** Two pieces:
- **A push channel for "something happened":** Grafana contact point → Telegram bot, which is
  free and works on your phone.
- **A dead-man's switch for "nothing is happening":** Healthchecks.io, free tier, pinged by a
  timer that runs only if the cluster is healthy. It catches the case where the alerting itself
  is dead (nuc1 down).

**Trade-offs:** none of substance. Keep the bot token in the same tmpfs secrets as the keys.

**ADR:** not needed. It is an ops change; record it in the runbook.

### B2. Paper fills are optimistic by construction

**Problem.** The paper venue fills every order **at its own limit price**, which is the price the
edge saw when it decided (ADR 0018). It never looks at the market at fill time. So paper results
include none of:
- the seconds between decision and send (edge → commit → authorize → proof → send);
- a price that moved in those seconds;
- a fill-or-kill that should have been killed.

The SOL run's gross P&L of −$0.05 on $1,144 is partly this: paper slippage is zero by
definition.

**Why it deserves your attention.** You're about to use paper results to choose strategies (the
indicator research) and to decide when to go live. If paper is systematically kinder than live,
the first live week will under-perform paper, and it will look like the strategy broke.

**Intuition behind the fix.** Make the paper venue act like a real fill-or-kill:
- At send time, look up the market price from an independent feed. The gateway can subscribe to
  the same 30 s candles, or read the md-stream file.
- If the market is worse than the limit, **kill** the order.
- If it is better, fill at the market (that is what a swap does).
- Record the slippage (`fill − decision price`) as a metric.

Paper then measures what live would have done.

**Trade-offs.**
- **Reproducibility:** the paper venue stops being a pure function of its inputs. Keep the seeded
  deterministic mode for tests, and make "market-checked" the deployment mode.
- **Results:** paper P&L will get a little worse. That is the point.

**ADR:** amend 0018. A short addendum is enough; no new ADR.

### B3. No randomized testing, and sanitizers aren't in the pipeline

**Problem.** The C++ suite (138 ctest tests) is thorough, but entirely hand-written cases. There
isn't a single randomized test in `tests/*.cpp`. AddressSanitizer/UBSan builds exist only in an
old Pi gate script (`deploy/pi-container/run-phase2-kernel-gate-inside.sh`), not in
`build-image.sh`, which is what you actually run.

**Why it's a problem.** The worst live incident so far was a mass-cancel result that couldn't be
encoded, which would have fail-stopped every voter at once. That was a *reachable state no one
wrote a test for*. In a replicated system, a bug that crashes the reducer crashes **all**
replicas, deterministically. Replication protects you from hardware failure, not from your own
bugs.

**Why it deserves your attention.** This is the class of bug that takes the whole cluster down,
and hand-written tests are structurally bad at finding it: you only test the states you thought
of.

**Intuition behind the fix.** Let the computer think of the states.
- Write a seeded generator of random but well-formed command sequences: policy setup, decisions,
  authorizations, fills, cancels, mass-cancels, acks, in random order and with random duplicates.
- After every step, assert the invariants that must always hold:
  - every result encodes, and the codec round-trips;
  - the snapshot round-trips;
  - replaying the log from zero gives the same state digest;
  - reserved quantities equal the sum of the reservations;
  - the position equals the sum of the fills.
- Run it for 10 k steps in normal CI, and for millions overnight under ASan+UBSan.
- Add the sanitizer build as a `build-image.sh sanitize` mode.

**Trade-offs.**
- **Build time:** the ASan build adds about 2× build time. Run it nightly, not on every image.
- **Production:** no production cost.

**ADR:** not needed. Add a line to the quality standards: "every reducer change runs the
randomized harness".

### B4. A reboot is a lost voter until you push keys by hand

**Problem.** Keys live only in tmpfs (`/run/user/<uid>/velocita-secrets`, ADR 0010). That is a
deliberate custody choice. But it means any reboot loses that voter until you re-push its keys.
The Pi has rebooted from brownouts. All three machines share home power and wifi, so one power
blip reboots all three, and the cluster stays down until you're at a keyboard.

**Why it deserves your attention.**
- In paper, it is lost volume.
- In live, an outage freezes you with a position open. Every position is supposed to be flat or
  bounded, but you can't exit until you're home.

**Intuition behind the fix.** Keep "keys are never readable on disk", but let the *machine* hold
them sealed to itself:
- systemd-creds with the NUCs' TPM 2.0 encrypts each key so only that motherboard, booting that
  OS, can decrypt it.
- A stolen disk is useless, and the voter comes back on its own after a reboot.
- The Pi has no TPM. Leave it manual. Two auto-recovering NUCs are a quorum, so the cluster heals
  itself even if the Pi waits for you.

**Trade-offs.**
- **Custody:** you trade "operator must be present" for "machine must be intact". That is
  weaker against someone with physical access *and* a running machine; stronger for availability.
  It is your call.
- **Live signer key:** keep it manual regardless (ADR 0019).

**ADR:** amend 0010, if you choose it. This is an owner decision, not a recommendation I'd make
for you.

### C1. The production catalog lives in `tests/`

**Problem.**
- The rows that name real on-chain assets — the SOL mint, the USDC mint, cbBTC on Base — are in
  `tests/v2_shadow_catalog_rows.hpp`.
- The `velocita-v2-shadow-catalog` binary, which ships in the voter image and builds the catalog
  the live cluster installs, is `tests/v2_shadow_catalog_main.cpp`.

**Why it's a problem.** "Tests" signals "safe to edit to make a test pass". One edit to a fixture
would change the catalog root the production cluster installs, and only a reviewer who knew this
quirk would catch it.

**Intuition behind the fix.** Production data goes where production code lives:
- move the rows to `src/catalog/truemarkets_rows.{hpp,cpp}` (or a reviewed data file with its
  SHA-256 in the repo);
- move the binary to `src/`;
- keep a test that pins the expected root `3b8b7f79…`, so a change to the rows fails loudly.

**Trade-offs:** a file move. No byte change, since the root is unchanged.

### C2. V1 is still built and shipped in the repo's front door

**Problem.** The V1 counter lab is still compiled and tested on every build:
- `raft-node`, `raft-client` and `raft-dump`;
- `core-tests` and `benchmark-tests`.

The repo root still holds its world:
- the root `Containerfile`, which uses unpinned `ubuntu:26.04` and bypasses the pinned toolchain;
- `deploy/quadlet/velocita-raft-{1,2,3}`;
- about 25 `deploy/run-v2-phase*-gate.sh` scripts from earlier phases.

**Why it's a problem.**
- Build time on every image.
- Confusion about which containerfile is real: a new reader or a new agent will find the root one
  first.
- Phase scripts that silently rot.

**Intuition behind the fix.** The tags already preserve history (`git checkout <v1 tag>` gets it
all back), so the main branch should only contain what runs.
- Move V1 sources and deploy files out of the main build behind `-DVELOCITA_BUILD_V1=OFF`, or
  delete them with a pointer to the tag.
- Move the phase gate scripts to `deploy/history/`.
- Delete or replace the root `Containerfile` with a pointer to `deploy/v2/`.

**Trade-offs.** You lose V1 benchmarks from the default build; they stay one flag away.

### C3. The docs are a library with no front desk

**Problem.**
- There are 63 docs (31 k lines), including a 6.2 k-line `CURRENT_HANDOFF.md`.
- The root `README.md` still says *"V2 is currently an architecture-and-tests proposal, not
  implemented trading behavior"*.
- There is no one-to-two-page "this is what runs today and why" document. `BASELINE_2026-10-01.md`
  comes closest.

**Why it deserves your attention.** You're handing work to new sessions (the research prompt) and
may hand it to people. Each starts by reading the wrong doc first.

**Intuition behind the fix.**
- Write `docs/ARCHITECTURE.md`: the map in §2 above, the trust boundaries, and links to the
  ADRs.
- Rewrite the root README's top to point at it.
- Move phase-era reviews and handoffs to `docs/history/`.
- `docs/README.md` is a protected path, so leave it, and point to the new doc from the root
  README.

### C4. Command dispatch can silently forget a command

**Problem.** `apply_new_command` is a chain of `std::get_if` checks ending in
`return invalid_command`. A new command type that someone forgets to add compiles fine and is
refused at runtime as "invalid".

**Intuition behind the fix.** Use `std::visit` with an overload set. The compiler then refuses to
build until every alternative is handled. Fold it into ADR 0020's allowlist work. At the same
time, consider splitting `v2_kernel.cpp` (3.8 k lines) by concern: policy, decisions, orders and
fills, control. The split costs nothing at runtime and lowers review cost.

### C5. Build script hygiene

**Problem.**
- `CMakeLists.txt` is 1,210 lines.
- `-Wall -Wextra -Wpedantic` is spelled out 140 times, and several V1 executables (`raft-node`,
  `raft-client`, `raft-dump`) get no warning flags at all.
- There is no `-Werror`.
- There are no presets for release or sanitize builds.

**Intuition behind the fix.**
- One `velocita_target()` helper that applies warnings (with `-Werror` for V2).
- Move the raft patch logic (about 150 lines) into `cmake/RaftFork.cmake`.
- Add a `CMakePresets.json` with `release` and `asan`, which B3 then uses.

### C6. Research code ships in the trading image; one stale comment

**Problem.**
- The edge image copies all of `model/src`, including about 3 k lines of `research/`. Research
  code that changes daily, in an image that trades, makes the image's identity change for
  reasons unrelated to trading.
- `shadow/edge.py` still says the log is never reclaimed; compaction has since shipped.

**Intuition behind the fix.**
- Copy only the runtime packages into the edge image, and exclude `research/` and `reporting/`.
- Update the comment.

### D. Known, tracked, re-confirmed (no new action)

- **D1. No CheckQuorum.** A one-way link failure can stall commits until it heals (ADR 0006).
  Liveness only, never safety: the authority barrier keeps a stale leader from authorizing sends.
- **D2. Correlated failure domain.**
  - All three hosts share home power and wifi.
  - The Pi has under-voltage, so check its 27 W PSU.
  - nuc1 concentrates the edge, gateway, monitoring and a voter.
  - Losing nuc1 stops trading safely (no new orders) but blinds monitoring. B1's external dead-man
    switch covers the blindness.
- **D3. One gateway serves one catalog row.** Trading SOL and cbBTC at once means two gateway
  processes with two keys and two sequence ranges. Fine, but plan it before adding the second
  live asset.

---

## 5. Recommended order

| Step | What | Why first | Byte change |
|---|---|---|---|
| 1 | **B1** alert delivery (Telegram + Healthchecks) | an hour of work; protects everything after it | no |
| 2 | **B3** randomized reducer harness + sanitizer mode | gives the safety net for steps 3–5 | no |
| 3 | **A3** measure the receipt peak (start now, it takes a week), then right-size | unblocks A1/A2 | yes |
| 4 | **A1** caller roles + exhaustive dispatch (C4) | closes the biggest trust gap | yes |
| 5 | **A2** kernel money ledger + loss limit + fill price/fee checks | the real loss breaker | yes |
|   | → land 3–5 as **one schema bump, one fresh cluster** | | |
| 6 | **B2** market-checked paper fills | makes paper results predictive of live | no |
| 7 | **C1, C2, C3, C5, C6** structure and docs cleanup | any quiet afternoon | no |
| 8 | **B4** TPM-sealed keys on the NUCs | only if you accept the custody trade-off | no |

Steps 3–5 fit naturally before the M5/M6 live gates in `LIVE_TRADING_JOURNEY.md`. None of them
needs the strategy research to finish, and the research doesn't need them.
