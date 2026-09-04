# V2 Phase 3 authoritative decision-evaluator design

Status: proposed contract for review; no Phase 3 behavior is implemented

Branch: `v2/phase-3-policy-evaluator`

Authority:

- [`ADR 0001`](adr/0001-v2-replicated-decision-oms-and-execution-boundary.md)
- [`V2 architecture contracts`](V2_ARCHITECTURE_CONTRACTS.md)
- [`V2 TDD implementation and validation plan`](V2_TDD_IMPLEMENTATION_AND_VALIDATION_PLAN.md)

## 1. Objective and non-goals

Phase 3 deterministically converts an admitted decision frame, one active
policy artifact, and replicated control state into a reason-coded decision
record.

It does not reserve risk, create an order, create an outbox effect, perform
network or storage I/O, or contact a venue. Those are later phases.

The core invariant is:

```text
same canonical state + same canonical command
    -> same result bytes + same next-state bytes
```

## 2. Existing facts and identified gaps

Phase 1 already defines bounded `PolicyArtifact`, `PolicyRule`, and
`DecisionFrame` values. Phase 2 can store an active policy, admit ordered source
evidence, deduplicate a command, and replay canonical state.

Two gaps are intentionally not hidden:

1. `DecisionFrame` has venue session/sequence and an evidence digest, but lacks
   explicit `SourceId`, `SourceGeneration`, `SourceSequence`, and event time.
   Therefore it cannot yet prove that the exact decision evidence was admitted
   by the replicated source cursor or apply the artifact's validity interval.
2. `PolicyRule` selects only a directive class. It cannot fully describe the
   side, price, quantity, time-in-force, or target order required by an
   executable action.

Changing either accepted Phase 1 byte contract requires an explicit schema
revision, new golden corpus, backward-compatibility decision, and replay tests.

## 3. Recommended semantic decisions

### 3.1 Ordered rules

Rules are evaluated in ascending array index. The first matching rule wins.
Evaluation stops immediately, including when that rule selects `no_action`.
If no rule matches, the result is `no_action` with reason `no_rule_matched`.

Why: array order is canonical, bounded, cheap, and deterministic. “Most
specific rule wins” would require a second precedence language and create
ambiguous policy reviews.

### 3.2 Predicate behavior

Each rule compares one `FeatureMicros` value with one threshold using the
explicit `PredicateOperator`. Comparisons operate on the checked fixed-point
integer atoms; no floating point, tolerance, locale, or host rounding mode may
affect the result.

The evaluator returns the matched rule index and number of rules visited. This
is both replay evidence and a bounded-cost metric.

### 3.3 Fail-closed validation order

Evaluation checks conditions in a fixed order:

1. command identity and structural frame validity;
2. policy identity exists and is active;
3. policy/frame account, instrument, and feature-schema scopes match;
4. control-authority mode permits the candidate directive;
5. source evidence is admitted and matches the retained digest;
6. policy validity and market-data readiness are satisfied; and
7. ordered predicates are evaluated.

The precise order matters because one input may violate several conditions.
A canonical result must report one stable reason on every architecture.

### 3.4 Control authority dominates economic policy

The policy may propose economic behavior; replicated control state has higher
authority.

| Strategy mode | Phase 3 behavior |
|---|---|
| `normal` | evaluate the policy |
| `no_new_risk` | suppress `place_limit` and `replace`; allow `no_action` and `cancel` |
| `cancel_only` | allow only `no_action` and `cancel` |
| `reduce_only` | fail closed for risk-increasing actions until Phase 4 can prove position reduction |
| `halted` | emit no strategy action; emergency cancellation uses its separate authority path |

Why: Phase 3 cannot infer that a replacement or order reduces exposure before
Phase 4 owns positions and reservations. Treating an unknown action as safe
would invert the authority hierarchy.

### 3.5 Decision identity and retention

An evaluation command carries an explicit non-zero `DecisionId`. Request
identity answers “is this the same cluster command?” Decision identity answers
“is this the same economic decision?” They are related but not interchangeable.

A bounded `DecisionRecord` retains at least:

```text
decision_id | policy id/version | frame/source evidence digest
source generation/sequence | state/control versions
directive | reason | matched rule | evaluation mode
```

Reusing one `DecisionId` with different provenance is a typed conflict. Exact
retry returns the retained original result. Retention expiry, when added, is a
replicated transition rather than a local clock eviction.

### 3.6 Shadow versus authoritative evaluation

Both modes run identical validation and predicates. The mode is retained in the
decision record. Phase 4 must reject creation of a risk-increasing effect from
a shadow decision.

Shadow is an authority property, not a logging preference. A dashboard label
or executor configuration cannot upgrade a shadow decision.

## 4. Increment checklist

### 3A: pure predicate and ordered-rule evaluator

Objective: freeze reusable evaluation semantics without changing accepted wire
or snapshot schemas.

