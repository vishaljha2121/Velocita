# V2 Phase 1 macOS candidate evidence

- Date: 2026-08-30
- Decision: candidate retained; not accepted or tagged
- Branch: `v2/phase-1-domain-contracts`
- Contract commit: `a6ee8df5870a`
- Host: macOS 26.5 ARM64
- Compiler: Apple Clang 21.0.0
- Cross-platform gates: native ARM64 Ubuntu container passed; Raspberry Pi
  ARM64 pending

## Objective and business reason

Phase 1 creates the deterministic identities, numeric domains, bounded trading
values, and canonical bytes that later Decision/OMS, Raft, storage, outbox, and
gateway phases share. Freezing these contracts early reduces the risk of an
identity, rounding, padding, endianness, or schema ambiguity becoming persisted
economic state.

This phase does not evaluate a strategy, reserve risk, create an order, append a
WAL record, run consensus, or contact a venue.

## TDD evidence

Each increment was observed RED before implementation:

1. the initial V2 target failed because `src/v2_wire.cpp` did not exist;
2. value-contract compilation failed because `velocita/v2/contracts.hpp` did
   not exist;
3. message-registry compilation failed on the missing kinds and checksum API;
4. payload-codec configuration failed because `src/v2_payload.cpp` did not
   exist; and
5. digest configuration failed because `src/v2_digest.cpp` did not exist.

The policy golden test also failed once because its checked-in hex literal had
two excess zero bytes. The literal was corrected against an independently
generated 144-byte fixture; the assertion was not weakened.

## Correctness tests

Debug full regression command:

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Result: 6/6 pass:

- `core-tests`
- `metrics-tests`
- `benchmark-tests`
- `v2-contract-tests`
- `v2-value-contract-tests`
- `v2-payload-codec-tests`

Release V2 result: 3/3 contract suites pass.

ASan/UBSan V2 result: 3/3 contract suites pass with
`-fsanitize=address,undefined -fno-omit-frame-pointer`.

The canonical one-rule policy is exactly 144 bytes with SHA-256:

```text
7cd115af41936eaaabbf53660d86e54d27c288c09f76f98efc1c2c75dd48ef30
```

Known-answer checks also cover CRC-32 of `123456789`, SHA-256 of the empty
message, and SHA-256 of `abc`.

## Release codec measurement

Command:

```sh
./build-v2-release/v2-contract-benchmark
```

Workload: 100,000 iterations over a maximum 32-rule policy. Observed sample:

```text
policy_encode iterations=100000 min_ns=208 p50_ns=250 p99_ns=334 max_ns=14916
policy_decode iterations=100000 min_ns=0 p50_ns=42 p99_ns=84 max_ns=6250
policy_sha256 iterations=100000 min_ns=1125 p50_ns=1208 p99_ns=1708 max_ns=18583
```

The zero minimum decode sample reflects timer granularity and is not a claim of
zero work.

Encoded-size ceilings:

| Value | Maximum bytes |
|---|---:|
| policy payload | 516 |
| decision-frame payload | 381 |
| order-effect payload | 165 |
| venue-observation payload | 116 |
| envelope with maximum generic payload | 65,620 |

Code-audited allocation upper bounds per call under the current implementation:

| Operation | Heap allocations at most |
|---|---:|
| payload encode | 1 |
| payload decode into fixed-capacity value | 0 |
| envelope encode | 1 |
| envelope decode with owned non-empty payload | 1 |

Encoder vectors reserve their validated final size before append. Payload
decoders return fixed-capacity values. The generic envelope decoder deliberately
owns its bounded payload; later owner-loop integration must decide whether to
retain this allocation off the Raft apply path or introduce a caller-owned
buffer/view API.

## C++ and failure review

- Strong IDs are trivially copyable wrappers and do not add runtime storage.
- Fixed-point operations are `constexpr`, checked before signed arithmetic, and
  return `std::optional` on overflow.
- Runtime structs are never copied directly to bytes; codecs write each field
  explicitly in little-endian order.
- Decoder count and length checks precede iteration or payload allocation.
- Envelope CRC is verified before copying the owned payload.
- Decoding constructs a temporary value and returns no value on failure, so no
  caller state is partially mutated.
- `std::array` fixes storage bounds and ownership but increases constant stack/
  object size; cache behavior must be measured when Phase 2 introduces the
  reducer.
- SHA-256 and CRC provide identity/corruption checks, not sender authentication.

## Limitations and remaining gate

These codec timings are not Raft throughput, end-to-end decision latency, WAL
performance, venue latency, or a production SLO. The benchmark ran on one macOS
host and has no statistical repetition or environmental controls suitable for a
capacity claim.

Before Phase 1 acceptance, Raspberry Pi ARM64 must build the exact commit and
prove:

1. all six tests pass;
2. Release V2 tests pass;
3. the policy golden SHA-256 is identical;
4. fixed-point compile-time/runtime results are identical;
5. malformed-input classifications remain identical; and
6. the target benchmark output and host power/thermal state are retained as
   evidence, without treating its timings as system throughput.

After synchronizing the committed source tree to the Pi, the target gate is one
command (run `sudo -v` first so voltage state is readable):

```sh
./deploy/run-v2-phase1-gate.sh --yes
```

The script does not stop, retag, or restart the running V1/V1.1 cluster. It
stores timestamped target evidence under
`~/.local/share/velocita/v2-phase1-gates/`. Because the normal deployment
sync excludes `.git`, the script also rejects any V2 contract source tree whose
content root differs from
`6eafe3ffe4b78819682517e78846e8b997cc8e7adb4735cfd222397a99b52640`.

## Rollback

V1 and V1.1 code paths are unchanged. Revert commit `a6ee8df5870a` or switch to
the preserved V1/V1.1 tag or branch. No existing wire/storage version is
reinterpreted and no prior evidence is deleted.
