# Documentation map

## V2 proposed architecture

- [`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md):
  why the replicated Decision/OMS cluster, committed outbox, and single active
  execution boundary were selected.
- [`V2_ARCHITECTURE_CONTRACTS.md`](V2_ARCHITECTURE_CONTRACTS.md): authority,
  identity, message, reducer, order, risk, outbox, reconciliation, persistence,
  threading, failure, and metric contracts.
- [`V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md):
  red-green-refactor discipline, test architecture, phased build order,
  acceptance gates, evidence, and rollback.

V2 is not yet implemented. These documents define the contract that future
implementation must satisfy.

## V1.1 development and evidence

- [`V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md)
- [`V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md`](V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md)
- [`CLIENT_ROUTING_WALKTHROUGH.md`](CLIENT_ROUTING_WALKTHROUGH.md)
- [`BENCHMARKING.md`](BENCHMARKING.md)

## V1 operations and recovery

- [`PI_OPERATOR_RUNBOOK.md`](PI_OPERATOR_RUNBOOK.md)
- [`V1_FAILURE_DRILL_RESULTS.md`](V1_FAILURE_DRILL_RESULTS.md)
- [`LOCAL_GIT_WORKFLOW.md`](LOCAL_GIT_WORKFLOW.md)

Release meaning and evidence summaries are maintained in the repository-level
[`CHANGELOG.md`](../CHANGELOG.md).
