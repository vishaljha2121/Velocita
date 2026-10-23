# CP-025 — approved outbound/log bounds integrated

2026-09-23, dirty branch v2/phase-5-corrections, base HEAD d44a4a9. Owner approved
all four decisions in F5_OUTBOUND_LOG_CAPACITY_REVIEW after CP-024. No commits,
tags, source freeze, target/venue/credential action. Protected paths untouched.

## Delivered ownership

- Fixed four-kind mailboxes per remote peer replace the unbounded outbound deque.
  Same-kind whole-frame coalescing, persistent lower-term watermark and fair
  draining; failed undersized copy-out leaves ownership intact. No response-field
  merging. Old Raft packets may be superseded, never trading outbox effects.
- Kernel callbacks encode borrowed bounded entry views to owned bytes; public
  egress is bounded copy-out. The old dynamic take API is removed from V2; dynamic
  decoding remains only in test adapters. V1's take API and pinned Raft unchanged.
  Unexpected encoding failures latch a node fault; owner/before_apply boundaries
  deny service/authority. Static review covers this latch; no claim that every
  impossible internal corruption branch has an independent injected-fault test.
- RetainedLog replaces the map/vector payload owner:8192 stable2048-byte cells,
  8128 ordinary/64 protected tokens. Operator command classifier unchanged;
  markers ordinary. Actual pool tags and full index guards survive modulo reuse.
  New local ProposeStatus::log_capacity_exhausted appended at8; no wire value.
- Local commands/markers and whole follower batches preflight log AND result
  capacity. Conflicting uncommitted suffixes contribute hypothetical ACTUAL pool
  credits; only successful durable deletion releases ownership. Log pressure uses
  the already-approved term/prev-only follower fallback, not a false ack.
  WAL staging remains separately bounded and fallible; this is not a new atomic
  disk-write or all-resources rollback guarantee under I/O failure.
- Startup scans every record's bindings/order, then streams the verified suffix
  into bounded ownership before writable WalSegment creation. Committed records
  count against log limits but emit no replay notifications. Oversized committed
  commands and uncommitted markers refuse with unchanged directory bytes.
- WalSegment constructor/rewrite no longer accumulate whole-history vectors.
  Rewrite validates/detects change, then streams survivors from the existing fd
  into a replacement; sync/rename/directory-sync checkpoint order is preserved.
  Dynamic readers remain explicitly diagnostic/test APIs. At most one decoded
  block is retained by the scanner; per-record codec allocations remain bounded.
  Repeated tail rewrites can still be quadratic I/O: no latency improvement claim.

No automatic compaction, archive policy or disk-retention limit was introduced.
Applying/draining does NOT reclaim log cells. Fullness can persist until safe
reclamation exists;64 control slots cannot guarantee quorum/emergency completion
or uninterrupted ordinary fill ingestion. Full F5/F6/F7 and live admission OPEN.

## Evidence and corrections

Main rebuilt CP-024 production before rerunning its three original behavioral
REDs:512 replies,129 packets/542604 bytes,ordinary log8129 beyond8128. Original
logs remain in ../f5_capacity_review_2026-09-23/. Current tests strengthen the
outbound expectation to ONE frame per peer/kind; all three paths are now GREEN.

New component workers initially reported missing-header/source compile REDs.
Those are NOT behavioral RED evidence. Worker WAL RED linked build/ archives
without a fresh-build proof, so it is NOT credited as verified-current original
TDD evidence either. Main's current-source materialization mutation below provides
fresh behavioral sensitivity without retroactively relabeling it as original RED.
Main final gates rebuilt every C++ implementation; worker partial-instrumentation
runs are not substitutes for them.

Permanent coverage: same-term success/failure exact fields; lower terms even after
drain; cross-kind independence; refill fairness; invalid/undersized no-consume;
borrowed input mutation and input/output aliasing; allocation-free mailbox paths;
actual Raft periodic recovery after dropped coalesced packets; queued bytes survive
durable suffix replacement; leader/follower exact saturation/control reserve;
whole mixed-batch refusal; markers ordinary; full-log restart; unchanged oversized
startup; saturated marker-suffix replacement and existing failed-second-rewrite
surviving log/result counts. Streaming tests check canonical surviving bytes after
suffix/prefix/no-op/empty rewrites and maximum single allocation, NOT total RSS.

