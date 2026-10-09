# Paper trading runbook (Phase 2)

Updated: 2026-10-01. Paper means the kernel authorizes orders and the paper
gateway fills them against the True Markets simulator. Nothing here reaches a
venue, and no venue key exists on any host. The owner's whole budget is $25;
this runbook is not an approval to trade live (the canary is gated separately,
`STRATEGY_PROMOTION_GATE.md` stage 2).

## Who holds which authority

| Process (nuc1) | Caller key | CallerId | Band | May do |
|---|---|---|---|---|
| Operator client (Mac) | 9 | 7 | operator | kill epoch, executor generation, catalog, strategy mode, risk limits |
| `velocita-v2-shadow-setup.service` (one shot) | 11 | 9 | operator | install the rule program, strategy mode normal |
| `velocita-v2-shadow-edge.service` (tick loop) | 12 | 21 | strategy | record source events, evaluate frames, ask for authorization (paper) |
| `velocita-v2-gateway.service` | 13 | 22 | strategy | claim authorized effects, report simulated fills |

The tick loop refuses to start with an operator CallerId (<= 15), and setup
refuses a strategy one, so the process that proposes can never change
controls. Each unit mounts only its own key file. Every voter still holds every
caller key: it authenticates them (the accepted symmetric-key gap, CP-051).

## 1. Keys (on the Mac)

    tools/velocita-secrets init caller-12        # strategy tick loop
    tools/velocita-secrets init caller-13        # paper gateway

Push both to EVERY voter host (each node authenticates every caller), with the
host's `velocitas` uid in the path:

    for h in nuc1 vishalkapi nuc2; do
      tools/velocita-secrets push caller-12 velocitas@$h /run/user/<uid>/velocita-secrets/caller-12.key
      tools/velocita-secrets push caller-13 velocitas@$h /run/user/<uid>/velocita-secrets/caller-13.key
    done

`caller-11` (setup) and `caller-9` (operator) already exist. The secrets check
(`secrets-presence.sh`) reports caller-12 and caller-13 on every voter once the
new node configs are in place.

## 2. Cluster

Render the node configs from `deploy/v2/three-host/node-N.conf.template`
(callers 12 and 13 and `gateway.caller = 22` are in the templates). Phase 2
changes kernel command bytes, so start a FRESH cluster (new incarnation), then
the usual ceremony from the Mac (operator client):

    velocita-v2-client ... --sequence 1 kill-epoch 1
    velocita-v2-client ... --sequence 2 executor-generation 1
    velocita-v2-client ... --sequence 3 install-catalog <ROOT> 1 1
    velocita-v2-client ... --sequence 4 assign-role 21 strategy
    velocita-v2-client ... --sequence 5 assign-role 22 gateway

On a fresh cluster the edge's state files start at source generation 1. (Only
if the tick loop were moved onto caller 12 on an OLD cluster: give the edge a
new `--source-id` such as 42, since a fresh state file cannot continue source
41's generation, point setup at the old `/state/submitter.{session,state}`, and
publish the 25-lot clip as `--policy-version 2` in both units, because policy
21 version 1 is already active with 10 lots.)

## 3. Install the program (operator, one shot)

`~/velocita-v2/shadow/shadow.env` as described in the edge unit, with
`VELOCITA_MODE_ARGS=` empty (shadow). Then on nuc1:

    systemctl --user start velocita-v2-shadow-setup
    journalctl --user -u velocita-v2-shadow-setup | grep SETUP
    # SETUP generation=1 first_sequence=1 kill_epoch=1 executor_generation=1 strategy_mode=1

It writes `/state/trend.program` with the edge's own program arguments (a test
keeps the two units' arguments equal) and installs it. Re-running it is safe.

## 4. Risk limits (operator, from the Mac)

The numbers come from the execution-catalog row (`tests/v2_shadow_catalog_rows.hpp`,
`velocita-v2-shadow-catalog --truemarkets-cbbtc`): account 1, venue 20,
instrument 1; one tick is $1 per BTC; one lot is 1,000 base atoms at 8
decimals = 0.00001 BTC; `quote_micros_per_tick_lot` = $1 x 0.00001 = 10 micros.

