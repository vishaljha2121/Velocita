# Results

What Velocita did in testing, in paper trading and live. Every number links to the record it came from.

## Live run, 15–16 October 2026

Release [`ba299ff`](https://github.com/vishaljha2121/Velocita/commit/ba299ff) on three voters, $25 USDC,
0.12 SOL per trade, $10 loss stop, the one-second stochastic rule driven by Binance SOLUSDT bars, swaps
through True Markets on Solana mainnet. No manual intervention from the first trade to the stop.

| | |
|---|---|
| Strategy intents committed | 25 (effects 1–25), Raft indices 20–134 |
| Swaps settled | 16 = 8 round trips, $202.13 volume |
| Released before signing | 8: the venue refused the route or the route lookup failed; each released by a committed command and re-sent ~25 s later |
| Intent to on-chain block | 2–3 s (median 2 s) |
| Ledger vs chain | 16 of 16 settlements equal the owner wallet's on-chain SOL and USDC change, to the atom |
| Realized P&L | −$0.952 (−952,140 USDC atoms), equal to the sum of the round trips on chain |
| Venue fees | $0.405, exactly 20.0 bp of each trade |
| Network fees | 1,488,000 lamports, all paid by the venue's fee payer |

**Where the money went.** Against the Binance reference one second before each block, buys filled 54–68 bp
above (average 59.1) and sells 30–44 bp below (average 33.2); 20 bp of each is the venue fee. The reference
itself moved −2.0 bp per round trip on average. So a round trip cost about 94 bp before the market did
anything: about 40 bp of fees and about 52 bp of venue price over the reference.

| Round trip | Held | Result | Result bp | Reference move bp |
|---|---|---|---|---|
| E1 → E3 | 10m 23s | −$0.1323 | −104.3 | −5.7 |
| E4 → E5 | 3m 34s | −$0.1113 | −87.7 | 0.0 |
| E9 → E11 | 4m 34s | −$0.1305 | −102.7 | −15.1 |
| E14 → E16 | 12m 37s | −$0.1278 | −100.5 | −14.2 |
| E17 → E18 | 7m 07s | −$0.1105 | −87.0 | 0.0 |
| E19 → E20 | 5m 56s | −$0.1263 | −99.4 | 0.0 |
| E21 → E22 | 17m 51s | −$0.1005 | −79.5 | +18.1 |
| E23 → E24 | 11m 17s | −$0.1130 | −89.0 | +0.9 |

**The stop: effect 25.** The buy committed at index 134 (03:06:29 UTC). Its transaction
[`3gPjgF…qiqk`](https://solscan.io/tx/3gPjgFxhmeoP2MornCnoMSjymr7b5XaEJMdje4LAMNWV1Cu8EVXGjxmq81J6qLYnCauxE2GaGqbXutkGUBasqiqk)
landed in slot 457,481,187 and failed (`InstructionError [2, Custom 5]`), moving nothing from the owner. The
venue reported "failed, temporarily unavailable", executed 0, and **no transaction hash**. Without a
transaction to verify, the gateway refused to settle and blocked after 180 s; without a fresh account, the
driver refused to decide. A later recovery release ([`64e6f9f`](https://github.com/vishaljha2121/Velocita/commit/64e6f9f))
added a "never executed" outcome that must be proven on chain. Its proof refused (`chain_unproven`; the gateway
does not log which check failed), and the wallet's history shows why: the transaction above had landed. The reservation is still held; settling it as "failed without charge"
from the found transaction is the remaining step.

Other observations: two short DNS failures on NUC1 (12 s while flat, 8 min while already blocked); one
change of Raft leader between effects 24 and 25 (term 6 → 8) with no effect on commits.

Evidence: [`archive/evidence/live_run_2026-10-15`](archive/evidence/live_run_2026-10-15) (wallet
transactions, driver events, the per-trade join) and
[`EFFECT25_VENUE_FAILURE.md`](archive/evidence/automated_launch_2026-10-15/EFFECT25_VENUE_FAILURE.md).

## Paper trading, 7–8 October 2026

The same rule on the three-voter cluster against the paper venue: fill-or-kill at the Binance close with a
20 bp fee per side, all orders through Raft, risk and the gateway.

| | |
|---|---|
| Duration | 17.2 h, 58 fills = 29 round trips, 0 rejected, 0 unknown outcomes |
| Volume | $803.98 |
| Result before fees | +$0.0036 |
| Result after fees | −$1.604 (fees $1.608) |
| Round trips positive before / after fees | 9 / 0 of 29 |
| Resilience | crossed an election burst (term 3 → 16), an edge redeploy and a 20 s loss of one voter with no unknown outcome |

Every fill reconciled between the venue and the kernel.
Evidence: [`stochastic_cash_2026-10-07/RESULTS.md`](archive/evidence/stochastic_cash_2026-10-07/RESULTS.md).

## Research checks before going live

| Check | Finding |
|---|---|
| Replay of the paper window through the same indicator code | 29 round trips, $803.96, +$0.0036: matches the live paper session |
| 7-day replay | 328 round trips, −0.96 bp of volume before costs |
| 30-day replay | 1,475 round trips, −0.12 bp before costs; 10 of 30 days positive |
| Venue reference price vs Binance, 24 h | venue 9.4 bp below Binance on average, standard deviation 2.4 bp |

The rule has no edge before costs; the live run was run to prove the system, and it measured the cost.
Evidence: [`stochastic_rule_replay_2026-10-09`](archive/evidence/stochastic_rule_replay_2026-10-09/README.md),
[`venue_price_basis_2026-10-09`](archive/evidence/venue_price_basis_2026-10-09/README.md).

## What testing found

| Found by | Problem | Fix |
|---|---|---|
| Durability tests | With an asynchronous log, upstream willemt/raft counts a leader's own vote before its entry is on disk, so a leader crash can lose an acknowledged entry | Pinned fork with a durable-index hook ([explanation](docs/guide/fix-durable-quorum.md), [ADR 0006](docs/adr/0006-asynchronous-durability-and-the-pinned-raft-fork.md)) |
| Paper run, 2026-10-09 | The gateway read state one entry old from a leader whose disk lagged its followers, re-reported a finished order, and stopped itself | The leader defers reads until its own apply catches up; the gateway discards reads older than results it has seen ([write-up](archive/evidence/gateway_stop_2026-10-09/README.md)) |
| Live preparation | One failed state read during an election stopped the gateway | Bounded 120 s read-outage retry ([explanation](docs/guide/fix-read-outage-retry.md), [`a835deb`](https://github.com/vishaljha2121/Velocita/commit/a835deb)) |
| Live preparation | A route the venue refused left an effect stuck as reserved | Release of an expired unsent effect through a committed command ([ADR 0027](docs/adr/0027-unsent-effect-release-and-reviewed-baseline.md)) |
| V1 failure drills | Failover, quorum loss, duplicate retries, observability outages | All passed: failover in 1.5 s, no commit without quorum, a retried request applied once ([results](docs/V1_FAILURE_DRILL_RESULTS.md)) |

Each release that ran on the cluster was built and tested natively on x86_64 and aarch64 inside pinned
containers (for example [64e6f9f](archive/evidence/automated_launch_2026-10-15/effect25-recovery/qualification-64e6f9f.json):
59 CTest targets and 54 continuous drill effects per architecture), and safety-critical changes carry
mutation runs (for example 36 of 36 mutants killed for the effect 25 recovery).
