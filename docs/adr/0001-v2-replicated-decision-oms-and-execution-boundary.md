# ADR 0001: V2 replicated decision/OMS and execution boundary

- Status: accepted architecture direction; not yet implemented
- Date: 2026-08-30
- Decision owners: project owner and architecture review
- Target release: V2
- Supersedes: no V1 or V1.1 behavior; V2 is a new product architecture
- Related contracts: `docs/V2_ARCHITECTURE_CONTRACTS.md`
- Implementation plan: `docs/V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md`

## Context

V1 and V1.1 are an educational native C++ Raft counter service. They proved
leader election, durable replicated commands, request deduplication, process
restart, external observability, failure drills, leader-aware routing, bounded
client recovery, and owner-loop scheduling. Raspberry Pi evidence also showed
that synchronous per-command SD-card persistence can block the Raft owner loop
long enough to destabilize leadership.

V2 changes the product problem. It must turn market observations and a
versioned strategy policy into risk-bounded order decisions, send those orders
to an external venue, and reconcile acknowledgements, fills, cancels, unknown
submissions, positions, and balances. The external venue is not part of the
Raft transaction and cannot be made exactly-once by consensus alone.

The original design idea was:

```text
model -> decision matrix -> executor observes a matching metric -> order
```

That shape is directionally useful but leaves the authoritative trigger in the
executor. Two executors or two failover generations can observe different
market state, and an executor can create an exchange effect that was never
committed by the cluster. The architecture needs one ordered decision authority
and a separate external-effect boundary.

## Decision

V2 will use a deterministic Raft-replicated **Decision/OMS cluster** and a
logically single active **execution gateway**.

```text
model/calibration ----> immutable DecisionPolicyArtifact
market-data edge -----> canonical bounded DecisionFrame
                                  |
                                  v
                    +---------------------------+
                    | Raft Decision/OMS cluster |
                    |                           |
                    | activate policy           |
                    | validate decision frame   |
                    | evaluate final predicate  |
                    | reserve risk              |
                    | transition order state    |
                    | commit outbox effect      |
                    +---------------------------+
                                  |
                         committed effects only
                                  |
                                  v
                    +---------------------------+
                    | active execution gateway  |
                    |                           |
                    | fence / rate limit        |
                    | encode / send / query     |
                    | classify unknown outcome  |
                    | reconcile venue truth     |
                    +---------------------------+
                                  |
                                  v
                                venue
                                  |
                     canonical venue observations
                                  |
                                  +------> Decision/OMS cluster
```

### Authoritative decision

The model or calibration service may use floating point, workers, files,
clocks, or external data to produce an immutable, versioned policy artifact.
It does not directly mutate live strategy state.

A market-data or feature service may use a local prefilter to decide that a
bounded `DecisionFrame` is worth proposing. That prefilter is an optimization,
not authority. The cluster validates the frame, checks the activated artifact,
performs the final deterministic policy evaluation, checks strategy mode and
risk, and commits either a typed rejection/no-action result or an order intent.

Every input that can change the authoritative result is either contained in
the committed command or named by an immutable content digest and version.

### Transactional outbox boundary

An accepted order decision, its risk reservation, deduplication receipt, order
entity, and outbound effect are created in one committed state-machine
transition. The apply handler returns data; it never calls the venue.

The execution gateway consumes committed outbox effects after apply. Delivery
is at least once by stable effect identity. A timeout is an explicit unknown
outcome and triggers reconciliation before retry. The gateway may refuse,
defer, encode, send, query, or reconcile an effect. It may not change the
economic intent or independently create risk.

### Order and venue authority

The cluster is authoritative for internal intent, risk reservations, accepted
venue evidence, positions, and the system's knowledge of order state. The venue
is authoritative evidence for whether an external order or fill exists.
Reconciliation compares those two authorities at an explicit venue watermark;
it never overwrites conflicting evidence to make a dashboard green.

### Single active executor

There is one active execution authority per venue/account domain. A future
standby may exist, but automatic promotion is prohibited until the old sender
can be fenced from external network effects. A Raft term fences replicated
state; it does not retract an HTTPS request already in flight.

Each effect carries the committed executor generation and kill epoch. The
gateway rejects stale generations and fails closed when its authority is
uncertain. Strong future promotion may require venue session exclusivity,
credential or egress fencing, or another external lease mechanism.

### Emergency risk reduction

Normal risk-increasing effects require a committed outbox record. The gateway
also implements a narrow, pre-authorized emergency policy: loss of current
authority, stale required market data, authenticated kill, or declared session
failure stops new risk and may invoke cancel/mass-cancel through reserved
capacity. It cannot increase quantity, improve a quote, or open a new position.
Every emergency action is retained as evidence and reconciled back into the
cluster before normal operation resumes.

### Persistence

A successful cluster decision means committed under the declared quorum-
durability policy, not merely copied into process memory. V2 adopts the bounded
segmented asynchronous WAL direction documented for V1.1: fixed-capacity
per-voter blocks stage records, a persistence owner flushes sealed batches, and
Raft protocol success remains behind explicit durability completion.

