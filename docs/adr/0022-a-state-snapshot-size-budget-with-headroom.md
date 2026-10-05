# 0022. The state snapshot has a size budget with headroom

Date: 2026-10-01
Status: Proposed (architecture review, `docs/ARCHITECTURE_REVIEW_2026-10-01.md` finding A3).
ADRs 0020 and 0021 depend on it.

## Context

The canonical state snapshot has a worst-case size of **65,519 bytes**. The ceiling is the
**64 KiB** wire envelope payload (`include/velocita/v2/wire.hpp`, `kMaxEnvelopePayloadBytes`),
and `state_snapshot.hpp` asserts both values. That leaves **16 bytes** of headroom. ADR 0015 already
noted it had "17 left".

Every safety feature still needed before live trading adds state: the role table (ADR 0020), the
money ledger (ADR 0021) and a loss window. None of them fit. Without a decision, each feature
becomes a fight over bytes, and each fight tends to be settled by shaving a bound that was chosen
for a reason.

Here is where the 65,519 bytes go. These are worst-case figures, computed from the constants in
`state_snapshot.hpp` and `kernel.hpp`:

| Part | Bound | Bytes | Share |
|---|---|---|---|
| Request receipts (deduplication memory) | 256 × 133 | 34,048 | **52 %** |
| Decision records | 16 × 551 | 8,816 | 13 % |
| Policies | 16 × 517 | 8,272 | 13 % |
| Source event receipts | 128 × 56 | 7,168 | 11 % |
| Two executable policy bundles | 2 × ~1.7 k | ~3,400 | 5 % |
| Everything else (accounts, reservations, orders, outbox, cursors, header) | | ~3,800 | 6 % |

## Decision

1. **Right-size the request receipts first.** They are half the snapshot.
   - Measure the high-water mark of `receipt_count`; it is already exported.
   - Run the cluster at the current cadence for a week, including a mass-cancel and a gateway
     restart.
   - Then set `kMaxRequestReceipts` to the measured peak × 2, rounded to a power of two; 128 is
     the expected outcome.

   That frees about 17 KiB. Also reduce `kMaxPolicies` 16 → 8, since the system uses 2–4; that
   frees about 4 KiB.
2. **Hold 10 % headroom as a rule.** Add `static_assert(kMaxStateSnapshotBytes <= 58'982)`
   (90 % of 64 KiB), with a comment that names this ADR. A feature that needs more space must
   remove bytes elsewhere, or reopen this ADR.
3. **Do not raise the 64 KiB ceiling now.** Chunked snapshot transfer between voters (the
   gateway already reads in 24 KiB chunks, ADR 0017) is the long-term answer if state must grow
   past 64 KiB. It changes the Raft snapshot path, though, and that is the most-reviewed and
   least-forgiving code in the system. Right-sizing buys years at this scale.

## Consequences

- **The bounds change, so the bytes change.** Snapshot schema bump; fresh cluster or stop-all
  restart. Bundle it with ADRs 0020 and 0021 so this happens once.
- **Lower receipt capacity can surface as `receipt_capacity_exceeded` under a burst.**
  - The ×2 margin and the measured peak make this unlikely.
  - The refusal is typed and alerted, and is not silent.
  - Add an alert at 75 % receipt fill.
- **The layout report and the byte-review docs** (`V2_MESSAGE_MEMORY_ATLAS.md`) must be
  regenerated.