The kernel's arithmetic (`src/v2_action_resolver.cpp`, `apply_authorize_decision`
in `src/v2_kernel.cpp`):

    notional   = quote_micros_per_tick_lot x price_ticks x lots = 10 x P x 25 = 250 P micros
    margin     = ceil(notional x initial_margin_ppm / 1e6)
    fee hold   = ceil(notional x maximum_fee_ppm / 1e6)
    authorized only if lots <= max_order_lots, reserved + lots <= max_reserved_lots,
    notional <= max_order_notional, held margin + fee (all open orders) <= max_reserved_exposure

The clip is 25 lots = 0.00025 BTC. At BTC $84,000 (P = 84,000 ticks):
notional = 250 x 84,000 = 21,000,000 micros = $21.00; margin $21.00; fee hold
ceil(21,000,000 x 0.015) = 315,000 = $0.315; held $21.315 <= $25.

| Owner setting | Field | Value | Why |
|---|---|---|---|
| $25 per order | `max_order_notional_micros` | 25000000 | $25 x 1,000,000 |
| $25 total exposure | `max_reserved_exposure_micros` | 25000000 | margin + fee holds of every open order |
| one clip per order | `max_order_lots` | 25 | 0.00025 BTC |
| one open order | `max_reserved_lots` | 25 | a second 25-lot order would reserve 50 |
| one position | `max_position_lots` | 25 | position after every open order fills |
| realized loss | `max_window_loss_micros` | 10000000 | realized loss since the last reset; at the limit only closing orders pass |
| long-only | `allow_short` | 0 | a sell may only reduce a long |
| spot, fully funded | `initial_margin_ppm` | 1000000 | the margin hold is the whole notional |
| fee hold | `maximum_fee_ppm` | 15000 | 1.5 %, above the measured 1.1-1.2 % per side |
| risk policy | `risk_policy_version` | 1 | must equal the program's `policy.risk_version` |
| new account | `expected_risk_version` | 0 | the account does not exist yet |
| audit | `reason_code` | 20260929 | non-zero reference into the operator's log |

Where the caps bind: exposure needs 250 P x 1.015 <= 25,000,000, so a buy is
refused (`insufficient_risk_capacity`) above BTC $98,522 (at P = 98,522 the
holds are 24,630,500 + 369,458 = 24,999,958; at 98,523 they are 25,000,212).
Above $100,000 the program's own template cap (`order.max_notional_micros`
25,000,000) already rejects the proposal at decision time. Money at risk is
the cost basis of at most 25 lots, bought only when notional plus fee hold fits
in $25. A round trip at 1.1-1.2 % per side costs about $0.46-0.50 on $21: fees
dominate at this size.

Check the summary first, then send (operator key on stdin as usual):

    tools/velocita-secrets client-key caller-9 | velocita-v2-client --caller-key-id 9 --caller-id 7 --key-stdin \
      --incarnation <INC> --session-file ~/.velocita/client.session --response-endpoint 100.91.80.64:42400 \
      --node 1=100.72.127.102:42301 --node 2=100.84.151.116:42302 --node 3=100.85.36.22:42303 \
      --aeron-dir /tmp/velocita-client --sequence 6 [--dry-run] \
      risk-limits account=1 instrument=1 risk_policy_version=1 expected_risk_version=0 \
        kill_epoch=1 executor_generation=1 max_order_lots=25 max_reserved_lots=25 \
        max_order_notional_micros=25000000 max_reserved_exposure_micros=25000000 \
        initial_margin_ppm=1000000 maximum_fee_ppm=15000 max_position_lots=25 allow_short=0 \
        reason_code=20260929 max_window_loss_micros=10000000 reset_loss_window=0

`COMMITTED ... code_name=applied ... risk_version=1`. A new account starts at
risk version 1 and position version 1, which is what the edge binds by default.
Changing limits later: `expected_risk_version` = the current version; it moves
up by one.