- [ ] RED tests cover every comparison operator and exact boundary;
- [ ] RED tests prove first-match-wins, terminal `no_action`, and no-match;
- [ ] invalid feature index and unknown enum fail closed;
- [ ] implementation uses bounded indexed loops and checked integer values;
- [ ] focused and full host tests pass under Release and sanitizers;
- [ ] decision corpus bytes/digest are reviewed; and
- [ ] C++ ownership, allocation, `constexpr`, and `noexcept` claims are audited.

This increment returns a non-authoritative evaluation value. It does not mutate
`TradingState` and cannot create an economic effect.

### 3B: decision/source schema revision

Objective: make causal evidence and event-time validity explicit.

- [ ] add source ID/generation/sequence and event time to the canonical input;
- [ ] decide whether V1 V2-contract frames remain decodable or fail with a
      typed unsupported-schema result;
- [ ] bump the relevant schema version rather than reinterpret old bytes;
- [ ] update payload size constants and malformed/truncation tests;
- [ ] create and review new golden bytes/digest; and
- [ ] prove old bytes receive the documented compatibility outcome.

### 3C: authoritative evaluation command and records

Objective: apply evaluation through the Phase 2 reducer foundation.

- [ ] add explicit command-kind value, never a `std::variant` ordinal;
- [ ] add typed result codes and a bounded `DecisionRecord` region;
- [ ] prove policy/scope/source/validity/control rejection precedence;
- [ ] prove exact retry and conflicting `DecisionId` behavior;
- [ ] update command/result codec, snapshot, replay, and reference model;
- [ ] prove shadow decisions cannot produce an outbox effect; and
- [ ] prove capacity exhaustion causes no partial mutation.

### 3D: economic action-template schema

Objective: define the deterministic economic proposal consumed by Phase 4.

The recommended direction is a bounded action template selected by a rule. A
template identifies directive class, side, time-in-force, and the canonical
sources or constants for price and quantity. Cancel/replace must identify a
deterministic target; they cannot mean “whichever order the executor finds.”

Before implementation, review:

- fixed values versus feature-index references for price/quantity;
- one-live-order-per-side versus explicit target `ClusterOrderId`;
- policy caps on price deviation and quantity;
- venue capability constraints such as post-only; and
- whether replace is native or canonicalized to cancel plus new submit.

No Phase 4 order/effect work begins until this contract is accepted.

### 3E: replay, measurement, and promotion

- [ ] handwritten golden corpus covers every result reason;
- [ ] generated reference model covers boundary and interaction sequences;
- [ ] direct, canonical replay, and every snapshot cut agree;
- [ ] evaluation duration, rules visited, and capacity high-watermarks are
      measured without claiming Raft throughput;
- [ ] host Release and sanitizer gates pass;
- [ ] pinned ARM64 Linux gate passes from an exact source root;
- [ ] physical Pi phase-closure gate passes; and
- [ ] changelog/evidence state limitations and rollback precisely.

## 5. C++ design guidance

The pure 3A API should resemble:

```cpp
RuleEvaluation evaluate_rules(const PolicyArtifact& policy,
                              const DecisionFrame& frame) noexcept;
```

Both inputs are `const&` because the evaluator borrows them only for the call
and may not mutate or retain them. The result is returned by value because it is
small, owns its data, and benefits from guaranteed copy elision/ABI register
return where applicable.

The loop is bounded by `kMaxPolicyRules`; no iterator or pointer escapes. No
allocation, exception, clock, random source, system call, hash-table traversal,
or virtual dispatch is required. `noexcept` is justified only if every called
operation is also non-throwing.

The full 3C reducer remains:

```cpp
ApplyResult apply(TradingState& state,
                  const Command& command,
                  ApplyScratch& scratch) noexcept;
```

Do not use the position of `EvaluateDecisionFrame` inside `std::variant` as its
wire kind. Variant order is a C++ implementation detail; the explicit
`KernelCommandKind` integer is the compatibility contract.

## 6. Failure interpretation

- A typed no-action/rejection is a successful deterministic evaluation, not an
  infrastructure failure.
- A codec or digest error means the command never reaches semantic apply.
- Capacity exhaustion is availability loss with safety preserved.
- A replay mismatch is a correctness failure and blocks the phase.
- A slower Pi result is a performance observation; differing bytes or outcomes
  are a portability/correctness failure.

## 7. Review decisions required before behavior code

Approve or amend these decisions:

1. ordered first-match-wins rules with no-match as `no_action`;
2. control-mode dominance shown in Section 3.4;
3. explicit `DecisionId` distinct from request identity;
4. explicit source/event-time schema revision before authoritative apply; and
5. staged 3A through 3E implementation rather than one large schema/reducer
   change.

The economic action-template details in 3D receive their own review after 3A
semantics are proven and before their schema is written.
