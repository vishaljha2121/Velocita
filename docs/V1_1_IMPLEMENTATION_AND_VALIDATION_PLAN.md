# V1.1 implementation and validation plan

Status: approved engineering plan; Phase 1 was accepted on Raspberry Pi ARM64
on 2026-08-27 and Phase 2 on 2026-08-28. Phase 3 is the next implementation
stage.

Release branch: `release/v1.1`

Baseline: annotated local Git tag `v1.0.0`

This document is the execution contract for V1.1. It explains what will be
changed, why it is necessary, how it will be implemented, what evidence must
be collected, and what must pass before the release can be accepted. Future
implementation work should refer to this document phase by phase. If code and
this plan disagree, either the code must be corrected or the design change
must be recorded here before the phase is accepted.

### CTO learning contract

Every implementation and experiment must be explained through these lenses:

1. **Objective:** what outcome the change is intended to create.
2. **Business reason:** why that outcome matters to availability, latency,
   correctness, operability, cost, or delivery risk.
3. **Mechanism:** how the code and runtime produce the outcome.
4. **Invariant:** what must remain true even during failure or overload.
5. **Failure mode:** how the design can fail and how that failure appears.
6. **Measurement:** which raw metric or evidence distinguishes success from
   opinion.
7. **Rollback:** how to return to the last accepted state without losing
   evidence or durable data.
8. **Release decision:** which gate promotes the candidate and what claims the
   evidence does—and does not—support.

The goal is not only to execute commands but to build the judgment required to
design, review, operate, and communicate a production system at CTO level.

Technical education must go to implementation depth. For each material C++
change, the review must cover the concrete types and interfaces, object
ownership and lifetime, value/reference/pointer semantics, `const` and
`noexcept` contracts, templates or compile-time behavior, error propagation,
allocation and syscall behavior, iterator/reference invalidation, concurrency
and memory-ordering assumptions, cache/locality implications, and the expected
cost on the owner loop. Explanations must connect source-level constructs to
the runtime behavior and evidence rather than stopping at syntax.

## 1. Executive objective

V1 proved basic Raft correctness and operational recovery for three logical
voters on one Raspberry Pi. It did not establish a trustworthy throughput
limit. Under load, the client repeatedly sent the same logical command to
followers without delay. The resulting retry traffic competed with Raft
heartbeats and replication, destabilized leadership, and created still more
retries.

V1.1 will break that positive-feedback loop by:

1. telling clients which node is the current leader;
2. routing retries directly to that leader;
3. backing off and limiting admission while no leader is known;
4. giving Raft control-plane work priority over client work;
5. bounding client work in every owner-loop iteration; and
6. measuring the relationship between logical demand and transport work.

The intended result is not merely a higher benchmark number. It is a system
whose behavior remains explainable and bounded below, near, and above its
capacity limit.

## 2. CTO-level system model

There are three different quantities that must not be confused:

- **Logical demand** is what a caller wants done, such as one increment.
- **Transport work** is every Aeron send attempt made to deliver that logical
  command. A retry is additional transport work, not additional business work.
- **Committed work** is a command durably replicated by a Raft majority and
  applied to the deterministic state machine.

The desired path is:

```text
one logical command
        |
        v
one offer to the known leader
        |
        v
local WAL + quorum replication + commit + apply
        |
        v
one successful response
```

The V1 failure path was:

```text
leader uncertain
      |
      v
blind immediate retries to followers
      |
      v
client traffic delays Raft control traffic
      |
      v
election/leadership instability
      |
      +------------------------------+
                                     |
                   even more retries+
```

This is a positive-feedback failure: overload creates retry traffic, and retry
traffic creates more overload. V1.1 introduces negative feedback: direct
routing when possible, bounded backoff when not, and reduced admission while
the control plane recovers.

## 3. Baseline evidence and problem statement

The accepted V1 behavior and complete evidence are recorded in
`CHANGELOG.md`. Two representative benchmark runs exposed the primary defect:

| Offered rate | Logical commands scheduled | Successful transport offers | `not_leader` responses | Approximate attempts per logical command |
|---:|---:|---:|---:|---:|
| 20/s | 1,400 | 64,826 | 63,426 | 46.3 |
| 35/s | 2,450 | 373,739 | 371,289 | 152.5 |

The Aeron offer-failure and backpressure counters were zero in both runs. That
means the first bottleneck to fix is not Aeron publication capacity. It is the
client routing/retry policy and its interaction with the Raft owner loop.

The long benchmark was also non-monotonic: some higher offered rates completed
more commands than lower rates. A capacity curve should normally degrade in a
repeatable direction. Non-monotonic results with extreme retries indicate a
control instability, so V1 has no certified throughput figure.

## 4. Release invariants

These rules apply to every phase. A phase cannot be accepted by improving
latency or throughput while violating one of them.

### 4.1 Safety invariants

- The same replicated request ID is applied at most once.
- A successful response is sent only after the entry is committed and applied.
- A node without a majority cannot commit new work.
- A stale or invalid leader hint cannot redirect a client outside configured
  membership.
- The single-owner rule remains intact: only the owner loop mutates the Raft
  server and its state machine.
- Wire decoding fails closed for malformed, truncated, or unsupported frames.
- WAL and metadata recovery behavior remains unchanged unless separately
  designed and tested.

