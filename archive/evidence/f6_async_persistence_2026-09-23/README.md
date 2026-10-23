# CP-043: nonblocking hard state and whole-suffix persistence

2026-09-23. Owner approved all five CP-042 choices before implementation.
Branch `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation uncommitted.
This is host implementation/evidence, NOT phase or deployment acceptance.

## What changed and why

With `start_writer()` active, term/vote/commit saves now run on the same serial
worker as WAL writes. Callbacks capture a desired metadata tuple separate from
the durable tuple. The owner pauses new Raft turns, apply, proposals, authority
and outbound draining while any prerequisite remains pending. Health and
completion polling remain available; existing READY results remain readable.
Election term reset and self vote can coalesce because neither intermediate
transition becomes externally visible. A durable acknowledgement still waits
for storage: responsiveness does not eliminate durable latency or quorum stalls.

The optional pinned `log_prepare_truncate` callback lets Raft select the exact
whole cut before any memory deletion. The owner drains staged/submitted WAL
asynchronously; the worker performs one replacement; exact completion permits
one replay of the owned input and one grant for that cut. Only then do existing
per-entry callbacks release log cells and RESERVED results. Both Raft conflict
branches retain their original response semantics. Replaying a campaign is NOT
used: that would risk advancing the term twice.

The native receive wrapper distinguishes processed, accepted/deferred and busy
from codec errors. One32,768-byte buffer owns a deferred frame after caller
storage expires. The existing outbound mailbox is quarantined rather than copied.
One168-byte job (host sizeof, compile-time bound256) is allocated per fence;
ReplicatedKernel inline sizeof is124,416 on this host. Bounded memory is not an
allocation-free or measured-latency claim; heap arenas/allocator overhead and
whole-process RSS are excluded. TradingState and canonical encodings are unchanged.

Worker completion uses release/acquire publication, then exact operation,
generation, nonwrapping sequence, tuple, captured indices and target checks.
Failures latch service closed. An unconsumed truncate grant after replay also
latches closed rather than authorizing a future unrelated cut. Generation
activation during a job is not supported; E1 must drain/fence it, not merely
ignore stale messages. Runtime clear is forbidden: the old library clear path
ignores callback errors, so it cannot be advertised as a resumable installer.

The fork additionally propagates self-vote errors and defines a non-shutdown
pending status before apply-index advancement. Null optional hooks retain the
old path, but correcting ignored self-vote errors intentionally changes that
failure behavior. CMake now checks raft_log.c too, not just raft.h/server.c.
The regenerated zero-context patch was applied to a clean local base and all
three reviewed hashes matched. Existing dependency trees were preserved; fresh
scratch dependencies were used, without reset or network fetch.

## RED and test provenance

Freshly rebuilt pre-change libraries reproduce all three original actual-fsync
REDs: election4/4, higher-term vote4/4, commit2/2 syncs on the protocol owner.
All checked real persisted tuples/protocol outcomes before failing owner-thread
I/O. They do NOT establish premature success in the old synchronous code.

A new actual conflicting-suffix test failed:
`RED F6: conflicting suffix rewrite executed on protocol owner`.
A standalone pinned-Raft test failed:
`self-vote persistence failure was ignored by campaign`.
Initial missing C-header includes/linkage in the latter were harness compilation
errors, not product REDs; corrected before recording the behavioral failure.

Permanent suites:

- `v2-async-persistence-tests`: election/vote/commit paused before file sync and
  before directory sync, higher-term responses, busy ingress/proposal/timer
  behavior, actual forwarding fsync observer, and file/directory EIO. Checks
  independent persisted tuples, absent output/apply/results, successful release
  and fail-closed recovery behavior. The old diagnostic's synchronous fixture is
  preserved as historical RED evidence, not registered as an expected-green test.
- `v2-async-suffix-tests`:16 cases crossing prev/entry conflict, same/higher term,
  staged/durable tail and pre-file/pre-directory durability boundaries. Caller
  bytes are overwritten after receive. Exact reservations, original/replacement
  command bytes, cut, response semantics and ONE worker rewrite are checked.
  Two orderly worker-exception/restart cases cover pre-replacement-sync and
  post-rename failure. These are NOT physical power-loss tests.
- `v2-persistence-completion-tests`: actual worker publishes a completion; a
  private test friend acquires the real handshake then corrupts each typed field
  before the actual acceptance function. Eight corruptions refuse output and
  latch failure; an extra delivery after consumption cannot mutate owner state.
  This seam adds no public injection API or alternate persistence algorithm.
- `v2-raft-persistence-tests`: successful no-optional-hook campaign plus failed
  self-vote callback propagation/no RequestVote emission.
- Existing authority composition now drives a real authorized order and marker
  across a paused metadata directory-sync boundary. Applied index/proof remain
  unavailable until release; the resulting proof sends the exact original payload.

Fixture corrections are not hidden as product fixes. Deferred acknowledgements
and post-compaction commit saves need explicit pumping. A former second-per-entry
rewrite fault no longer exists: its saturated960-slot test now fails after the
single whole-cut957 rewrite and checks all960 memory reservations remain until
restart, then exactly957 original result identities survive. An early dispatch
in the new marker fixture correctly abandoned its pending request; the fixture
was corrected to await the proof, not the gateway contract weakened. Failed
intermediate regression logs are retained separately from final results.

## Negative controls

Direct source compilation (cache-line64 matching the libraries), not CMake
timestamp mutation, produces these intended failures:

| Removed behavior | Intended assertion |
|---|---|
| Pending outbound drain guard | protocol output was readable before metadata directory durability |
| Async before-apply fence | state applied before metadata directory durability |
| Completion sequence equality | corrupted completion was accepted |
| Candidate self-vote error propagation | self-vote persistence failure was ignored by campaign |
| Whole-cut prepare callback invocation | suffix cells or reservations released before worker preparation |

This does not claim every conjunct was mutation-tested. The prepare mutant
initially failed only because it never reached the worker pause; the permanent
test was strengthened to catch immediate ownership loss, then the mutant rerun.

## Verification and remaining limits

Final results/commands are in `verification.txt` and accompanying raw logs.
Release and full C++ ASan+UBSan are rebuilt before CTest. Sanitizer flags include
nonrecovering UB and halt-on-error, with leak checking disabled. Linked Raft C
remains uninstrumented in that configuration; do not label it fully instrumented
Raft. Focused TSan directly instruments the new replication/metadata/WAL path,
not every linked component. Independent inventory expects90 host/87 V2 suites;
tool tests24 + gate tooling16 are separate from the passive Python model corpus.

Two bounded Sol agents implemented/reviewed assigned areas; main supplied the
pinned patch, whole-suffix RED and16-cell matrix, authority integration, additional
guards, fixture corrections, mutation verification and final regression review.
Protected paths were not edited; no stage/commit/tag/device/venue action occurred.

Still open: composed asynchronous crash-image coverage beyond these pause/EIO
traces and existing store models, E1 actual lagger install/activation with delayed
old writes, E2 independently authenticated backup durability, and target re-gates,
memory/latency/storage measurements. Live service must explicitly start the
worker; no-worker mode and stop/join remain explicit cold/test operations, not
fallbacks. Pi is deferred, not waived. E1/E2/E3 acceptance and F5/F6/F7 remain OPEN.
