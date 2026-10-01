# Kickoff prompt: indicator strategy research (new session)

Paste everything below the line into a new Claude Code session started in
`/Users/vishaljha/aeron-raft-pi`.

---

You are continuing work on **Velocita V2**, a Raft-replicated trading kernel at
`/Users/vishaljha/aeron-raft-pi` (branch `v2/phase-5-corrections`, baseline tag
`v2-baseline-2026-10-01`, head `7730e9f` or later). Your task is **strategy research**: find
technical-indicator strategies (ADX, RSI, a volatility regime as a "VIX" proxy, and combinations)
on high-liquidity assets that can at least break even **after fees** on True Markets, and possibly
profit. Also write a short scoping note on cross-venue funding-rate arbitrage. Do this research
inline: **do not spawn subagents unless I explicitly ask**.

## The objective and the money

- Goal: True Markets leaderboard **volume**, without losing much money. The total trading budget is
  **$25**. The weekly loss tolerance is **$10–12**. The budget is not approval to trade live. Live
  trading needs my explicit sign-off and the items in `docs/LIVE_TRADING_JOURNEY.md`.
- Costs are the whole problem. Every venue cost below was measured, not assumed
  (`docs/evidence/truemarkets_markup_2026-09-28/README.md`):

  | Asset | Buy all-in | Sell all-in | Round trip |
  |---|---|---|---|
  | SOL (Solana) | ~+100 bp | ~+98 bp | **~1.99 %** |
  | cbBTC (Base) | ~+108–114 bp | ~+123 bp | **~2.33 %** |

  The fee is a flat 1 % of USDC on each side. A strategy needs a **gross edge per round trip above
  ~2 %** to break even. So only low-turnover, large-edge-per-trade strategies can work.
- Already established: with no edge, volume costs about **$86 of volume per $1 lost on cbBTC** and
  **$101 per $1 on SOL**. A live SOL paper run confirmed it: $1,144 of volume for −$11.38 net, with
  gross P&L −$0.05 (99.5 % of the loss was fees). A strategy that is worth anything must beat that
  ratio, or make money outright.

## What the architecture can run (design inside these limits)

**Kernel (C++, replicated by Raft across 3 voters).** It evaluates a **rule program** on every
DecisionFrame:
- an ordered list of at most **32 rules** (`kMaxPolicyRules`);
- over a vector of at most **32 integer features** in micros (`kMaxDecisionFeatures`), see
  `include/velocita/v2/contracts.hpp`;
- each rule is `feature <op> threshold → action`. The operators are `<`, `<=`, `==`, `>=`, `>`.
  **The first match wins; no match means HOLD.**
- the actions are `no_action`, or `place_limit` with a **buy** template (ENTER_LONG) or a **sell**
  template (EXIT).
- There is **no arithmetic in the kernel**. Every indicator is computed outside it.

**Positions.** **Long-only spot**: either the whole stake in the asset or flat in USDC. Shorting is
refused by kernel risk limits (ADR 0015: `max_position_quantity`, `allow_short=false`, operator-only).
**Perpetuals are out of scope** (ADR 0004).

**Edge (Python, nuc1).** It computes the features and submits frames:
- `model/src/velocita_model/shadow/edge.py`, `FeatureSource`. It receives **daily closes** for
  history (closes only: **no high or low**), plus a live price.
- The live price is either the reference price (`--price-source reference`, BTC/ETH) or the
  **30 s candle close** (`--price-source candles --price-chain solana`, SOL).
- The intraday minute window is at most **1440 minutes** (`volume.MinuteWindow`).
- Indicators that need OHLC (ADX, ATR) need either a **close-only variant** or a new OHLC
  bootstrap. Build that from the recorded 30 s candles, or say explicitly that Binance bars are a
  proxy. Report which you used.

**Feature schema and identities.** The existing features are index 0 `trend` (close/SMA−1),
1 `ret1`, 2 `vol` (stdev of daily log returns), 3 `dev` (close/SMA20−1), and 4 a
strategy-specific extra.
- The policy and feature-schema ids are in `model/src/velocita_model/shadow/volume.py`
  (`STRATEGIES`):
  - `rules.DEMOS` programs: 21/31;
  - churn 22/32, trend_churn 23/33, rotate 24/34, dip 25/35.
- A new strategy gets a **new policy id and a new feature-schema id**. Index N means one thing
  forever.

**Execution.** The gateway (ADR 0017) runs the paper venue: fill-or-kill at the measured costs
(ADR 0018). A decision at bar t fills at the next bar (`lag_days=1` in research). There are no
resting orders, and **no maker rebates** on this path.

