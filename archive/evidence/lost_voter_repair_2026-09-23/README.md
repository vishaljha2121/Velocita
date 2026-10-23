# Lost-voter repair: current-API reachability, not a repair GREEN

2026-09-23. CP-028 baseline, branch `v2/phase-5-corrections`, HEAD `d44a4a9`.
Main authorized this bounded parallel review lane. Production/shared files,
protected paths and build configuration were not edited. No remote/device,
credential, network, staging, commit or deployment action was taken.

Review: `../../F5_LOST_VOTER_REPAIR_REVIEW.md`, **PROPOSED**. Choices A/B remain
approved; new repair admission/local bytes and fencing protocol are not encoded.

## Actual result

`../../../tests/lost_voter_repair_reachability.cpp` compiles and passes, exit 0.
It is intentionally outside CTest. Raw stdout/stderr is retained in `trace.txt`.
No repair entrypoint exists; therefore this is a successful characterization of
the open capability and existing safety, not an original behavioral repair RED.
Do not weaken `recover()` to make a purported availability test green.

Two separate two-voter fixtures use the real pinned Raft callbacks, durable WAL,
bound identity/metadata and normal peer codec. They create the same authored first
command: caller 7, sequence 1, payload tag 0x31, `AdvanceKillEpoch(7)`. Only the
old leader's local WAL flush is varied. Both route the real append/response path
and withhold the later leader-commit announcement from the donor.

- With leader flush: leader durable/commit/applied 1, explicit state version 1
  and kill epoch 7, result `applied`. Donor durable 1, commit/applied 0.
- Without leader flush: leader durable/commit/applied 0, state version/kill epoch 0,
  no committed result. Donor durable 1, commit/applied 0.
- The complete named-file images in the two donor directories are BYTE-IDENTICAL.
  Donor metadata is term 1, voted-for 1, commit 0 in both. Each contains the exact
  authored canonical command at WAL index/term 1/1.
- Stop both owners and move the lost leader's metadata out of its directory to a
  quarantine file, modeling unavailable voting memory while retaining evidence.
  Ordinary recovery returns exact `BoundMetadataErrorKind::absent`; the failed
  voter's remaining named bytes are unchanged.
- Intact donor recovery succeeds with durable 1, commit/applied 0, one RESERVED
  result, no READY result and no authority proof. Its complete files are unchanged.

This proves a complete retained suffix above the donor's commit checkpoint can
include an old quorum-committed command, and identical donor bytes can also arise
without that old commitment. A copy operation therefore cannot infer commitment
or safe deletion from donor C. The future repair must preserve the suffix and
establish renewed quorum commitment; it may legitimately commit a formerly
uncommitted admitted entry then.

The oracle uses independently authored numeric expectations and exact command
payload equality. It does not call production logic to decide the expected
commitment/result. It does use the actual command encoder for transport bytes;
this is not an independent proof of canonical encoding or reducer economics.
The fixtures are laboratory voters in one process, not host-failure tolerance.

## Build provenance and reproduction

No shared CMake build was run by this lane. Direct compilation linked the known
fresh CP-028 Release libraries whose main build is recorded in
`../f5_archive_codec_2026-09-23/README.md`. The replication archive was last built
2026-09-23 13:11:40 local time; `src/v2_replication.cpp` was 11:20:05 and its header
11:07:13. A read-only dependency check additionally inspected all 40 object
dependency files across the six linked archives: 89 repository dependencies,
NONE newer than its consuming object. Timestamps are a freshness check, not a
content-addressed reproducible-build proof. The CP-028 build record, complete
current dependency hashes and exact library hashes are retained together.

- `provenance.txt`: freshness counts and SHA-256 of all six linked archives.
- `linked-source-files.sha256`: all 89 current repository dependency hashes.
  Its SHA-256 is `20da213b06d3681bb4388cfbc1196846e679893454c1b058b166136abae595ef`.
- New test SHA-256:
  `2a2099c3154096884cbb1206e1762508bda04ed9824d408124560943c6d7ba33`.
- Executable SHA-256:
  `20522cbe0e38cfef157d245e5af1e81a3cb53a3fa3fca0ab7d6bee5ac1e1c7d9`.

Compiler: Apple clang 21.0.0 (`clang-2100.1.1.101`), arm64-apple-darwin25.6.0.
The direct fixture is C++20/O1/NDEBUG and cache-line ABI 64; linked libraries use
CP-028 Release/O3/NDEBUG and the same ABI, not sanitizer instrumentation.

```sh
cd /Users/vishaljha/aeron-raft-pi
c++ -std=c++20 -O1 -DNDEBUG -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Itests -Ibuild/_deps/willemt_raft-src/include \
  tests/lost_voter_repair_reachability.cpp \
  -L/private/tmp/velocita-full-audit.lYB4P5/release \
  -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator \
  -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread \
  -o /private/tmp/lost_voter_repair_reachability_cp028

# Parent was created with mktemp -d; the child must not exist.
/private/tmp/lost_voter_repair_reachability_cp028 \
  /private/tmp/lost_voter_repair.9dfDRP/trace
```

That exact fixture path now exists and is preserved; choose a NEW `mktemp -d`
parent plus a nonexistent child for reproduction. The test refuses existing
roots and never recursively erases a prior fixture. Compile exit 0; run exit 0.
An initial compile typo used `request_fingerprint` instead of `payload_digest`;
it was corrected before the first run and is not a production RED claim.

`git diff --check` passed. No new whole-suite, sanitizer, mutation, latency,
power-loss, cross-host transport-fence or end-to-end repair result is claimed.
No legacy target result is relabeled as current F5/F6/F7 acceptance.
