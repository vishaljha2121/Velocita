# 0024. Operator corrections: closing a conflict, and flattening

Date: 2026-10-08
Status: Accepted by the owner on 2026-10-08 ("lgtm; continue with next steps"). Both sections are
built. Neither is deployed: both change the command format (schema 11) and need a new incarnation.

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

## 2. Reducing orders, and a flatten mode

Every order in this system comes from a market-data frame and one of the active policy's order
templates. The gateway refuses to send an order whose frame is stale. A sell written by the kernel
from nothing would have no frame and would bypass that guard. So the flatten path keeps the same
chain (frame, decision, authorization, authority proof, gateway) and changes only who chooses.

### 2.1 `reduce_only` now means what it says

In `reduce_only` an order that reduces the position is decided and authorized as usual. Everything
else is suppressed, as before. `no_new_risk` and `cancel_only` are unchanged: they suppress every
order.

"Reduces" is the test the loss limit already used: with everything the account still has reserved
counted in, the order moves the position toward zero and not through it.

The strategy still chooses when. `reduce_only` lets its exit rule work; it does not force an exit.

### 2.2 `flatten` forces the exit

`flatten` is a new strategy mode (value 6), set with `strategy-mode flatten K E`. On every
authoritative frame the kernel ignores the rules and decides for itself:

- If the account has a position, nothing reserved, and the active policy has an order template on
  the closing side, the decision is an order from that template. Its quantity is the position,
  capped by the template's quantity cap and the account's order limit. Its price comes from the
  frame through the template's price source, so the template's price and staleness caps apply.
- Otherwise the decision is `control_suppressed`.

If the position is larger than one order may be, the next frame sells the rest. The edge sends a
frame at least every 30 s (ADR 0023), so a flatten starts within 30 s of the command.

A flatten decision has no matched rule (`matched_rule_index` is 65535) and records the mode it was
decided in.

### 2.3 An order is sendable only in the mode that decided it

| Mode now | Sendable orders |
|---|---|
| `normal` | any authorized order (unchanged) |
| `reduce_only`, `flatten` | only an order whose decision was made in that same mode |
| `no_new_risk`, `cancel_only`, `halted` | none |

So an entry authorized a moment before the operator switches to `flatten` is not sent, and a
flatten order still waiting when the operator switches away is not sent either. One function,
`submit_permitted`, answers this. Authorization, the leader's authority proof and the gateway's
pre-send guard all use it or the same rule. Authorization also checks again that the order still
reduces (`order_not_reducing`).

### 2.4 The gateway re-arms by itself for a reducing mode

Any control change stops new orders at the gateway, and until now every such stop needed a gateway
restart. A flatten that needs a second manual step on another host is not a flatten.

A change of the strategy mode alone, into `reduce_only` or `flatten`, is now a stop that re-arms by
itself (`reducing_mode`), under the same conditions as a fence: the quiet period has passed and the
venue's book matches the kernel's. A change that also moves the kill epoch or the executor
generation still latches. So does leaving a reducing mode: after `strategy-mode normal` the gateway
must be restarted, as before. A stop that was already latched stays latched.

### 2.5 What flatten cannot do

- It needs the edge to be sending frames and the gateway to be running. If either is down, sell at
  the venue by hand and reconcile afterwards.
- It does not cancel an open order. While anything is reserved it decides nothing. The paper venue
  and the True Markets swap are fill-or-kill, so nothing rests there.
- It is a limit order at the frame price. If the venue will not fill at that price, the next frame
  tries again at the next price.
- It closes only the instrument the active policy trades.
- It does not override the kernel's other checks: risk limits, an order in conflict, or the
  gateway's reconciliation latches still hold it. Resolve those first.

### 2.6 Procedure

1. `velocita-v2-client ... strategy-mode flatten K E` from the Mac.
2. Watch the position reach zero (`velocita_v2_account_position_lots`).
3. Choose what comes next: `halted` to stay out, or `normal` and a gateway restart to trade again.

Tests: the real program path (rules, templates, authorization, fills) for both modes, including a
position larger than one order, a short, and each refusal; the gateway guard and authority over
every mode; and one end-to-end run with real voters, gateway and paper venue that holds two lots,
flattens, re-arms after the quiet period and sells exactly two lots.
47 mutants across the kernel, the gateway guard, the gateway authority, the leader's authority
barrier and the gateway's re-arm rule: 43 killed. The other 4 are checks that a second check
repeats (a bounds test before a comparison that already fails, a mode range before the mode rule,
and the two places the leader applies the same sendable rule to a proof).

## 3. Also added with this change

- `velocita_v2_strategy_mode` on every voter (1 normal, 2 no_new_risk, 3 cancel_only, 4 reduce_only,
  5 halted, 6 flatten). The mode was not exported before.
- Alert `VelocitaReducingModeLeftOn`: the mode has been `reduce_only` or `flatten` for 30 minutes.

