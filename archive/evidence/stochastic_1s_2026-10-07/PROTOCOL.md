# One-second stochastic engineering pilot — fixed before collection

Owner authorized implementation, commit-based build and deployment on October 7,
2026 after reviewing the one-second plan. This is a bounded paper engineering
pilot, not a new profitability claim or completion of statistical strategy tests.
The existing cluster, account, 30 bp/side scenario, 12-lot sizing, $25 original
funding and single-writer ledger are preserved. No operator risk change, fresh
cluster, funding reset, live orders or credentials are required.

Strategy: one-second closed SOLUSDT candles; stochastic high/low over 900 seconds,
K SMA10 seconds, D SMA30 of smoothed K; EMA300 > EMA1200 trend filter. EMA seeds
with an arithmetic mean of its first N closes, alpha=2/(N+1). Flat range raw K=50.
Entry: K crosses above D with K>=60 and positive trend, then all three conditions
remain true for three contiguous seconds (cross only required at the start).
Exit: K<D and K<=40 for three contiguous seconds. Post-exit cooldown 60 seconds.
All computations outside the kernel; new policy/schema 128/138, existing flag
layout. IDs reserved here after registry inspection; source remains 41 with its
existing persisted generation/session. Snapshot/account gates override signals.

At second t, evaluate the signal from completed second t-1 and use second t's
close as a delayed FOK reference price. No signal-close fill; actual submit/fill
latency adds to the one-second model delay. Signals/confirmations do not advance
on duplicate, future, malformed or gapped candles. Recover indicator history
without replaying orders. Bootstrap: up to three fixed 1000-row public GETs at
1s interval, then the public read-only WebSocket. Reconnect is bounded by the
pilot deadline and connection-attempt cap, never orders automatically replayed.

Pilot entry window 30 minutes after explicit activation, five more minutes of
fresh-data exit opportunity, maximum 2400 forwarded decision attempts across
restarts (including bounded resync attempts). Existing account order cap 240
and $10 drawdown stay active. Feed age <=2s; no catch-up decision bursts. Source
must retain causal information and reject invalid timestamps. A signal frame
can request at most one existing bounded stale-risk resync. End state may remain
long if no fresh data/settlement; never fabricate an exit or claim guaranteed loss.

Preflight all three voters healthy, one leader, gateway/account reconciled,
existing position flat at cutover, no active unknown effects, WAL usage <50% of
configured limit, free disk >=2 GiB. Calculate conservative additional WAL bound
before activation. Stop on capacity/backlog/reconciliation faults; retain evidence.
No voter restart or Raft/kernel modification intended. If current account is long,
wait for its normal exit; do not force a liquidation for cutover.

Journal: preserve account history and identity; add an immutable variant epoch
with activation time, baseline cash/fees/volume/order count and deadline. Existing
five-minute rules keep separate evidence. Account-lifetime dashboard values retain
history; new variant-only metrics subtract the captured baseline. Observer stays
read-only. Do not infer signal-to-fill latency from fill timestamp modulo a second.

Validation before deployment: hand-computed indicator oracles, timestamp/gap and
one-second delay tests, persisted attempt/cooldown/budget tests, account continuation
and observer read-only tests, named mutations, clean model/tool suites, pinned
commit build. Public feed check is keyless market data only. Offline economic
battery (walk-forward, both nulls, walk-forward permutation, costs and delay
sensitivity with untouched holdout) remains a separate preregistered study before
performance promotion; this engineering pilot is not evidence that it passed.

Use only one cluster. Keep voter/exporter pair untouched. Build from a commit via
deploy/v2/build-image.sh. Preserve shared dirty work. Operator ceremonies stay on
Mac. If a source-tree ship swaps mounted files, restart Prometheus/Grafana. No
Safari automation. Every deviation and deployment result belongs in DEPLOYED.md.

## Owner cost correction before any implementation/collection

Owner requested matching the approximately 20 bp TM fee. New orders use 20 bp
per side, zero additional fixed execution allowance; buy-side symmetry remains
an assumption. Finalized sell evidence: 0.002018 / 1.009007 = 0.1999986%. Quote to
settlement gross output moved about 5.85 bp; fee-only is not measured all-in cost.
Preserve prior fills at their original 30 bp, current cash and inventory, and
record an immutable cost/strategy epoch. Durable venue restore retains recorded
fees and uses configured fees only for subsequent orders; test this explicitly.
The existing maximum-fee risk limit remains 30 bp (20 is within it). No fresh
cluster/account, operator risk update, or retroactive repricing of the book.

Capacity refinement before deployment: at most 40 additional fills plus one exit
reserve. Preflight ordinary retained entries <=1000 on every voter. 2400 frames
reserve 4800 entries; conservatively allow 32 extra entries for each of 41 orders
(1312), leaving at least 1016 ordinary slots of 8128 after starting at 1000.
At 2048 bytes per retained cell this is under 13 MB additional retained data;
actual WAL bytes must also remain below its independent limit. Request receipts
are bounded/reused and checked for pinning; no infinite one-second run is promised.
