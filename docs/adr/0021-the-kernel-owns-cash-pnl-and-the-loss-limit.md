# 0021. The kernel owns cash, realized P&L and the loss limit

Date: 2026-10-01
Status: Proposed (architecture review, `docs/ARCHITECTURE_REVIEW_2026-10-01.md` finding A2).
It needs the owner's decision. **Blocking for live trading.** It supersedes the "loss breaker"
and "fill price/fee check" items in `docs/LIVE_TRADING_JOURNEY.md`.

## Context

Today the system's money accounting is split, and its authoritative half is the weaker one:

- **The kernel** (replicated, deterministic) books a fill's **quantity** only.
  `apply_observe_fill` updates `position_quantity` and releases reservations, but it ignores the
  fill's price and fee. `RiskAccount` has no cash, cost basis or realized P&L.
- **The edge** (`model/src/velocita_model/shadow/paper_book.py`: Python, one process, SQLite on
  nuc1) keeps cash, P&L and the **weekly loss stop**.
  - It does not see fills. It **infers** them from position changes reported in
    `stale_risk_state` refusals, and books them at the **pending order's limit price**.
  - The stop is enforced by adding `exits_only=1` to the TICK line it sends to the submitter.
- **The gateway** keeps its own paper balances (`/state/paper.state`) and does not reconcile them
  against the edge's book.

For paper trading this was acceptable: the paper venue fills at the limit, so the inference is
exact. **For live trading it is not.**

- A live fill's price and fee are what the venue says, not the limit.
- A loss stop that lives in the least-trusted process, and is advisory to the submitter, is not a
  breaker. The book's ch52 rule is that the authority must hold the money truth.

## Decision

1. **Fills carry money, and the kernel checks it.** `apply_observe_fill` validates the fill.
   - **Price:** at or better than the order's limit (buy: ≤ limit; sell: ≥ limit).
   - **Fee:** at or below the reserved `fee_hold` share for the quantity.

   A violation marks the order `conflict` with a new `ApplyCode::fill_price_conflict` /
   `fill_fee_conflict`, and does not book the fill (the same treatment as an unbookable quantity
   today).
2. **A per-account money ledger in `RiskAccount`:**
   - `cash_micros`;
   - `cost_basis_micros` (average cost of the held lots);
   - `realized_pnl_micros`;
   - `fees_paid_micros`;
   - `window_realized_pnl_micros` and `window_start_ns`.

   A buy debits notional plus fee and raises the cost basis. A sell credits the notional minus
   fee, and realizes against the average cost. All arithmetic is in integer micros, checked, and
   identical to `paper_book.py`'s rules so both can be cross-checked.
3. **An operator-configured loss limit.** `ConfigureRiskLimits` gains `max_window_loss_micros` and
   `loss_window_ns`.
   - The window is measured on **committed event time**: the fill's venue timestamp. The reducer
     still reads no clock.
   - While the window's realized loss exceeds the limit, `AuthorizeDecision` refuses any
     **risk-increasing** order with `ApplyCode::loss_limit_reached`. Exits are still allowed.
   - Only an operator `ConfigureRiskLimits` or the window rolling over clears it.
4. **The edge's paper book becomes a view.** It reads cash and P&L from the committed state
   instead of inferring them. The `exits_only` token stays as a second, earlier stop, not the
   only one.

## Consequences

- **Byte change** (snapshot, codec, results): about 48 bytes per risk account, so about 192 for
  4 accounts. This does **not** fit in today's snapshot headroom (16 bytes). ADR 0022 must land
  first or together.
- **The mark-to-market loss of an open position** is still not limited, only realized loss is. For
  long-only spot swaps that close quickly this is acceptable. Note it in the runbook.
- **The weekly paper P&L and the kernel's P&L become checkable against each other.** A mismatch
  is an alert, not a debate about which is right.
