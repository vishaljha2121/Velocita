# Ongoing stochastic paper session — owner requested October 7, 2026

Owner explicitly replaced the 30+5 minute pilot deadline with ongoing paper
trading while its virtual balance can fund the strategy. This is paper only,
not approval for live trading, a funding reset, or an alpha claim.

Keep the same cluster incarnation, gateway, risk limits, 0.12 SOL clip, 20 bp
fee-only scenario, indicators, one-second delayed signals, and 60s cooldown.
New policy/schema 129/139 and variant stochastic_1s_cash_v1 distinguish this
session. Feature meanings remain exit flag, entry flag, and price ticks.
Policy time validity uses INT64_MAX; there is no scheduled operational deadline.

Start with the reconciled remaining $24.869170, four historical fills, and flat
inventory. No top-up. Old ledger remains intact. A standalone finalized copy
preserves the completed pilot; a new session ledger copies its entire history
and preserves old epoch/stop/completion records under prior_* tables. A new
immutable baseline separates ongoing-session performance from account lifetime.
Historical fills retain their actual 20 bp fee. Only the edge writes the active
ledger; observer mounts stay read-only.

Normal balance stop: only while flat, when cash < price_ticks * 12 lots * 100
USDC micros/tick/lot plus the rounded-up 20 bp purchase fee. It does not wait
for exactly zero cash and does not resize orders. While SOL is held, selling
remains possible even with little USDC. A normal stop publishes a checked
standalone final ledger and explicit reason; it does not reset the balance.

Remove the pilot-only 2400-frame, 40-fill and 12-lifetime-connection limits.
Reconnects retain the existing exponential backoff and no replay of old orders.
Keep 2s data-age checks, gap rebootstrap, causal indicators, kernel risk limits,
reconciliation gates, $10 drawdown stop and 240 lifetime-order guard (venue hard
capacity 256). Those safety/technical conditions can stop before cash runs out.
Raft compacts at 2048 entries with 8128 ordinary slots, a durable all-voter floor,
and an independent 1 GiB WAL cap. No changes to Raft or gateway implementations.
Do not bypass storage/refusal faults or automatically reset accumulated history.

Deployment: committed isolated source, native nuc1 build via build-image.sh.
One cluster only; voters/exporters untouched. Stop gateway for committed-state
inspection/policy setup, then restart against final controls and verify its arm
before starting the edge. No owner risk ceremony or secrets upload is needed.
No Safari automation. New paper_run label distinguishes the ongoing session in
Prometheus/Grafana while the actual cluster incarnation is unchanged.

Required checks: preserve balance/fees/history, no timed/frame cutoff, insufficient
cash includes fees, low cash while long still permits exit, reconciliation faults
never masquerade as balance exhaustion, ordinary read-only final-ledger access,
no false completion on deadline alone, no replacement of existing final records,
named mutation checks, existing model/deployment tests, native build tests and
terminal verification of current metrics, all three voters and one leader.
