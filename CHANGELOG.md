# Velocita Raft changelog

This changelog records release intent, validated behavior, measured failures,
and the evidence required to promote the next release. It is deliberately more
than a commit log: implementation claims and experimental conclusions are kept
separate, and a throughput number is not published until a repeatable benchmark
meets its acceptance gates.

The project currently uses milestone names (`V1`, `V1.1`, `V2`) and the corresponding
container tag `localhost/velocita-raft:v1`. `CMakeLists.txt` still declares the
project version as `0.1.0`; artifact and semantic-version unification remains
release-engineering work.

## [V2] - In development

Status: Phase 1 domain and byte-contract candidate implemented and validated on
macOS ARM64 and native ARM64 Ubuntu 26.04 in a constrained container;
Raspberry Pi ARM64 acceptance remains pending. No deterministic Decision/OMS
reducer, risk engine, outbox, execution gateway, or live trading behavior is
implemented. No V2 release has been tagged, and no V1/V1.1 wire, storage,
deployment, or evidence artifact has been deleted or superseded in place.

The accepted architecture direction is recorded in
[`ADR 0001`](docs/adr/0001-v2-replicated-decision-oms-and-execution-boundary.md).
The authoritative domain, identity, message, order, risk, outbox, execution,
reconciliation, persistence, threading, failure, and metric contracts are in
[`docs/V2_ARCHITECTURE_CONTRACTS.md`](docs/V2_ARCHITECTURE_CONTRACTS.md).
The test-first build order and phase acceptance gates are in
[`docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`](docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md).

### Objective

Evolve the counter laboratory into a deterministic trading Decision/OMS
platform. A Raft cluster will own authoritative policy activation, decision
evaluation, risk reservations, order intent, accepted venue evidence,
positions, deduplication, and a transactional outbox. A logically single
active execution gateway will deliver committed effects to a venue and return
acknowledgement, fill, cancel, unknown-outcome, and reconciliation evidence
through the cluster.

### Accepted design decisions

- A model or calibration service produces an immutable, versioned decision
  policy artifact; publication does not silently activate it.
- A market-data/feature edge proposes bounded canonical decision frames. The
  Raft state machine performs the final authoritative evaluation.
- An accepted decision atomically records the order, risk reservation,
  deduplication receipt, and committed outbox effect.
- Raft apply performs no external I/O. The executor delivers committed effects
  at least once by stable identity and reconciles unknown outcomes before
  retry.
- The executor may refuse or defer an unsafe effect but may not originate or
  economically modify an order.
- One executor generation is active per venue/account. Automatic standby
  promotion remains prohibited until the old sender can be externally fenced.
- Normal new-risk traffic and emergency stop/cancel traffic use separate
  bounded lanes and authority rules.
- V2 retains the bounded asynchronous WAL direction: fixed-capacity staging
  may move and amortize persistence work, but protocol success stays behind an
  explicit quorum-durability fence.
- Strategy economics are the final implementation phase, after identity,
  deterministic replay, OMS/risk, outbox, simulation, storage, execution,
  reconciliation, kill, and operational recovery are proven.

### Development method

V2 uses test-driven development: observe the contract test fail, implement the
smallest bounded behavior that makes it pass, refactor with all tests green,
and retain evidence. Every phase defines tests, metrics, failure drills,
acceptance gates, and rollback boundaries before production code is added.

### Phase 1 candidate - domain types and versioned contracts

Commit `a6ee8df5870a` implements the macOS candidate:

- zero-cost strong identity wrappers prevent implicit cross-domain ID use;
- checked fixed-point price, quantity, money, fee, and feature types make scale,
  overflow, and rounding policy explicit;
- fixed-capacity policy, decision-frame, order-effect, and venue-observation
  values have typed structural validation;
- the V2 envelope uses explicit little-endian fields, a versioned initial
  message-kind registry, bounded lengths, fail-closed flags, and CRC-32;
- canonical payload codecs reject truncation, trailing bytes, excessive counts,
  unknown schema/enums, missing identity/digests, and invalid domain values;
- SHA-256 provides immutable artifact and corpus identity; and
- a Release microbenchmark reports codec timing, encoded-size ceilings, and
  code-audited allocation upper bounds without claiming system throughput.

