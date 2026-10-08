# 0021. The kernel owns cash, realized P&L and the loss limit

Date: 2026-10-01
Status: Accepted by the owner on 2026-10-07 and built, with the changes recorded in the amendment
at the end. Not deployed: it changes the stored format, so it needs a new incarnation.
Originally proposed in `docs/ARCHITECTURE_REVIEW_2026-10-01.md`, finding A2. **Blocking for live trading.** It supersedes the "loss breaker"
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

## Amendment 2026-10-07: what was built, and where it differs

Built as decided: a fill is checked and then booked into a per-account ledger, and
`AuthorizeDecision` refuses new risk at the loss limit while still authorizing an order that only
reduces the position. The fill command did not change; it already carried price and fee.

Four differences from the text above:

1. **The loss window has no clock.** A fill observation carries no committed time, and adding one
   would change the venue observation that the gateway and every venue adapter write. The limit is
   therefore on realized loss **since the operator last reset it**. `ConfigureRiskLimits` gains
   `max_window_loss` and `reset_loss_window`; `window_start_ns` and `loss_window_ns` are not built.
   A weekly limit is a weekly reset by the operator. A time-based window can be added when fills
   carry a venue time.
2. **The fee cap is a rate on the fill's own notional**, not the reserved hold's share:
   `fee <= ceil(fill notional x maximum_fee_ppm)`. A sell filled above its limit has a larger
   notional and so a proportionally larger fee; measured against the hold, that honest fill would
   have been a conflict.
3. **Fees are realized when paid and are not part of the cost basis**, as `paper_book.py` does: a
   buy realizes minus its fee; a sell realizes proceeds minus the average cost of the lots sold,
   minus its fee. When part of a position is closed the cost removed is rounded down, and closing
   the whole position removes whatever is left, so no micro is lost. With these rules
   `cash + cost basis = realized P&L` for a long or flat account, and `cash - cost basis` for a
   short one.
4. **`cash` is net cash from fills since the account was created**, starting at zero. The kernel is
   not told the funding amount. Venue cash should equal funding plus this number.

A loss limit of zero means no limit. The operator command requires the field and prints "NONE" in
its description when it is zero, so it cannot be omitted by accident. A live gateway should refuse
to arm against an account whose limit is zero; that check is not built yet.

Sizes: the risk account is 169 bytes in a snapshot (121 before), the limits command body 110
bytes (101 before), and the in-memory account budget rises from 128 to 192 bytes. Both schema
versions move from 9 to 10, so a voter refuses data written by the previous format.

Not built from the decision: item 4, the edge reading its book from committed state. The voters
publish the ledger as metrics instead, so the edge's book and the kernel's can be compared and
alerted on. The edge's own stops stay as the earlier, second line.

Known limit, unchanged: only realized loss is limited. The loss on an open position is not.
