# V2 Phase 3D action-contract experiment

Date: 2026-09-06

Status: first host contract slice green; canonical policy integration,
resolution, reducer, replay, ARM64, and physical Pi gates pending

## Objective

Freeze bounded typed contracts for converting an ordered rule match into an
exact economic proposal without yet creating risk, orders, effects, or venue
I/O.

The authority boundary is:

```text
model desired action
    -> immutable strategy-policy envelope
    -> exact economic proposal
    -> Phase 4 dynamic risk check and atomic reservation
```

Policy and risk violations reject. They do not silently resize, reprice, or
downgrade the requested action.

## TDD evidence

The new `v2-action-template-contract-tests` target was written before its
interface. The observed RED build failed because
`velocita/v2/action_template.hpp` did not exist. The minimum contract
implementation then made the focused test green.

The complete functional suite passes 17/17. The optimized Release and
ASan/UBSan V2 suites each pass 14/14.

Directed tests currently prove:

- price and quantity sources are dimensionally distinct C++ types;
- fixed and feature-derived values have one canonical representation;
- invalid feature indices and conversion denominators fail closed;
- safety quantity and notional limits are positive;
- deviation and tick-offset caps cannot be negative;
- the deviation reference must be an unshifted frame feature;
- every economic action binds logical order slot, instrument-spec version, and
  venue-capability version;
- cancel has a canonical no-price/no-quantity shape;
- no-action cannot masquerade as an economic action template;
- Phase 3D represents post-only separately from time-in-force;
- required post-only cannot combine with IOC or FOK; and
- an exact proposal retains policy, risk-policy, instrument, capability,
  account, instrument, venue, decision, and logical-target provenance.

## Layout evidence

For the current 64-byte target-cache-line build:

```text
PriceSource:             32 B /  32 B budget
QuantitySource:          24 B /  24 B budget
ActionSafetyCaps:        72 B /  80 B budget
ActionTemplate:         152 B / 160 B budget
EconomicActionProposal: 112 B / 128 B budget
```

These are resident C++ object sizes. No canonical policy, kernel, or snapshot
payload size changed in this slice. Compile-time assertions and the layout
report expose the ceilings so later policy integration cannot grow silently.

## C++ design notes

`PriceSource` and `QuantitySource` are separate aggregates instead of a single
untyped integer source. This prevents price ticks from being accidentally used
as quantity lots at compile time.

The structs are union-like but do not use a C++ union: the `kind` tag selects
the active interpretation and validation requires every inactive field to be
zero. That gives each meaning one valid object representation before the
canonical codec is introduced.

Feature conversion uses an explicit positive number of `FeatureMicros` atoms
per output tick or lot plus an explicit rounding mode. Price alone permits a
checked tick offset. Quantity offsets are intentionally absent.

The legacy `TimeInForce::post_only` remains available to older V2 contracts,
but the Phase 3D validator rejects it. New actions represent duration with
GTC/IOC/FOK and represent post-only as an independent exact requirement. There
is no “permitted” state that delegates the economic choice to the executor.

## Limitations and next slice

- `ActionTemplate` is not yet embedded in `PolicyArtifact`.
- No canonical action/template codec or schema digest exists yet.
- Feature conversion is structurally validated but not resolved.
- The conversion factor must later be cross-checked against the bound feature
  schema; it cannot be trusted merely because a policy supplied it.
- Exact notional must later be calculated with checked arithmetic from
  authoritative versioned instrument economics and independently recomputed by
  Phase 4 risk.
- Runtime price, quantity, notional, deviation, freshness, capability, and
  reject-not-clamp enforcement are not implemented yet.
- No risk is checked or reserved in Phase 3D.

The next TDD slice defines the versioned instrument/capability input needed to
calculate notional and validate order properties, then implements a pure
resolver before changing the policy wire schema.