### 4.2 Liveness and boundedness invariants

- Retries are bounded by the logical command deadline.
- Backoff never blocks the event loop with `sleep`; retry eligibility is
  represented as a timestamp.
- Pending logical commands and discovery probes have explicit capacity bounds.
- Every owner-loop activity has a finite batch or time budget.
- Raft ticks and peer traffic cannot be starved by client ingress.
- Every scheduled benchmark command has exactly one terminal accounting
  outcome.

### 4.3 Hot-path observability invariant

The owner loop may update fixed-size, preallocated counters and timestamps. It
must not format Prometheus text, perform network I/O for monitoring, allocate
per metric event, use request IDs as labels, or calculate ratios on the hot
path. Export, rate calculation, dashboards, and alerts remain external.

## 5. Canonical validation metric

### 5.1 Headline metric: retry amplification

Every client-routing, recovery, load, and failure experiment must report:

```text
retry_amplification = total_transport_attempts / logical_commands_started
```

Definitions:

- `logical_commands_started` increments exactly once when a logical command is
  admitted into the client retry state machine. It does not increment again
  when the same request ID is retried.
- `total_transport_attempts` increments once for each attempt to offer that
  logical command to a node publication, regardless of whether the Aeron offer
  succeeds, is back pressured, is not connected, or otherwise fails.
- Commands rejected before admission because no leader is known are counted in
  a separate admission-rejection counter and are not in the denominator.

Why this definition matters: counting only successful offers would conceal
transport pressure when Aeron starts rejecting attempts. Counting benchmark
schedules rather than admitted logical commands would mix admission control
with routing efficiency. Both are important, but they are different questions.

The Prometheus recording rule will calculate the operational value outside the
hot path:

```promql
sum(rate(velocita_client_transport_attempts_total[1m]))
/
clamp_min(sum(rate(velocita_client_logical_commands_started_total[1m])), 1)
```

The exact aggregation labels will be finalized with the exporter schema. Raw
counters remain the source of truth in benchmark evidence.

### 5.2 Expected values

- `1.00` means one transport attempt per logical command.
- `1.05` means approximately one extra transport attempt per 20 logical
  commands.
- `2.00` is expected briefly when each command first reaches a follower and is
  then redirected once to the leader.
- Sustained values materially above `1` under stable leadership indicate
  waste, stale routing knowledge, or a retry bug.
- During failover, a bounded temporary rise is expected. Its peak, duration,
  and total attempts per affected command must be reported.

### 5.3 Primary release threshold

Under stable leadership and throughout the declared healthy operating range:

```text
retry_amplification >= 1.00 and retry_amplification <= 1.05
```

A request deliberately sent to a healthy follower should normally complete in
no more than two transport attempts: follower, then known leader.

### 5.4 Guardrail metrics

Retry amplification is the headline efficiency metric, not a substitute for
correctness. Every experiment must also report the relevant guardrails:

| Dimension | Metric or evidence | Pass expectation |
|---|---|---|
| Correctness | scheduled terminal-outcome accounting | Every command accounted for exactly once |
| At-most-once apply | request-ID deduplication drill and FSM result | One logical mutation despite same-ID retries |
| Availability | completion and timeout totals | No timeout below claimed healthy capacity |
| Consensus health | leader count, term changes, leader changes | One leader; no recurring churn in steady state |
| Replication | log, commit, apply indexes and lags | Voters converge; steady lags return to zero |
| Owner-loop health | last tick age and maximum tick gap | Remains below the phase threshold |
| Resource bounds | pending depth, discovery probes, admission rejects | Never exceeds configured capacity |
| Transport health | Aeron offer outcomes and queue high-watermarks | Failures explained and bounded |
| Host health | CPU, memory, disk, temperature, throttling, governor | No power/thermal/storage invalidation |

## 6. Planned raw metric schema

Metric names are a schema contract once released. During implementation they
may change only through an explicit plan update and corresponding tests.

### 6.1 Client metrics

- `velocita_client_logical_commands_started_total`
- `velocita_client_transport_attempts_total`
- `velocita_client_retries_total`
- `velocita_client_not_leader_responses_total`
- `velocita_client_admission_rejected_total{reason="leader_unknown"}`
- `velocita_client_capacity_rejected_total`
- `velocita_client_completed_commands_total`
- `velocita_client_timed_out_commands_total`
- `velocita_client_protocol_errors_total`
- `velocita_client_leader_known`
- `velocita_client_pending_commands`
- `velocita_client_pending_commands_high_watermark`
- Aeron offer outcomes by bounded `outcome` label

### 6.2 Node owner-loop metrics

- `velocita_raft_owner_loop_iterations_total`
- `velocita_raft_tick_total`
- `velocita_raft_last_tick_age_seconds`
- `velocita_raft_owner_loop_max_gap_seconds`
- `velocita_raft_client_fragments_processed_total`
- `velocita_raft_peer_fragments_processed_total`
- `velocita_raft_client_budget_exhausted_total`
- `velocita_raft_peer_budget_exhausted_total`
- existing role, leader, term, log, commit, apply, queue, and Aeron counters

Counters are monotonic. Current state and queue depth are gauges. Request ID,
suite ID, arbitrary error text, and channel URI are prohibited as Prometheus
labels because they create unbounded cardinality.

