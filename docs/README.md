# Documentation map

Start with [`ORDERBOOK_BASELINE.md`](ORDERBOOK_BASELINE.md): what this tree contains, what was removed from
the published V1 tree and why, and the gaps between it and orderbook trading.

## Guide

One page per subsystem and concern, each linking down to the code and the decisions.

- [Kernel](guide/kernel.md), [Gateway](guide/gateway.md), [Signer](guide/signer.md), [Edge](guide/edge.md)
- [Secrets](guide/secrets.md), [Storage: databases and caches](guide/storage.md), [Memory: budgets and arenas](guide/memory.md)
- [Debugging and operations tools](guide/debugging.md)
- Fixes found by testing: [durable quorum](guide/fix-durable-quorum.md), [read-outage retry](guide/fix-read-outage-retry.md)

## Architecture

- [`V2_ARCHITECTURE_CONTRACTS.md`](V2_ARCHITECTURE_CONTRACTS.md): authority, identity, message, reducer,
  order, risk, outbox, reconciliation, persistence, threading, failure and metric contracts.
- [`V2_MESSAGE_MEMORY_ATLAS.md`](V2_MESSAGE_MEMORY_ATLAS.md): every message and its bounded memory.
- [`V2_CANONICAL_CODEC_REPLAY_GUIDE.md`](V2_CANONICAL_CODEC_REPLAY_GUIDE.md): byte formats and replay.
- [`V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md`](V2_MODEL_AND_MARKET_DATA_EDGE_DESIGN.md): how the Python
  edge attaches to the cluster and why it cannot author an order.
- [`V2_DECISION_EVALUATOR_DESIGN.md`](V2_DECISION_EVALUATOR_DESIGN.md): the authoritative rule evaluator
  in the kernel.
- [`V2_DEFERRED_FUNCTIONAL_SCOPE.md`](V2_DEFERRED_FUNCTIONAL_SCOPE.md) and
  [`V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md`](V2_1_PERFORMANCE_OPTIMIZATION_SCOPE.md): what was deliberately
  left out, and the measured optimisations that were deferred.

## Decisions

[`adr/`](adr) holds 29 architecture decision records, from the replicated decision and execution boundary
([ADR 0001](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)) through resting-order
confirmation ([ADR 0008](adr/0008-resting-orders-are-confirmed-not-assumed.md)), key custody
([ADR 0019](adr/0019-live-execution-and-key-custody.md)), caller roles
([ADR 0020](adr/0020-commands-are-authorized-by-caller-role.md)), kernel-owned cash and loss limits
([ADR 0021](adr/0021-the-kernel-owns-cash-pnl-and-the-loss-limit.md)), swap verification
([ADR 0025](adr/0025-bounded-swap-verification-and-policy-bound-recovery.md)) and gateway authentication
([ADR 0029](adr/0029-the-signer-authenticates-the-gateway-for-a-day.md)). Superseded proposals, such as
[ADR 0011](adr/0011-the-swap-execution-model.md), are kept as written.

## Venue and promotion

- [`STRATEGY_PROMOTION_GATE.md`](STRATEGY_PROMOTION_GATE.md): shadow, paper, then a live canary.
- [`MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`](MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md),
  [`TRUEMARKETS_DOC_DISCREPANCIES.md`](TRUEMARKETS_DOC_DISCREPANCIES.md) and
  [`QUESTIONS_FOR_TRUEMARKETS.md`](QUESTIONS_FOR_TRUEMARKETS.md): venue facts verified against the API.

## Operations

- [`NUC_OPERATOR_RUNBOOK.md`](NUC_OPERATOR_RUNBOOK.md), [`CLEAN_RESET_RUNBOOK.md`](CLEAN_RESET_RUNBOOK.md)
  and [`PAPER_TRADING_RUNBOOK.md`](PAPER_TRADING_RUNBOOK.md)
- [`ALERT_DELIVERY_RUNBOOK.md`](ALERT_DELIVERY_RUNBOOK.md) and [`MD_ARCHIVE_RUNBOOK.md`](MD_ARCHIVE_RUNBOOK.md)
- [`TOOLCHAIN.md`](TOOLCHAIN.md): the pinned build image

## History

The V1 Raft counter, the phase-by-phase design reviews, the live-run evidence and the session handoffs are
on the published tree at [github.com/vishaljha2121/Velocita](https://github.com/vishaljha2121/Velocita)
(`main`; this branch starts from its commit `710677f`). Release summaries are in
[`../CHANGELOG.md`](../CHANGELOG.md).
