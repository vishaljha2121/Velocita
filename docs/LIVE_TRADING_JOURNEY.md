# Live trading journey: paper to a True Markets canary

Written 2026-09-30. **Live trading is not approved.** This is the ordered plan and the list of what
must be true at each gate. Nothing here authorizes a credential, a venue call, funding, or a byte
change. Each gate names its evidence; each **OWNER** item is a decision only the owner can make.

Decision record: ADR 0019 (execution architecture and key custody). Related: ADR 0009 (two-step send),
0010 (key custody), 0011 (swap model), 0013 (authority), 0015 (limits), 0017 (gateway), 0018 (paper
venue); `STRATEGY_PROMOTION_GATE.md`; roadmap M5 to M7 in `V2_NUC_PI_TRADING_ROADMAP.md`.

Budget: **$25 total** (the owner's whole trading budget), with a loss cap of **$10 to $12 per week**
stated by the owner. At the measured ~1 % fee per side a $25 round trip costs about $0.50, so fees
dominate at this size.

## 0. Where things stand

| Piece | State |
|---|---|
| Shadow and paper | Running (Phase 2). Paper fills fill-or-kill at the limit with measured costs (ADR 0018). |
| Kernel risk | Per-order, reserved, position, long-only limits, operator-only (ADR 0015). No loss limit. Fill fee and price are not checked (ADR 0018 finding 1). |
| Gateway | Separate process, one-shot authority proof per risk-increasing send, halts new risk on any unknown (ADR 0017). |
| True Markets adapter | **Mock-level only** (this change): `TrueMarketsVenue` behind `sim::VenueTransport`, injected HTTP/MCP API, injected `Signer`, test signer only. 22 venue + 7 wire tests, 23 of 23 mutants killed. |
| `--venue truemarkets` | Refused in every build (no live transport compiled). Tested on the binary. |
| Cluster network | Wifi only. Election bursts: term 49 to 191 in 6 h, both remote hosts dropping from nuc1's view at once. Every election stops the gateway's new risk. |

## 1. The journey, in order

Each step starts only when the previous gate is met. Steps 1 to 4 can overlap; 5 onwards cannot.

### Step 1. Paper earns the right to go further (promotion gate)

Gate: all of `STRATEGY_PROMOTION_GATE.md` Stage 1, then the Stage 2 items that do not need a venue:
fills reconcile exactly with kernel positions, no unknown outcome left unresolved, hard limits set and
tested. Paper P&L is reported, never used as the argument.

**OWNER:** accept the research battery result (walk-forward p above 0.05 needs explicit acceptance).

### Step 2. Make the cluster stable enough to trade (new, from the live findings)

Today every election halts new risk and needs a manual restart to re-arm, and elections come in bursts.
A live canary would be halted most of the time, and each manual restart is a startup reconciliation
(step 7) done by hand. Before live:

1. **Raft pre-vote**, so a host that briefly loses the others cannot inflate the term and depose a
   healthy leader. Evidence: the same six-hour window with zero spurious elections, or a measured
   reduction with the cause of each remaining one explained.
2. **Automatic, reconciliation-gated gateway re-arm.** After a leader change the gateway stays halted
   until a full reconciliation pass (step 6) and the balance check (step 7) both come back clean, then
   re-arms itself. It must never re-arm with an unknown outcome, a conflict, or a balance mismatch
   (ch49 §"Cold and warm start gates": enablement is derived from the gates, not from a process
   listening). Tests: election during an in-flight swap, election storm, re-arm refused on each
   unclean condition, and mutation of each refusal.
3. **OWNER action: wired Ethernet** for nuc1 and both remote hosts (or at minimum a dedicated access
   point). The simultaneous drop of both remote hosts points at the shared wireless link, which
   software cannot fix.

Gate: a 72-hour soak with no unexplained election, and every forced election drill re-arms only after
a clean reconciliation.

### Step 3. Close the venue contract (roadmap M5)

The exact calls of one live order, with what is recorded and what is not:

| # | Call | Recorded? | Notes |
|---|---|---|---|
| 0a | `POST /v1/auth/api-key/token` (ES256 JWT over `{key_id}.{timestamp}`) | Envelope verified by experiment (D-13) | REST; needs the API key |
| 0b | MCP `login_request_code` / `login_verify_code` (email OTP) | Input schema only | MCP trading session; a person must enter the code |
| 1 | MCP `get_quote {chain, side, base_asset, qty, qty_unit}` or REST `POST /v1/conductor/quotes` | Input schema; output recorded 2026-09-28 (quote_id, order_side, qty, qty_out, fee, fee_asset, issues; payloads present but omitted) | Documented non-committal |
| 1' | MCP `trade_prepare {same}` | Input schema and description only | **May submit directly (D-26)**; used only by the fallback flow |
| 2 | Sign each payload ("Turnkey stamps") | Not recorded | The signer decodes the unsigned transaction and checks it (ADR 0019 §1) |
| 3 | MCP `trade_execute {order_id, signatures}` or REST `POST /v1/conductor/orders` | Input schema only | Whether it accepts a `quote_id` is unknown (Q-1) |
| 4 | REST `GET /v1/conductor/orders/{order_id}/status` | Path only | No MCP status tool exists; response schema unknown |
| 5 | MCP `get_balances` | Input schema only | Needed for step 7 |

Questions that must be answered before arming (the adapter's fixture shapes stand in for them):

- **Q-1** Is the quote-bound flow executable: can a quote's payload be signed and submitted, and by
  which call and id?
- **Q-2** For a buy with `qty_unit=base`, is the base amount exact and the USDC debit bounded, or is
  only the USDC amount exact? (Exact-quote buys need ADR 0011's swap operation, a kernel byte change.)
- **Q-3** Does the signed transaction itself enforce the quoted amounts (minimum out / maximum in),
  so a worse route cannot execute? Without this no bound is enforceable (ADR 0011 §6, D-28).
- **Q-4** Quote lifetime, and the execute reply for an expired quote.
- **Q-5** Execute and status reply schemas; the complete status vocabulary; which words are final.
- **Q-6** Does status answer for a quote id when the submit reply is lost? (Adapter setting
  `status_accepts_quote_id`, default false, which leaves such an order unknown.)
- **Q-7** Rate limits per call, and how pacing is signalled (observed: `-32602 invalid params`, D-27).
- **Q-8** Minimum notional and size increment for SOL on solana. $10 quotes returned without a size
  issue; execution at that size is unverified.
- **Q-9** Settlement finality: when is a swap final, and can an accepted swap fail on chain?
- **C-1** Is the stamp credential the same P-256 API key as the JWT key (ADR 0010 assumed so)?
- **C-2** Can a credential be trade-only with withdrawals disabled? Until yes, a leaked credential can
  empty the account (ADR 0019 §1, "Blast radius").

How evidence is gathered, safely: owner-run captures of `get_quote` payloads only (non-committal),
redacted of wallet address and organization id before they enter the repo; never `trade_prepare`,
which can submit. Execute and status replies can only be recorded from a real order, which is step 9.

**OWNER:** whether and when to contact True Markets (the roadmap says not this iteration); which
instrument (SOL on solana measured about 200 bp round trip; cbBTC on base about 234 bp, and the
strategy trades BTC); whether the account used is the one that later holds funds.

Gate (M5 exit): Q-1, Q-3, Q-5, Q-9 and C-1 answered from a venue source; decoders replaced with
recorded shapes; every row of section 4 below either verified or explicitly accepted as not
applicable.

### Step 4. Kernel fixes that must precede real money

1. **Fill fee and price checks in the kernel** (ADR 0018 finding 1). The adapter now refuses a quote
   outside the limit or fee cap and flags a settled breach as a conflict, but the kernel still books
   any fee it is told. Required before M7 by ADR 0018.
2. **A loss breaker.** No kernel rule reads realized loss. Until one exists the weekly cap is
   enforced by funding (the account holds only the canary capital) and by an operator alert that
   halts the strategy (section 3).
3. **An operator flatten path.** On a swap venue "flatten" means selling the whole base position.
   Today `reduce_only` suppresses every proposal including exits, so there is no in-system way to
   flatten. Needed: an operator-authorized, reduce-only sell of exactly the position, through the
   same authority and gateway path (ch49 §"Levels of intervention": flatten permits only a
   documented reduction with price limits). Manual sale in the TM web app is the emergency fallback,
   and it creates a position the kernel cannot explain until adopted.

Each needs its own review; items 1 and 3 may touch bytes (the snapshot has 17 bytes of headroom,
ADR 0015).

### Step 5. Build the signer (ADR 0019 §1)

A separate process and UID on nuc1, no network access, a Unix socket for the gateway's UID only.
It holds the TM signing credential, loaded by the operator. It decodes each venue payload (a Solana
transaction for SOL) and signs only when the decoded effect matches the `SigningIntent`: side, mint,
wallet, exact base amount, quote bound within the kernel's reservation, not expired. It refuses any
replayed request sequence or signed handle, and writes an append-only audit. There is no sign-bytes
operation. (ch57 §"Exchange API keys and the signing service".)

Tests: recorded payloads (from step 3) decoded, every field tampered and refused, replay across a
signer restart refused, and a mutation run on every refusal. The largest risk in the journey lives
here: if a routed swap cannot be decoded well enough to prove the bound, the signer refuses and live
stays blocked.

**OWNER:** where the credential lives at rest (systemd credential or a 0600 file on nuc1, or a
hardware token), and who may start the signer.

### Step 6. Build the live venue transport

A new library, linked only by the gateway, implementing `TrueMarketsApi`: TLS to the two TM hosts
only, a session from a token the operator mints (ADR 0010; the canary stops when the token expires,
which is accepted friction), JSON-RPC id matching, bounded replies, and `no_response` whenever the
answer cannot be proven. Egress allowlist: the TM API and MCP hosts and nothing else. Only then is
`kLiveVenueTransportCompiled` set to true, in a reviewed change. The interlocks stay: the adapter
library itself still references no network symbol, and no voter or operator binary links a venue
library.

Reconciliation against TM (ch46 §"Reconciliation invariants", ch50 §"Continuous reconciliation"),
already implemented at mock level:

| Venue answer | Adapter answer | Kernel result |
|---|---|---|
| settled, exact | `known`, filled = quantity; identified ack and fill delivered | Match / Adopt: position booked once |
| settled, partial, over, beyond limit or over fee cap | booked as reported where it fits, `inconsistent` | Conflict: slot blocked, gateway halts |
| failed or expired | `not_found` | released (`rejected`, or `cancelled` if it had been acknowledged) |
| pending | `known`, nothing filled | Incomplete: hold kept, asked again |
| no answer, or no queryable id | `unavailable` | Unavailable: hold kept, gateway halted |

An unknown submission is never resent; a retry is a new authorization of a new order after the old one
is proven absent.

### Step 7. Startup reconciliation of balances (new, from the live findings)

The paper venue keeps balances in process memory and resets them when the gateway restarts. A real
account does not reset, so on every live start the gateway must reconcile three views before new
risk can take effect:

1. the venue's balances (`get_balances`: USDC and the base asset in the trading wallet);
2. the kernel's committed position for the account and instrument;
3. the operator's funding record (what was deposited, minus fees and realized P&L booked).

Base asset: zero lot tolerance. USDC: a documented atomic tolerance with an owner and an expiry
(ch50 §"Cadence and zero tolerance"). Any mismatch keeps new risk stopped and raises a critical
alert; it is resolved by the ch50 §"Break workflow", never by editing a cache. A balance difference
can block trading but can never be booked as a fill (ADR 0011 §6). The same check runs after every
fill (event-triggered) and on a periodic sweep. Not implemented yet: the adapter has no balance call.

### Step 8. Drills (roadmap M6)

On the live-wired build against a recording TM fake, then repeated where safe on the funded account
with no strategy enabled. Each drill records command receipt, epoch commit, sender observation and
the venue's answer (ch49 §"Automatic breakers"):

| Drill | Pass means |
|---|---|
| kill epoch during an in-flight swap | no new send after commit; the in-flight swap is reconciled, not assumed cancelled |
| `strategy-mode halted` / `no_new_risk` | no authorization, no signature request |
| stop the signer mid-send | `pre_send_failure`, nothing submitted |
| lost execute reply | `ambiguous`, halt, status adopts or releases; no resend |
| lost prepare reply (fallback flow) | unknown, halt, stays blocked until an operator resolves it |
| election during a send | the proof expires; re-arm only after clean reconciliation |
| gateway restart with a position held | step 7 passes against the real balance, or new risk stays stopped |
| balance mismatch injected | halted, alert fires, break workflow closes it |
| venue pacing | local bucket refuses first; venue pacing on execute is ambiguous |
| flatten | the operator path sells exactly the position at a bounded price |
| token expiry mid-canary | sends fail closed, nothing ambiguous left behind |
| NUC loss, Pi loss, partition, disk full | as in roadmap M6 |

Gate: every drill passes, Grafana shows the stage and reason for each, and the alerts fire and clear.

### Step 9. First controlled trade

One buy and one sell of the smallest size TM accepts (target $10), with the strategy disabled, the
signer in a confirm mode that requires the operator to approve each signature, and every raw reply
recorded (redacted). Purpose: record the real execute and status replies, verify fee and settlement,
and prove that reconciliation matches to the lot and the micro. This is also where the fixture
shapes are replaced.

**OWNER:** approve this specific trade, fund the account with the canary capital only, and be
present for it.

### Step 10. Canary (roadmap M7)

Sizing (ch52 §"Forward testing wins": an information purchase, controls stricter than normal):

| Limit | Value | Enforced by |
|---|---|---|
| Capital at the venue | $25, and nothing else in the account | funding (the only hard bound on a leaked credential) |
| Per-order notional | $10 in week 1, then up to $24 (a buy's debit plus 1.2 % fee must fit $25) | kernel `max_order_gross_notional` |
| Open positions | one, long only | kernel `max_position_quantity`, `allow_short=0` |
| Fee | 120 bp maximum | kernel `maximum_fee_ppm=12000`, adapter `max_fee_bps=120`, signer bound |
| Order rate | 2 sends per minute at most | adapter bucket, gateway quota |
| Weekly loss | stop at $5 in week 1; owner cap $10 to $12 afterwards | operator alert and halt (no kernel breaker yet) |
| Total loss | stop and review at $12 cumulative | operator |

At the owner's cap, two bad weeks spend the whole $25 budget. The demo strategy trades about nine
times a year, so a canary of a few weeks may see no signal trade at all; the controlled trade of step
9 is then most of the evidence.

Stop conditions (any one halts the canary: `strategy-mode halted`, then investigate):

- an unknown outcome not resolved within one status interval series (roadmap: unknowns cannot look healthy);
- any reconciliation conflict, adapter breach, or balance mismatch;
- a signer refusal of a payload the adapter had accepted (the two disagree about the venue);
- a fill fee above 110 bp or a route more than 30 bp from the reference;
- the weekly or total loss limit;
- more than three gateway halts in a day, or any election storm;
- token expiry or any unexplained authentication failure;
- any alert without a runbook entry.

**OWNER:** sign off the specific canary (size, duration, stop conditions), and choose alert delivery
(roadmap M3a leaves this to the owner).

### Step 11. Review, then expand or roll back

Expansion needs complete attribution and reconciliation with no unexplained residual (M7 exit), and a
new owner approval. Volume or profit alone is never acceptance.

## 2. Monitoring and alerts for live

Existing gateway series: `velocita_gateway_new_risk_stopped`, `velocita_gateway_heartbeat_timestamp_seconds`,
unknown outcomes, rejects by reason, fee total, positions, stage latency. To add before live, all
critical unless noted:

- signer: up, refusals by verdict, key version, last signature age;
- adapter: breaches, local rate limiting, status backlog (orders uncertain or accepted but unsettled) and its age;
- balance reconciliation: last clean check age, mismatch flag;
- realized and unrealized loss against the weekly and total limits (warning at 50 %, halt at 100 %);
- election rate and leader changes (warning), gateway re-arm refusals;
- token expiry countdown (warning at 30 minutes).

Metric labels stay bounded; no order ids, signatures, payloads or wallet addresses in metrics or logs
(roadmap M3a).

## 3. Rollback

In order, fastest first. Each is a tested command (ch45 §"Operator control plane"; ch49 §"Levels of
intervention"):

1. `strategy-mode halted K E`: no new decisions are authorized.
2. `kill-epoch K+1`: every earlier decision becomes unauthorizable.
3. Stop the signer unit: nothing more can be signed, whatever the gateway does.
4. Stop the gateway; reconcile any uncertain order from its status before restarting anything.
5. Flatten through the operator path (step 4 item 3), or, as a last resort, sell in the TM web app
   and record the external trade for adoption.
6. Revoke or rotate the TM credential; the owner withdraws the funds.
7. Return to `--venue paper`. The paper venue starts from its configured balance, which must be set
   to match the kernel's position (the restart-reset finding).

A swap cannot be cancelled once submitted (ch19 §"The effect cannot be retracted"), so rollback stops
future risk; it cannot undo a submitted trade. That is why the bounds sit before signing.

## 4. Every item in MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md, with current status

That file is protected and not edited here. Status as of 2026-09-30, from the discrepancy log and the
recorded evidence. "Blocks live" means the canary cannot start until it is resolved.

### Confirmed (2026-09-04)

| Fact | Status now | Blocks live? |
|---|---|---|
| API base URL `https://api.truemarkets.co` | still confirmed | no |
| Auth: ES256 JWT, `POST /v1/auth/api-key/token` | confirmed; envelope verified by experiment (D-13) | no |
| Asset catalog `GET /v1/conductor/assets` | confirmed, public (D-11); the mint address source of record | no |
| Quotes `POST /v1/conductor/quotes`, non-committal | confirmed; MCP `get_quote` output recorded 2026-09-28 | no |
| Orders `POST /v1/conductor/orders`, market orders | path confirmed; request and reply body unrecorded; MCP `trade_prepare` may submit directly (D-26) | yes (Q-1, Q-5) |
| Order status `GET /v1/conductor/orders/{id}/status` | path confirmed; response unrecorded | yes (Q-5) |
| A Market Data API exists | not locatable on this host (D-18, D-19); MCP price history works | no (prices come from the existing feed) |

### Blocking questions

| # | Question | Status | Consequence |
|---|---|---|---|
| 1 | Resting limit orders on DeFi? | none on the MCP surface (D-23); venue-wide unverified | market-making NO-GO; project pivoted to a low-turnover taker strategy |
| 2 | Post-only? | not documented for DeFi (D-4) | not applicable to swaps |
| 3 | Public DeFi order book? | none advertised (D-23) | market-making NO-GO |
| 4 | Maker rebates or volume tiers on DeFi? | not on the DeFi path (D-4); fee measured flat 1 % both sides (D-5) | fees dominate; volume strategy not viable |
| 5 | Chain and on-chain cost? | solana and base (MCP schema); gas sponsored per owner report, unverified in docs (D-5); route adds 0 to 24 bp | SOL is cheapest; Q-9 settlement still open |

### Per-pair economics

| Field | Status | Blocks live? |
|---|---|---|
| `tick_size` | published nowhere (D-12, D-14); the catalog row must choose one and it has no venue meaning for a swap | yes: owner and TM must agree the lot and tick the kernel uses (Q-8) |
| `lot_size` | published nowhere; token `decimals` is precision, not a lot (D-20) | yes (Q-8) |
| `min_notional_quote` | unknown; $10 quotes returned without a size issue | yes (Q-8) |
| `maker_fee_bps` | not applicable (no maker path) | no |
| `taker_fee_bps` | measured 100 bp flat, buy and sell, SOL and cbBTC (2026-09-28) | no; re-verify on the first trade |
| `gas_cost_quote` | sponsored per owner report; unverified | no if the first trade confirms it |
| leaderboard eligibility | unknown | no (not a canary goal) |

### Market-data mechanics

| Field | Status | Blocks live? |
|---|---|---|
| book endpoint and schema | none (D-23) | no (not used) |
| WebSocket URL and channels | `/v1/defi/market` connects; every documented channel name rejected | no |
| sequence numbers or checksums | unknown | no for the 30 s strategy; staleness is guarded by the decision frame |
| public trade prints | unknown | no |
| stated trade direction | unknown | no |
| venue event timestamps | candles only | no |
| rate limits | undocumented; pacing reported as `-32602` (D-27) | yes for order calls (Q-7) |
| auth for market data | documented public (ADR 0010 amendment) | no |

### Leaderboard semantics

All five rows (ranked metric, maker multiplier, eligible pairs, window and thresholds, wash detection)
remain unknown. None blocks the canary; buying leaderboard volume at the retail fee is not viable
(markup evidence, "Consequences"). Self-trading and wash volume stay prohibited regardless.

## 5. Implemented, mocked, unverified

| Item | State |
|---|---|
| quote or prepare, inspect, sign, execute, status state machine | implemented, tested against the real reducer |
| request encoding (get_quote, trade_prepare, trade_execute, status path) | implemented from the recorded input schemas |
| quote reply decoding | implemented, tested on recorded bytes |
| execute and status reply decoding | fixture shape: unverified |
| payload format | mock (`MOCKTX1`): the real transaction is unrecorded |
| signer | interface and a deterministic test signer only; no production signer |
| HTTP/MCP transport, TLS, sessions | not written; injected in tests |
| balance reconciliation, pre-vote, automatic re-arm, flatten verb, kernel fee check, loss breaker | not written (steps 2, 4, 7) |
| `--venue truemarkets` | parsed and always refused |

## 6. Owner decisions, collected

1. Accept the research result and the Stage 1 and 2 evidence (step 1).
2. Wired networking for the cluster (step 2).
3. Contact True Markets with Q-1 to Q-9, C-1, C-2, or not (step 3).
4. Instrument: SOL on solana (cheapest measured) versus cbBTC on base (what the strategy trades) (step 3).
5. Where the signing credential lives, and who starts the signer (step 5).
6. Approve and attend the first controlled trade; fund the account with the canary capital only (step 9).
7. Canary size, duration, loss limits and stop conditions; alert delivery channel (step 10).
8. Expansion or rollback after review (step 11).