## 7. Phase 1: versioned leader-aware routing

Acceptance checkpoint, 2026-08-27:

- implemented the V3 leader-aware response and V1/V2 decoding compatibility;
- implemented explicit member-to-Aeron-target validation and a term-aware
  client router;
- migrated the production client, benchmark driver, and deployment client
  commands to explicit `ID=URI` request targets;
- added exact logical-command, transport-attempt, and retry-amplification
  benchmark evidence; and
- passed production and benchmark-instrumented native tests plus three-node
  macOS and Raspberry Pi ARM64 integration tests.

The loopback test reached a leader in two attempts from a deliberately selected
follower, then completed all 25 measured benchmark commands with 25 transport
attempts and retry amplification `1.00`.

The Raspberry Pi gate rebuilt both production and benchmark-instrumented ARM64
images with all three test executables passing. A follower-first increment
reached leader 3 in two attempts. Repeating request ID
`1787831360569930796` returned the same counter value `20` and log index `35`,
proving that redirect and retry did not duplicate either application or log
admission. The stable-leader measured window admitted 75 logical commands,
made 75 transport attempts, completed all 75, recorded zero timeouts and zero
leader-hint protocol errors, and produced retry amplification `1.00`. The
rolling follower-first/leader-last deployment retained quorum and restored all
three production services after the isolated benchmark run. The Phase 1 gate
is closed.

### 7.1 Objective

Replace blind round-robin retries with deterministic routing to the current
leader while preserving same-request-ID semantics and compatibility with V1
response frames.

### 7.2 Why this phase comes first

A follower already knows the current leader when Raft has one. V1 discards
that knowledge at the protocol boundary. Backoff alone would reduce damage but
would still route inefficiently. Owner-loop scheduling alone would make the
storm less harmful without removing its source. Leader-aware routing removes
the largest known amplification mechanism at the earliest point.

### 7.3 Wire protocol design

The current production response uses wire version 1. The benchmark response
uses version 2 for timing fields. V1.1 will introduce response version 3 so
production and benchmark frames share a common leader-aware base.

Proposed V3 base response:

| Field | Type | Purpose |
|---|---|---|
| magic | `uint32_t` | Reject unrelated/corrupt frames |
| version | `uint16_t` | Select the decoding schema |
| status | `uint16_t` | `ok`, `not_leader`, or defined error |
| extension flags | `uint32_t` | Declare optional trailing sections |
| leader ID | `int32_t` | Configured node ID; `-1` means unknown |
| request ID | `uint64_t` | Correlate response and preserve deduplication |
| value | `int64_t` | Replicated counter result |
| term | `int64_t` | Term associated with the response/hint |
| index | `int64_t` | Relevant Raft log index |

The base is 48 bytes. A benchmark-only timing extension may add the current
timing fields, producing a 96-byte frame. Extension presence must be declared,
not inferred from accidental trailing bytes.

Decoder rules:

- emit V3 after this phase;
- accept legacy V1 production responses;
- accept legacy V2 benchmark responses;
- accept V3 with known extensions;
- reject unsupported versions, unknown mandatory flags, malformed lengths,
  invalid status values, and trailing data not declared by the schema;
- map legacy responses to `leader_id = -1`.

### 7.4 Explicit node-to-channel mapping

Positional channel order is too implicit for leader routing. Replace anonymous
request channel strings with a structure conceptually equivalent to:

```cpp
struct RequestTarget {
    raft_node_id_t node_id;
    std::string channel;
};
```

The CLI should accept explicit mappings such as:

```text
--request-target 1=aeron:udp?endpoint=127.0.0.1:40111
```

Configuration must fail at startup for duplicate node IDs, duplicate mappings,
missing members, malformed IDs, or an empty target set. This converts a hidden
ordering assumption into a validated configuration contract.

### 7.5 Leader-hint state machine

The client caches `(leader_id, observed_term)`.

- `ok` confirms the responding target as leader for that term.
- `not_leader` with a valid member ID and a term not older than the cached term
  updates the preferred target.
- `leader_id = -1` changes the state to leader unknown or suspect; it must not
  invent a target.
- A hint for a non-member is rejected and counted as a protocol error.
- A hint from an older term cannot overwrite newer knowledge.
- A newer observed term invalidates an older leader mapping until reconfirmed.

Every retry uses the original request ID. Routing changes delivery, not command
identity.

### 7.6 Expected code areas

- `include/velocita/wire.hpp` and `src/wire.cpp`
- `include/velocita/client_session.hpp` and `src/client_session.cpp`
- `src/raft_node.cpp`
- `src/client_main.cpp`
- `src/benchmark_main.cpp`
- deployment scripts that construct client arguments
- wire, client-routing, benchmark, and integration tests

### 7.7 Required tests and experiments

| Test | Method | Required evidence |
|---|---|---|
| V3 round trip | Encode/decode every status and boundary value | Exact field equality |
| Legacy compatibility | Decode recorded/generated V1 and V2 frames | Successful mapping; leader unknown |
| Malformed frames | Truncation, bad magic/version/flags/status/length | Deterministic rejection |
| Mapping validation | Duplicate, missing, invalid target IDs | Startup failure with bounded error |
| Stale hint | Deliver older-term hint after newer one | Cached leader unchanged |
| Invalid hint | Return non-member leader ID | Hint rejected and error counted |
| Follower-first | Send one logical command to a known follower | Success in at most 2 attempts |
| Warm cache | Send subsequent commands with stable leader | Amplification 1.00 to 1.05 |
| Same-ID redirect | Redirect an increment after follower response | Counter advances exactly once |

