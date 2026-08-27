# Velocita Raft changelog

This changelog records release intent, validated behavior, measured failures,
and the evidence required to promote the next release. It is deliberately more
than a commit log: implementation claims and experimental conclusions are kept
separate, and a throughput number is not published until a repeatable benchmark
meets its acceptance gates.

The project currently uses milestone names (`V1`, `V1.1`) and the corresponding
container tag `localhost/velocita-raft:v1`. `CMakeLists.txt` still declares the
project version as `0.1.0`; artifact and semantic-version unification remains
release-engineering work.

## [V1.1] - Unreleased

Status: planned. No item in this section is an implementation claim until it is
moved under **Implemented changes** and supported by tests and benchmark
evidence.

The authoritative execution specification for this release is
[`docs/V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](docs/V1_1_IMPLEMENTATION_AND_VALIDATION_PLAN.md).
It defines the phase objectives, wire/routing design, invariants, canonical
retry-amplification metric, experiment scorecard, and pass/fail gates. This
changelog remains the concise release record; the plan is the implementation
contract.

### Objective

Eliminate the leader-routing retry storm exposed by V1 benchmarking, protect
Raft control-plane progress from client load, and establish a repeatable safe
operating envelope before assigning a throughput number.

### Why V1.1 is required

V1 clients receive `not_leader` without a leader identity, rotate blindly to
the next request channel, and retry immediately. Under unstable leadership,
this turns a small logical workload into a large internal workload. The node
owner loop also polls as many as ten client fragments before peer traffic, and
that count is not a time budget because a proposal may perform synchronous WAL
work.

Representative V1 benchmark evidence showed:

- at 20 requests/s, one run scheduled 1,400 total commands, completed 3, timed
  out 1,397, received 63,426 `not_leader` responses, and made 64,826 successful
  Aeron offers: 46.3 successful transport attempts per logical command;
- at 35 requests/s, one run scheduled 2,450 total commands, completed 1,548,
  timed out 902, received 371,289 `not_leader` responses, and made 373,739
  successful Aeron offers: 152.5 attempts per logical command; and
- every Aeron offer-failure and backpressure counter in those two runs was
  zero, so the dominant failure was routing/retry amplification rather than
  Aeron rejecting publication offers.

### Planned implementation stages

#### Stage 1 - Versioned leader-aware routing

- Add `leader_id` to `not_leader` responses, using `-1` when the node does not
  know a leader.
- Introduce a backward-compatible response wire version and tests for legacy,
  leader-aware, malformed, and truncated responses.
- Map configured Raft node IDs to Aeron request publications.
- Cache leader hints with their observed term and ignore stale or non-member
  hints.
- Retry the same request ID directly against the hinted leader rather than
  rotating blindly.

#### Stage 2 - Bounded recovery and admission control

- Add deadline-bounded exponential retry backoff with jitter when no leader is
  known.
- Add explicit `leader_known`, `leader_suspect`, and `leader_unknown` client
  states.
- Stop admitting unbounded new work during a leaderless interval; fail fast or
  use a small time-bounded queue while allowing bounded discovery probes.
- Preserve open-loop benchmark scheduling and record leaderless admission
  rejection instead of pausing the generator and hiding outages.

#### Stage 3 - Owner-loop control-plane priority

- Service overdue Raft ticks and bounded peer traffic before client ingress.
- Flush vote, heartbeat, replication, and acknowledgement work before admitting
  more proposals.
- Bound client processing by fragment count and time/tick slack rather than
  relying only on `poll(..., 10)`.
- Bound peer and response drains so priority cannot become starvation.
- Preserve the single-owner Raft execution model; do not introduce concurrent
  mutation of the Raft server.

#### Stage 4 - Measurement and release proof

- Export raw logical-command, transport-attempt, retry, `not_leader`,
  leaderless-admission, completion, and timeout counters.
- Calculate retry amplification outside the hot path:

  ```text
  retry_amplification = transport_attempts / logical_commands_started
  ```

- Add Grafana panels and alerts for amplification, leader knowledge, admission
  rejection, term/leader churn, owner-loop tick age, and completion rate.
- Repeat the same target-hardware load matrix with fresh state per repetition,
  exact sample merging, stable power, recorded temperature/governor, and no
  unrelated workload.

### Planned new metrics

The final names may change during schema review, but their meanings must not:

- `velocita_client_logical_commands_started_total`
- `velocita_client_transport_attempts_total`
- `velocita_client_retries_total`
- `velocita_client_not_leader_responses_total`
- `velocita_client_admission_rejected_total{reason="leader_unknown"}`
- `velocita_client_completed_commands_total`
- `velocita_client_timed_out_commands_total`
- `velocita_client_leader_known`
- `velocita_client_retry_amplification` as an off-path recording rule, not a
  division performed by the client owner loop

Request IDs, arbitrary error text, and suite IDs must not become metric labels.
They belong in bounded evidence files to avoid unbounded Prometheus cardinality.

### V1.1 release acceptance gates

- Stable-leader retry amplification is approximately 1.00 and no greater than
  1.05 over the declared healthy operating range.
- A request initially routed to a healthy follower normally reaches the leader
  in no more than two transport attempts.
- Leader loss produces bounded retries and bounded admission rejection, not a
  retry burst that grows with event-loop speed.
- A leaderless interval does not cause unbounded pending work or queue growth.
- No peer messages are dropped and no recurring term changes occur under the
  declared healthy load range.
- Every scheduled benchmark command is accounted for as completion, timeout,
  admission rejection, capacity rejection, or explicit protocol/application
  error.
- Same-request-ID retry still applies an increment at most once after an
  unknown outcome.
- Three voters converge after follower restart, leader failover, quorum
  restoration, and full supervised restart.
- Results are repeatable across at least three independent repetitions at each
  claimed rate before a capacity or latency statement is published.

### Implemented changes

#### Stage 1 development checkpoint - leader-aware routing

- Added response wire version 3 with explicit extension flags, `leader_id`, and
  term-aware routing information. The decoder continues to accept V1 production
  and V2 benchmark responses and represents their absent leader as unknown.
- Added validated `RequestTarget {node_id, channel}` configuration. Duplicate
  IDs, duplicate channels, missing member mappings, and non-member mappings fail
  before Aeron publications are created.
- Added a pure C++ leader router that caches `(term, leader_id)`, ignores
  older-term hints, rejects non-member hints, invalidates knowledge on a newer
  unknown/invalid term, and redirects the same request ID to a valid leader.
- Changed `raft-client` and `raft-bench` from anonymous `--request-channel`
  lists to explicit `--members` plus repeatable `--request-target ID=URI`
  mappings.
- Changed the one-shot client from broadcasting every retry to all voters to
  offering to one target and following the returned leader hint.
- Added exact benchmark counters for admitted logical commands and all
  transport attempts. Summary schema 2 and Prometheus evidence now report
  retry amplification without adding work to the Raft node owner loop.

This is an implementation checkpoint, not a completed Phase 1 acceptance
claim. Raspberry Pi ARM64 validation remains outstanding.

### V1.1 validation results

Local development evidence on 2026-08-27:

- production build: all three native test executables passed;
- benchmark-instrumented build: all three native test executables passed;
- three-node macOS loopback: an initial request found leader 3 in two attempts;
- forced follower-first route: follower 1 redirected the same request ID to
  leader 3 and the increment completed in exactly two attempts; and
- five-second measured loopback benchmark: 25 logical commands started, 25
  transport attempts, 25 completions, zero timeouts, zero invalid hints, and
  retry amplification `1.00`. One discovery retry occurred during warm-up.

These results validate the development implementation and test harness. They do
not close the Phase 1 gate until the rebuilt ARM64 image passes the corresponding
tests on the Raspberry Pi.

## [V1] - 2026-08-25

Status: functionally accepted for a three-process educational cluster on one
Raspberry Pi 5. Capacity not certified.

### Implemented

#### Native Raft service

- Implemented the service, client, storage, wire protocol, dump tool,
  benchmark driver, and exporters in C++20 with no Java runtime in the final
  image.
- Integrated pinned native Aeron and `willemt/raft` revisions.
- Ran an embedded Aeron C media driver per process and used Aeron UDP for client
  requests, responses, `RequestVote`, and `AppendEntries` traffic.
- Supported one voter or three statically configured logical voters.
- Kept Raft mutation under a single owner thread.

#### Replicated state and correctness

- Added a deterministic replicated counter FSM supporting `increment` and
  replicated `get` commands.
- Replicated stable request IDs and retained FSM results for request
  deduplication.
- Preserved the same request ID across client retries so an unknown-outcome
  command could be resolved without applying it twice.
- Returned responses only after committed entries were applied to the FSM.

#### Persistence and recovery

- Added an append-only, versioned WAL with little-endian encoding, record
  lengths, and CRC32 integrity checks.
- Persisted term, vote, and commit metadata through atomic file replacement.
- Replayed committed state and deduplication results on restart.
- Added `raft-dump` for offline inspection of metadata and log records.
- Failed startup closed on corrupt or torn records rather than silently
  accepting ambiguous durable state.

#### Packaging and service management

- Built a multi-stage ARM64 Podman image that runs tests during the builder
  stage and uses an unprivileged runtime UID.
- Ran each voter in a separate rootless container with its own Aeron shared
  memory and durable named volume.
- Added rootless Quadlet units and a systemd user target for boot management.
- Enabled bounded `on-failure` restart policy and rate limiting so persistent
  faults become visible instead of producing infinite crash loops.
- Used `loginctl` linger so the rootless user service manager starts at boot and
  remains available without an interactive SSH session.

#### Observability

- Added a fixed-layout, versioned shared-memory metrics page per voter.
- Kept Prometheus formatting and HTTP serving in a separate native exporter.
- Exported Raft role, leader, term, log/commit/apply indexes, FSM value, owner
  tick age, Aeron fragments/bytes, publication outcomes, queue depths, and
  observed leadership/term changes.
- Added external host, process, filesystem, microSD, memory, CPU, and Raspberry
  Pi thermal metrics.
- Installed bounded local Prometheus and Grafana services with provisioned
  recording rules, alerts, cluster-health panels, and benchmark panels.
- Verified that stopping observability components does not stop Raft.

#### Operations and testing

- Added a Pi operator runbook, repeatable cluster lifecycle scripts, and a
  bounded all-in-one failure-drill runner with exit-trap restoration.
- Added native core, shared-metrics, and benchmark tests.
- Added an open-loop benchmark driver with intended send times to prevent
  coordinated omission.
- Recorded exact warmup and measured samples, explicit failures, fixed-bucket
  histograms, p50 through p99.99, maximum, counts, and benchmark-only ownership
  timings.
- Reset benchmark cluster state for every repetition and merged exact samples
  rather than averaging percentiles.
- Kept benchmark-only timestamp instrumentation out of the production image.

### What V1 did well

The bounded failure-drill suite on 2026-08-22 passed every automated scenario:

| Scenario | Result | Evidence |
|---|---|---|
| Baseline | PASS | Three voters converged; counter 17; node 2 leader |
| Supervised crash restart | PASS | Node 1 PID changed 1618 to 2610; restart count increased 0 to 1 |
| Leader failover | PASS | Leadership moved from node 2 to node 1 in 1,522 ms |
| Quorum loss | PASS | Commit remained 24 with one voter; same-ID retry resolved once; counter 18 to 19 |
| Observability isolation | PASS | Raft remained available while exporter, Grafana, and Prometheus were stopped independently |
| Exporter-down alert | PASS | Alert fired 18,584 ms after exporter shutdown |
| Final recovery | PASS | Three voters converged; counter 19; node 1 leader |

These results prove process-level restart, majority failover, quorum safety,
same-ID deduplication after an unknown outcome, observer isolation, and follower
catch-up. They do not prove physical high availability because all voters share
one Pi, kernel, power supply, network interface, and microSD card.

Low-rate smoke runs also proved that the complete client-to-quorum-to-apply path
could execute without timeouts. One 15-second run completed all scheduled work
at 1, 3, and 5 requests/s, with measured p99 end-to-end latency of 64.6 ms,
98.1 ms, and 147.2 ms respectively. These are experiment observations, not
release latency objectives.

For one successful 5 requests/s run, benchmark-only attribution reported:

| Stage | p99 |
|---|---:|
| Client offer to matching response | 147.2 ms |
| Local WAL append | 45.7 ms |
| Quorum append | 56.1 ms |
| Admission to commit | 143.1 ms |
| Commit to apply | 6.0 us |

The spans overlap and must not be added. They showed that FSM application was
cheap while persistence, quorum progress, and scheduling dominated the
observed latency.

### What failed or remained unproven

#### Retry amplification and unstable progress

The load driver exposed a liveness failure mode rather than a clean throughput
knee. A short rebaseline run completed all measured commands at 5, 10, 20, and
30 requests/s before timing out work at 40 requests/s, but a later three-repeat
sweep was highly non-monotonic:

| Offered rate | Measured scheduled | Measured completed | Measured timed out |
|---:|---:|---:|---:|
| 20/s | 3,600 | 0 | 3,600 |
| 25/s | 4,500 | 47 | 4,453 |
| 30/s | 5,400 | 322 | 5,078 |
| 35/s | 6,300 | 1,248 | 5,052 |
| 40/s | 7,200 | 687 | 6,513 |

Higher offered rate sometimes completed more work than a lower rate. That is
not a valid capacity curve; it is evidence that leadership state and retry
feedback dominated the experiment.

The client protocol did not include a leader hint. On `not_leader`, the driver
rotated to the next node and retried immediately with no backoff. This created a
positive feedback loop: rejection caused retry traffic, retry traffic consumed
owner-loop time, delayed consensus progress caused more rejection, and the
cycle repeated.

The node owner loop ran an overdue Raft tick first but then polled up to ten
client fragments before peer traffic. Because a proposal can include
synchronous WAL work, a fragment-count limit was not a meaningful time budget.
V1 therefore lacked protected scheduling for heartbeat, election, and
replication work.

Consequently, V1 has no certified sustainable throughput or latency SLO. The
correct conclusion is not “capacity is 5/s” or “capacity is 30/s”; the correct
conclusion is “repair routing, retry, admission, and scheduling behavior, then
repeat the experiment.”

#### Known product limits deferred beyond V1.1

- Three voters on one Pi are one physical failure domain, not high availability.
- Static membership only; no joint-consensus membership changes.
- No snapshot transfer or WAL compaction; the WAL grows indefinitely.
- Torn/corrupt final records fail safely but require manual operator recovery.
- Reads are replicated log entries rather than lease or ReadIndex operations.
- Synchronous persistence remains in the proposal path.
- No authenticated or encrypted Raft/client transport; endpoints are restricted
  to loopback for the V1 topology.
- No representative matching-engine, market-data, risk, or venue operation
  classes; the workload is still a replicated counter.

These are real limits, but they are intentionally not mixed into V1.1's retry
and liveness scope. A release should have one coherent risk-reduction objective
rather than becoming an unbounded rewrite.

### Benchmark and lab incidents separated from product findings

- An early benchmark readiness probe exhausted its small `/dev/shm` allocation;
  the harness was corrected to provide explicit Aeron shared-memory capacity.
- A benchmark client later exited with signal 135 (`SIGBUS`) when mapped Aeron
  term buffers exceeded its container shared-memory limit; the driver container
  was raised to 512 MiB.
- A sudden Pi power loss produced unclean journal warnings and a Podman overlay
  link warning. Subsequent `podman system check --quick` completed and the
  production cluster recovered, but this was an infrastructure incident, not a
  controlled Raft durability test.
- Kernel logs recorded historical undervoltage events. After changing the power
  source, `vcgencmd get_throttled` returned `0x0`; benchmark temperature rose
  from approximately 52.4 C to 60.1 C without thermal throttling.
- The harness now resets disposable benchmark volumes for each repetition,
  increases client shared memory, preserves immutable evidence, restores the
  production target through an exit trap, and stops disposable voters before
  capturing their final role/term/index status.

Keeping these incidents separate prevents infrastructure and harness defects
from being misreported as consensus-algorithm defects while still retaining
them as operational lessons.

### Recommended change for V1.1

V1.1 should implement the four staged changes documented in the unreleased
section: leader-aware routing; bounded retry and admission control;
control-plane-first owner-loop scheduling with bounded client work; and
off-path retry-amplification measurement followed by repeatable target-hardware
validation.

### Evidence references

- [`docs/V1_FAILURE_DRILL_RESULTS.md`](docs/V1_FAILURE_DRILL_RESULTS.md)
- [`docs/BENCHMARKING.md`](docs/BENCHMARKING.md)
- [`docs/PI_OPERATOR_RUNBOOK.md`](docs/PI_OPERATOR_RUNBOOK.md)
