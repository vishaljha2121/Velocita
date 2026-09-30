# WAL maintenance-fence focused review

The direct deterministic suite exercises the actual `WalWriter` worker and an
actual `WalSegment`. While a maintenance body is deliberately blocked, owner
submission refusal, duplicate-maintenance refusal and both completion polls
return without waiting. The maintenance completion remains absent until the
body returns, and WAL submission remains refused through the completed-but-not-
collected state. Release/acquire handoff makes the immutable input and body
result visible. Collection permits reuse, and each accepted job runs exactly
once. A thrown body permanently fails the writer, preserves the exception and
does not publish successful maintenance completion.

Two ownership requirements remain caller-enforced rather than represented by
the API: the owner must drain prior WAL submissions/completions before submitting
maintenance, and must not call public `poll()` concurrently with a started worker
thread. The present state machine correctly refuses new block submission after
maintenance is queued, but cannot prove that earlier blocks have been applied to
the owner arena. E2 integration must keep those preconditions explicit; this
component test is not compaction/restart or E2 completion evidence.

No shared build directory was used. See `catalog-maintenance-green.log` for the
warning-clean and sanitizer commands/results.
