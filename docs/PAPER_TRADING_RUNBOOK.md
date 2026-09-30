# Paper trading runbook (Phase 2)

Updated: 2026-09-29. Paper means the kernel authorizes orders and the paper
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
      --aeron-dir /tmp/velocita-client --sequence 4 [--dry-run] \
      risk-limits account=1 instrument=1 risk_policy_version=1 expected_risk_version=0 \
        kill_epoch=1 executor_generation=1 max_order_lots=25 max_reserved_lots=25 \
        max_order_notional_micros=25000000 max_reserved_exposure_micros=25000000 \
        initial_margin_ppm=1000000 maximum_fee_ppm=15000 max_position_lots=25 allow_short=0 \
        reason_code=20260929

`COMMITTED ... code_name=applied ... risk_version=1`. A new account starts at
risk version 1 and position version 1, which is what the edge binds by default.
Changing limits later: `expected_risk_version` = the current version; it moves
up by one.

## 5. Gateway, then paper

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

## 6. Halting

| Need | Command (operator client) | Effect |
|---|---|---|
| stop new orders | `strategy-mode halted K E` | control lane; frames record `control_halted`, nothing is sent for authorization |
| stop new orders, keep evaluating | `strategy-mode no_new_risk K E` (or `reduce_only`, `cancel_only`) | every place-limit proposal, exits included, is `control_suppressed` |
| hard stop | `kill-epoch K+1` | halts AND makes every earlier decision unauthorizable |
| resume | `strategy-mode normal K E` (after a kill: the new K) or re-run setup | refused while any order's outcome is unknown |
| stop proposing entirely | `systemctl --user stop velocita-v2-shadow-edge` | no frames at all |

K and E are the current kill epoch and executor generation (every `COMMITTED`
line prints them). The client has no mass-cancel verb yet: open paper orders
are resolved by the gateway's simulator.
