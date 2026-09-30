# 0015. Risk limits are operator-only commands that bound position

Date: 2026-09-29
Status: Accepted. The owner approved it ("lgtm; start with P2", "these parameters should be operator
adjustable like long only or position sizing", "be as close to the book as well").
Implemented in `~/.cache/velocita-work/kernel` (119/119 ctest) and pending merge. See
`docs/PHASE2_PAPER_HANDOFF.md`.

Byte contract change: kernel codec schema 8→9, state snapshot schema 8→9, and static identity
schemas 8→9.

## Context

Paper trading lets the kernel authorize real orders for the first time. Reading the risk path
before Phase 2 found three gaps:

1. **Nothing bounded the filled position.**
   - `max_order_quantity` and `max_reserved_quantity` bound open orders only.
   - `position_quantity` moved on fills, but no rule read it.
   - A strategy could buy clip after clip, each within the per-order and reserved caps, and
     accumulate an unbounded long.
2. **Nothing enforced long-only.** On TrueMarkets spot a short is impossible, but the kernel would
   authorize one, and only the venue would refuse it.
3. **Any authenticated caller could send `ConfigureRiskLimits`.** Kill epoch, executor generation,
   strategy mode and catalog installation were operator-gated; limits were not. A compromised or
   buggy strategy process could raise its own limits.

What the book requires (Deterministic Speed ch49 §"The complete check set" and §"Limits governance",
ch18 §"Where authorisation actually has to live", ch45 §"Risk service"):
- Limits apply to the current position plus projected open orders.
- An explicit zero denies; an unset limit must mean neither unlimited nor an accidental deny.
- Limits are replicated, versioned records carrying scope, units, effective index, authorizer,
  reason, expiry and previous hash.
- Material changes get four-eyes review.
- One worker must not both propose and approve a limit.

## Decision

1. **Operator-only gate.** `ConfigureRiskLimits` joins the operator-gated set in `apply()`.
   - From a caller outside `1..kMaxOperatorCallerId` it returns `unauthorized_control`.
   - The refusal happens before the receipt lookup, so it leaves no receipt and no state change.
     A replay of the same bytes must return the refusal again, never a completed action.
2. **Symmetric position cap plus a short permission.** Three fields are appended to
   `ConfigureRiskLimits`:
   - `max_position_quantity` (lots, must be ≥ 0; 0 denies new exposure);
   - `allow_short` (a byte, only 0 or 1 decodes; 0 means long-only);
   - `reason_code` (u32, non-zero).

   The first two are also appended to `RiskAccount`.
3. **Authorization rule.** P is the signed position, R the open quantity (`reserved_quantity`,
   both sides) and q the proposal quantity.
   - A buy passes iff P + R + q ≤ cap.
   - A sell passes iff P − R − q ≥ (allow_short ? −cap : 0).
   - Otherwise the refusal is `ApplyCode::position_limit_exceeded` (67, appended).
   - Overflow gives `risk_arithmetic_overflow`.
   - The check runs after the existing capacity checks and before any mutation.
4. **Lowering a cap below the current position is accepted.** The account becomes reduce-only:
   buys are refused and reducing sells still pass. That is what an operator lowering a cap means.
   Refusing the change would leave the operator unable to tighten during a drawdown.
5. **Strategy re-sync.** A `stale_risk_state` authorization refusal carries
   `ApplyResultDetailKind::risk_state` (7) with {risk_version, position_version,
   position_quantity}. See ADR 0016 for why the strategy needs it.
6. **No dual-version reader, and the identity moves too.**
   - The upgrade is a fresh cluster. The static identity schemas move to 9.
   - So a V8 binary and a V9 binary cannot accept the same manifest and form one cluster. Such a
     cluster would diverge at the first 101-byte limits entry, which V8 decodes as 88 bytes.

## How this maps to the book's limit record

| Book field | Here |
|---|---|
| scope | (account, instrument) |
| units | typed fixed point (`QuantityLots`, `MoneyMicros`, ppm) |
| effective log index | the Raft commit of the command |
| authorizer | the operator `CallerId` in `CommandIdentity`, bound by the payload digest |
| reason | `reason_code`, a reference into the operator's audit record; the text is not replicated |
| previous hash | `expected_risk_version` compare-and-set: versions are unique per scope and the log keeps each version's record |
| old→new diff | the operator client prints the new values before sending; the log replays both versions |
| expiry | **not implemented** (deviation 1) |
| four-eyes | **not in the kernel** (deviation 2) |
| "the order stores the exact limit version" | already true: the decision frame binds `risk_version` and `position_version`, and authorization refuses a mismatch |

## Deviations from the book, with reasons

1. **No expiry.**
   - The book's expiry exists mainly for overrides and emergency changes.
   - This system has no override path: limits are routine settings, and emergencies use strategy
     mode and kill epoch, which only reduce authority.
   - An 8-byte expiry per account (4 accounts) plus the flags would not fit the snapshot. The
     canonical snapshot had 53 bytes under its 64 KiB ceiling; this design uses 36 and leaves 17.
   - Revisit if overrides are ever introduced.
2. **Four-eyes is enforced in operator tooling, not the kernel.**
   - ch18 argues this directly. A second-approval gate must withhold submission until both
     signatures exist.
   - A pending-approval state machine inside the replicated reducer couples an operational process
     to the hot replication path, for no benefit.
   - There is one operator today, so four-eyes is recorded as not applicable, not as satisfied.
3. **Open orders count against both sides (conservative).**
   - Per-side reserved quantities would need 16 more bytes per account, which would not fit
     `RiskAccount`'s 128-byte budget together with the new fields.
   - With one order slot and one position, this over-restriction never binds.
   - Revisit if a strategy quotes both sides.
4. **Symmetric cap, not separate long and short caps.**
   - Separate caps cost 16 bytes per account; 64 bytes in all does not fit the 53 free.
   - One cap plus a sign permission keeps every bound explicit, as the book requires that no limit
     be unbounded.

## Consequences

- **Fresh cluster.** Mandatory for this deploy; goldens were regenerated by
  `tools/v2_schema_goldens.py`, which first reproduces the reviewed V8 values from their fields.
- **Tighter memory.** `TradingState` is 81,456 of 81,920 bytes and the layout tripwire went from
  512 to 448 bytes. The snapshot has 17 bytes of headroom, so the next venue-contract change (M5)
  will need a budget decision.
- **Kernel-level backstops.**
  - A strategy can no longer raise its own limits.
  - A long-only account cannot go short, even if the venue would allow it.
  - The position can never exceed one clip at the $25 configuration.

## Alternatives rejected

- **An additive command (`ConfigurePositionLimits`) instead of extending `ConfigureRiskLimits`.**
  - It would avoid a codec schema bump for the existing body.
  - But it splits one limit record into two versioned commands with separate compare-and-set, and a
    half-configured account becomes representable.
  - The snapshot and identity bump was needed anyway.
- **Letting only the venue enforce long-only.** Rejected. The book puts position limits in the core
  risk gate, and a venue-side refusal is evidence after the fact, not prevention.
- **Refusing a cap below the current position.** Rejected; see decision 4.
