# Promoting the stochastic paper strategy to live: plan and status

Written 2026-10-07 after the owner said the paper strategy looks good and asked for a plan to promote
it. **Live trading is not approved by this document.** It orders the work, says what is done, and
names the owner decisions. `LIVE_TRADING_JOURNEY.md` remains the step-by-step gate list; this file
maps the checkpoint review (`docs/evidence/stochastic_cash_2026-10-07/CHECKPOINT_REVIEW.md`) onto it.

## Where the strategy stands

`stochastic_1s_cash_v1`: long-only SOL, 0.12 SOL clips, one-second bars, paper venue. Its first
session made 29 round trips in 17 hours and was flat before fees (`RESULTS.md`). The system around
it is working; the economics are not proven:

- The paper fee is 20 bp a side. The measured True Markets cost is about 100 bp to buy and 98 bp
  to sell. At the measured cost the first session would have lost $7.96, about $1 for every $101 of
  volume.
- The paper price is the Binance SOLUSDT close. A live order fills at True Markets' price.
- The paper venue always fills completely at the limit.

So the first thing "live" buys is evidence about real fills and real cost, at the smallest size
(the book, ch. 52: a small live test is an information purchase). It is not expected to make money
at 100 bp a side.

## What must be true before the first real order

Last updated 2026-10-09 16:30 UTC. Status words: **Done** (built, tested, running on the hosts),
**Built** (committed and tested, not running), **Open**.

| # | Requirement | Status | Where |
|---|---|---|---|
| 1 | The edge cannot freeze or go blind (review A1, A2) | **Done** 2026-10-08 | `0063c86` |
| 2 | The market-data allowlist is enforced (A3) | **Done** 2026-10-08 | `0063c86` |
| 3 | The archive cannot silently fill (B1) | **Done** 2026-10-08: fewer commands, usage metric, alerts at 70% and 90% | `d542601`, `9dcca2c`, `3264351` |
| 4 | The kernel checks every fill's price and fee and owns cash, P&L and a loss limit (ADR 0021) | **Done** 2026-10-08: running with a $10 loss limit | `4c04f75`, `f55209e` |
| 5 | Snapshot room (ADR 0022) | **Partly done**: policy table cut to 8 and running. Receipt table change waits for the week of measurement (from 2026-10-07) | `d8ad46d` |
| 6 | Callers are authorized by role (ADR 0020) | **Built** 2026-10-08, not deployed (schema 11, needs a new incarnation) | `11ad1ae` |
| 7 | An operator flatten path | **Built** 2026-10-09, not deployed: `reduce_only` lets exits through, `flatten` sells the position (ADR 0024) | `04b474b` |
| 8 | An operator command to resolve an order in conflict | **Built** 2026-10-08, not deployed (ADR 0024) | `9f96cf3` |
| 9 | The signer (ADR 0019 section 1) | **Built** 2026-10-09 with a test key and mock payloads only; not shipped in any image. A real key format and a real payload decoder are not built | ADR 0019 amendment |
| 10 | The venue transport | **Built** 2026-10-09 against a local mock only; it can connect only to the loopback address and is in no image. TLS, the real endpoint and recorded reply shapes are not built | ADR 0019 amendment 2 |
| 11 | The venue contract questions | **Open**; several need the owner or True Markets | journey step 3 |
| 12 | The paper venue can reject, partly fill and time out | **Built** 2026-10-09, off by default: four gateway flags switch on faults the venue already simulated. Needs a gateway restart and the owner's choice of rates | runbook 5.2.1 |
| 13 | Decide and fill on the venue's own price, with cost measured on both sides | **Partly done** 2026-10-09: the price difference is measured and small (9 bp level, 2.5 bp noise, venue 1 to 2 s behind). Open: the paper fee stays 20 bp against a measured 100 bp (owner decision), and executable quotes need a call outside the keyless-GET approval | `docs/evidence/venue_price_basis_2026-10-09/` |
| 14 | Startup balance reconciliation against the venue | **Open** | journey step 7 |
| 15 | Outside heartbeat armed | **Open**: installed, waits for the owner's Healthchecks URL | review B6 |
| 16 | Drills while holding a position: kill, leader loss, gateway restart | **Open** | journey step 8 |
| 17 | A live gateway refuses an account with no loss limit | **Built** 2026-10-09: always on for the live venue, `--require-loss-limit` for paper. Not deployed (gateway image) | `989fe04` |
| 18 | Leader steps down when it cannot reach a quorum | **Built** 2026-10-08, not deployed | `20329b6` |
| 19 | Raft library assertions active in release builds | **Built** 2026-10-08, not deployed | `83a103e` |
| 20 | Review of the files the kernel review did not read (node loop, write-ahead log, archive store, codecs, authentication) | **Done** 2026-10-09: node loop, authentication, write-ahead log, archive store, protocol, codecs, Aeron transport and the skipped kernel ranges read. The second pass's findings are plan items 22 to 24; the third pass found no defect | `REVIEW_2_NODE_AND_STORAGE.md`, `REVIEW_3_CODECS_AND_REMAINDER.md` |
| 21 | Review of the research and backtest code | **Partly done** 2026-10-09: the running rule replayed on a week of data with the live code. No edge before costs; the result is the cost. The research code itself is not yet read | `docs/evidence/stochastic_rule_replay_2026-10-09/` |
| 22 | A leaked or faulty key cannot fill the log: per-caller rate limit at ingress | **Built** 2026-10-09, not deployed (voter image) | review 2, finding 1 |
| 23 | A voter survives a crash or power cut without a person: archive leftovers and a torn last log block | **Open**. Done so far: the runbook, and the start-up error now names a stray archive file. Not done: automatic cleanup, and any repair for a torn block | review 2, findings 2 and 4 |
| 24 | The log writer thread sleeps when idle | **Built** 2026-10-09, not deployed (voter image) | review 2, finding 3 |
| 25 | A reconciliation that agrees with a finished order does not stop the gateway | **Built** 2026-10-09 after it stopped the paper run; not deployed | `docs/evidence/gateway_stop_2026-10-09/` |

