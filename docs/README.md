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
- [`V2 Phase 1 macOS candidate evidence`](evidence/V2_PHASE1_MACOS_CANDIDATE.md):
  exact contract commit, tests, codec measurements, limits, and pending
  Raspberry Pi gate.
- [`V2 Phase 1 ARM64 Linux container candidate evidence`](evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md):
  native ARM64 Ubuntu/GCC compatibility and sanitizer evidence, with the
  hardware claims it deliberately does not make.
- [`V2 Phase 2 policy-kernel experiment evidence`](evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md):
  first bounded reducer increment, TDD evidence, invariants, cross-compiler
  matrix, and incomplete-phase limits.

V2 Phase 1 has passed macOS and native ARM64 Ubuntu container candidate gates.
It is not accepted or tagged until the Raspberry Pi target-hardware gate
passes. Phase 2 now implements policy lifecycle, deduplication, and replicated
control fencing, but no order decision or external-effect authority.

Phase 2 reducer development has started on an experiment branch while the
Phase 1 Pi gate is pending. The experiment is not an accepted Phase 2 branch
and cannot authorize trading or external effects.

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