**Tradeable assets.** The live two-row catalog is:
- account 1 / instrument 1: **cbBTC on Base**;
- account 2 / instrument 2: **SOL on Solana**.

ETH appears on the True Markets CeFi list (`docs/evidence/V2_TRUEMARKETS_CATALOG_2026-09-17.md`).
It has **no measured DeFi cost and no catalog row**, so treat it as research-only until costs are
measured. Adding an asset means a catalog row (`tests/v2_shadow_catalog_rows.hpp`) and a **fresh
cluster**.

**Throughput.** Each forwarded tick costs two retained Raft log entries. `min_interval_ns`
throttles it. Daily or 4 h decision cadences are fine.

## Existing research code (reuse it; do not rewrite)

The code is in `model/src/velocita_model/research/`:
- `rules.py`:
  - `Rule`, `Action`, `Settings`, `Costs`;
  - `features` / `feature_series` (closes only);
  - `backtest(bars, asset, program, settings, costs)`: whole stake, fill at t+lag, measured cost per
    side;
  - `first_match`: pinned to the kernel evaluator mirror `simulation/rule_evaluator.py`.
- `bars.py`:
  - `DailyBars` holds **closes and quote volume only**;
  - `load_binance` reads the checksummed monthly archives;
  - `_api_klines(http, pair, interval, …)` handles any interval, including OHLC fields;
  - `load_truemarkets` (closes, about 3 years);
  - `load_binance_daily_api`.
- `battery.py`:
  - `optimize`, which maximises the profit factor over an integer grid;
  - `walk_forward`;
  - `in_sample_test` (permutation or random-walk null; the optimizer re-runs inside every null
    draw);
  - `walk_forward_test`.
- `permutation.py`: `permute` (joint day shuffle) and `random_walk`.
- `metrics.py`: `profit_factor`, `total_return`, `max_drawdown`, `sharpe`, `cagr`, `p_value`.
- `costs.py`: `measured()` and `model(path, symbol)`, from the quote evidence JSON.
- Also `validation.py`, `strategy.py` and `volume_battery.py` (1 s / 30 s tick research), and
  `tick_data.py`.
- Tests: `model/tests/test_research.py`, `test_battery.py`, `test_rule_strategies.py`,
  `test_volume_battery.py`, `test_rule_evaluator_parity.py`.

Cached public data (no credentials) lives in `~/.cache/velocita-research/`:
- `binance/`: daily archives for BTC and ETH (2018–), SOL, ZEC, PENDLE and PENGU;
- `hourly/`: SOL 1 h from 2023;
- `pit/`;
- `btc1s/`.

Recorded True Markets market data (30 s candles, reference prices):
- the live recorder runs on nuc1 (md-stream);
- the nightly archive and its Mac backup are in `~/velocita-backups/md/` (`docs/MD_ARCHIVE_RUNBOOK.md`).

## Prior evidence (read before designing)

These are in `docs/evidence/`:
- `demo_strategies_2026-09-28/`: trend, dip_in_uptrend and calm_trend on daily Binance data.
  **BTC trend SMA125 was the only program that survived the in-sample nulls.**
- `minara_battery_2026-09-28/` (+ `POINT_IN_TIME.md`): the momentum rotation was **rejected** after
  the point-in-time universe test.
- `volume_battery_2026-09-30/`: README, REPORT, DEPLOYED, HOLDOUT, RESEARCH_LEDGER, SOURCES and
  `research_results.json`. The 1 s / 30 s volume strategies show **no edge**. Copy the format of
  the pre-registered ledger.
- `volume_strategy_2026-09-28/`: including `SOL_BAND_REBALANCER.md`.
- `truemarkets_markup_2026-09-28/` (costs) and `truemarkets_marketdata_2026-09-28/` (feed schemas).

## Method (non-negotiable)

1. **Pre-register first.** Before running anything, create
   `docs/evidence/indicator_battery_<date>/RESEARCH_LEDGER.md`. It must fix:
   - the candidates and their integer grids;
   - the assets and the timeframes (daily, and 4 h if justified);
   - the train / walk-forward split dates;
   - a **held-back final period that is opened once**, at the end.

   Do not change it after seeing results. Record every deviation.
2. **Candidates to consider** (prune or extend in the ledger, with reasons):
   - ADX/DI trend filter (strong trend + DI+>DI− to enter);
   - RSI mean-reversion **inside an uptrend** (RSI(2–14) oversold above SMA);
   - realized-vol / ATR regime filter ("VIX proxy"; DVOL is not available to us) layered on trend;
   - Donchian breakout;
   - combinations, at most two indicators each, to limit data-mining.
