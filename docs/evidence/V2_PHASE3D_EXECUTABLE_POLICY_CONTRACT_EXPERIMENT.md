# V2 Phase 3D executable-policy contract experiment

Date: 2026-09-07

Status: candidate contract and host layout experiment green; canonical codecs,
replicated lifecycle, ARM64, Pi, and NUC capacity acceptance pending

## Objective

Define a strictly bounded companion artifact that maps existing ordered policy
rules to deduplicated economic action templates without reinterpreting
`PolicyArtifact` V1 or duplicating its predicates.

The candidate limits are:

```text
active/staged executable artifacts = 2
templates per executable artifact  = 8
rules per policy                    = 32
feature descriptors                 = 32
template slot 0                     = no_action
template slots 1..8                 = one-based template references
```

These become accepted wire constants only after canonical-size, ARM64, physical
Pi, and NUC gates pass with identical values.

## TDD evidence

`v2-executable-policy-contract-tests` was added before the interface. The
observed RED build failed because `velocita/v2/executable_policy.hpp` did not
exist. The minimum contract and binding implementation then made the focused
test green.

The host functional suite passes 19/19. Focused optimized Release and
ASan/UBSan executable-policy/layout gates each pass 2/2.

The tests prove:

- schemas, identities, digests, counts, and array regions are bounded;
- unused descriptors, templates, and rule slots are canonical zero;
- duplicate templates and unreferenced templates are rejected;
- slot zero is structurally available for no-action but cannot satisfy an
  economic policy rule;
- nonzero slots are one-based and cannot exceed the active template count;
- rule directive and selected template directive must agree;
- executable identity and digest bind one exact base policy artifact;
- feature-schema identity and digest bind one exact feature definition;
- generic features have no economic conversion;
- price and quantity features have positive authoritative conversions; and
- templates cannot reinterpret quantity as price or override the feature
  schema's conversion factor.

## C++ contract structure

`validate(FeatureSchemaArtifact)` and `validate(ExecutablePolicyArtifact)` are
single-object structural checks. `bind_executable_policy` is a separate pure
relational check across the base policy, feature schema, and executable
companion.

This separation lets canonical decoders reject malformed values before state
lookup, while activation can return a precise cross-artifact mismatch. All
objects are trivially copyable and contain fixed `std::array` storage; there is
no heap ownership, pointer graph, virtual dispatch, I/O, clock, or randomness.

`ActionTemplateSlot` is a strong `std::uint8_t` ID. It cannot be implicitly
constructed from `LogicalOrderSlot`: the former selects policy data while the
latter identifies an OMS order target. Both may contain small integers, but
mixing them is a type-level correctness error.

## Host layout result

For the 64-byte target-cache-line build:

```text
FeatureDescriptor                         16 B /   16 B ceiling
FeatureSchemaArtifact                    568 B /  576 B ceiling
ExecutablePolicyArtifact               1,392 B / 1,408 B ceiling
active + staged complete resident set  3,920 B
current TradingState budget headroom   3,896 B
candidate projected TradingState      73,752 B
provisional hardware-test ceiling     81,920 B
```

The active-plus-staged candidate exceeds the old 72-KiB state ceiling by 24
bytes before lifecycle metadata. This is an expected successful guardrail: the
old budget cannot be silently reused. The accepted replacement ceiling is not
yet chosen.

Canonical footprint and snapshot headroom are not inferred from `sizeof`.
Padding is not wire data. The next codec slice must calculate exact maximum
bytes and prove the complete snapshot remains under its declared envelope or
trigger an explicit envelope/schema decision.

## Limitations and next gate

- Neither artifact is in `TradingState`.
- There is no propose/validate/activate/supersede command for the companion.
- No canonical codec or golden digest corpus exists for these schemas.
- Rule evaluation does not yet select a template.
- The action resolver is not invoked by the authoritative reducer.
- No order, reservation, effect, adapter, or live-trading authority exists.

Next, freeze canonical bytes and exact maximum payload sizes. Only then can the
active/staged state layout be reviewed on ARM64, Pi, and NUC before reducer
integration.