### 7.8 Phase 1 acceptance gate

- All existing tests remain green.
- New wire and routing tests pass.
- A deliberate follower-first command normally completes in at most two
  attempts.
- A stable-leader sample has retry amplification no greater than 1.05 after
  leader cache warm-up.
- No duplicate application, invalid-member routing, or decoding ambiguity is
  observed.

### 7.9 Phase 1 code-review route

Read Phase 1 in dependency order rather than executable order:

1. `include/velocita/wire.hpp` and `src/wire.cpp`: V3 response contract,
   extension flags, legacy V1/V2 compatibility, and malformed-frame rejection.
2. `include/velocita/client_routing.hpp` and `src/client_routing.cpp`: pure,
   term-aware mapping from Raft node IDs to request publications.
3. `src/raft_node.cpp`: the point where a follower exposes its current
   `leader_id`, and where an `ok` response identifies the committing leader.
4. `include/velocita/client_session.hpp` and `src/client_session.cpp`: Aeron
   publications keyed by explicit node identity rather than anonymous order.
5. `src/client_main.cpp`: one logical command, one stable request ID, and
   follower-to-leader redirect.
6. `include/velocita/benchmark.hpp` and `src/benchmark_main.cpp`: exact
   logical-command and transport-attempt accounting.
7. `tests/core_tests.cpp` and `tests/benchmark_tests.cpp`: executable protocol,
   routing, deduplication, and evidence invariants.
8. `deploy/cluster-client.sh`, `deploy/run-benchmark-suite.sh`, and
   `docs/BENCHMARKING.md`: how the contract becomes a repeatable Pi experiment.

This order follows the architecture: protocol first, policy second, transport
third, executable orchestration fourth, and evidence last.

## 8. Phase 2: bounded recovery and admission control

### 8.1 Objective

Make leader loss a bounded recovery event instead of an event-loop-speed retry
storm. Protect memory and the control plane while continuing to measure caller
demand honestly.

### 8.2 Leader knowledge states

The client will distinguish:

- `known`: a target is confirmed for the current observed term;
- `suspect`: the cached leader may be stale after failure or newer-term
  evidence;
- `unknown`: no valid leader target is available.

This state is operationally useful because normal routing, discovery, and
admission are different policies. A single Boolean cannot explain that
difference.

### 8.3 Retry scheduling

Each pending command will carry at least:

- original request ID;
- logical admission and deadline timestamps;
- attempt count;
- last target ID;
- next retry timestamp; and
- current phase/state.

An Aeron `offer()` success is not a Raft completion. It proves only that the
local publication accepted the frame. Therefore an offered command also has a
bounded response-silence deadline. The Phase 2 candidate starts at 250 ms,
below the full command deadline but above the measured healthy low-load tail.
If no matching response arrives, the route becomes unknown and the same
request ID re-enters bounded discovery. This avoids waiting the entire command
deadline after sending to a leader that has just died.

When the leader is unknown, retry eligibility follows bounded exponential
backoff with deterministic positive-floor jitter:

```text
upper_bound = min(cap, base * 2^attempt)
delay       = deterministic_jitter(request_id, attempt, base..upper_bound)
next_retry  = min(now + delay, command_deadline)
```

Tentative starting parameters are a 5--10 ms base and a 200--250 ms cap. They
are hypotheses, not magic production constants. Tests validate the bounds;
target-hardware failover measurements determine the final values.

Deterministic jitter makes tests reproducible while preventing all pending
commands from retrying simultaneously. The lower bound is deliberately
positive: textbook full jitter may choose zero and recreate an event-loop-speed
retry. No owner loop or benchmark thread sleeps to implement backoff; the
scheduler simply skips work until `next_retry`.

### 8.4 Admission control during leaderless intervals

When leadership is unknown:

- stop admitting unbounded new commands into the pending map;
- retain a small explicitly bounded queue only if its maximum wait is part of
  the API contract, otherwise reject immediately;
- permit a bounded number/rate of discovery probes;
- record the rejected demand as `reason="leader_unknown"`; and
- keep the open-loop benchmark scheduler running so the outage is visible as
  rejection rather than hidden by coordinated omission.

Backoff and admission control solve different problems. Backoff limits work
already admitted. Admission control limits new work arriving during recovery.

The Phase 2 candidate chooses an unconfirmed-admission limit of one. That one
pending command is the bounded discovery probe. While state is `unknown` or
`suspect`, further open-loop demand receives the explicit
`admission_rejected_leader_unknown` terminal outcome. A valid leader hint sends
the same request ID directly to that target; a successful response changes the
state to `known` and normal admission resumes. The values 5 ms, 250 ms, and one
probe are initial safety parameters to validate, not permanent market-system
constants.

### 8.5 Required tests and experiments