3. **Indicators get hand-computed oracle tests**: worked small examples computed by hand, not by
   the code under test. Also mutation-check the tests (see
   `~/.claude/projects/-Users-vishaljha/memory/velocita-cpp-quality-standards.md`). Wilder smoothing
   for ADX/RSI/ATR must be stated and tested.
4. **The battery for every candidate:**
   - in-sample permutation;
   - random-walk null;
   - walk-forward;
   - walk-forward permutation;
   - costs at ×0.8 / ×1.0 / ×1.25 of measured;
   - then the holdout, once.
5. **Report for each candidate:**
   - gross and net return per trade;
   - trades per year and **volume per year per $25 stake**;
   - **volume per $1 lost** (compare against $86 / $101);
   - max drawdown;
   - profit factor and the p-values.

   Use the honesty rule (book ch52): a negative result is reported as plainly as a positive one.
6. **The funding-rate arbitrage scoping note** (`FUNDING_ARB_SCOPING.md` in the same folder):
   - Use only public, keyless funding history (e.g. Binance / Hyperliquid funding endpoints via
     GET).
   - Quantify the annualised carry against our $25 and the ~$0.50–0.60 entry and exit costs.
   - Expected conclusion to test, not assume: it is uneconomic at $25, and it is out of scope (ADR
     0004 excludes perps; it needs a second venue and margin).
   - Note that True Markets' feed lists a hypercore-perp SOL market, and whether that changes
     anything.
7. **Deliverables:**
   - the evidence folder (README, REPORT, RESEARCH_LEDGER, SOURCES, results JSON);
   - the research code and tests under `model/`;
   - a recommendation: which candidate (if any) goes to paper, and the exact rule program, feature
     schema and new policy id it would need.

   Do not touch the cluster or deploy anything. A paper deployment is a separate, approved step
   (`docs/PAPER_TRADING_RUNBOOK.md`).

## Running things

- Python tests: from `model/`, `PYTHONPATH=src python3 -m unittest discover -s tests` (stdlib
  unittest, no pytest; see `model/README.md`).
- Use `$CLAUDE_JOB_DIR/tmp` (or a durable dir), never `/tmp`, for scratch. macOS cleans `/tmp`.
- The shell is zsh: use arrays (not word-split `$A`), and `$HOME`, not `~`, inside quotes.
- Network: **GET-only, keyless** public market data. `ReadOnlyHttp` enforces this. No venue keys,
  no order sends.

## Rules of the repo

- **Git:**
  - Never `git add .` / `-A`, `reset`, `clean` or `stash`. Stage explicit paths only.
  - Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - Commit only when I ask.
- **Protected paths, do not modify:** `docs/README.md`,
  `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`, `docs/V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`,
  `docs/adr/0002-*`.
- **No secrets** in any file. Raw markup capture files are private.
- **Do not attribute claims to the Deterministic Speed book without reading the chapter.** It is at
  `~/Desktop/Trading/velocita-book/chapters`. Ch52 is honesty; ch25 is wash trading. Churn-style
  volume is a wash-trading concern worth a sentence in the report.
- Confirm with me before anything destructive or irreversible.

## Where everything is

**Architecture and status**
- `docs/BASELINE_2026-10-01.md`: what runs where, the images, the cluster incarnation.
- `docs/adr/0001…0019`: the design decisions, especially:
  - 0004: no perps;
  - 0011: the swap model;
  - 0015: risk limits;
  - 0016: callers;
  - 0017: gateway;
  - 0018: paper fills;
  - 0019: live and key custody.

**Operations**
- `docs/M3_DEPLOYMENT_LOG.md`: incidents, findings 1–52.
- `docs/PAPER_TRADING_RUNBOOK.md`, `docs/CLEAN_RESET_RUNBOOK.md`, `docs/MD_ARCHIVE_RUNBOOK.md`.
- `docs/LIVE_TRADING_JOURNEY.md`: the gates to live.
- `docs/TOOLCHAIN.md`: the pinned build image.
- `CHANGELOG.md`: CP-075 to CP-078.

**Code**
- Kernel: `include/velocita/v2/` and `src/`.
- Edge and model: `model/src/velocita_model/shadow/` (`edge.py`, `volume.py`, `paper_book.py`,
  `ledger.py`) and `model/src/velocita_model/cli.py`.
- Research: `model/src/velocita_model/research/`.
- Catalog rows: `tests/v2_shadow_catalog_rows.hpp`.

**Currently running:** a SOL churn 24 h paper run, started 2026-10-01 14:47:30 EDT. Do not
interfere with it.

**Memory notes:** `~/.claude/projects/-Users-vishaljha/memory/`, especially the quality standards
and the trading budget.

Start by reading the prior evidence READMEs and `rules.py` / `battery.py`. Then draft the
pre-registered ledger and show it to me before running the battery.