The exact 144-byte policy corpus has SHA-256
`7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30`.
Debug, Release, legacy regression, AddressSanitizer, and UndefinedBehaviorSanitizer
tests pass on macOS ARM64. The exact Phase 1 content root also passes Release,
benchmark-instrumented, AddressSanitizer, and UndefinedBehaviorSanitizer tests
under native ARM64 Ubuntu 26.04 with GCC 15.2. Raw commands, measurements,
limitations, and the remaining target gate are recorded in
[`docs/evidence/V2_PHASE1_MACOS_CANDIDATE.md`](docs/evidence/V2_PHASE1_MACOS_CANDIDATE.md)
and
[`docs/evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md`](docs/evidence/V2_PHASE1_ARM64_CONTAINER_CANDIDATE.md).

Phase 1 is not accepted and will not be tagged until the same canonical bytes,
fixed-point results, malformed-input behavior, and tests pass on Raspberry Pi
ARM64.

### Phase 2 experimental increments - policy lifecycle and control authority

Experimental commits `7bc9e42` and `c783ab7` introduce the first pure reducer
increment and its native ARM64 Ubuntu development gate. The increment provides
a fixed-capacity policy catalog, retained request outcomes, exact/conflicting
duplicate handling, immutable and monotonic policy identity, and bounded
proposed/validated/active/superseded transitions. It performs no I/O, clock or
random read, locking, heap growth, order creation, risk change, or external
effect.

Seven Release suites, four benchmark-instrumented V2 suites, and four
ASan/UBSan V2 suites pass under ARM64 Ubuntu 26.04 with GCC 15.2 for source root
`627b38f488aad60d3f0a65874a4bb132e196400c31c2e406f4b615e1095221d3`.
Detailed RED/GREEN evidence, invariants, C++ design notes, and limitations are
recorded in
[`docs/evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md`](docs/evidence/V2_PHASE2_POLICY_KERNEL_EXPERIMENT.md).

Commits `6de06ec` and `2849a52` add the next TDD increment: fail-safe strategy
mode, monotonic kill epochs, monotonic executor generations, generation-fenced
mode transitions, and the extended ARM64 gate. The latest committed source
root is
`8e4cba2743760eb71bb3ee7277d63183c2e706cc3bec5a6d449f0eec21291a72`;
8/8 Release suites, 5/5 benchmark-instrumented V2 suites, and 5/5 ASan/UBSan
V2 suites pass under ARM64 Ubuntu 26.04 with GCC 15.2.

This is not Phase 2 completion or acceptance. It remains on
`experiment/v2-phase2-kernel` while Phase 1 awaits its Raspberry Pi gate. State
digest/snapshot, reconciliation-gated authority restoration, orders,
reservations, positions, outbox,
reconciliation, model-based replay, reducer measurement, Raft/storage
integration, and real Pi evidence remain pending.

### Version preservation

- V1 remains recoverable at annotated tag `v1.0.0`.
- Existing V1.1 branches and phase tags remain unchanged.
- V2 begins on branch `v2/architecture-contracts` from the documented V1.1
  storage-design checkpoint.
- V2 will use new wire/storage versions and will never reinterpret V1/V1.1 WAL
  data in place.
- `v2.0.0` will be created only after the release-wide acceptance gates pass.

## [V1.1] - Unreleased

Status: in progress. Stages 1 and 2 are accepted on Raspberry Pi ARM64; Stages
3 and 4 remain pending. No planned item is an implementation claim until it is
moved under **Implemented changes** and supported by tests and target-hardware
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

#### Stage 1 accepted - leader-aware routing

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

Phase 1 passed its Raspberry Pi ARM64 acceptance gate on 2026-08-27.

#### Stage 2 accepted - bounded recovery and admission control

- Added explicit client leader-knowledge states: `unknown`, `suspect`, and
  `known`. A valid redirect is suspect until an `ok` response confirms the
  leader for the observed term.
- Added a pure monotonic recovery policy with deterministic exponential jitter
  between a positive 5 ms floor and a 250 ms cap. A retry never runs at or
  after its command deadline.
- Added a 250 ms response-silence deadline because successful local Aeron
  publication does not prove remote receipt, quorum commit, or response. A
  silent route becomes unknown and discovery resumes with the same request ID.
- Added an unconfirmed-leadership admission limit of one command and a globally
  paced discovery probe. Further open-loop demand is recorded as explicit
  leader-unknown admission rejection rather than hidden by pausing the load
  generator.
