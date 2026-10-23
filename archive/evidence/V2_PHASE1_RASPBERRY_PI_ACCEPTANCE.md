# V2 Phase 1 Raspberry Pi acceptance evidence

- Date: 2026-09-03
- Decision: Phase 1 domain and byte contracts accepted on Raspberry Pi ARM64
- Original contract commit: `a6ee8df5870a`
- Original ARM64 candidate evidence commit: `e1bd0f0`
- Combined source-admission behavior commit: `33d27f6`
- Pi-gate implementation commit: `b5ca06d`
- Tested source root:
  `0282782ed53a1727a92acaf6c830f5a0651554e40cc70861d8eb15d7373f32b8`
- Raw Pi evidence: `20260903T192942Z`

## Why the combined gate is valid Phase 1 evidence

The successful physical-Pi run used the later combined V2 gate rather than the
obsolete host-native Phase 1 script. The Phase 1 contract headers, codecs, and
their three test suites were unchanged between the retained Phase 1 ARM64
candidate and the tested source. Git comparison produced no changed paths for:

```text
include/velocita/v2/domain.hpp
include/velocita/v2/contracts.hpp
include/velocita/v2/wire.hpp
include/velocita/v2/payload.hpp
src/v2_digest.cpp
src/v2_wire.cpp
src/v2_payload.cpp
tests/v2_contract_tests.cpp
tests/v2_value_contract_tests.cpp
tests/v2_payload_codec_tests.cpp
```

The combined gate reran those exact tests in Release, benchmark-instrumented
Release, and ASan/UBSan Debug configurations on the physical Raspberry Pi's
ARM64 CPU. It additionally ran legacy and Phase 2 tests. Therefore the later
source root is a superset checkpoint without silently changing the Phase 1
contract under test.

## Environment and results

```text
physical host         Raspberry Pi 5, ARM64
container user space  Ubuntu 26.04 LTS
compiler              GCC 15.2.0
CPU governor          ondemand
power before          throttled=0x0
power after           throttled=0x0
outer result          PASS
container result      PASS
```

| Configuration | Combined result |
|---|---:|
| Release, legacy + V2 | 9/9 pass |
| Benchmark-instrumented Release, V2 | 6/6 pass |
| ASan + UBSan Debug, V2 | 6/6 pass |

The Phase 1 subset is the three contract/value/codec suites in each V2
configuration. Their golden bytes, malformed-input behavior, checked
fixed-point arithmetic, explicit endian codecs, and digest behavior all passed.

The raw evidence is retained on the Pi at:

```text
~/.local/share/velocita/v2-source-admission-pi-gates/20260903T192942Z
```

`source_head=unavailable` is expected because deployment excludes `.git`. The
gate matched `source_root` to `expected_source_root`; the canonical manifest is
the tested-byte identity.

## Measurement boundary

The codec microbenchmark executed on the Pi and reported approximately:

```text
policy encode p50   389 ns
policy decode p50   334 ns
policy SHA-256 p50  5,240 ns
```

These are warm in-process codec measurements, not Raft throughput, OMS
capacity, order latency, or an SLO. Passing on the Pi establishes compatibility
and the tested correctness properties; it does not establish production
performance.

## Acceptance statement

Phase 1's domain, validation, fixed-point, wire, payload, and digest contracts
are accepted for continued V2 development. Any future change to their
canonical bytes, schema meaning, scale, or identity semantics requires an
explicit compatibility decision and a new golden-corpus gate.
