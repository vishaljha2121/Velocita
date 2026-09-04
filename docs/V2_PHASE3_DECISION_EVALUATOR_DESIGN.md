# V2 Phase 3 authoritative decision-evaluator design

Status: decisions 1 through 5 approved; Phase 3A and 3B are green on the host,
but Phase 3 remains unaccepted

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

Phase 3 began with two intentionally visible gaps:

1. `DecisionFrame` lacked explicit `SourceId`, `SourceGeneration`,
   `SourceSequence`, event time, and receive time. Phase 3B resolves the value
   and byte-contract portion of this gap with decision-frame schema V2.
   Authoritative comparison against replicated source state remains Phase 3C.
2. `PolicyRule` selects only a directive class. It cannot fully describe the
   side, price, quantity, time-in-force, or target order required by an
   executable action.

Changing either accepted Phase 1 byte contract requires an explicit schema
revision, new golden corpus, backward-compatibility decision, and replay tests.
Phase 3B applies that discipline only to `DecisionFrame`; policy artifacts,
order effects, and venue observations remain independently versioned at V1.

## 3. Recommended semantic decisions

### 3.1 Ordered rules

Rules are evaluated in ascending array index. The first matching rule wins.
Evaluation stops immediately, including when that rule selects `no_action`.
If no rule matches, the result is `no_action` with reason `no_rule_matched`.

Why: array order is canonical, bounded, cheap, and deterministic. “Most
specific rule wins” would require a second precedence language and create
ambiguous policy reviews.

This aligns with the Velocita book's design principles, with one qualification:
the book does not mandate first-match-wins for every strategy language. It does
mandate that order-sensitive replicated evaluation use a declared deterministic
order, warns that unordered-container iteration can make first-match behavior
replica- or build-dependent, criticizes an existing CMC gate whose first reject
is determined by hash-table iteration, and recommends an explicit ordered array
for a new risk design. First-match-wins is the Phase 3 policy-language choice we
make on top of those principles.

Relevant book sections are:

- `chapters/03-replicated-state-machines.tex`, Rule 3: unordered-container
  iteration must not determine first-match or serialization outcomes;
- `chapters/17-cmc-client-management.tex`, “The eight automatic checks”: an
  emergent hash iteration order is deterministic only accidentally and does not
  constitute a declared priority contract; and
- `chapters/49-risk-engine-margin.tex`, “Ordered evaluation”: check order is
  part of the safety/latency contract and a new design should use an explicit
  ordered array.

#### Alternatives considered

| Alternative | Benefit | Why it is not the Phase 3 default |
|---|---|---|
| Hash/unordered-container iteration | convenient lookup and plugin registration | order is emergent from buckets, insertion, library, and capacity; unacceptable for replicated first-match semantics |
| Last matching rule wins | later entries act like overrides | every rule must run, shadowing is less obvious, and an unrelated appended rule may silently replace an earlier action |
| Evaluate all, then rank directive severity | result independent of physical order | requires a second precedence contract; `cancel`, `replace`, and `place` do not have one universally correct economic ordering |
| Explicit numeric priority plus sorting | priority can differ from storage order | adds tie rules, duplicate-priority validation, sorting/canonicalization, and another field without adding value while the artifact is already an ordered array |
| Most-specific predicate wins | resembles firewall or routing selection | “specificity” is undefined for mixed numeric predicates and overlapping ranges; ties still need deterministic precedence |
| Reject whenever multiple rules match | exposes overlap instead of choosing | safe but makes deliberate fallback/override bands impossible and requires evaluating the full array; useful as an optional policy-lint mode |
| Compose every matching rule | expressive for multi-condition strategies | the current rule emits a complete directive, so merging conflicting actions has no defined algebra; conjunctions need an explicit grouped-expression schema |
| Compiled decision tree/DAG | can reduce comparisons for large policies | substantially larger verifier, codec, and replay surface; unjustified for the current maximum of 32 rules |

#### Accepted costs and safeguards

First-match-wins makes array position economically meaningful. Moving one rule
can change behavior even if every individual predicate is unchanged. Therefore:

- artifact order is covered by the artifact digest and canonical bytes;
- any reorder requires a new immutable policy version;
- tests cover overlaps, exact boundaries, terminal `no_action`, and no-match;
- evaluation returns the matched rule index and rules visited;
- activation tooling should warn about unreachable or fully shadowed rules;
- future grouped predicates or action composition require a new schema rather
  than changing first-match semantics in place; and
- the evaluator never stores rules in or derives precedence from an unordered
  container.

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
4. source evidence is admitted and matches the retained digest;
5. policy validity and market-data readiness are satisfied;
6. ordered predicates are evaluated; and
7. replicated control authority permits or suppresses the candidate
   directive.

