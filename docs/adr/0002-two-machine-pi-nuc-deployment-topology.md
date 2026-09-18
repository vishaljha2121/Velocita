# ADR 0002: Two-machine Pi/NUC deployment topology

- Status: proposed deployment constraint; validate before real-money use
- Date: 2026-09-03
- Decision owners: project owner and architecture review
- Target release: V2 deployment
- Related architecture: `0001-v2-replicated-decision-oms-and-execution-boundary.md`

## Context

The currently available production hardware is one x86-64 NUC and one ARM64
Raspberry Pi. The system may run more than one process per machine, but process
count does not create independent machine failure domains.

A conventional three-voter Raft cluster needs a majority of two. Placing two
voters on one of these machines and one on the other therefore produces
asymmetric availability:

- loss of the single-voter host leaves quorum on the two-voter host;
- loss of the two-voter host removes quorum; and
- one host-level fault can affect two nominally independent voters.

Both machines may also share power, network, physical location, deployment
artifacts, credentials, and operator actions. Raft does not eliminate those
common-mode failures.

## Decision

Until a third independent machine is available, the system will be described
as a **two-machine production pilot with limited failover**, not as a highly
available three-node deployment.

The preferred temporary topology is:

```text
x86-64 NUC
  - primary Decision/OMS instance
  - single active, fenced execution gateway
  - market-data edge
  - Prometheus/Grafana where resources permit

ARM64 Raspberry Pi
  - independent replica or second voter
  - watchdog and external health observation
  - backup/evidence replication
```

The precise voter mode remains subject to deployment validation. With two
voters, both machines are required for quorum. A deliberately managed
single-writer plus warm-replica mode may be safer operationally until automatic
promotion and executor fencing are proven. No topology may claim availability
that its physical failure domains do not provide.

Running a `2 + 1` three-process arrangement is permitted for protocol,
leader-routing, rolling-upgrade, and mixed-architecture tests. It is not
accepted as protection against losing the host carrying two voters, and the
extra co-located voter must justify its CPU, memory, storage, and operational
cost.

The target topology after obtaining a third independent host is one voter per
failure domain:

```text
NUC       -> voter 1 and active execution gateway
Pi        -> voter 2
Host 3    -> voter 3
```

The execution gateway remains logically single-active. A new generation may
send risk-increasing effects only after the previous generation is externally
fenced; a Raft term alone cannot cancel a venue request already in flight.

## Mixed-architecture contract

ARM64 and x86-64 voters are allowed only when they pass deterministic replay
and wire-compatibility gates. Replicated and persisted data must use:

- fixed-width integer fields and explicitly defined byte order;
- canonical serialization rather than raw C++ object bytes;
- integer price ticks and quantity lots rather than consensus-affecting
  floating-point calculations;
- deterministic container traversal and reducer behavior;
- identical protocol and state-schema versions; and
- architecture-specific images built from the same reviewed Git commit.

Pointers, native structure padding, uninitialized bytes, native `sizeof`
assumptions, host clocks, random values, and unspecified hash-container order
must not influence replicated outcomes or state digests.

## Promotion and failure behavior

- Loss of quorum fails closed for new risk-increasing decisions.
- Loss of the execution gateway does not authorize a second sender until the
  old generation is fenced and unknown venue outcomes are reconciled.
- Manual promotion must have an operator runbook, retained evidence, and a
  rollback path.
- A surviving process is not evidence that the system still has safe write
  authority.
- Monitoring and dashboards are projections and cannot grant authority.

## Validation gates

Before real-money execution, retain evidence for:

1. identical committed-log replay and final state digest on ARM64 and x86-64;
2. canonical wire-codec golden vectors on both architectures;
3. NUC power loss, Pi power loss, and clean restart after each;
4. one-way and two-way network partitions;
5. interruption during WAL persistence and subsequent recovery;
6. stale executor-generation and stale kill-epoch rejection;
7. response loss, duplicate delivery, and request-ID conflict handling;
8. unknown venue submission followed by authoritative reconciliation;
9. backup restoration and documented manual promotion; and
10. incompatible protocol or state-schema versions failing closed.

Each drill must distinguish safety, availability, recovery time, and operator
work. A test that preserves safety by becoming unavailable is not an
availability pass.

## Consequences

### Benefits

- uses the available hardware without overstating its resilience;
- exercises heterogeneous builds and deterministic state-machine contracts;
- isolates observation and recovery evidence from the primary machine; and
- creates a direct migration path to three independent voters later.

### Costs and limitations

- two physical machines cannot provide normal three-voter, one-host-failure
  write availability;
- the Pi may have materially different storage, power, thermal, and latency
  characteristics from the NUC;
- automatic execution failover remains unsafe without external fencing; and
- shared power, networking, location, software, and operator failures remain
  common-mode risks.

## Review trigger

Revisit this ADR when a third independent host becomes available, the venue's
fencing capabilities are known, or validation shows that the mixed-architecture
or persistence latency violates the declared safety and latency objectives.

The governing operational principle is: **voters are logical participants;
failure domains determine availability**.
