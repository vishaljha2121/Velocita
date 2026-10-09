# 0024. Operator corrections: closing a conflict, and flattening

Date: 2026-10-08
Status: Accepted by the owner on 2026-10-08 ("lgtm; continue with next steps"). Section 1 is built.
Neither section is deployed: both change the command format (schema 11) and need a new incarnation.

## Context

The kernel review (`docs/evidence/kernel_raft_review_2026-10-08/REVIEW.md`) found two states an
operator could not leave from inside the system.

- **D2. An order in conflict has no way out.** A fill that fails the quantity, price or fee check
  freezes its order as `conflict`. Nothing ever left that state: the reservation stayed held, the
  order slot stayed occupied, and `SetStrategyMode(normal)` was refused. With limits sized for one
  order, one conflict stopped the account for the life of the incarnation.
- **D3. No flatten path.** `reduce_only`, `no_new_risk` and `cancel_only` suppress every proposed
  order, including one that would close the position.

## 1. An order in conflict is closed by an operator correction

A new operator-only command, `ResolveOrderConflict`:

| Field | Meaning |
|---|---|
| `cluster_order_id` | the order to close; it must be in `conflict` |
| `expected_kill_epoch`, `expected_executor_generation` | must equal the current values, as for other controls |
| `unbooked_quantity` | lots the venue filled that the kernel has not booked; 0 if none |
| `unbooked_price` | their average price in ticks; required with a quantity, forbidden without |
| `unbooked_fee` | their fee in micros; forbidden without a quantity |
| `evidence_digest` | SHA-256 of whatever the operator checked at the venue; must not be zero |

What it does, in one step:

1. Books the stated quantity into the position and the money ledger with the same arithmetic as a
   fill, on the order's own side. It applies **no** limit-price, fee-cap or quantity check. Those
   checks are why the fill was refused; the correction records what the venue actually did.
2. Releases everything the order still reserves.
3. Marks the order `filled` if anything was ever booked for it, otherwise `cancelled`, and settles
   any of its effects that could still be sent.

After that the order is an ordinary finished order: its slot is free, it can be swept, and the
strategy can return to `normal`.

What it does not do:

- It does not check the venue. The operator must confirm the order is finished there first. The
  client prints this before sending.
- It cannot be used on an order that is not in conflict (`order_not_in_conflict`), and it cannot be
  repeated.
- It does not bypass the loss limit. A corrected loss counts toward the window like any other.

The refused fill and the correction both stay in the Raft log and the archive, so both values are
on record.

Client: `velocita-v2-client ... resolve-conflict ORDER_ID KILL_EPOCH EXECUTOR_GENERATION FILLED_LOTS
PRICE_TICKS FEE_MICROS EVIDENCE_SHA256`. Use `--dry-run` first; it prints what will be booked.

Tests: hand-computed ledgers for a whole order, nothing filled, a partial fill, an overfill and a
sell; every refusal leaves the account, orders and reservations unchanged. 44 mutants, all killed.