The storage implementation may amortize and move durability latency off the
owner loop. It may not acknowledge a volatile entry as durable. Snapshots and
compaction are separate recovery mechanisms and include every deduplication,
outbox, order, reservation, policy, and reconciliation fact required for safe
replay.

## State ownership

### Replicated Decision/OMS cluster

- activated policy and feature-schema versions;
- strategy mode, enablement, kill epoch, and executor generation;
- accepted decision inputs and deterministic outcomes;
- desired quote/order intent and order lifecycle knowledge;
- risk limits, reservations, inventory, positions, and fill ledger;
- request, effect, fill, and venue-event deduplication receipts;
- pending, delivered, unknown, and reconciled outbox state;
- venue acknowledgements, rejects, fills, cancels, and reconciliation results;
- source sequence and recovery watermarks; and
- snapshot metadata, state digest, membership, and retained retry responses.

### Model, market-data, and feature edge

- raw feed parsing and book reconstruction;
- floating-point calibration or model inference;
- immutable artifact construction and content hashing;
- raw event archive and explanation traces; and
- prefiltering that can propose, but never authorize, a decision.

### Execution gateway

- authentication, credentials, TLS/REST/WebSocket/FIX session mechanics;
- request encoding and venue capability lowering;
- client-to-venue identifier mapping;
- rate-limit and emergency-capacity accounting;
- transport attempts and response classification;
- open-order, execution, position, and balance queries; and
- raw venue evidence and reconciliation cursors.

### Read models and observability

- Grafana dashboards, Prometheus rendering, query indexes, reports, and
  historical analytics are disposable projections;
- they may lag or rebuild and never become the source of trading authority; and
- hot paths update only fixed-size bounded counters or pages.

## Non-negotiable invariants

1. No risk-increasing venue effect is sent before its outbox record commits.
2. Raft apply performs no network, filesystem discovery, wall-clock read,
   random draw, unbounded allocation, or external service call.
3. The executor never originates or economically modifies an order decision.
4. The same request, effect, fill, and venue event identity changes economic
   state at most once.
5. An unknown submission retains its reservation and blocks conflicting risk
   until authoritative reconciliation.
6. Only accepted fill evidence changes position; acknowledgement is not fill.
7. Cancel-pending quantity remains risk-bearing until fill/cancel evidence or
   reconciliation closes it.
8. Replay uses the live reducer but disables external delivery.
9. Snapshot installation retains all retry, outbox, order, risk, identity, and
   source-watermark state needed to make replay equivalent to live apply.
10. A stale executor generation or kill epoch cannot send queued new risk.
11. Loss of quorum prevents new authoritative risk decisions.
12. Same-host voters are not described as host fault tolerance.

## Alternatives considered

### Executor evaluates the matrix and trades directly

Rejected. It minimizes local latency but makes the executor the true decision
authority. Failover, replay, and concurrent observation can diverge.

### Raft stores the matrix while the executor evaluates live metrics

Rejected. Policy is replicated but the decisive input and trigger evaluation
are not. An order can exist without a committed causal record.

### Raft apply calls the venue

Rejected. Venue I/O is nondeterministic and unbounded. A callback stall blocks
consensus, and replay can duplicate an irreversible effect.

### Single writer and local journal

Accepted only as a development harness or deliberate deployment mode. It is
simpler and often faster, but automatic promotion requires fencing and cannot
provide one-machine-failure continuity.

### Multiple active execution gateways

Rejected until external fencing and idempotency prove that two senders cannot
create duplicate effects.

## Consequences

### Benefits

- every venue effect has a committed, replayable cause;
- risk is reserved before an order leaves the system;
- strategy and model versions are attributable;
- duplicate delivery and duplicate fills are deterministic no-ops;
- venue stalls cannot block Raft apply;
- unknown outcomes are visible business states;
- the same reducer supports live operation, replay, simulation, and recovery;
- independent hosts can later provide real one-machine-failure tolerance; and
- strategy logic can evolve without rewriting identity, risk, outbox, or
  reconciliation contracts.

### Costs and risks

- every authoritative decision pays quorum and durability latency;
- schema, snapshot, migration, deduplication, and reconciliation complexity is
  substantial;
- new risk is unavailable without quorum;
- market-data ingestion must be bounded and must preserve decision evidence;
- the executor remains a liveness dependency until safely fenced failover is
  implemented;
- venue APIs can leave effects ambiguous even when the cluster is healthy; and
- the current synchronous SD-backed WAL is unsuitable for the intended event
  rate and must not be hidden by longer timeouts.

## Evidence that can invalidate or revise this ADR

This ADR must be revisited if evidence shows that:

- quorum commit cannot meet the declared decision latency on the target
  persistence topology;
- the chosen venue cannot provide identity or reconciliation evidence strong
  enough to resolve unknown submissions;
- the policy cannot be evaluated deterministically from bounded logged inputs;
- decision frames require so much data that the replicated core becomes a raw
  market-data archive;
- externally fenced active/passive execution cannot be implemented for the
  required availability objective; or
- a simpler single-writer design meets the actual availability requirement and
  materially reduces verified operational risk.

Reversal requires a new ADR, retained evidence, migration and rollback plans,
and no deletion or force-moving of V1/V1.1 release history.
