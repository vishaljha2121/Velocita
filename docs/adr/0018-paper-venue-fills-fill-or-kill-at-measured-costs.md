# 0018. The paper venue fills fill-or-kill at the reference price with measured costs

Date: 2026-09-29
Status: Accepted. The owner decided "fills: FOK at measured costs" and set a total budget of $25.
Implemented in `~/.cache/velocita-work/paper`: 22 tests, and 31/31 mutants killed. Pending merge.
The files are also in the `gateway` copy.

No kernel byte change. The venue sits behind the existing `sim::VenueTransport` seam.

## Context

Paper trading needs a venue that behaves like TrueMarkets closely enough for the paper record to
mean something. It must also be deterministic enough to test every failure path. The book (ch46)
is blunt: "a happy-path mock proves nothing"; the simulator must cover delay, duplicate, reorder,
drop, reject and split.

The existing `SimulatedVenue` stores orders in unbounded `std::vector`s. Its `partial_fill` also
leaves the remainder resting, which contradicts fill-or-kill.

Cost evidence: `docs/evidence` measured TrueMarkets round-trip costs at about 1.1–1.2% per side
(`quotes_buy_sell_1631utc.json`). At $25, one round trip costs about $0.58. Fees dominate at this
size, and the paper record must show that honestly.

## Decision

1. **A standalone `PaperVenue`**, bounded (256 orders, 128 pending reports) and deterministic
   (seeded faults, no clock reads). It reuses the `sim::` result types and the `VenueTransport`
   seam.
2. **Fill-or-kill at the order's limit price.** The limit is the reference price the strategy
   used. The whole quantity fills, or the whole order is killed.
   - Partial fills happen only under the `split` fault. They exist to prove the kernel books
     split fills.
3. **Fee.** `fee = ceil(notional × bps / 10,000)`, integer only, rounded **against us**.
   - Buy is 115 bp and sell 120 bp by default; both are configurable.
   - The fee is reported in `VenueObservation.fee`.
   - Each split piece is rounded on its own, so a split never costs less than the whole order.
4. **Venue balances.** The quote balance defaults to $25 and the base balance to 0.
   - A buy whose notional plus fee exceeds the quote balance is killed (`insufficient_quote`).
   - A sell exceeding the base balance is killed (`insufficient_base`). This is the venue's
     long-only backstop.
5. **Faults and how each resolves:**

   | Fault | What happens |
   |---|---|
   | `drop_request` | `send` returns `ambiguous` and nothing executes |
   | `drop_response` | The order executes but `send` is `ambiguous`; the reports are held until a query releases the venue's own identified reports |
   | `delay` | Reports are withheld until they are due |
   | `duplicate` | An identical copy arrives one tick later |
   | `reorder` | The fill arrives before the ack |
   | `reject` | The order is killed |
   | `split` | The order fills in pieces |
   | `query_unavailable` | The query claims no fact |

6. **A killed order is not recorded**, so a later query truthfully answers `not_found`. The gateway
   then submits `order_reconciled` with venue order id 0, which releases the hold. See
   "Kernel findings" item 3.

## Why fill at the limit and not at a live quote

The strategy's decision and the kernel's authorization are both bound to that reference price. A
fill at a different, live price would add a second market-data dependency inside the venue. It
would also make a paper result disagree with the shadow ledger for reasons unrelated to execution.

Costs are what the measured evidence covers, and they are charged in full. The residual that
remains unmeasured is price movement between decision and fill. That is recorded as a limit of
paper trading, not hidden.

## Kernel findings from building it (recorded, not worked around)

1. **The kernel never checks a fill's fee or price.**
   - A fee 20× the hold, a negative fee, and a buy filled above its limit were all applied.
   - The fee hold is released, not reconciled against the actual fee.
   - Until the kernel checks fills, promotion-gate criterion 5 (fill costs within the measured
     range) is verified from gateway evidence.
2. **One venue receipt is reserved per lot.** Orders above about 59 lots are refused with
   `venue_event_capacity_exceeded`. The 25-lot clip fits.
3. **No "known but killed" query answer on the submit path.** A nonzero venue id with 0 filled
   moves the order to `live` and keeps its hold. A real adapter must map killed orders to venue
   id 0.
4. **Fills after a dropped response can be adopted only from identified reports.** A totals-only
   snapshot is refused as missing evidence.
5. **Same execution id, new sequence.** It gets `fill_identity_conflict` and the position does not
   move twice. But the order is not marked `conflict`, so a genuinely conflicting report is
   dropped without a visible sign.

## Consequences

- Paper results include the full measured costs, and position and reservation behaviour under
  every ch46 fault is tested against the real reducer.
- Finding 1 must be fixed before any live canary (roadmap M7).
- The paper venue links the test-only simulator library for wire rendering.
  `v2_venue_wire.cpp` should move to its own library.

## Amendment 2026-10-07: the stochastic paper run departs from two terms of this ADR

**Status: accepted for now by the owner on 2026-10-07** ("let the cost model be that for now"). The
rules below apply for as long as it stands, and the closing condition at the end is unchanged.

The stochastic paper run started on 2026-10-07
(`docs/evidence/dual_indicator_paper_2026-10-07/`) keeps fill-or-kill and the bounded,
persisted paper venue. It departs from this ADR in two ways:

| Term of this ADR | What the run does | Evidence for the run's value |
|---|---|---|
| Per-side cost is the **measured** True Markets cost (SOL: 100 bp buy, 98 bp sell) | 30 bp per side: 20 bp service fee plus 10 bp execution allowance | Only the sell-side service fee has live evidence (`docs/evidence/cost_recheck_2026-10-05/`); the rest is assumed |
| The limit, and so the fill price, is the True Markets reference price | The close of the last completed Binance SOLUSDT five-minute bar | None: it is a proxy from another venue and another quote asset |

**Rules while the deviation stands**

1. The paper venue's own numbers are labelled "30 bp scenario" wherever they are shown.
2. The observer re-prices the same fills at the measured costs and publishes that P&L and the
   volume per dollar lost beside the scenario figures (`velocita_indicator_measured_cost_*`,
   `velocita_indicator_volume_per_usd_lost{cost="measured"}`).
3. Any statement about this strategy's economics quotes the measured-cost figure, or both.
4. A paper result from this run is not evidence for the live gates in
   `docs/LIVE_TRADING_JOURNEY.md` until the cost is measured on both sides and the fill price
   comes from True Markets.

**To close it,** either measure the buy-side fee and the execution cost and replace the table
above with the measurement, or return the run to the measured costs.


## Owner correction 2026-10-07: one-second pilot fee epoch

The owner requested matching the approximately 20 bp TM fee for the new pilot.
New paper fills use 20 bp per side with zero additional fixed execution allowance.
Only the SOL sell fee is settlement-verified; buy symmetry and Binance proxy fills
remain assumptions. Historical 30 bp fills and account cash are retained exactly.
A cutover record fixes the prior-order prefix and new fee epoch; the observer
checks both and shows pilot-only P&L separately from account lifetime history.
The 100/98 bp repricing is retained as a historical-route stress comparison, not
a claim about the current service fee. These labels supersede the 30 bp display
rule for the new epoch only. No live gate is closed by this change.