## 5. Gateway, then paper

### 5.1 The paper venue's state file (first start only)

The paper venue keeps its balances, its known orders and the reports it has not
yet seen acknowledged in `~/velocita-v2/gateway/state/paper.state` (`/state/paper.state`
in the container). Every change is written (temporary file, fsync, rename) before
the answer that depends on it, so a restarted gateway answers exactly as the one
before it: same cash, same base, same duplicates and queries. The file is bound to
the cluster's incarnation; a fresh cluster needs a fresh file. The unit never
creates it, and refuses to start (exit 64, no restart loop) when it is missing,
torn or corrupt. A corrupt file is never replaced or edited: move it aside and
treat it as a reconciliation break (section 5.3).

First start on a fresh (flat) cluster, as a one-shot with the unit's own
arguments plus the bootstrap flags (it reads committed state once, writes the
file, prints `PAPER_STATE created ... mode=fresh`, and exits without trading):

    systemctl --user stop velocita-v2-gateway
    ExecStart=$(systemctl --user show -p ExecStart --value velocita-v2-gateway)   # copy the podman line
    # run that podman line by hand, with `--name velocita-v2-gateway-init` and, at the end:
    #     --paper-state-init --paper-quote-usd 25
    systemctl --user start velocita-v2-gateway

It refuses if the kernel holds any lots on the row (use adoption) or an order on
the row is not terminal. `--paper-quote-usd` is accepted only with a bootstrap
flag; on a normal start it is refused, because the balance is the file's.

**Adopting a position that already exists** (a cluster that traded before the
venue was persisted). The bootstrap reads the kernel's committed position for the
row and writes base = that position, quote = funding minus the cost basis you
state, recorded in the file as an adoption (lots, cost, applied index, state
version):

    ... --paper-adopt-position --paper-quote-usd 25 --paper-adopt-cost-micros <COST>