| Test | Method | Required evidence |
|---|---|---|
| Backoff bounds | Fake monotonic clock and deterministic request IDs | Delay within base/cap/deadline |
| No busy retry | Advance fake clock less than `next_retry` | Attempt counter unchanged |
| Deadline | Advance through command deadline | One timeout terminal outcome |
| Pending bound | Submit more than configured capacity | Depth never exceeds limit |
| Leaderless admission | Force unknown state and submit demand | Explicit bounded rejections |
| Recovery | Provide valid new-term leader response | Routing resumes to new leader |
| Leader kill | Stop current leader on Pi under low load | Bounded peak attempts and recovery |
| Quorum loss | Stop two voters | No commit; bounded attempts/depth |
| Quorum restore | Restart one voter and retry same ID | Exactly-once outcome |

### 8.6 Phase 2 acceptance gate

- Backoff, deadline, and admission behavior is deterministic under fake-clock
  tests.
- Pending depth and discovery probes never exceed configured bounds.
- Leader loss causes a finite retry-amplification spike that returns to the
  stable band after election.
- Quorum loss does not commit work or create unbounded retry traffic.
- Every offered logical command ends as completed, timed out, admission
  rejected, capacity rejected, or an explicit error.
- Same-ID recovery still applies a mutation at most once.

### 8.7 Implementation checkpoint

The local candidate adds a pure `ClientRecoveryPolicy`, explicit
`unknown`/`suspect`/`known` router state, monotonic retry deadlines, deterministic
positive-floor jitter, a bounded 250 ms response-silence timer, a bounded
unconfirmed-admission gate, and a globally paced discovery probe. Each pending
benchmark command now carries its original
request ID, intended-send time, command deadline, last target, attempt count,
retry count, and next retry time.

Benchmark summary schema 3 adds admission rejection, discovery probes, retry
counts, pending high-watermark, final leader state, and terminal-accounting
evidence. `failures.csv` records attempts, last target, and leader state for
each failed command. The driver aborts if total or measured scheduled commands
do not equal their exactly-one terminal outcomes.

Both production and benchmark-instrumented macOS builds pass all three native
test executables. A three-voter loopback smoke test completed 15 of 15 measured
commands with 15 transport attempts, retry amplification `1.00`, zero timeout,
terminal accounting 15 of 15, and final state `known`. These are development
results only. `deploy/run-phase2-recovery-drills.sh --yes` is the bounded Pi
gate for leader failover under demand, a leaderless quorum interval, recovery,
and same-ID exactly-once resolution.

A forced local leader-removal diagnostic then scheduled 40 measured commands,
accounted for all 40, completed 9, timed out 3, rejected 28 while leadership
was unknown, admitted at most two pending commands, and made 58 attempts for 12
admitted commands (amplification `4.83`). The two remaining local voters did
not elect within the eight-second measured window, so final state remained
unknown. This is bounded failure behavior, not a Phase 2 recovery pass. It
provided a concrete target-hardware question rather than acceptance evidence.

### 8.8 Raspberry Pi acceptance evidence

Phase 2 passed on Raspberry Pi ARM64 on 2026-08-28 using production image
`015581814255` and benchmark-instrumented image `6cab4df1978b`. The rolling
upgrade preserved quorum and converged three voters before the disruptive
gate. Recovery suite `20260828T180907Z` then recorded:

- leader failover: 75 scheduled, 70 completed, 5 leader-unknown admission
  rejections, 86 attempts, 8 discovery probes, pending high-watermark 2,
  amplification `1.229`, and zero retries in the stable tail;
- quorum loss and restoration: 100 scheduled, 67 completed, 30
  leader-unknown admission rejections, 117 attempts, 43 discovery probes,
  pending high-watermark 2, amplification `1.671`, and zero retries in the
  stable tail; and
- same-ID unknown-outcome recovery: request ID `1787940625718131511` changed
  the replicated counter exactly once from 20 to 21 after quorum returned,
  while the commit index remained 208 during the no-quorum assertion.

Every scenario passed terminal-accounting, bounded-pending, bounded-discovery,
recovery-to-known, stable-tail, and exactly-once assertions. All three
supervised voter services were active after cleanup. This closes the Phase 2
gate; these recovery results are not a maximum-throughput claim.

An earlier run, `20260828T180440Z`, reported `preflight: voters are not
converged` because its AWK observer used `log[...]`, conflicting with AWK's
built-in `log()` function. The parser exited before evaluating the metrics, so
the harness converted an observer crash into a convergence false negative.
The run is retained as invalid harness evidence. Commit `b5d530d` renamed the
array to `current_index`, and the corrected suite passed immediately afterward.

## 9. Phase 3: owner-loop control-plane priority

### 9.1 Objective

Ensure heartbeats, elections, replication, and commit progress receive service
before new client work, even near overload. Bound all work so priority does not
turn into starvation.

### 9.2 Why fragment count alone is insufficient

V1 calls the Raft tick and then polls up to ten client fragments before peer
traffic. Ten sounds small, but processing a client proposal can perform a
synchronous WAL operation. A count limit is therefore not a time guarantee.
If client work delays peer heartbeats, followers start elections, clients lose
their leader, and retries amplify the load.

### 9.3 Proposed owner-loop order

