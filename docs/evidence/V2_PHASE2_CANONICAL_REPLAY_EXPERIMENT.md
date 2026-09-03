# V2 Phase 2 canonical codec and replay experiment evidence

Date: 2026-09-03

- Command codec commit: `a04094e`
- Replay-equivalence commit: `0a20e4f`
- Decision: retain as a green experimental increment; physical Pi acceptance
  is pending

## Scope

This increment adds canonical versioned byte frames for all eight current
kernel command alternatives and for `ApplyResult`, plus a byte-level replay
adapter. It proves a fixed corpus produces identical canonical suffix results
and final snapshot whether applied continuously or restored from a midpoint
snapshot and replayed from the suffix.

It does not add durable files, Raft integration, compaction, order state, or
external execution authority.

## Frozen format and identity evidence

- Maximum command frame: 608 bytes
- Fixed result frame: 110 bytes
- Final replay-corpus state digest:
  `596ec54fb24f55b91579330e08cefa63b3af8d39c1e277cd835c5af4e1b566c8`
- Command corpus hash:
  `8d08b0a23e6918b4bc8ec6a373a7e6b8a7e9d1128b394254ee757fd94894a5db`
- Result corpus hash:
  `0d592606abcfb67db92426ffd8e661c6ffa83563aa27d1a9d9fd11968a2b9ff4`

## Host evidence

- 12/12 full Release suites passed.
- The focused replay suite passed under ASan/UBSan.
- No V1 wire or production runtime path was changed.

## ARM64 development evidence

Evidence directory:

```text
~/.local/share/velocita/v2-phase2-container-gates/20260903T215310Z
```

Environment and identity:

```text
machine=aarch64
distribution=Ubuntu 26.04 LTS
compiler=c++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
source_head=0a20e4fcf8ebe8e75fb34f8fd4221beaade08480
source_root=810f8c4db00d6ae37061694ab3be6f775b3312f0b837c09b00c589903d5e04aa
```

Results:

- Release: 12/12 passed.
- benchmark-instrumented V2: 9/9 passed.
- ASan/UBSan V2: 9/9 passed.

This is compatibility evidence in a constrained ARM64 Linux container. It is
not physical Pi, durability, or capacity evidence.

## Physical Pi acceptance command

After syncing this committed source to the Pi:

```bash
BUILD_JOBS=2 ./deploy/run-v2-canonical-replay-pi-gate.sh --yes
```

The wrapper rejects any source tree whose canonical root differs from
`810f8c4db00d6ae37061694ab3be6f775b3312f0b837c09b00c589903d5e04aa`.

## Remaining risks

- The corpus is deterministic but small; generated model-based replay remains.
- Snapshot persistence and crash-safe selection do not exist.
- V2 frames are not yet integrated into Raft/WAL transport.
- Receipt expiry and compaction policy remain undefined.
- Phase 2 remains experimental and cannot authorize trading.

The detailed educational treatment is in
[`V2_CANONICAL_CODEC_REPLAY_GUIDE.md`](../V2_CANONICAL_CODEC_REPLAY_GUIDE.md).