Count: 4 done, 8 built and waiting for a new incarnation (6, 7, 8, 18, 19, 22, 24, 25), 4 built and
waiting for a gateway image or inert (9 test key only, 10 mock only, 12 off by default, 17), 4 partly
done (5, 13, 20, 21), 5 open (11, 14, 15, 16, 23).

Items 6, 7, 8, 18 and 19 are one deployment: schema 11 needs a new incarnation, the owner's
approval, a flat account, and the operator ceremony (which now includes the two `assign-role`
commands).

## What is running now

Incarnation `abd1e149a26d4062ba5bbba2d90b3305`, started 2026-10-08 13:38 UTC.

- Voters and gateway: `localhost/velocita-v2:ledger-f55209e`. Edge and observer:
  `localhost/velocita-v2-shadow:paper-ledger-f55209e`.
- Paper account: fresh $25.00, no orders. Kernel loss limit $10, position limit 12 lots,
  fee cap 30 bp.
- The previous session's results: `docs/evidence/stochastic_cash_2026-10-07/RESULTS.md`
  (58 orders, $803.98 volume, net -$1.60, of which fees $1.61).

## Order of work

1. ~~Finish and deploy the kernel change (items 3, 4, 5).~~ Done 2026-10-08.
2. ~~Roles, flatten, conflict resolution, leader step-down, assertions (items 6, 7, 8, 18, 19).~~
   Built 2026-10-09 as one format change (schema 11). Not deployed.
3. **Make paper honest about the venue (items 12, 13).** A paper venue that can refuse and partly
   fill, a True Markets reference price for the decision, and the measured fee. This is where the
   strategy's real economics will show, still with no money at risk.
4. **Signer and transport against mocks (items 9, 10).** Everything that can be built without a
   credential or a venue call.
5. **The remaining reviews (items 20, 21).**
6. **Venue contract and balance reconciliation (items 11, 14).** Needs recorded venue replies.
7. **Drills (item 16), then the journey's steps 9 to 11.** First controlled trade, canary, review.

Steps 2 to 5 need no credential and no venue call. Step 6 onward does, and each needs its own
explicit approval.

## Owner decisions that block progress

- The Healthchecks URL for the outside heartbeat.
- Whether the walk-forward research result is accepted as the basis for a canary
  (`STRATEGY_PROMOTION_GATE.md`, Stage 1).
- Where the signing credential lives at rest and who may start the signer (ADR 0019).
- Whether True Markets offers a trade-only, no-withdrawal credential. Until it does, a leaked key
  can move the whole account, so the account should hold only the canary capital.
- Wired Ethernet for the three hosts (journey step 2.3). On 2026-10-08 nuc2 dropped off the network
  for about 20 seconds and leadership moved.
