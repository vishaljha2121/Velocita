# V2 Phase 2 canonical state-snapshot experiment evidence

- Date: 2026-09-03
- Decision: retain canonical snapshot/state-digest codec as an experimental
  Phase 2 increment
- Branch: `experiment/v2-phase2-kernel`
- Behavior commit: `865ec08`
- ARM64 development-gate result: PASS
- ARM64 source root:
  `17bf519d09f4b4e3f87fcf399d0fe18999b5358112736c38c2e7262be6afba75`
- Raspberry Pi checkpoint: pending

## Objective

Give the deterministic kernel a portable, bounded representation of its
authoritative state. The same logical state must produce the same bytes and
SHA-256 digest on macOS, ARM64 Linux, a future x86-64 NUC, and after process
restart. Invalid input must not partially replace live state.

This increment is a codec, not a persistence engine. It does not choose when
to snapshot, write a file, call `fsync`, compact a WAL, install a Raft
snapshot, or retire receipts.

## Reviewed byte contract

The frame is:

```text
offset  bytes  meaning
0       4      magic: "V2ST"
4       4      canonical payload length, unsigned little-endian
8       2      snapshot schema version, unsigned little-endian
10      N      canonical logical TradingState payload
10 + N  32     SHA-256(schema version || canonical payload)
```

The payload writes explicit-width integer fields in retained deterministic
order. It includes all currently authoritative policy records, source
cursors, source-event receipts, and request receipts. It encodes only occupied
array entries. Unused capacity, C++ padding, pointer values, allocator state,
metrics, and reconstructable scratch are excluded.

Schema version is part of the digest domain. An implementation must reject an
unknown schema rather than guessing that old bytes mean a new structure.

## Why raw `TradingState` memory is prohibited

`TradingState` is an in-process ownership layout, not a storage protocol.
Writing `sizeof(TradingState)` bytes would persist implementation accidents:

- `std::size_t` width and host byte order;
- compiler-inserted alignment padding;
- inactive `std::array` slots;
- future member reordering; and
- bytes that may never have had domain meaning.

Those details can differ across compilers and architectures. Canonical logical
encoding separates the stable state contract from the replaceable C++ memory
layout.

## Transactional decode contract

Decode receives both the destination and a caller-owned
`StateSnapshotScratch`. It clears and fills `scratch.candidate`, checks the
frame, bounds, schema, SHA-256, enum domains, contract validation, duplicate
identities, source cursor relationships, and exact payload consumption, then
assigns the candidate to the destination once.

```text
untrusted bytes
      |
      v
scratch.candidate -- validate everything --> destination = candidate
      |
      +---------------- any error ----------------> destination unchanged
```

This avoids hidden heap allocation and makes failure atomic at the API
boundary. `std::span` is a non-owning `(pointer, length)` view: the caller owns
the input/output lifetime, and the codec cannot grow the buffer behind the
caller's back. `std::array` gives fixed-capacity storage whose size is known at
compile time.

## TDD and validation evidence

The new test target was first observed failing because
`velocita/v2/state_snapshot.hpp` did not exist. The GREEN implementation then
passed the focused snapshot suite, the full host regression, and the host
ASan/UBSan V2 regression.

The committed source passed the pinned native ARM64 Ubuntu 26.04 development
gate:

| Configuration | Result |
|---|---:|
| Release, legacy + V2 | 10/10 pass |
| Benchmark-instrumented Release, V2 | 7/7 pass |
| ASan + UBSan Debug, V2 | 7/7 pass |

The retained run is `20260903T202323Z` under
`~/.local/share/velocita/v2-phase2-container-gates/`. It used GCC 15.2, four
ARM64 CPUs, 4 GiB memory, 256 MiB `/dev/shm`, and two build jobs.

Tests freeze and exercise:

1. magic, length, schema, payload, and digest placement;
2. the empty-state golden digest
   `0e737df191c265e7d2a4825c229ffe5220a4fc0e9a9a679731ac475ea5e44eb8`;
3. populated encode/decode/re-encode byte equality;
4. identical bytes despite different inactive array slots;
5. rejection of corruption, truncation, trailing bytes, bad magic, unknown
   schema, invalid enums, invalid in-memory counts, and small output buffers;
6. byte-equivalent preservation of the destination after every tested decode
   failure; and
7. exact encodability of the declared 45,595-byte maximum snapshot.

## C++ review

- `Writer` and `Reader` operate on bounded spans and explicit offsets.
- Integral encoding is little-endian and independent of object layout.
- Signed integers are reconstructed with `std::bit_cast`, preserving the bit
  pattern without implementation-defined signed conversion assumptions.
- The public functions are `noexcept`; their reachable implementation uses no
  allocation, exceptions, I/O, locks, clocks, or randomness.
- Decode reads into a distinct owned object; it never retains references into
  input bytes.
- Validation loops are bounded by compile-time capacities. Some duplicate
  checks are quadratic in count, but the maximum counts are small and fixed;
  this is deterministic work, not unbounded input complexity.
- The maximum snapshot is below 64 KiB, but callers should retain the buffer
  and scratch in long-lived owning objects rather than repeatedly placing
  large values on a constrained thread stack.

## Known limitations

- No snapshot file writer, atomic rename, durability fence, recovery selector,
  or Raft snapshot installation exists yet.
- SHA-256 detects identity/corruption; it does not authenticate an untrusted
  writer. Authentication belongs at the deployment/security boundary.
- The format covers only fields currently present in experimental
  `TradingState`; later order, risk, position, reconciliation, and outbox
  regions require an explicit schema evolution.
- Receipt/event retention and replicated expiry are still unresolved. The
  current maximum is a development bound, not production capacity sizing.
- There is no randomized command-stream replay model yet.
- Snapshot encode/decode latency and allocation instrumentation have not yet
  been added to the benchmark evidence; no performance claim is made.

## Acceptance boundary and next decision

This increment is retained as green experimental evidence, not complete Phase
2 acceptance and not authorization to trade. The next reducer increment should
add a deterministic replay corpus and prove:

```text
apply full command stream == restore snapshot + apply suffix
```

for canonical result bytes and final state digest. Physical Raspberry Pi
acceptance remains required for this exact source root before the checkpoint
is marked target-green.