- Extended pending-command state with last target and next retry time while
  retaining the original request ID for every transport attempt.
- Added benchmark summary schema 3 evidence for discovery, admission rejection,
  retry counts, pending high-watermark, leader state, and exactly-one terminal
  accounting. Failure rows now retain attempts, last target, and leader state.
- Added a one-command Pi recovery runner covering leader failover under demand,
  an explicit leaderless/quorum-loss window, bounded recovery, and same-ID
  exactly-once resolution.

The candidate passed production and benchmark-instrumented native tests and a
15-command three-node macOS smoke run at retry amplification `1.00`.

A local leader-removal diagnostic remained bounded—40 of 40 terminal outcomes,
pending high-watermark 2, 58 attempts for 12 admitted commands—but the two
remaining voters did not elect before the eight-second window ended. It
therefore produced 28 explicit admission rejections, 3 timeouts, amplification
`4.83`, and final leader state unknown. This is retained as failed recovery
evidence, not presented as a pass or throughput result.

Phase 2 passed its Raspberry Pi ARM64 acceptance gate on 2026-08-28. The
production image was `015581814255` with benchmark instrumentation disabled;
the diagnostic image was `6cab4df1978b` with instrumentation enabled. A
follower-first/leader-last rolling upgrade preserved quorum, moved leadership
from node 3 in term 89 to node 2 in term 90, and converged all three voters at
log, commit, and applied index 39 with counter value 20.

Recovery suite `20260828T180907Z` produced the following immutable evidence:

- leader failover under 5 commands/s scheduled 75 commands, completed 70,
  rejected 5 during unknown leadership, used 86 transport attempts, bounded
  pending depth at 2, recovered with zero retries in the stable tail, and
  measured retry amplification `1.229`;
- explicit quorum loss scheduled 100 commands, completed 67, rejected 30
  during unknown leadership, used 117 transport attempts and 43 discovery
  probes, bounded pending depth at 2, recovered with zero retries in the stable
  tail, and measured retry amplification `1.671`; and
- unknown-outcome recovery retried request ID `1787940625718131511` after
  quorum restoration and changed the counter exactly once from 20 to 21 while
  the commit index remained held at 208 during the no-quorum check.

Suite `20260828T180440Z` is retained as a test-harness incident, not a product
failure: Ubuntu AWK rejected the convergence observer's `log[...]` array name
because `log()` is built in. The observer therefore returned a false negative
before comparing voter state. Commit `b5d530d` renamed the array to
`current_index`, after which the complete target-hardware gate passed.

#### Stage 3 candidate - owner-loop control-plane priority

- Added an allocation-free `OwnerLoopScheduler` using
  `std::chrono::steady_clock` time points, a one-millisecond Raft tick interval,
  a 250-microsecond client safety margin, and named positive work/queue bounds.
- Changed Raft outbound and completion transfer from whole-deque temporary
  vectors to one-message `std::optional` moves so the owner loop, rather than
  queue size, controls work per iteration without an additional drain-vector
  allocation.
- Reordered the node loop to service overdue ticks, bounded peer ingress, Raft
  output, and peer publication before polling at most one client fragment when
  sufficient tick slack remains. A second bounded peer flush follows client
  admission before bounded response publication.
- Shared pre/post-client drain and peer-offer budgets across the complete
  iteration, preventing two service points from silently doubling the stated
  limits.
- Added a statically dispatched, template-based iteration coordinator and fake
  clock/poller tests that prove tick/peer/client ordering, tick-slack
  suppression, the one-client-fragment limit, and cumulative work budgets.
- Added fixed peer and response queue capacities and stopped client polling
  while the external response queue is full. The existing peer-drop behavior
  remains visible through its counter; committed responses are retained until
  bounded drain capacity is available.
- Advanced the fixed shared-memory metrics page to version 2 and added
  owner-loop iteration count, maximum tick gap, full client/peer poll-budget
  counts, and client-poll skip reasons. Plain owner-thread counters are copied
  every 100 ms; the external exporter performs text rendering and unit
  conversion.

