# V2 Phase 3D/3E executable-policy and measurement evidence

Status: accepted on host and physical Raspberry Pi 5

## Objective

Finish the Phase 3 authority path from an admitted decision frame and active
policy to an exact, bounded economic proposal without entering Phase 4 risk,
order, effect, or venue-I/O scope.

## What changed

- `StageExecutablePolicy` commits one atomic candidate bundle containing the
  feature schema, executable policy, instrument economics, and venue
  capabilities.
- `ActivateExecutablePolicy` selects the exact staged policy/version/digest and
  atomically supersedes the previous authority.
- Reused executable identity plus changed instrument/venue contents is a typed
  identity conflict, not idempotent success.
- Superseding the active base policy clears its executable authority in the
  same reducer transition.
- Authoritative evaluation requires the matching executable bundle, maps the
  first matching rule to a bounded template, and runs the pure resolver.
- Exact proposals are retained in cold decision records. The hot record retains
  only a compact resolution code and stays one cache line.
- Kernel command/result and snapshot schemas move to version 3. Older schema
  bytes fail explicitly instead of being reinterpreted.

## Why this shape

The four facts in the staged bundle form one semantic revision. Publishing
them separately would permit a replica to combine rules from one revision with
units or venue capabilities from another. Atomic staging turns that temporal
coupling into one deterministic state transition.

Keeping `EconomicActionProposal` out of `ApplyResult` prevents its 112-byte
object footprint from being copied into all 256 retained request receipts. The
proposal is audit-cold; lookup and outcome fields are hot. This keeps
`DecisionRecordHot` at 64 bytes while preserving exact replay evidence.

## Tests and invariants

- 22 Release tests pass on the host.
- 19 V2 tests pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- Handwritten canonical replay and 32-seed independent model replay converge.
- Snapshot validation rechecks artifact digests, rule/template binding,
  instrument/capability versions, active/staged scope, proposal provenance,
  and lifecycle state.
- The generated replay corpus digest is
  `ef2ad774e8e13f57ac65a3884f903603e9e6fe439f0aa48da98d776d51e247df`.
- The handwritten replay final-state digest is
  `4ea5fbac67aa64b9c1d69cb05d5f74fcc95d79354d5bca30ceb77050406428f3`.

## Host layout evidence

| Item | Measured bytes | Ceiling/stride |
|---|---:|---:|
| complete executable bundle | 2,088 | bounded member contracts |
| active plus staged bundles | 4,176 | two bundles |
| `TradingState` | 77,848 | 81,920 |
| maximum canonical snapshot | 63,361 | less than 65,536 |
| hot decision record | 64 | 64 |
| cold decision record | 584 | 640-byte slot |
| maximum kernel command | 1,789 | 1,920 |
| canonical kernel result | 127 | 128 |

The state retains 4,072 bytes of provisional headroom. These capacities remain
provisional until the deferred NUC validation after the nine-phase build.

## Measurement contract

`v2-phase3-benchmark` reports:

- worst-case ordered evaluation with all 32 rules visited;
- pure exact action-resolution duration;
- full in-memory authoritative reducer duration;
- observed decision and receipt high-watermarks; and
- an explicit scope marker: `in_memory_no_raft_no_wal_no_network`.

The measurement is a kernel development signal only. It is not a Raft
throughput claim and must not be compared to end-to-end order latency.

## Physical Pi acceptance

The gate completed on 2026-09-07 with evidence at
`~/.local/share/velocita/v2-phase3-acceptance-pi-gates/20260907T212930Z`.

- wrapper and container result: PASS;
- compiler: GCC 15.2.0 on Ubuntu 26.04 ARM64;
- target and runtime L1 data-cache line: 64 bytes;
- exact and expected source root:
  `f2d350757c9f2f71f0eab4e2600af982bea44d56027082fafc70764c77073f5b`;
- power before and after: `throttled=0x0`;
- temperature: 43.55 C before and 49.60 C after;
- worst-case 32-rule evaluation: p50 222 ns, p99 241 ns;
- exact action resolution: p50 185 ns, p99 204 ns; and
- full in-memory authoritative reducer: p50 740 ns, p99 778 ns.

Both decision and receipt workload high-watermarks reached 16. The benchmark
explicitly excluded Raft, WAL, and network work.

## Decision

Accept Phase 3 and allow Phase 4 TDD planning to begin. This acceptance does
not authorize a live venue adapter, real-money trading, or any external effect.
The 80-KiB state ceiling remains provisional until the deferred NUC gate.
