# Fields that must be confirmed before connecting the True Markets adapter

Every row below is a fact that changes a conclusion. A wrong maker fee inverts
the sign of the expected edge; a wrong tick size makes every quote invalid; an
assumed order book that does not exist makes the entire study describe a
strategy that cannot be run.

Nothing is defaulted to a plausible value. The collector refuses to start on any
pair whose fields are still `unverified`, and that refusal is the feature.

Verification state as of **2026-09-04**, from `docs.truemarkets.co` and its
quickstart page.

## Confirmed

| Fact | Value | Source |
|---|---|---|
| API base URL | `https://api.truemarkets.co` | quickstart |
| Auth scheme | ES256 JWT, `POST /v1/auth/api-key/token`, signing `{key_id}.{timestamp}` | quickstart |
| Asset catalog | `GET /v1/conductor/assets`, filterable by `venue` (CeFi/DeFi) | quickstart |
| Quotes | `POST /v1/conductor/quotes`, non-committal | quickstart |
| Orders | `POST /v1/conductor/orders`, places **market orders** | quickstart |
| Order status | `GET /v1/conductor/orders/{order_id}/status` | quickstart |
| A "Market Data" API exists | advertises real-time quotes, book, candles | docs index |

`POST /v1/conductor/orders` is the only endpoint that can create an external
effect. This package never calls it: `ReadOnlyHttp` refuses any non-GET request
and any path containing `/orders`, before a socket is opened.

## Blocking - answer these first

These decide whether the project is a market-making project at all.

| # | Question | Why it blocks everything |
|---|---|---|
| 1 | Does the DeFi surface support **resting limit orders**? | A passive strategy is impossible without them. Documented retail flow is RFQ + market order. |
| 2 | Is **post-only** available and documented? | Without it, a "maker" quote can cross and pay taker fees, inverting the economics. |
| 3 | Is there a **public order book** for DeFi pairs, at what depth? | Spread, depth, imbalance, microprice, and queue modelling all require one. |
| 4 | Are **maker rebates / volume-tiered fees** available on the DeFi path, or only on the KYC'd CeFi firm path? | Determines whether maker volume is even rewarded for this account. |
| 5 | Which **chain** do the DeFi pairs settle on, and what does a fill cost on-chain? | Gas is amortised into the cost floor; at 1 USDC per quote it can exceed the entire spread. |

If (1) or (3) is "no", stop. The correct output is a venue-level NO-GO and a
decision about whether to pivot to a taker-side volume strategy. Continuing to
build a market-making simulator against a venue with no resting quotes would
produce a scorecard for a strategy that cannot exist.

## Per-pair economics

| Field | Needed for | Consequence if guessed |
|---|---|---|
| `tick_size` | quote rounding | quotes round to invalid prices |
| `lot_size` | size rounding | sizes round to invalid quantities |
| `min_notional_quote` | 1 USDC feasibility | the whole research size may be unquotable |
| `maker_fee_bps` | cost floor | wrong sign on expected edge |
| `taker_fee_bps` | forced-flatten cost | understated inventory risk |
| `gas_cost_quote` | cost floor | the term most likely to disqualify the strategy |
| eligibility for the leaderboard | pair selection | studying an ineligible pair wastes the whole window |

## Market-data mechanics

| Field | Needed for | Consequence if absent |
|---|---|---|
| book endpoint path and response schema | all book metrics | cannot collect |
| WebSocket URL and channel names | low-latency collection | REST polling only, worse staleness |
| sequence numbers or checksums | gap detection | gaps become invisible, and invisible gaps become "results" |
| public trade prints | fill modelling | no external flow, so no fill model at all |
| stated trade direction | markout attribution by side | direction must be inferred, weakening every markout |
| venue event timestamps | latency measurement | latency is unmeasurable; only local receipt time exists |
| rate limits | collector cadence | risk of throttling or a ban |
| authentication requirement for market data | whether collection needs a read-only key | may block collection entirely |

## Leaderboard semantics

| Field | Why it matters |
|---|---|
| Which volume metric is ranked (raw vs multiplier-adjusted) | changes the required activity by the multiplier factor |
| Maker multiplier value, if any | the worksheet computes both readings because this is unknown |
| Eligible assets and pairs | an ineligible pair contributes nothing |
| Competition window and current thresholds | required activity per hour is meaningless without it |
| Whether self-trading or wash volume is detected and penalised | it is prohibited here regardless; this is about understanding the ranking |

The competition worksheet computes both the raw-volume and adjusted-volume
readings and labels every input `unverified`. It never claims qualification,
placement, or that a target is reachable.

## How to record an answer

Set the value in `model/config/research.toml` and mark its confidence:

```toml
tick_size  = "0.0001"
confidence = "verified"
source_url = "https://docs.truemarkets.co/..."
```

`confidence` is stored per field in `pair_metadata` alongside its source URL and
collection timestamp. A value we inferred must never become indistinguishable
from one the venue documented - that is how an assumption becomes a fact nobody
remembers making.
