# V2 Phase 3A ordered-rule evaluator evidence

Date: 2026-09-04

- Behavior commit: `6ee47b7`
- Decision: retain as a green host increment; Phase 3 remains unaccepted

## Objective

Freeze bounded fixed-point predicate and ordered first-match semantics before
changing the accepted decision-frame schema or integrating evaluation into the
authoritative reducer.

## TDD evidence

The first valid RED build reached the new test translation unit and failed for
the intended reason:

```text
tests/v2_rule_evaluator_tests.cpp:1:10: fatal error:
'velocita/v2/rule_evaluator.hpp' file not found
```

An earlier configure attempt failed while trying to fetch Aeron because DNS was
unavailable. It is not counted as RED evidence because it never reached the new
contract test. The valid run reused the existing pinned local Aeron and
willemt/raft sources with disconnected FetchContent.

GREEN host evidence:

- focused Debug evaluator suite: 1/1 passed;
- complete Release suite: 14/14 passed; and
- V2 ASan/UBSan suite: 11/11 passed.

## Accepted experimental behavior

- all five explicit fixed-point comparison operators obey exact integer
  boundaries, including negative and extreme 64-bit atoms;
- rules are evaluated in ascending array index;
- the first match is terminal, including a matching `no_action` rule;
- no match returns reason-coded `no_rule_matched` and `no_action`;
- the result reports matched rule index and rules visited;
- malformed policy/frame enums fail before predicate evaluation; and
- every rule feature reference is preflighted before first-match evaluation, so
  an early match cannot hide a malformed later reference.

## C++ design review

`velocita_v2_evaluator` depends on `velocita_v2_contracts`; the contract library
does not depend on evaluator behavior. This keeps the dependency graph directed
from semantic implementation toward stable value types.

The public function borrows immutable values:

```cpp
RuleEvaluation evaluate_rules(const PolicyArtifact& policy,
                              const DecisionFrame& frame) noexcept;
```

`const&` avoids copying the bounded arrays and prevents mutation through this
interface. No borrowed pointer or reference escapes. `RuleEvaluation` is a
small trivially-copyable owned value, so callers do not inherit an input
lifetime dependency.

The implementation performs two bounded indexed loops. Structural reference
validation costs at most `kMaxPolicyRules` iterations; economic matching costs
at most another `kMaxPolicyRules`. The second loop short-circuits at the first
match. It allocates no memory and performs no I/O, clock read, random access,
virtual dispatch, exception throw, or unordered-container iteration.

`FeatureMicros` is passed to the internal comparator by value because it is a
small fixed-width value. Comparisons use signed integer atoms directly; they do
not subtract values, avoiding overflow at `INT64_MIN`/`INT64_MAX` boundaries.

`kNoMatchedRuleIndex` is the maximum `uint16_t` value. A static assertion proves
the configured maximum rule count cannot collide with that sentinel or
overflow the 16-bit matched-index/rules-visited fields.

## Limits and next gate

This is not authoritative decision evaluation. It does not:

- confirm that the policy is active in `TradingState`;
- verify policy/frame scope, source evidence, event time, or control authority;
- retain a `DecisionId` or decision record;
- produce canonical result bytes;
- reserve risk, create an order, or create an outbox effect; or
- establish ARM64, Pi, Raft, durability, performance, or trading acceptance.

The next planned increment is Phase 3B's reviewed decision/source schema
revision. Canonical-byte changes require a schema/version decision and new
golden evidence before authoritative reducer integration.