```text
1. Run an overdue Raft periodic tick.
2. Poll a bounded peer-ingress batch.
3. Enqueue and offer bounded peer egress (votes/heartbeats/replication/acks).
4. Drain and apply bounded completed Raft work.
5. Admit client fragments only while count and tick-slack budgets remain.
6. Flush bounded peer egress again after proposals.
7. Offer a bounded number of client responses.
8. Publish fixed-size metrics and idle according to the strategy.
```

Initial client ingress should be conservative, likely one fragment per
iteration. Peer batches may be larger but must still be bounded. The final
numbers must be named constants/options and justified by measurement.

### 9.4 Tick-slack budget

The owner loop should know the next Raft tick deadline. Client work is admitted
only if sufficient slack remains. Both conditions apply:

```text
client_fragments_this_iteration < client_fragment_budget
and
now + client_safety_margin < next_tick_deadline
```

This is not a hard real-time guarantee because a synchronous WAL call may take
longer than expected. It is still materially safer than entering an arbitrary
batch without considering the next control-plane deadline. Moving durable I/O
off-thread requires a separate ordered-persistence design and is outside this
V1.1 scope unless measurements prove it unavoidable.

### 9.5 Fairness rules

- Peer priority means service first, not infinite draining.
- Client work receives a finite opportunity whenever control-plane deadlines
  are healthy.
- Peer and response queues have capacity bounds and high-watermark metrics.
- V1.1 will not reorder messages within a peer stream; Raft message ordering is
  correctness-sensitive.
- No additional thread may mutate Raft or the FSM.

### 9.6 Required tests and experiments

| Test | Method | Required evidence |
|---|---|---|
| Scheduling order | Fake pollers/clock with all queues ready | Tick and peer work precede client work |
| Client count bound | Fill client ingress | Per-iteration count never exceeds budget |
| Tick-slack bound | Move clock close to tick deadline | Client admission skipped; tick serviced |
| Peer bound | Fill peer ingress/egress | Batches finite; queues accounted |
| Fairness | Sustained peer and client readiness | Both progress without tick violation |
| Sustained low load | Pi benchmark below proposed capacity | One stable leader; no recurring term churn |
| Overload | Offered rate beyond capacity | Controlled rejection/latency, not election storm |
| Follower catch-up | Stop/restart follower under load | Lag returns to zero |

### 9.7 Phase 3 acceptance gate

- Deterministic scheduler tests prove priority and all configured bounds.
- No recurring leader or term churn occurs in the claimed healthy load range.
- Owner-loop tick age and maximum gap remain inside the selected threshold.
- Peer drops remain zero in the healthy range.
- Client overload becomes visible as bounded rejection, queueing, or timeout;
  it does not destabilize the Raft control plane.
- Retry amplification remains in the stable band below capacity.

### 9.8 Implementation checkpoint

The initial candidate introduces a header-defined `OwnerLoopScheduler` whose
only state is an `OwnerLoopConfig` value and the last consumed monotonic tick
time. Construction validates all durations, work limits, and queue capacities
once. Runtime tick consumption and client-admission decisions are inline and
`noexcept`; they perform duration arithmetic and integer comparisons without
allocation, locking, wall-clock access, or virtual dispatch.

The initial policy values are hypotheses:

| Policy | Initial value | Reason to start here |
|---|---:|---|
| Raft tick interval | 1 ms | Preserve the existing periodic granularity |
| Client tick-safety margin | 250 us | Skip new client work near the next tick |
| Peer ingress | 64 fragments/iteration | Prioritize a finite peer batch |
| Client ingress | 1 fragment/iteration | Bound synchronous proposal/WAL exposure |
| Raft output drain | 128 messages/iteration | Exceed expected three-node peer generation while finite |
| Completion drain | 64 responses/iteration | Bound encoding and queue transfer |
| Peer offers | 64 attempts/iteration | Bound publication work across configured peers |
| Response offers | 16 attempts/iteration | Make client response progress without dominating peers |
| Peer/response queue capacity | 4,096 each | Preserve the existing peer bound and add a response bound |

The loop order is now tick, peer ingress, bounded Raft drain, peer offers,
tick-slack-gated client ingress, a second drain/peer offer using the *remaining*
per-iteration budget, then bounded response offers. Raft and the FSM remain
owned by the original thread.

The iteration coordinator is a function template whose clock and work
operations are callable template parameters. Production lambdas therefore use
compile-time static dispatch, while tests inject a fake monotonic clock and
fake ready queues. The deterministic test asserts the exact phase sequence,
the one-fragment client limit, the peer poll limit, tick-slack suppression, and
that the two drain/offer service points share one cumulative budget.

`RaftNode` now transfers one outbound message or completion at a time through
`std::optional`. Moving a `PeerMessage` transfers ownership of variant/vector
storage without creating the prior whole-deque temporary vector; the caller
sets the exact bound. The external response queue stops new client polling at
capacity and only drains as many completed responses as it can retain.

Shared metrics page version 2 adds owner-loop iterations, maximum observed tick
gap, full client/peer poll-budget counts, and client-poll skips split between
tick slack and response backlog. The owner thread accumulates these as plain
integers and copies them into lock-free fixed-size cells only at the existing
100 ms status-publication point. The external exporter performs Prometheus
formatting and unit conversion. During a rolling target upgrade the exporter
must be restarted after all voters so it can read the new page version.

