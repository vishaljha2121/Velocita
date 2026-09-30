# Strategy promotion gate: shadow -> paper -> live canary

Pre-registered 2026-09-29, before any live shadow result, so the bar cannot be
moved to fit an outcome. Owner-approved defaults: 30 days in shadow and the
daily-close book as the one that counts.

**Budget: the owner's total trading budget is $25** (2026-09-29). Shadow and
paper books are sized to it (superseding the first $1,000 virtual book), kernel
limits are $25 per order and one open position, and a round trip costs about
$0.58 at the measured ~1.1-1.2% per side, so fees dominate at this size.

## Why profit is reported, not gated, in shadow

The demo strategy (BTC trend, SMA 125) trades about 9.5 times a year in its
backtest: 30 days of shadow is zero to three trades, which is noise. Evidence
that a strategy makes money comes from the research battery (in-sample and
walk-forward permutation tests, random-walk test; `docs/evidence/`). Shadow
proves something different: that the live system makes the same decisions the
backtest would, reliably, at the measured costs. Shadow P&L is shown on the
dashboard ("Shadow book") and never used to argue for promotion.

## Stage 1: shadow -> paper

All of the following, measured on the live cluster over one continuous window:

| # | Criterion | Measured by |
|---|---|---|
| 1 | Research battery passed for this exact strategy and parameter (in-sample permutation p < 0.05). The walk-forward permutation p must be reported; above 0.05 requires the owner's explicit acceptance. | Evidence report under `docs/evidence/` |
| 2 | 30 consecutive days of shadow on the three-voter cluster. | Deployment log |
| 3 | Python-vs-cluster rule agreement 100% (every committed decision). | `velocita_shadow_agreement_total` |
| 4 | Decisions kept up with the market: no gap in recorded decisions longer than 10 minutes outside declared maintenance. | `velocita_shadow_last_decision_age_seconds` |
| 5 | Fidelity: the daily book's fills match a backtest replay of the same days (same side on the same days) and every fill's cost lies inside the measured quote range. | Replay report (to build with the ledger's fills table) |
| 6 | No unexplained edge errors, stranded voters or Raft log exhaustion in the window. | Alerts history |

## Stage 2: paper -> live canary

Paper means the kernel authorizes orders and a TrueMarkets simulator fills them
(Phase 2). All of Stage 1 still holding, plus:

- Fills reconcile exactly with the kernel's positions; no unknown outcomes.
- Kill-switch and failure drills pass (roadmap M6).
- Hard limits set and tested: per-order spend, total exposure, fees, loss,
  order rate, stale-data cutoff.
- The venue contract is closed (roadmap M5).
- The owner signs off on the specific canary (size, duration, stop conditions).

Volume or profit alone is never acceptance (roadmap M7).