Both normal and benchmark-instrumented builds pass all three native test
executables. A three-process macOS loopback smoke run elected node 1, completed
25 of 25 measured commands at 5 commands/s with 25 attempts, zero timeouts,
retry amplification `1.00`, and p99 `11,719,084 ns`. These are development
results only. A second 50 commands/s development run completed 250 of 250
measured commands with 250 attempts, zero timeouts, amplification `1.00`, p99
`11,921,000 ns`, stable term 1, and no response-backlog polling blocks. Maximum
observed tick gaps were `2,978,375 ns`, `4,744,666 ns`, and `3,598,917 ns` for
nodes 1--3. The processes were stopped immediately after measurement, so their
final one-entry follower lag is not convergence evidence. Deterministic
ordering and configured-budget tests now pass in both build modes. Phase 3
remains a candidate until sustained fairness, overload behavior, follower
catch-up, and the Raspberry Pi acceptance matrix pass.

Raspberry Pi run `20260829T120112Z` was retained as exploratory evidence only
because `vcgencmd get_throttled` was already `0x50000` before measurement. A
clean-boot repeat, `20260829T124828Z`, held `performance` governor and remained
at `throttled=0x0`, but failed the Phase 3 acceptance gate:

- at 5 commands/s, 75 measured schedules produced 50 completions and 25
  leader-unknown admission rejections, with amplification `2.68`, p50
  `395,937,951 ns`, and p99 `828,406,854 ns`;
- at 20 commands/s, 300 schedules produced 64 completions, 230 admission
  rejections, and 6 timeouts, with amplification `5.086`, p50
  `1,041,311,701 ns`, and p99 `1,973,153,543 ns`;
- the 5/s voters stayed converged in term 1, but the 20/s voters ended
  leaderless in terms 7--9, with maximum owner-loop gaps between 1.068 and
  1.471 seconds; and
- post-run Linux pressure-stall information reported CPU `some avg10=0.02`
  versus I/O `full avg10=75.14` and `full avg300=57.25`. Memory remained
  available, no swap was configured, storage integrity checks passed, and the
  kernel reported no current-boot undervoltage or filesystem I/O error.

The state machine remained fast (`commit_to_apply` p99 below 12 us); the delay
was before commit. Code inspection explains the physical amplification: every
log offer opens, appends, `fsync`s, and closes the WAL, while every commit-index
advance rewrites and `fsync`s a metadata temporary file, renames it, and
`fsync`s the parent directory. Three logical voters share one SD card, so one
logical command can impose approximately nine durability barriers. The
control-plane-first scheduler cannot preempt a blocking durability syscall.
Phase 3 therefore remains unaccepted pending an explicit ordered-persistence
design and storage-isolation experiments; increasing retry deadlines alone is
not an accepted fix.

The resulting design checkpoint evaluates a fixed-capacity per-voter block
arena based on the book's memory-mapped ring and double-buffered snapshot
patterns. The accepted direction is bounded memory staging plus asynchronous
ordered batch persistence, not memory-only acknowledgment. It requires an
explicit durable-index completion path because the pinned `willemt/raft`
callbacks require `log_offer`, term, and vote changes to be flushed before they
return. Snapshot and compaction may recycle a WAL prefix only after a
checksummed replacement snapshot is durable. The full proposal, rejected
shortcuts, experiments, and release gates are recorded in
`docs/V1_1_PHASE3_STORAGE_DESIGN_PIVOT.md`.

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
not substitute for target-hardware validation.

Raspberry Pi ARM64 acceptance evidence on 2026-08-27:

- the production candidate image `e2b3cb3c5c10` and benchmark-instrumented
  candidate image `53913e4eeec0` each built with all three tests passing;
- a follower-first/leader-last rolling upgrade preserved quorum, caught both
  followers up before leader replacement, and restored three active services;
- the fully upgraded cluster elected node 3 in term 81 and all three voters
  ran the production candidate image;
- request ID `1787831360569930796` reached follower 1 and then leader 3 in two
  attempts, incrementing the counter from 19 to 20 at log index 35;
- repeating that exact request ID returned value 20 and index 35 in two
  attempts, demonstrating at-most-once application and no duplicate log
  admission; and
- benchmark suite `20260827T115818Z` completed 75 of 75 measured commands with
  75 transport attempts, zero timeouts, zero capacity rejections, zero invalid
  leader hints, retry amplification `1.00`, p50 `46,016,965 ns`, and p99
  `101,675,703 ns`.

This closes the Phase 1 gate. It validates routing correctness and efficiency
at the gate load; it is not a certified maximum-throughput claim.

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
