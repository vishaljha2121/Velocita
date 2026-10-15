# 0028. A recent settlement window with a lifetime count

Date: 2026-10-15.
Status: **PROPOSED 2026-10-15 — implemented and tested locally; ships only with a new incarnation.**

## Problem

Swap state keeps every settlement of the run in a 16-entry array and refuses a new intent once the
array is full. The owner-approved automated run, the paper stochastic rule at 0.12 SOL clips, makes
about 98 effects a day and allows 240 lifetime orders. A larger array does not fit:
- a swap-mode snapshot has 15,830 bytes of slack against the legacy layout, enough for about 88 more
  settlements;
- `TradingState` uses 81,392 of its 81,920-byte arena.

## Decision

- The 16 entries become a window of the most recent settlements. When it is full, the oldest entry
  is evicted. A 4-byte `settled_total` counts every settlement for the life of the run, and intents
  are refused only after one million settlements.
- Running totals (base, quote, cost, fees, purchases, realized P&L) were already state, and they stay
  authoritative.
- Validation:
  - **Complete history** (`settled_total` equals the window count): unchanged, an exact replay from
    the opening that must reproduce every total.
  - **After eviction:** the window must be full and in strictly increasing effect order, with no
    effect after `last_effect`, each transaction once, and each entry finalized with a buy-, sell-
    or failure-shaped amount. The totals must lie within their bounds, and cost requires inventory.
- Duplicate and conflict detection works within the window. That covers the gateway's idempotent
  retry of the latest settlement. An evicted settlement replayed later changes nothing.
- The live observer keeps its own record of up to 1,024 fills. A fill may leave the gateway's feed
  only while that feed shows a full window and the fill is older than every entry in it.

## Consequences

The swap state encoding grows by 4 bytes (3,756 bytes, or 3,821 with a price amendment). Old
snapshots therefore do not decode, so this ships only with the new incarnation and never under
`b79e65be…`. Once history is evicted, the totals are no longer re-derivable from state alone. They
remain protected by Raft agreement and the snapshot checksum, as every other running total is.
