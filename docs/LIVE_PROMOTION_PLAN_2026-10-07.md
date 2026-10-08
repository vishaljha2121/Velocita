# Promoting the stochastic paper strategy to live: plan and status

Written 2026-10-07 after the owner said the paper strategy looks good and asked for a plan to promote
it. **Live trading is not approved by this document.** It orders the work, says what is done, and
names the owner decisions. `LIVE_TRADING_JOURNEY.md` remains the step-by-step gate list; this file
maps the checkpoint review (`docs/evidence/stochastic_cash_2026-10-07/CHECKPOINT_REVIEW.md`) onto it.

## Where the strategy stands

`stochastic_1s_cash_v1`: long-only SOL, 0.12 SOL clips, one-second bars, paper venue. Since
2026-10-07 the account has traded 26 orders. The system around it is working; the economics are
not proven:

- The paper fee is 20 bp a side. The measured True Markets cost is about 100 bp to buy and 98 bp
  to sell. At the measured cost the run so far loses about $1 for every $100 of volume.
- The paper price is the Binance SOLUSDT close. A live order fills at True Markets' price.
- The paper venue always fills completely at the limit.

So the first thing "live" buys is evidence about real fills and real cost, at the smallest size
(the book, ch. 52: a small live test is an information purchase). It is not expected to make money
at 100 bp a side.

## What must be true before the first real order

| # | Requirement | Status |
|---|---|---|
| 1 | The edge cannot freeze or go blind (review A1, A2) | Done, deployed 2026-10-08 |
| 2 | The market-data allowlist is enforced (A3) | Done, deployed 2026-10-08 |
| 3 | The archive cannot silently fill (B1) | Edge sends far fewer commands (deployed). Metric and alerts built, not deployed |
| 4 | The kernel checks every fill's price and fee and owns cash, P&L and a loss limit (B3, ADR 0021) | Built and tested; needs a new incarnation to deploy |
| 5 | Snapshot room for it (ADR 0022) | Policy table cut to 8. Receipt table change waits for the week of measurement |
| 6 | Callers are authorized by role (B4, ADR 0020) | Not built |
| 7 | An operator flatten path (journey step 4.3) | Not built |
| 8 | The signer (ADR 0019 section 1, journey step 5) | Not built. Needs recorded venue payloads to finish (journey step 3) |
| 9 | The venue transport (journey step 6) | Not built. Mock-tested only until the owner approves a venue call |
| 10 | The venue contract questions (journey step 3) | Open; several need the owner or True Markets |
| 11 | The paper venue can reject, partly fill and time out (review C3) | Not built |
| 12 | Decide and fill on the venue's own price, with cost measured on both sides (B5) | Not done |
| 13 | Startup balance reconciliation against the venue (journey step 7) | Not built |
| 14 | Outside heartbeat armed (B6) | Installed; waits for the owner's Healthchecks URL |
| 15 | Drills while holding a position: kill, leader loss, gateway restart (journey step 8) | Not done on this deployment |
| 16 | A live gateway refuses an account with no loss limit | Not built |

## Order of work

1. **Finish and deploy the kernel change (items 3, 4, 5).** One new incarnation carries the ledger,
   the loss limit, the archive metric and the policy-slot metric. Needs the account flat and the
   owner's go-ahead. The operator's limits command then sets `max_window_loss_micros=10000000`.
2. **Roles (item 6) and the flatten path (item 7).** Both are kernel changes; do them before the
   next incarnation if possible so there is only one more format change.
3. **Make paper honest about the venue (items 11, 12).** A paper venue that can refuse and partly
   fill, a True Markets reference price for the decision, and the measured fee. This is where the
   strategy's real economics will show, still with no money at risk.
4. **Signer and transport against mocks (items 8, 9).** Everything that can be built without a
   credential or a venue call.
5. **Venue contract and balance reconciliation (items 10, 13).** Needs recorded venue replies.
6. **Drills (item 15), then the journey's steps 9 to 11.** First controlled trade, canary, review.

Steps 1 to 4 need no credential and no venue call. Step 5 onward does, and each needs its own
explicit approval.

## Owner decisions that block progress

- The Healthchecks URL for the outside heartbeat.
- Go-ahead for the new incarnation (step 1) at a flat moment.
- Whether the walk-forward research result is accepted as the basis for a canary
  (`STRATEGY_PROMOTION_GATE.md`, Stage 1).
- Where the signing credential lives at rest and who may start the signer (ADR 0019).
- Whether True Markets offers a trade-only, no-withdrawal credential. Until it does, a leaked key
  can move the whole account, so the account should hold only the canary capital.
- Wired Ethernet for the three hosts (journey step 2.3). On 2026-10-08 nuc2 dropped off the network
  for about 20 seconds and leadership moved.