Independent mutation checks (all exit1 at the named intended assertion):

| Mutation | Observable failing assertion | Log |
|---|---|---|
| Ordinary entries may use protected tokens | ordinary entry borrowed protected capacity | pool-mutant.txt |
| Released protected suffix credited as ordinary | ordinary plan borrowed hypothetically released protected token | credit-mutant.txt |
| Free suffix ownership before durable truncation | failed durable truncation released a surviving result reservation | early-release-mutant.txt |
| Constructor materializes full history again | WAL constructor retained whole history in one growing allocation | wal-materialize-mutant.txt |
| Remove lower-term comparison | lower term | mailbox-term-red.txt |
| Stop advancing fair cursor | starvation | mailbox-fair-red.txt |
| Disable occupied-slot replacement accounting | same-term whole replacement | mailbox-coalesce-red.txt |
| Zero borrowed payload copying | AE golden bytes | mailbox-owned-bytes-red.txt |
| Remove bounded inner staging | output/input alias | mailbox-alias-red.txt |

The last five were worker direct source-copy mutations reviewed by main. The
first four are main direct source-copy mutants linked against freshly built
Release archives. No mutant was applied to production files. Alias staging fixes
a defect found while reviewing the worker encoder; canonical bytes are unchanged.
First integrated regression failed because the new generic failure check obscured
the injected disk-error cause. Main moved that check AFTER explicit Raft error
reporting, still BEFORE apply/authority work; the original assertion was preserved.

The proposal's small-frame sizes were4 bytes low: overhead88 means RV136,
RVR113,AER129,empty AE140. All still fit160; no contract capacity changed.
The prototype's per-peer estimate was not a shipping node footprint. Actual owner
has two fixed peer slot sets regardless voter_count:66680 bytes even on two voters.

## Final host verification

- Fresh Release59/59,14.07 seconds; separate registered outbound/log scenarios.
- Fresh Debug ASan+UBSan59/59,157.18 seconds. C++ compiled with
  -fsanitize=address,undefined -fno-sanitize-recover=undefined
  -fno-omit-frame-pointer; ASAN_OPTIONS=detect_leaks=0,
  UBSAN_OPTIONS=halt_on_error=1. Pinned Raft C remains UNINSTRUMENTED.
- Tooling40/40. Model109/109 remains historical CP-020; not rerun or modified.
- Independently maintained gate inventory59 host/56 V2 matches CTest JSON.
  git diff --check clean. No new TSan, target, power-loss or latency evidence.

Build/run reproduction from repo root:

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake -S . -B /private/tmp/aeron-phase67-audit-build -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer'
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Actual temporary mutation/work directory: /private/tmp/f5-capacity-integration.Q49Pji.
Main mutants compile directly with C++20,-O1,cacheline64,include/tests and pinned
Raft include; substitute mutant source for its translation unit and link fresh
Release archives. For pool/credit mutants only retained-log tests+mutant are
needed. For early-release use v2_result_admission_edges.cpp; materialization uses
v2_wal_streaming_tests.cpp. No CMake mutation cache is trusted.

## Measured native backing (host, cacheline64)

`layout.cpp` probes actual shipping declarations: ReplicatedKernel inline91424;
OutboundMailboxes66680; RetainedLog owner64 plus16777216 payload bytes plus327680
descriptor bytes; ResultLedger229400; raft_entry_t32. Descriptor40 bytes:
index offset0/8 bytes,length8/2,pool10/1,padding11..15,result optional16/24.
No native padding is serialized. Current library ring reaches10240 entries:
327680 steady bytes,491520 old+new transient resize peak. These are component
subtotals, excluding allocator overhead, WAL/scratch/network buffers and other
owners—not a process RSS ceiling. Future transport egress buffers are additional.
