# Actual archive compaction integration-test scope

`tests/v2_archive_compaction_tests.cpp` includes the established replication
fixture under a renamed main and drives only public `ReplicatedKernel` APIs.
It permanently checks:

- three actually committed commands through asynchronous compaction, beginning
  with the independently authored 100-byte command from the original archive-
  loss RED;
- three unread READY results survive compaction in FIFO order;
- the real WAL no longer contains the reclaimed prefix while the exact command
  is recovered by decoding the authoritative named catalog and its digest-named
  CP-028 segment;
- restart continuity and a second compaction that increments generation,
  preserves row one exactly and appends a second contiguous row covering two
  new entries `[4,5]` (the pinned Raft library has no snapshottable prefix when
  only one post-snapshot entry exists; that fixture rule is not a product RED);
- recovery refusal when the required archive is missing or its authoritative
  catalog is malformed; and
- laboratory local evidence succeeds in the integrated case, while two-host
  mode with an actual two-voter replicated commit refuses to begin compaction
  without independent-copy evidence.

With no argument the test creates and removes its own unique `mkdtemp` root for
CTest. An optional caller-supplied new root is retained for diagnosis. It does
not touch shared build trees or guess a process-ID-based cleanup path. The exact
golden command is decoded again from the named archive after restart, not merely
checked before restart.
It makes no Pi, independent-copy, power-loss, performance, E1, E3 or full F5
acceptance claim.