Normal and instrumented builds pass all native tests. A three-process macOS
loopback smoke run completed 25 of 25 measured commands at 5 commands/s with
25 transport attempts, no timeout, retry amplification `1.00`, and p99
`11,719,084 ns`. A second run at 50 commands/s completed 250 of 250 measured
commands with amplification `1.00`, p99 `11,921,000 ns`, stable term 1, no
response-backlog polling blocks, and maximum observed tick gaps of
`2,978,375 ns`, `4,744,666 ns`, and `3,598,917 ns` for nodes 1--3. This confirms
that the one-millisecond policy is a service target rather than a hard
real-time guarantee. The processes were terminated immediately after the load
window, leaving the followers one final commit behind; that shutdown snapshot
is not convergence evidence. Deterministic ordering and configured-budget
coverage now pass in both build modes. Phase 3 remains a candidate until
sustained fairness, overload behavior, follower catch-up under load, and
Raspberry Pi validation pass.

### 9.9 Raspberry Pi storage-bound acceptance failure

The first Pi comparison, `20260829T120112Z`, was diagnostically useful but not
certifiable because the boot had already latched undervoltage and throttling.
After correcting the power path, rebooting, verifying `throttled=0x0`, and
selecting the `performance` governor, repeat `20260829T124828Z` still failed:

| Rate | Scheduled | Completed | Admission rejected | Timed out | Amplification | p50 | p99 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 5/s | 75 | 50 | 25 | 0 | 2.680 | 395.9 ms | 828.4 ms |
| 20/s | 300 | 64 | 230 | 6 | 5.086 | 1,041.3 ms | 1,973.2 ms |

At 5/s the voters remained converged in term 1. At 20/s, owner-loop maximum
gaps reached 1.068--1.471 seconds and the voters ended without a leader in
terms 7--9. The client then correctly bounded discovery and shed admission,
but this is not healthy service.

The host evidence isolates synchronous storage rather than CPU saturation:
CPU pressure was `some avg10=0.02`, whereas I/O pressure was
`full avg10=75.14` and `full avg300=57.25`. The state machine itself remained
microsecond-scale. The current storage path performs one WAL `fsync` per log
offer on every voter. Each commit-index advance also writes and `fsync`s an
atomic-replacement metadata file, renames it, and `fsync`s its directory on
every voter. With all voters on one SD card, a logical command can cause about
nine serialized durability barriers.

This result satisfies the condition in Section 9.4 that measurement must prove
ordered persistence work unavoidable before it enters scope. The next design
checkpoint must preserve these invariants:

- a voter never acknowledges an entry as durable before its ordered WAL batch
  is stable;
- term and vote persistence remains synchronous with the Raft safety decision;
- commit/apply and client success never overtake durable log persistence;
- only the owner thread mutates Raft and the FSM; and
- storage completions return to the owner in order through bounded queues.

A longer response-silence timeout may reduce false recovery traffic, but it
would only hide the blocking interval. Phase 3 remains open until a storage
isolation experiment and an ordered-persistence design remove or explicitly
bound the owner-thread stall.

The evaluated storage pivot and its safety gates are specified in
[`V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md`](V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md).
Its decision is to use fixed per-voter memory blocks as a bounded staging and
group-commit layer, while retaining a quorum-durability fence before commit,
apply, and successful client acknowledgment. A memory-only acknowledgment mode
may be used for causal benchmarking but is not the default durability contract.

## 10. Phase 4: observability, experiments, and release proof

### 10.1 Objective

Export enough low-overhead evidence to explain V1.1 behavior, then run a
repeatable target-hardware matrix that distinguishes correctness, recovery,
latency, and capacity claims.

### 10.2 Metrics implementation

- Producers update only fixed-layout raw counters/gauges in shared memory.
- The separate exporter reads snapshots and formats Prometheus exposition.
- Prometheus calculates rates and amplification through recording rules.
- Grafana displays outcomes but is not the source of truth; raw benchmark files
  and Prometheus queries are retained as evidence.
- Counter resets are handled by `rate()`/`increase()` semantics and recorded
  process start times.

### 10.3 Dashboard expectations

The V1.1 dashboard should include:

- retry amplification over time with a 1.05 healthy threshold;
- logical-command rate versus transport-attempt rate;
- completion, timeout, admission-rejection, and capacity-rejection rates;
- leader-known state and current leader by node;
- leader count, term changes, and leader changes;
- owner-loop tick age/max gap and client budget exhaustion;
- log/commit/apply indexes and lag;
- Aeron offer outcomes, queue depths, and high-watermarks; and
- CPU, memory, temperature, throttling, disk latency/capacity, and process
  health for correlation.

### 10.4 Experiment classes

#### A. Functional regression

- wire compatibility and corruption tests;
- storage/recovery tests;
- same-ID deduplication;
- three-voter convergence;
- follower stop/catch-up;
- leader failover;
- quorum loss/restoration; and
- supervised process restart and reboot recovery.

#### B. Routing experiments

- cold client deliberately starts at a follower;
- warm leader cache under stable leadership;
- stale hint from an older term;
- unknown and invalid leader hints; and
- multiple clients starting simultaneously.

#### C. Recovery experiments

- kill the leader at a recorded time under low steady load;
- measure leaderless duration, attempts, rejections, pending high-watermark,
  election time, and recovery time;