COST is what the held lots cost in micro-dollars, fees included (from the paper
book or the gateway journal's fills: notional + buy fee). It refuses when the
kernel is flat, when COST exceeds the funding, or when an order on the row is
open. Moving off the old stand-in flags: the gateway that ran with
`--paper-base-lots 25 --paper-quote-usd 3` is equivalent to adopting 25 lots
with COST = 22,000,000 (25 - 3 dollars). Stop the unit, run the adoption once
with `--paper-quote-usd 25 --paper-adopt-cost-micros <COST>`, then REMOVE
`--paper-base-lots` and `--paper-quote-usd` from `VELOCITA_GATEWAY_RISK_ARGS`
(both are refused on a normal start) and start the unit. Check the first
`ARMED new_risk ... kernel_lots=25 venue_lots=25` line.

### 5.2 Start

    systemctl --user enable --now velocita-v2-gateway
    # shadow.env: VELOCITA_MODE_ARGS=--mode paper
    systemctl --user restart velocita-v2-shadow-edge

The submitter prints `READY ... mode=paper caller_id=21`. Per tick it sends
RecordSourceEvent and an authoritative EvaluateDecisionFrame bound to the
account's risk and position versions; after a committed `action` decision with
a proposal it sends AuthorizeDecision. The kernel is the risk authority:
already long -> `position_limit_exceeded`, an order open -> `order_slot_occupied`,
over the caps -> `insufficient_risk_capacity`. On `stale_risk_state` the kernel
reports the account's current versions and the next frame binds them
(`velocita_paper_resyncs_total`). If the account is not at 1/1 when paper
starts, pass `--risk-version R --position-version P` in `VELOCITA_MODE_ARGS`.

Watch: `velocita_paper_authorizations_total{code}`, `velocita_paper_risk_version`,
`velocita_paper_position_version`, `velocita_paper_resyncs_total`, beside the
existing `velocita_shadow_*` series (names unchanged).

### 5.2.1 Making the paper venue refuse, split and time out

By default the paper venue fills every order completely at its limit. Four gateway flags turn on
the faults it can already simulate, each as orders per thousand (0 to 1000, default 0):

| Flag | What the venue does | What the system does |
|---|---|---|
| `--paper-reject-permille N` | kills the order (as a fill-or-kill that cannot fill) | reservation released; the strategy tries again on a later bar |
| `--paper-split-permille N` | reports one fill in two pieces | the kernel books each piece |
| `--paper-lost-request-permille N` | never receives the order; the send times out | the gateway stops new risk (`unknown_outcome`), asks the venue, finds nothing and releases; restart the gateway to resume |
| `--paper-lost-response-permille N` | fills the order but the reply is lost | the gateway stops new risk, asks the venue and adopts the fill; restart the gateway to resume |

The outcomes are drawn from a seed stored in the paper state file, so a run repeats exactly.
The two lost-message flags stop trading until someone restarts the gateway. That is what a
venue timeout does today; turn them on only for a watched session.

### 5.3 When the gateway stops new risk, and when it re-arms by itself

The gateway arms new risk only at a committed read where (a) the paper venue's
base equals the kernel's position for the row, to the lot, (b) no order on the
row is open and no venue report or gateway evidence is uncommitted, (c) the read
is not older than anything the gateway or the venue already saw committed, and
(d) `--allow-new-risk` and `--freshness-ms` are set. This check runs at startup
and on every state read (about every 250 ms), not only after a stop.

| Stop cause (`STOP new_risk cause=`) | What it is | Re-arms by itself? |
|---|---|---|
| `fence` | term or leader change, or a state read that timed out | yes: after `--rearm-quiet-ms` (default 30 s) with no further term change, and only through the check above |
| `unknown_outcome` | a send whose outcome could not be established | no: restart after reconciling the order |
| `conflict`, `reconciliation_exhausted` | the cluster holds a conflict, or a query ran out of attempts | no |
| `refused_evidence` | the kernel refused a venue report | no |
| `control_change` | kill epoch, executor generation or strategy mode changed | no: these are the operator's controls; restart the gateway after the change |
| `reducing_mode` | only the strategy mode changed, to `reduce_only` or `flatten` | yes: like a fence, after the quiet period and the check above |
| `reconciliation_mismatch` | the venue's book disagrees with the kernel at a comparable cut | no: see below |
| `capacity`, `corrupt_state`, `wrong_cluster`, `authority` | sequences or evidence queue exhausted; corrupt state; wrong cluster; clock or control regression | no |
| `no_loss_limit` (gauge only) | the gateway requires a kernel loss limit (`--require-loss-limit`, always for the live venue) and the account has none | yes: as soon as `risk-limits` sets `max_window_loss_micros` above zero |

What re-arm does NOT do: it never re-sends an order whose outcome is unknown,
never resolves a mismatch, never clears a latched cause, and never re-arms while
an order on the row is open (an election mid-order re-arms only after the fill
commits). An effect authorized while the gateway was stopped and never sent is
sent once, under a fresh proof, after the re-arm.

**Reconciliation mismatch** (`RECONCILIATION mismatch kernel_lots=K venue_lots=V`,
alert `VelocitaGatewayReconciliationMismatch`, gauge
`velocita_gateway_reconciliation_mismatch`): new risk stays stopped and both
values stay on `velocita_gateway_reconciliation_position_lots{source}`. Follow the
book's break workflow (ch50): keep the journal and a copy of `paper.state`,
find the missing or extra event, and close it with evidence. Do not edit the file
or the kernel to make the numbers agree. For paper, the closing actions are a
kernel correction through the operator path, or (only if the venue side is shown
wrong) moving the file aside and adopting the kernel's position with section 5.1.

Series: `velocita_gateway_stops_total{cause}`, `velocita_gateway_rearms_total`,
`velocita_gateway_stop_cause{cause}` (1 per active cause; `awaiting_arm` = only
the check above is pending; `not_configured` = never arms),
`velocita_gateway_reconciliation_verdict{verdict}`,
`velocita_gateway_reconciliation_last_match_timestamp_seconds`,
`velocita_gateway_venue_base_lots`, `velocita_gateway_venue_quote_usd`.
`velocita_gateway_fees_usd_total`, `velocita_gateway_volume_usd_total{side}` and
`velocita_gateway_last_fill_timestamp_seconds` are read from `paper.state` at
start (they survive restarts); every other gateway counter is per process.

### 5.4 A voter that will not start after a crash or power cut

Two refusals at start are deliberate and need a person (review of 2026-10-09, findings 2 and 4).
The cluster keeps running on the other two voters meanwhile.

| Journal line | Cause | Repair |
|---|---|---|
| `verified archive open refused` | the voter died while publishing an archive segment and left a file the catalog does not list | below |
| `corrupt complete WAL block` | a power cut tore the last write-ahead-log block | none yet. Do not edit the file. A voter cannot be rebuilt from the others until snapshot install exists, so it stays out until the next incarnation |

Archive repair, on the affected host, with the voter stopped. The data directory is
`~/velocita-indicator-paper/<run>/data/kernel/v2-archive`.

1. List it. The expected names are `v2-identity.bin`, `v2-execution-catalog.bin`,
   `v2-archive-catalog.bin` and segments `s-<64 hex>.bin`.
2. Move, do not delete, `v2-archive-segment.bin.tmp` and `v2-archive-catalog.bin.tmp` if present,
   to a directory outside the data root. Neither was ever published.
3. Start the voter. If it still refuses, one `s-*.bin` segment is not in the catalog. It is the
   newest by modification time. Move that one file out the same way and start again. The
   write-ahead log still holds its entries, and the next compaction publishes them again.
4. If it still refuses, stop. Do not move anything else. The voter stays out until the next
   incarnation.

## 6. Halting

| Need | Command (operator client) | Effect |
|---|---|---|
| stop new orders | `strategy-mode halted K E` | control lane; frames record `control_halted`, nothing is sent for authorization |
| stop new orders, keep evaluating | `strategy-mode no_new_risk K E` (or `cancel_only`) | every place-limit proposal, exits included, is `control_suppressed` |
| stop entries, let the strategy exit | `strategy-mode reduce_only K E` | an order that reduces the position goes through; everything else is `control_suppressed` (ADR 0024) |
| sell the position now | `strategy-mode flatten K E` | on the next frame (at most 30 s) the kernel orders a sale of exactly the position at the frame price; the gateway re-arms by itself (ADR 0024) |
| hard stop | `kill-epoch K+1` | halts AND makes every earlier decision unauthorizable |
| resume | `strategy-mode normal K E` (after a kill: the new K) or re-run setup | refused while any order's outcome is unknown; then restart the gateway (a mode, kill-epoch or generation change latches its new-risk stop, section 5.3) |
| stop proposing entirely | `systemctl --user stop velocita-v2-shadow-edge` | no frames at all |

K and E are the current kill epoch and executor generation (every `COMMITTED`
line prints them). The client has no mass-cancel verb yet: open paper orders
are resolved by the gateway's simulator.

## 7. Install a volume strategy (paper only)

Four volume programs sit beside the trend program. Each has its own policy
id and feature schema id, because feature 4 means something different in each.
The feature vector is always the four daily features (trend, ret1, vol, dev,
indices 0-3, the same as the trend program), then the strategy's one feature at
index 4, then the price in ticks (index 5). Rules are first-match; no match is
HOLD (no order).

| `--strategy` | Policy | Schema | Feature 4 (exact) | Rules | Parameters (default) |
|---|---|---|---|---|---|
| `trend` (running) | 21 | 31 | none (4 features) | `0 gt 0 enter_long`, `0 le 0 exit` | `--value 125` |
| `churn` | 22 | 32 | phase = (floor(event_ns / (P x 1e9)) mod 2) x 1,000,000 | `4 gt 0 enter_long`, `4 le 0 exit` | `--period-s` P (60) |
| `trend_churn` | 23 | 33 | phase, as churn | `0 le 0 exit`, `4 gt 0 enter_long`, `4 le 0 exit` | `--value 125 --period-s 60` |
| `rotate` | 24 | 34 | rotation = (event_ns mod (R x 1e9)) / 1000, microseconds into the period | `0 le 0 exit`, `4 lt W x 1e6 exit`, `0 gt 0 enter_long` | `--value 125 --period-s` R (1800) `--exit-window-s` W (60) |
| `dip` | 25 | 35 | mdev = round_half_even((price / SMA - 1) x 1e6), SMA over the last N UTC-minute closes | `4 lt -x x 100 enter_long`, `4 gt y x 100 exit`, else hold | `--window-min` N (30) `--enter-below-bps` x (20) `--exit-above-bps` y (10) |

Definitions (`model/src/velocita_model/shadow/volume.py`):

- **event_ns** is the venue's own timestamp of the reference price, not our
  clock. Phase and rotation are functions of that one number, so a replay or a
  restarted edge computes the same feature for the same tick. A tick counter
  would restart at every edge restart and depend on which prices the throttle
  dropped; the event time does neither. Periods are aligned to the Unix epoch
  (1800 s divides a day: rotations start at :00 and :30).
- **Minute closes (dip):** the last observed price in each UTC minute. Every new
  observation enters the window, including throttled ones, so the window does
  not depend on `--min-tick-interval-s`. A minute without a price carries the
  previous close. The newest minute's close is the tick's own price (as today's
  close is in the daily features). Memory is at most N closes plus one carried
  close (N <= 1440). The window lives in memory: after each edge start, ticks are
  refused as `warming_up` for N minutes (`velocita_shadow_messages_total{outcome="warming_up"}`).
- **Periods versus ticks.** After each fill the next authorization is refused
  `stale_risk_state` (one tick lost to the re-sync, ADR 0016). At 30 s ticks, a
  60 s phase holds two ticks: one acts, one may be lost. Keep `--period-s` and
  `--exit-window-s` at least 2 x `--min-tick-interval-s`, or trades are skipped.
- The model digest is the SHA-256 of the program's parameter line, for example
  `velocita.volume.v1 churn sma_days=125 period_s=60` (the program file's first line).

### 7.1 Clip and limits

The recommended clip is **12 lots** = 0.00012 BTC. At BTC $84,000 (P = 84,000 ticks):

    notional = 10 x P x 12 = 120 P micros = 10,080,000 = $10.08
    margin   = $10.08 (100 %); fee hold = ceil(10,080,000 x 0.015) = 151,200 = $0.1512
    held     = $10.2312 <= $25 exposure;  12 <= max_order_lots 25;  12 <= max_reserved_lots 25
    template cap: 120 P <= 25,000,000 until BTC $208,333

So every current limit holds. One limit no longer means "one clip": with
`max_position_lots=25` a second 12-lot buy passes while long 12 (12 + 12 <= 25).
Tighten the position to the clip, **while flat** (below a position it becomes
reduce-only, and a 12-lot sell from 25 leaves 1 lot that can never be sold
in 12-lot clips). From the Mac, with R the current risk version
(`grep paper_risk_version ~/velocita-v2/textfile/velocita-shadow.prom` on nuc1)
and K, E the current kill epoch and executor generation:

    tools/velocita-secrets client-key caller-9 | velocita-v2-client --caller-key-id 9 --caller-id 7 --key-stdin \
      --incarnation <INC> --session-file ~/.velocita/client.session --response-endpoint 100.91.80.64:42400 \
      --node 1=100.72.127.102:42301 --node 2=100.84.151.116:42302 --node 3=100.85.36.22:42303 \
      --aeron-dir /tmp/velocita-client --sequence <NEXT> [--dry-run] \
      risk-limits account=1 instrument=1 risk_policy_version=1 expected_risk_version=<R> \
        kill_epoch=<K> executor_generation=<E> max_order_lots=12 max_reserved_lots=12 \
        max_order_notional_micros=25000000 max_reserved_exposure_micros=25000000 \
        initial_margin_ppm=1000000 maximum_fee_ppm=15000 max_position_lots=12 allow_short=0 \
        reason_code=20260930 max_window_loss_micros=10000000 reset_loss_window=0

The risk version moves to R + 1; the edge re-syncs on its next refusal. If the
account is long 25 when you switch, either keep `--order-quantity-lots 25` for
this strategy (volume per dollar lost does not depend on the clip; only the
speed does), or install it first with 25 lots, let one exit fill, then tighten
the limits and reinstall with 12 lots as `--policy-version 2`.

What it costs, flat prices, 12 lots at $84,000: buy fee ceil(10,080,000 x 115 bp)
= $0.1159, sell fee $0.1210, **$0.2369 per round trip for $20.16 of volume, about
$85 of volume per dollar lost**. The $12 weekly cap is about 50 round trips, about
$1,000 of volume. Churn at 60 s phases can do up to 30 round trips an hour, so
the cap binds within about two hours of flat prices; price moves add to or
subtract from that.

### 7.2 Deploy the code (once)

Export this commit to nuc1 as in `M3_DEPLOYMENT_LOG.md` item 45 (`git archive`,
`COPYFILE_DISABLE=1 tar --no-xattrs` over ssh, check `SOURCE_COMMIT`), then on nuc1:

    cd ~/src/aeron-raft-pi
    podman build -f deploy/v2/Containerfile --target shadow -t localhost/velocita-v2-shadow:m3 .
    install -m 0644 deploy/v2/systemd/velocita-v2-shadow-edge.service \
        deploy/v2/systemd/velocita-v2-shadow-setup.service ~/.config/systemd/user/
    systemctl --user daemon-reload
    systemctl --user restart velocita-v2-shadow-edge

With `VELOCITA_STRATEGY_ARGS` unset the trend program and every TICK line are
byte-identical to what runs now (`model/tests/test_shadow_volume.py`), except
that a TICK line gains ` exits_only=1` while the buy stop of section 7.4 is on;
the restart adds the paper book and its series. The gateway's
`velocita_gateway_volume_usd_total{side}` needs the voter image rebuilt
(`podman build -f deploy/v2/Containerfile -t localhost/velocita-v2:m3 .`) and
`systemctl --user restart velocita-v2-gateway`. The paper venue's balances are
persisted (section 5.1), so a restart while long keeps the position; the restarted
gateway arms only after its startup reconciliation matches.

### 7.3 Install one strategy

1. Choose the arguments. In `~/velocita-v2/shadow/shadow.env` add ONE of:

        VELOCITA_STRATEGY_ARGS=--strategy churn --period-s 60 --order-quantity-lots 12
        VELOCITA_STRATEGY_ARGS=--strategy trend_churn --value 125 --period-s 60 --order-quantity-lots 12
        VELOCITA_STRATEGY_ARGS=--strategy rotate --value 125 --period-s 1800 --exit-window-s 60 --order-quantity-lots 12
        VELOCITA_STRATEGY_ARGS=--strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 --order-quantity-lots 12

   optionally followed on the same line by `--weekly-loss-cap-usd 12` (the
   default) and the research run's interval, `--expected-week-volume-usd LO,HI
   --expected-week-loss-usd LO,HI`. Both units read this one variable after
   their own arguments, so it replaces `--order-quantity-lots 25` in both and
   the program the edge names is the one setup installs.
2. Install it (nuc1). Stop the edge first: between activation and the edge's
   restart its frames would name the superseded policy.

        systemctl --user stop velocita-v2-shadow-edge
        systemctl --user start velocita-v2-shadow-setup
        journalctl --user -u velocita-v2-shadow-setup | grep SETUP
        # SETUP generation=G first_sequence=S kill_epoch=K executor_generation=E strategy_mode=1
        grep -E '^(# generated|policy\.(id|version|feature_schema_id)|features|rule)' ~/velocita-v2/shadow/state/trend.program
        systemctl --user start velocita-v2-shadow-edge

   Expected for churn: `# generated from velocita.volume.v1 churn sma_days=125
   period_s=60`, `policy.id = 22`, `policy.version = 1`,
   `policy.feature_schema_id = 32`, `features.generic = 5`, `rule.1 = 4 gt 0
   enter_long`, `rule.2 = 4 le 0 exit`. (The file keeps its old name.)
3. Check: `journalctl --user -u velocita-v2-shadow-edge | grep READY` shows
   `mode=paper`; the Strategy dashboard's `velocita_shadow_feature_micros{name="phase"}`
   (or `rotation`, `mdev`) moves; the Paper trading dashboard's last row fills.

Rules of the kernel that shape reinstalls:

- **A changed program needs a new version.** Any program-shaping argument
  (strategy, value, period, window, thresholds, clip, caps) changes the policy
  bytes; under the same id and version the kernel refuses it
  (`immutable_policy_conflict`). Add `--policy-version N+1` to
  `VELOCITA_STRATEGY_ARGS`. `--weekly-loss-cap-usd`, the expected intervals,
  `--paper-*-bps` and `--min-tick-interval-s` do not shape the program: change
  them and restart only the edge.
- **A superseded version cannot come back.** Returning to trend after a volume
  strategy: `VELOCITA_STRATEGY_ARGS=--policy-version 2` (policy 21 version 1 is
  superseded), then step 2. Returning to churn after rotate: churn's next version.
- **Capacity.** The kernel keeps at most 16 policy records per cluster and never
  reclaims one. Trend v1 uses one; each install above uses another. Keep a count;
  a fresh cluster resets it.

### 7.4 The weekly loss budget

The edge keeps a paper book (`model/src/velocita_model/shadow/paper_book.py`,
persisted in `/state/ledger.db`, table `paper_book`):

- A fill is booked only when the kernel's position confirms it: an `applied`
  authorization is the pending order, and the next `stale_risk_state` re-sync
  that moves the position by that order's size books it at the order's price
  (FOK at the limit, ADR 0018). A move no order explains (for example the long
  a trend strategy left) is adopted and marked at that tick's price.
- Fees are the paper venue's, ceil(notional x bps / 10,000) micros at 115 bp
  buy and 120 bp sell (`--paper-buy-bps`, `--paper-sell-bps`: keep them equal
  to the gateway's `--buy-fee-bps`, `--sell-fee-bps`). Each fee is realized at its
  fill and taken from cash at once; a sell realizes its price move against the
  cost of the lots sold.
- The week is the ISO week (UTC) of the tick that confirms the fill. Loss =
  max(0, -realized P&L of the week).
- **The stop:** while the week's loss is above `--weekly-loss-cap-usd` (default
  12), or while the book is flat and its cash cannot pay a clip plus its fee,
  the edge sends `exits_only=1` on each TICK line. The submitter still records
  the frame but does not ask the kernel to authorize a proposal from an
  enter_long rule (`authorization=withheld`); exits are asked for as before.
  The stop lifts on Monday 00:00 UTC. It is the edge's own brake, not a kernel
  control: the kernel limits and `strategy-mode` (section 6) still apply.
- One round trip can overshoot the cap: the loss is known only once a sell is
  confirmed.

Series: `velocita_paper_volume_usd_total`, `velocita_paper_fills_total{side}`,
`velocita_paper_week_{volume,fees,pnl,loss}_usd`, `velocita_paper_week_fills`,
`velocita_paper_weekly_loss_cap_usd`, `velocita_paper_buys_stopped{reason}`,
`velocita_paper_buys_withheld_total{reason}` (buys proposed while flat and never
sent: `loss_budget`, `insufficient_balance`), `velocita_paper_cash_usd`,
`velocita_paper_unattributed_position_changes_total`, and when configured
`velocita_paper_expected_week_{volume,loss}_usd{bound="low"|"high"}`. The gateway
adds `velocita_gateway_volume_usd_total{side}` (filled notional).

Dashboard, Paper trading, last row "Volume strategies: this ISO week against the
loss budget": volume, fees (cents), net P&L, volume per dollar lost, trades per
hour, the loss-budget gauge (percent of the cap), observed weekly volume and
loss beside the expected interval and the cap, the stop and withheld buys,
volume by side, and gateway fees over a rolling 7 days.
