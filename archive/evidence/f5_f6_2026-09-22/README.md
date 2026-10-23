# F5/F6 first parallel increment — CP-013, 2026-09-22

Base HEAD d44a4a9, branch v2/phase-5-corrections, uncommitted remediation.
This is host correction/review evidence, not F5/F6 closure or target acceptance.
No new canonical bytes, economic schema, membership/config format, source pin,
hardware/venue contact, credentials, commit or tag. Protected work is preserved.

## F5: bound snapshot input before allocation

`SnapshotStore::load` previously constructed an unbounded string and then a second
file-sized vector before checking the canonical snapshot. The permanent test saves
a valid snapshot, verifies a positive round trip, extends the disposable file to
2 MiB, and measures allocations only while loading it. Original RED:

`snapshot loader allocated beyond the canonical file bound before refusal`

Smallest GREEN: a fixed buffer of outer-header34 + canonical-maximum65483 + CRC4
+ one lookahead byte = **65,522 bytes**. Bounded read avoids file-size-query races
and detects oversized/trailing data without allocating in proportion to the file.
The little-endian reader now takes a span rather than requiring a vector.
This is cold recovery stack storage, not additional TradingState/snapshot bytes.
The allocation probe measures heap requests; it does not measure the whole
process's stack/RSS. Stream infrastructure still allocates its ordinary buffers.

Wrong magic retains the existing corruption result. Unsupported outer framing
still refuses. For oversized payloads, the canonical decoder's schema-first
prefix check preserves future-schema refusal instead of silently allowing WAL
fallback. Current-version oversized corruption remains unavailable as a snapshot.
Existing future-schema/checksum/replay tests remain in the full regression.
The source-copy mutant replacing the fixed buffer with file-sized storage fails
the same allocation assertion. No new guessed capacity or wire format is involved.

## F6: stop service after ambiguous persistence failure

Permanent cases extend v2_authority_storage_failure_tests.cpp:

| Path | Reproduced or tested property | Correction |
|---|---|---|
| WAL flush write/sync exception | Old process remained a serving leader | Latch unusable; public flush checks usability before further work |
| Term persistence exception | Follow-on authority operation still ran | Latch unusable; publish metadata cache only after successful save |
| Vote persistence exception | Dedicated latch-only mutant lets authority calls continue | Same transactional cache/latch rule |
| Commit-checkpoint exception | Old process still reported serving leadership | Save a candidate tuple, publish cache on success, latch failure |
| Failure after metadata rename | Named file exposes complete new tuple `(1,-1,0)` while callback fails | Node must refuse further protocol/authority service regardless of visible tuple |
| Election self-vote failure | Pinned Raft ignores `raft_vote` return; owner tick returned success | Recheck the failure latch after raft_periodic even when its return code is zero |

The pinned source calls `raft_vote(me_, me->node)` without checking its result
in raft_become_candidate. Main added a separate RED before the final owner guard:
`self-vote persistence failure was swallowed by owner tick`. This is not a new
Raft byte/protocol decision: callback failure must already make this owner unusable.
No mutation of the pinned dependency was needed.

Term/vote/commit now persist a copied candidate metadata tuple and update the
owner cache only after save returns. This prevents a failed save from making a
later equality check look already persisted. Disk may nevertheless contain new
bytes if failure occurs after rename; latching stops the process from interpreting
that ambiguity as permission to continue. Restart/recovery decides what is usable.
The post-rename test establishes named-file visibility, NOT power-loss durability.

The optional MetadataCheckpoints constructor argument is default-inert and
test-only. Existing format, normal syscall sequence and V1 remain unchanged.
Public outbound/proof/admission paths already consult require_usable; tests
check non-serving leadership, follow-on authority refusal and outbound refusal.
Diagnostic raw Raft role/state is not itself live authority. Stop/restart cleanup
does not grant new work. There is no blanket claim for all storage/allocation paths.

## RED and mutation provenance

- Snapshot allocation and self-vote owner-reporting REDs were captured directly
  by main before their fixes; raw text is retained here.
- Agent flush, term and commit tests ran RED before their fixes, but outputs were
  console-only, despite the request to retain logs. Main reran the retained old
  binaries into `*-old-binary-red.txt`. These are fresh captures of the earlier
  binaries, not invented original log files or a source-freeze claim.
- Vote-specific and post-rename tests were added after the metadata fix. Do not
  describe them as original RED-first runs. Vote has an independently killed
  latch-only source mutant; post-rename is positive fault coverage.
- Main independently rebuilt four CURRENT-source copies with only the flush,
  term, vote or commit failure latch removed. Each selected case exits1 at its
  matching service-after-failure assertion. The mutation `.patch` files and
  outputs are retained. Mutants never edited shared production source.
- Self-vote pre-fix source differs from the fix by the post-periodic latch check;
  snapshot allocation mutation changes only the buffer allocation policy.
- Unknown F6_CASE selectors fail, rather than silently running zero tests.

## F5 obligations reproduced, deliberately still RED

The standalone tests/v2_f5_recovery_red.cpp is NOT registered as a passing CTest
suite. It uses public committed-command fixtures and retains the following:

| Mode | Observed current-tree result | Meaning |
|---|---|---|
| membership | Existing three-voter directory reopened with one voter becomes leader | No durable cluster/membership binding; A04 open |
| replacement | Leader compacted through12; fresh configured voter still applied0 | Local restart is not peer snapshot catch-up; A08 open |
| archive | Audited command at3 covered by compact4 disappears from both WAL formats | No archive handoff exists; A14 open |

The archive test proves the exact canonical record was durable before compaction
and covered by the returned compact index; it checks both segment and legacy WAL.
Absence of an archive interface is separately source-traced, not asserted by a
fake archive oracle. A current-state snapshot is not the command-history archive.
These diagnostics will be converted into contract-specific refusal/convergence/
archive assertions when the reviewed interfaces exist; an incidental constructor
exception must not be mistaken for the intended fixed behavior.

One diagnostic link attempt met the concurrent constructor-signature change while
using old archives. It was a build/setup failure, not a RED. Main rebuilt the tree
and linked consistently before capturing the three intended failures above.

## Remaining design and approval boundaries

Read ../../F5_F6_RECOVERY_DURABILITY_REVIEW.md and proposed ADR0014. Identity/
domain, capacity/full-policy dimensions, archive/durability policy and snapshot
install bytes are not encoded. Initial node defaults and the single-voter lab
API were NOT opportunistically changed. Whole-snapshot transfer is not inherently
unsafe if its explicit budget/work isolation holds; chunking is a scheduling and
transport choice. F4 markers remain ordinary work outside the emergency reserve.

F6 remains open for sync-sensitive power-loss modeling, the original two-slot
versus atomic-replace plan decision, asynchronous metadata fencing and owner-gap
measurement. Current background WalWriter calls poll without exception capture:
a thrown write/sync error terminates its thread/process rather than publishing an
owner completion/error. That source-traced fail-stop/diagnostic gap was not fixed
or runtime-proven in this increment. No claim of volatile acknowledgement follows
from it. log_offer and compaction/error ordering still need scoped coverage.

## Verification

Final rebuilt host Release **43/43**, Debug ASan+UBSan **43/43**; runtime UB halt,
leaks disabled, existing C++ instrumentation (not newly instrumented Raft C).
Offline tools **40/40**, passive model **109/109**, bytecode writes disabled.
Independent gate inventory **43 total /40 V2** includes the new bounds suite.
Agent work and final source were frozen before full regression. No deployment
acceptance; NUC/Pi gates and mandatory observability remain in the roadmap.