- remove quorum and hold it long enough to prove bounds;
- restore quorum and retry an unknown-outcome request ID.

#### D. Load experiments

First run a quick discovery sweep with fresh state for each point:

```text
1, 3, 5, 10, 20, 30, 35, 40 logical commands/s; one repetition
```

Then select rates around the observed knee and run certification:

- at least three independent repetitions per rate;
- 10 seconds or more warm-up;
- 60--180 seconds measured duration;
- fresh benchmark volumes and Aeron directories per repetition;
- exact sample merging, never averaging percentiles;
- stable PSU, no undervoltage history for the run, recorded CPU governor,
  temperature, throttling, memory, and disk space; and
- no unrelated workload on the Pi.

### 10.5 Required experiment scorecard

Every experiment directory must contain enough data to fill this scorecard:

| Field | Meaning |
|---|---|
| release/commit/image ID | Exact code and artifact under test |
| run start/end UTC | Correlation and duration |
| topology/configuration | Voters, channels, budgets, backoff, deadlines |
| offered/scheduled | External demand |
| admitted/started | Logical commands entering retry state |
| transport attempts | Total delivery work |
| retry amplification | Attempts divided by started commands |
| completed | Successful terminal outcomes |
| admission/capacity rejected | Explicit load shedding |
| timed out/errors | Failed terminal outcomes |
| p50/p90/p99/p99.9/max | End-to-end latency for valid completions |
| leaderless duration | Control-plane recovery interval |
| leader/term changes | Consensus stability |
| pending/queue high-watermarks | Bound verification |
| tick age/max gap | Owner-loop health |
| log/commit/apply convergence | Replication health |
| host context | Power, temperature, governor, CPU, memory, disk |
| result and reason | PASS/FAIL/INVALID with a precise explanation |

Zero completed samples do not produce a zero-latency claim. Percentiles are
reported as unavailable when there are no samples. A run affected by power
loss, thermal throttling, storage corruption, or unrelated workload is marked
`INVALID`, preserved for incident analysis, and excluded from capacity claims.

### 10.6 Release-level acceptance gates

V1.1 may be tagged only when all of the following are true:

- all native tests pass in a clean build;
- all four phase gates pass;
- stable-leader retry amplification is between 1.00 and 1.05 throughout the
  claimed healthy range;
- follower-first routing normally completes in no more than two attempts;
- leader loss creates bounded retries/admission rejection and returns to the
  stable amplification band;
- a leaderless interval causes no unbounded pending work;
- there are no timeouts below the claimed healthy capacity;
- there is no recurring leader/term churn below that capacity;
- all three voters converge after every required recovery drill;
- results repeat across at least three independent certification repetitions;
- the production image contains no benchmark-only timestamp instrumentation;
  and
- `CHANGELOG.md` is updated with implemented changes and exact evidence rather
  than planned claims.

## 11. Phase-by-phase Git workflow

The local-only history protects each accepted boundary:

```text
main (tag: v1.0.0)
  \
   release/v1.1
      \
       v1.1/stage-1-leader-routing
       v1.1/stage-2-recovery-control
       v1.1/stage-3-owner-loop
       v1.1/stage-4-release-proof
```

For each phase:

1. create its branch from the latest accepted `release/v1.1`;
2. implement only the phase scope;
3. run native tests and the phase-specific experiments;
4. record evidence and update this document if an approved design decision
   changed;
5. commit a coherent checkpoint;
6. merge the phase branch into `release/v1.1`; and
7. create an offline Git bundle after meaningful accepted milestones.

Only after all release gates pass should `release/v1.1` be merged into `main`
and tagged `v1.1.0`. No Git remote or GitHub repository is required.

## 12. Agent execution protocol

For every future implementation phase, the implementing agent must:

1. read this document and `CHANGELOG.md` before editing;
2. identify the current phase, objective, invariants, metrics, tests, and gate;
3. state any design deviation before making it part of the implementation;
4. keep the primary retry-amplification definition unchanged unless this plan
   is deliberately revised;
5. implement on the corresponding local phase branch;
6. run the narrowest useful tests during development and a clean full test at
   the phase boundary;
7. distinguish Mac build evidence from Raspberry Pi ARM64 evidence;
8. never report a planned feature as implemented;
9. preserve failed/invalid experiment evidence and explain its status; and
10. update `CHANGELOG.md` only with behavior supported by code and evidence.

## 13. Out of scope for V1.1

- Physical high availability across separate machines, power supplies,
  storage devices, and networks.
- Dynamic Raft membership.
- A production matching engine or exchange connector.
- Moving WAL persistence to `io_uring` or another asynchronous writer without
  a separate ordered-durability design.
- TLS, authentication, multi-tenant isolation, and public Internet exposure.
- Claiming Raspberry Pi benchmark results as exchange-production latency.

These are valuable later stages. Keeping them out of V1.1 prevents the release
from mixing a known routing/liveness correction with unrelated architecture
changes.

## 14. Definition of done

V1.1 is done when the implementation makes normal work approximately one
transport attempt per logical command, makes failover behavior bounded and
measurable, preserves Raft safety, protects the control plane from client load,
and produces repeatable Pi evidence from which a defensible operating envelope
can be stated.

Until those conditions are met, `V1.1` remains unreleased and any throughput
figure remains an experiment observation rather than a capacity promise.