The precise order matters because one input may violate several conditions.
A canonical result must report one stable reason on every architecture.
`halted` is the categorical exception and may terminate before source and rule
work because it permits no strategy directive. Other control modes require a
candidate directive before their authority can be evaluated.

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

- [x] RED tests cover every comparison operator and exact boundary;
- [x] RED tests prove first-match-wins, terminal `no_action`, and no-match;
- [x] invalid feature index and unknown enum fail closed;
- [x] implementation uses bounded indexed loops and checked integer values;
- [x] focused and full host tests pass under Release and sanitizers;
- [x] semantic fixtures and result fields are reviewed; canonical result bytes
      and their digest correctly remain Phase 3C work; and
- [x] C++ ownership, allocation, and `noexcept` claims are audited.

This increment returns a non-authoritative evaluation value. It does not mutate
`TradingState` and cannot create an economic effect.

### 3B: decision/source schema revision

Objective: make causal evidence and event-time validity explicit.

- [x] add source ID/generation/sequence, event time, and receive time to the
      canonical input;
- [x] decide whether V1 V2-contract frames remain decodable or fail with a
      typed unsupported-schema result;
- [x] bump only the decision-frame schema rather than reinterpret old bytes;
- [x] update payload size constants and malformed/truncation tests;
- [x] create and review new golden bytes/digest; and
- [x] prove old bytes receive the documented compatibility outcome.

The accepted compatibility policy is intentionally narrow:

- policy artifact V1, decision frame V2, order effect V1, and venue
  observation V1 have independent version constants;
- the authoritative decision-frame decoder accepts only V2;
- the frozen legacy V1 corpus returns typed `unsupported_schema` before V2
  length interpretation;
- missing source identity or timestamp provenance fails closed; and
- event and receive timestamps must be present and positive, but are not
  ordered against one another because no shared clock-domain contract has yet
  been declared.

No V1-to-V2 migration tool is included because no production V2 decision-frame
records exist. Inventing the absent provenance would produce plausible but
false audit data.

### 3C: authoritative evaluation command and records

Objective: apply evaluation through the Phase 2 reducer foundation.

The approved deterministic rejection/evaluation precedence is:

1. validate command identity and the decision-frame contract;
2. resolve the exact active policy;
3. verify account, instrument, and feature-schema scope;
4. verify admitted source identity and evidence digest;
5. verify policy event-time validity and market-data readiness;
6. evaluate rules in canonical array order; and
7. apply replicated control authority to permit or suppress the proposed
   directive.

`halted` is the categorical exception: it may terminate before source and rule
work because no strategy directive is permitted. Other modes require the
candidate directive before their authority can be evaluated.

Before adding retained decisions, Phase 3C enforces these layout guardrails:

- canonical payload size and in-memory object size have separate ceilings;
- arena stride is rounded independently from canonical wire bytes;
- the target cache-line assumption is a compiled ABI parameter and appears in
  build evidence;
- layout tests and reports expose `sizeof`, `alignof`, stride, and budget; and
- the retained decision representation separates frequently read hot outcome
  fields from cold audit provenance, subject to measured target behavior.

The pre-3C layout gate is implemented in commit `e650f31`. On the development
host with a declared 64-byte target line, it currently reports:

```text
DecisionFrame: canonical maximum 421 B, object 432 B, arena stride 448 B
PolicyArtifact: canonical maximum 516 B, object 912 B
TradingState: object 57,152 B with a 65,536 B review ceiling
```

These are different resource dimensions. Canonical bytes govern transport and
durable encoding; `sizeof` governs resident C++ storage; arena stride governs
address spacing and internal fragmentation. Changing one does not silently
change either of the others. Compile-time ceilings deliberately make Phase 3C
fail to build if retained decisions overflow the reviewed working-set budget.

- [x] declare the target cache-line size as build metadata;
- [x] separate canonical payload, object, and arena-stride budgets;
- [x] add compile-time ceilings and a machine-readable layout report;
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

Approved decisions:

1. ordered first-match-wins rules with no-match as `no_action`;
2. control-mode dominance shown in Section 3.4;
3. explicit `DecisionId` distinct from request identity;
4. explicit source/event-time schema revision before authoritative apply; and
5. staged 3A through 3E implementation rather than one large schema/reducer
   change.

The economic action-template details in 3D receive their own review after 3A
semantics are proven and before their schema is written.

Phase 3A evidence is recorded in
[`V2 Phase 3A ordered-rule evaluator evidence`](evidence/V2_PHASE3_RULE_EVALUATOR_EXPERIMENT.md).
