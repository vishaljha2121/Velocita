# CP-024 capacity diagnostics and proposed layout — 2026-09-23

Dirty branch v2/phase-5-corrections, base HEAD d44a4a9. Production unchanged from
CP-023. These are original behavioral REDs against UNAPPROVED candidate limits,
not evidence that an already accepted numeric contract was broken. No GREEN
implementation or target acceptance is claimed. No test was removed from CTest.

Permanent diagnostic source: `../../../tests/v2_outbound_log_capacity_red.cpp`.
It is deliberately outside CMake/CTest pending owner review. The three selectors
exit1 at their intended capacity assertions; all earlier fixture invariants pass:

- responses:512 truthful replies accumulated; exceeds candidate64.
- payload:129 descriptors,542604 encoded bytes for largest peer; exceeds256 KiB.
  This counts encoded contents, not allocator overhead or total process memory.
- log:8129 ordinary retained entries with previously drained results; exceeds
  candidate8128. One ready result at the final observation is expected.

The original stdout/stderr is retained in the three `*-red.txt` files.
No source guard was removed to create these failures. Exact candidate assertions
will need adjustment after quota/protocol approval; preserve these original logs.

## Reproduce

From repo root, build before linking or running:

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j8
c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Itests -Ibuild/_deps/willemt_raft-src/include \
  tests/v2_outbound_log_capacity_red.cpp \
  -L/private/tmp/velocita-full-audit.lYB4P5/release \
  -lvelocita_v2_replication -lvelocita_v2_kernel -lvelocita_v2_evaluator \
  -lvelocita_v2_contracts -lvelocita_core -lwillemt_raft -pthread \
  -o /private/tmp/f5-queue-review.hDdB4e/capacity-red
CAPACITY_REVIEW_CASE=responses /private/tmp/f5-queue-review.hDdB4e/capacity-red
CAPACITY_REVIEW_CASE=payload /private/tmp/f5-queue-review.hDdB4e/capacity-red
CAPACITY_REVIEW_CASE=log /private/tmp/f5-queue-review.hDdB4e/capacity-red
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
```

Paths above are actual run provenance, not durable build-directory requirements.
Create your own temporary build/output directory when reproducing elsewhere.
The diagnostic creates its own unique temporary fixture directory; failure leaves
that fixture available for inspection. No repository data is deleted.

Fresh full Release:54/54,5.87 seconds. Build log retained. CTest's raw per-test log
at run time: `/private/tmp/velocita-full-audit.lYB4P5/release/Testing/Temporary/LastTest.log`.
ASan+UBSan54/54 and tooling40/40 remain CP-023 historical, not rerun for this
test/document-only review. Model109/109 remains CP-020 historical. No mutation,
new sanitizer, target, latency, TSan or power-loss claim in CP-024.

## Native PROTOTYPE measurement, not production layout

```sh
c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Ibuild/_deps/willemt_raft-src/include \
  docs/evidence/f5_capacity_review_2026-09-23/layout-proposal.cpp \
  -o /private/tmp/f5-queue-review.hDdB4e/layout-proposal
/private/tmp/f5-queue-review.hDdB4e/layout-proposal
```

Observed host values: peer_mailboxes33336; log_descriptor40; raft_entry32;
payload_cells16777216; log_descriptors327680; raft_ring_steady327680;
raft_ring_resize_peak491520. The probe includes real ResultHandle/raft_entry_t,
but mailbox/descriptor declarations are PROPOSED scaffolding, not shipping types.
Allocator headers, codec/WAL scratch, network buffers and full process memory
are excluded. No portable byte contract follows from native sizeof.

Main reviewed two read-only bounded worker analyses and independently checked
the code, arithmetic, diagnostic failures, native probe and regression. Main
recommends retaining existing Raft allocation for MVP rather than expanding the
fork; see `../../F5_OUTBOUND_LOG_CAPACITY_REVIEW.md` for alternatives and limits.
