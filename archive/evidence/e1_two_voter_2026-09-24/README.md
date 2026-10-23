# CP-051: E1 for the two-voter pilot

2026-09-24, `v2/phase-5-corrections`, HEAD `d44a4a9`; remediation uncommitted.
Owner direction this checkpoint: MVP topology stays ONE voter per host (NUC + Pi,
two voters). A 2-on-NUC + 1-on-Pi layout is deferred until a compute budget is
measured. The online snapshot installer is deferred to the three-voter release.

## Why E1 changes shape with two voters

With exactly two voters an entry commits only when BOTH hold it durably, and
compaction never passes commit. An intact voter therefore always holds every
entry its leader has compacted and can catch up from its own log. Falling
behind the compacted prefix requires lost storage, which the approved
lost-voter repair direction handles offline (`F5_LOST_VOTER_REPAIR_REVIEW.md`),
not an online install. The three-voter lagger RED stays open as a tracked V2.1
exit; it was freshly reproduced unchanged (`three-voter-lagger-red.txt`).

## What `tests/v2_two_voter_catchup_tests.cpp` exercises

1. Partitioned peer: six commands committed together, six more staged by the
   leader alone. Leader commit and compaction cut both equal the jointly
   committed prefix and do not exceed the peer's durable index. After peer
   restart and heal, both voters durably hold the full suffix from the log.
2. Unsynced acknowledgement: the peer stages a probe and replies WITHOUT
   syncing; the leader must not count it. The peer then loses power (object
   destroyed without flush), recovers at the old prefix, and reacquires the
   probe through ordinary AppendEntries.

Oracles are independent: expected digests come from the reducer alone; the
indices come from values the fixture itself controls. Neither scenario reads
the refusal reason from the code under test.

Approved contract asserted, not hidden: the pinned library has no automatic
leader no-op (`F4_AUTHORITY_BARRIER_BYTE_REVIEW.md` §4). After a re-election the
healed old-term suffix waits for one current-term command, which the test
commits explicitly. The first draft of this test wrongly expected the suffix to
commit without it; that was an oracle error, corrected against the approved
contract, not a production defect.

## Evidence honesty

These tests pin EXISTING behavior; they did not start RED. Coverage is proved
by mutation instead: removing only the follower durable-acknowledgement clamp
(`src/v2_replication.cpp`, `reported = durable`) in a scratch copy fails the
exact intended assertion "leader counted a staged, unsynced peer acknowledgement
toward quorum"; the unmutated baseline passes through the identical direct
compile (`clamp-mutant.txt`, `clamp-mutant.diff`). No shared production file
was mutated.

Release 98/98 (`release-tests.txt`). The sanitizer result is recorded in
`verification.txt` from a run executed alone; an earlier sanitizer run overlapped
a Release CTest by mistake and is not counted.

## Not covered

Laboratory deployment mode only: `two_host_pilot` compaction additionally needs
independent destination evidence (CP-048), exercised by its own suites. No
lost-storage detection: a wiped-and-rebootstrapped voter is indistinguishable
locally from a fresh one; that is the lost-voter repair work, required before
a live canary (roadmap M6). No real processes, network, NUC or Pi.
