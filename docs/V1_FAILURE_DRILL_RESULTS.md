# Velocita V1 failure-drill results

Date: 2026-08-22  
Environment: three logical Raft voters on one Raspberry Pi 5  
Runner: `deploy/run-failure-drills.sh --yes`

## Acceptance result

| Drill | Result | Evidence |
|---|---|---|
| Baseline | PASS | Three voters converged; counter 17; node 2 leader |
| Supervised crash restart | PASS | Node 1 PID changed from 1618 to 2610; systemd restart count changed from 0 to 1 |
| Leader failover | PASS | Leadership moved from node 2 to node 1 in 1,522 ms |
| Quorum loss | PASS | Commit index remained 24 with one voter; the same request ID resolved once after quorum returned; counter changed from 18 to 19 |
| Observability isolation | PASS | Raft remained available while the metrics exporter, Grafana, and Prometheus were stopped independently |
| Metrics-exporter alert | PASS | `VelocitaRaftExporterDown` fired 18,584 ms after exporter shutdown |
| Final convergence | PASS | Three voters converged; counter 19; node 1 leader |

## What this proves

- An unexpected voter process death is recovered by systemd without operator
  intervention.
- A majority can elect a replacement leader and continue committing after the
  old leader stops.
- A lone voter does not advance the commit index or apply a command.
- Retrying an unknown-outcome request with the same request ID applies it once,
  not twice.
- Prometheus, Grafana, and the metrics exporter are observers, not dependencies
  of consensus or the replicated state machine.
- All voters catch up to the same committed state after service restoration.

## Measurement interpretation

The 1,522 ms failover measurement runs from the operator's stop injection until
the external metrics reader observes exactly one different live leader. It is
an availability/control-plane measurement, not matching-engine latency.

The recorded `alert_wait_ms=15` for quorum loss is not total alert detection
latency. Its timer began after the intentionally unsuccessful client request,
by which point the stale-owner alert was already firing. It proves the alert
was present, but must not be used as a 15 ms alerting objective.

The 18,584 ms exporter-alert measurement runs from exporter shutdown until
Prometheus exposes the firing alert. It includes scrape, rule-evaluation, and
the configured 15-second `for` interval.

## Scope limits

All three voters share one Pi, kernel, power supply, network interface, and
microSD card. These results prove logical process isolation and Raft behavior;
they do not prove physical high availability. Physical-voter, packet-loss,
storage-fault, and host-loss drills remain future work.

No request-path percentile budget can be inferred from these drills. V2 budget
work must next measure client-observed end-to-end latency, sustained throughput,
event-loop delay, Aeron backpressure, WAL cost, and behavior under controlled
load without adding synchronous metrics work to the owner loop.
