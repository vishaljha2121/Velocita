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
Padding is not wire data. The canonical-codec slice now proves:

```text
maximum FeatureSchemaArtifact payload       332 B / 384 B candidate ceiling
maximum ExecutablePolicyArtifact payload  1,278 B / 1,344 B candidate ceiling
active + staged maximum canonical content 3,220 B
```

The 3,220-byte figure is not yet the next snapshot size. It excludes presence,
lifecycle, activation-index, and framing fields that do not exist until reducer
integration. It is the exact maximum content contribution against which that
next snapshot schema will be designed.

Canonical arrays encode only their declared active prefix. Fixed `std::array`
storage remains useful for bounded in-memory ownership, while the wire does not
pay for unused feature, template, or rule-binding slots. Every decoder first
checks the fixed prefix, then declared capacity, then exact computed length,
and only then reads variable data and invokes the structural contract.

The executable-policy fixture hashes to
`4930933b06c80157af8eafa00c4734bc4cd1df506a25edcbde63be4e6bcf09e4`;
the feature-schema fixture hashes to
`fdffdaf4820545ad8ab58b7e07e42bf83441e573f2cc5b6466b09d1a49575d27`.
These corpus hashes detect accidental byte-order, field-order, or width changes.

The codec TDD RED build failed because the new constants and overloads did not
exist. GREEN is 20/20 for the full host suite and 3/3 for the focused
ASan/UBSan contract, codec, and layout suite. Maximum-capacity encode/decode
fixtures pass rather than relying only on small examples.

The macOS Release benchmark is development evidence, not capacity acceptance:

```text
maximum executable encode p50/p99   625 ns / 834 ns
maximum executable decode p50/p99   166 ns / 209 ns
```

The Raspberry Pi 5 ARM64 gate passed source root
`5525e2002b564416dfca0405d91e8b1080b6dbba394ad9be7cf2c14afaf9fce1`
on 2026-09-07. The expected and observed roots matched, the compiled and
runtime cache-line values were both 64 bytes, power was `throttled=0x0` before
and after, and temperature rose from 44.1 C to 49.05 C.

Worst-case Pi Release codec measurements were:

```text
feature schema encode p50/p99       296 ns / 297 ns
feature schema decode p50/p99       240 ns / 241 ns
executable policy encode p50/p99  1,870 ns / 1,945 ns
executable policy decode p50/p99    889 ns / 908 ns
```

These establish ARM64 feasibility with substantial margin because artifact
encoding and decoding occur on bounded control-plane admission/recovery paths,
not once per market-data decision. NUC x86-64 is intentionally deferred until
the nine-phase implementation completes. Until that identical-source gate, the
counts and replacement state/snapshot ceilings remain provisional rather than
cross-platform accepted constants.

Both target wrappers now pin digest-aware source root
`fcba3fbd3ece8f67ea75d0fac0bf97354f66c47e09afc1d61cdce1a4e9d8a1fe`:

```bash
# Raspberry Pi 5
sudo -v
BUILD_JOBS=2 ./deploy/run-v2-executable-policy-pi-gate.sh --yes

# x86-64 NUC
BUILD_JOBS=2 ./deploy/run-v2-executable-policy-nuc-gate.sh --yes
```

They run the same Release, benchmark-instrumented, and ASan/UBSan suites and
the same worst-case codec benchmark. The Pi wrapper additionally rejects any
run whose firmware power state is not `throttled=0x0`; that firmware interface
does not exist on the NUC and is not fabricated there.

## Limitations and next gate

- Neither artifact is in `TradingState`.
- There is no propose/validate/activate/supersede command for the companion.
- Canonical codecs and self-digest verification exist, but the reducer does not
  yet invoke them. Lifecycle admission must enforce digest and cross-artifact
  binding atomically before staging.
- Rule evaluation does not yet select a template.
- The action resolver is not invoked by the authoritative reducer.
- No order, reservation, effect, adapter, or live-trading authority exists.

Next, implement the active/staged reducer lifecycle and its snapshot/replay
representation. That work will expose the exact lifecycle overhead needed to
replace the old state and snapshot ceilings without guessing.

## Artifact digest preimage decision

The approved digest rule is:

```text
SHA-256(type-specific domain separator || canonical authoritative fields)
```

The artifact's own digest field is the only omitted field. A feature schema
uses domain `velocita.feature-schema.v1`; an executable policy uses domain
`velocita.executable-policy.v1`. The executable preimage includes the bound
base-policy digest and feature-schema digest, all active templates, and every
active rule binding.

Implementation hashes canonical serialized fields rather than `sizeof(T)` raw
memory. That avoids compiler padding, byte-order, and ABI differences between
ARM64 and x86-64. Computation temporarily supplies a nonzero self digest only
to reuse structural encoding, then removes those exact 32 bytes from the
preimage. All other validation, including both bound digests, remains active.

Verification distinguishes two failures:

- `invalid_contract`: malformed or non-canonical content never reaches digest
  comparison; and
- `digest_mismatch`: structurally valid authoritative content differs from the
  claimed digest.

The digest golden corpora are:

```text
feature schema      95f7c0a99a1098eccff3652c9ce66404220fd5768f6b6febbc3bf4191a9bb690
executable policy   497298b19ad71de9519835f249fe1e68e3c386f87c36c774998805b2e4885dd6
```

The TDD RED build failed on the absent `artifact_digest.hpp`. GREEN is 21/21
for the full host suite and 3/3 for focused ASan/UBSan tests. Tests prove that
changing the claimed self digest leaves the recomputed value unchanged, while
valid changes to feature conversions, bound policy digests, safety caps, or
rule bindings produce a mismatch. Invalid bindings remain contract failures.

The new target source root is
`fcba3fbd3ece8f67ea75d0fac0bf97354f66c47e09afc1d61cdce1a4e9d8a1fe`.
It supersedes the earlier codec-only root for subsequent Pi/NUC gates. On the
macOS development host, maximum executable-artifact digest construction was
3,459 ns p50 and 4,750 ns p99. This remains control-plane evidence, not a
market-data hot-path or throughput claim.

The digest-aware Raspberry Pi gate passed that exact source root on 2026-09-07.
Expected and observed roots matched; compiled and runtime cache-line values
were 64 bytes; power remained `throttled=0x0`; and temperature rose from
44.65 C to 50.7 C. The 100,000-iteration worst-case measurements were:

```text
feature schema encode p50/p99                 296 ns /    297 ns
feature schema decode p50/p99                 240 ns /    241 ns
executable policy encode p50/p99            1,834 ns /  2,000 ns
executable policy decode p50/p99              889 ns /    908 ns
feature schema artifact digest p50/p99      3,852 ns /  3,907 ns
executable policy artifact digest p50/p99  14,204 ns / 16,592 ns
```

This accepts the complete Phase 3D contract/codec/digest slice on Raspberry Pi
ARM64. It does not promote the provisional cross-platform capacities or state
ceiling before the deferred NUC gate and exact lifecycle snapshot layout.
