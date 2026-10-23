# Documentation map

## Architecture

- [`V2_ARCHITECTURE_CONTRACTS.md`](V2_ARCHITECTURE_CONTRACTS.md): authority, identity, message, reducer,
  order, risk, outbox, reconciliation, persistence, threading, failure and metric contracts.
- [`V2_END_STATE_GUARANTEE_CHECKLIST.md`](V2_END_STATE_GUARANTEE_CHECKLIST.md): the guarantees the system
  is meant to hold, one by one.
- [`V2_MESSAGE_MEMORY_ATLAS.md`](V2_MESSAGE_MEMORY_ATLAS.md): every message and its bounded memory.
- [`V2_CANONICAL_CODEC_REPLAY_GUIDE.md`](V2_CANONICAL_CODEC_REPLAY_GUIDE.md) and
  [`V2_CHECKPOINT_TECHNICAL_GUIDE.md`](V2_CHECKPOINT_TECHNICAL_GUIDE.md): byte formats, replay and storage.
- [`V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`](V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md): how the Python
  edge attaches to the cluster and why it cannot author an order.
- [`V2_PHASE3_DECISION_EVALUATOR_DESIGN.md`](V2_PHASE3_DECISION_EVALUATOR_DESIGN.md): the authoritative
  rule evaluator in the kernel.

## Decisions

The [`adr/`](adr) directory holds 29 architecture decision records, from the replicated decision and
execution boundary ([ADR 0001](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)) through key
custody ([ADR 0019](adr/0019-live-execution-and-key-custody.md)), caller roles
([ADR 0020](adr/0020-commands-are-authorized-by-caller-role.md)), kernel-owned cash and loss limits
([ADR 0021](adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md)), swap verification
([ADR 0025](adr/0025-bounded-swap-verification-and-policy-bound-recovery.md)) and gateway authentication
([ADR 0029](adr/0029-the-signer-authenticates-the-gateway-for-a-day.md)). Superseded proposals, such as
[ADR 0011](adr/0011-the-swap-execution-model.md), are kept as written.

## Design reviews

The `F3_*` to `F6_*` and `M2_*` files are byte-level and protocol reviews written before each boundary was
built: the execution catalog, authority barrier, archive and snapshot formats, durability fences and the
node process. [`ARCHITECTURE_REVIEW_2026-10-01.md`](ARCHITECTURE_REVIEW_2026-10-01.md) is a whole-system
review at the V2 baseline.

## Going live

- [`LIVE_TRADING_JOURNEY.md`](LIVE_TRADING_JOURNEY.md): from paper trading to the first True Markets swap.
- [`LIVE_PROMOTION_PLAN_2026-10-07.md`](LIVE_PROMOTION_PLAN_2026-10-07.md): the promotion plan and its gates.
- [`STRATEGY_PROMOTION_GATE.md`](STRATEGY_PROMOTION_GATE.md): shadow, paper, then a live canary.
- [`MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md),
  [`TRUEMARKETS_DOC_DISCREPANCIES.md`](TRUEMARKETS_DOC_DISCREPANCIES.md) and
  [`QUESTIONS_FOR_TRUEMARKETS.md`](QUESTIONS_FOR_TRUEMARKETS.md): venue facts verified against the API.

## Operations

- [`NUC_OPERATOR_RUNBOOK.md`](NUC_OPERATOR_RUNBOOK.md) and [`PI_OPERATOR_RUNBOOK.md`](PI_OPERATOR_RUNBOOK.md)
- [`ALERT_DELIVERY_RUNBOOK.md`](ALERT_DELIVERY_RUNBOOK.md), [`MD_ARCHIVE_RUNBOOK.md`](MD_ARCHIVE_RUNBOOK.md)
  and [`CLEAN_RESET_RUNBOOK.md`](CLEAN_RESET_RUNBOOK.md)
- [`PAPER_TRADING_RUNBOOK.md`](PAPER_TRADING_RUNBOOK.md) and [`TOOLCHAIN.md`](TOOLCHAIN.md)

## V1 and V1.1

The original Raft counter laboratory: [`V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md),
[`V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md`](V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md),
[`CLIENT_ROUTING_WALKTHROUGH.md`](CLIENT_ROUTING_WALKTHROUGH.md), [`BENCHMARKING.md`](BENCHMARKING.md) and
[`V1_FAILURE_DRILL_RESULTS.md`](V1_FAILURE_DRILL_RESULTS.md). [`LOCAL_GIT_WORKFLOW.md`](LOCAL_GIT_WORKFLOW.md)
describes how the repository was kept before it was published.

## Evidence

Qualification runs, deployment records and session handoffs are in [`../archive/`](../archive), unchanged
from when they were written. Release summaries are in [`../CHANGELOG.md`](../CHANGELOG.md).
