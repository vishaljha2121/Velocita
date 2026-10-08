# 0023. Archive retention for a run with no end date

Date: 2026-10-07
Status: Accepted by the owner on 2026-10-07 (section 2). Sections 1 and 2 are built; neither is deployed yet.

## Context

Every voter keeps the whole committed log. Compaction moves the oldest 2,048 entries out of the
write-ahead log into an archive segment and records the segment in a catalog. The archive is never
trimmed, and it has a fixed budget:

- 1 GiB of segment bytes (`kDefaultArchiveLogicalByteLimit`), compiled in, not configurable;
- 4,096 catalog rows (`kMaxArchiveCatalogRows`), one per compaction.

Ordinary commands are refused 16 MiB and 4 rows before those limits
(`ReplicatedKernel::archive_admission`, `ProposeStatus::log_capacity_exhausted`). The reserve is
kept for the operator kill path.

Until 2026-10-07 every paper run had a deadline measured in minutes or hours, so the budget was
never in sight. The stochastic cash session has no deadline. It sends one bar to the cluster every
second, and each bar commits two entries (the source event and the decision).

Measured on the running cluster on 2026-10-07:

| Quantity | Value |
|---|---|
| Entries committed | 2 per second (121.6 per minute) |
| Compactions | one every 17 minutes |
| Segment size | about 579 KB (about 283 bytes per entry) |
| Archive growth per voter | about 49 MB per day, 85 segments per day |
| Ordinary byte budget (1 GiB less 16 MiB) | reached in about 20 days |
| Ordinary row budget (4,092 rows) | reached in about 48 days |

So the session started on 2026-10-07 stops accepting ordinary commands around 2026-10-27. A
strategy exit is an ordinary command. A kill is not, so the position could still be killed, but the
strategy could not close it.

Almost all of that log is "nothing to do": of 8,192 decisions in the first two and a quarter hours,
24 asked for an order.

## Decision

### 1. Measure and alert (built)

The voter publishes `velocita_v2_archive_bytes`, `velocita_v2_archive_byte_limit`,
`velocita_v2_archive_segments` and `velocita_v2_archive_segment_limit`. Two alerts watch the larger
of the two fractions against the ordinary budget: `VelocitaArchiveFilling` at 70% (warning) and
`VelocitaArchiveNearlyFull` at 90% (critical). The Raft dashboard shows the percentage and the days
left at the last six hours' growth. This follows the book's recorder rule (ch. 52: alarm at 70%
occupancy).

At today's rate 70% is about six days before the limit and 90% about two.

### 2. Send a bar to the cluster only when it can matter

The edge forwards a bar when any of these is true, and otherwise records it locally and sends
nothing:

- the entry or the exit flag is set;
- a protective stop is latched (drawdown, capacity);
- 30 seconds have passed since the last forwarded bar (a heartbeat, so "last decision" and the
  rule-agreement check stay live and a silent edge is still visible).

Expected effect: from 2 entries per second to about 0.07, roughly a thirty-fold reduction. The
byte budget then lasts about 600 days and the row budget longer.

What is given up: a committed record of every one-second "hold". The edge's own journal still has
every bar and its flags. The kernel's safety does not depend on the stream being continuous: each
authorization checks the age of the market data it is given.

### 3. Rotate the incarnation as the backstop

When `VelocitaArchiveFilling` fires, the operator flattens, finalizes the session, and starts a new
incarnation that continues the same account, as was done for the cash session on 2026-10-07. With
section 2 in place this is a yearly task rather than a three-weekly one.

## Alternatives considered

- **Raise the compiled budgets.** Sixteen times the bytes buys under a year at today's rate and
  still ends. Start-up verifies every segment, so a larger archive means a longer restart,
  worst on the Pi's SD card. The catalog is a fixed 4,096-row array checked by a static assert;
  growing it changes a durable format. Rejected as the primary answer; it treats the symptom.
- **Trim the archive after export.** Drop a segment once it has been copied to cold storage and
  confirmed there. This is the real long-term answer for a system that runs for years, and ADR 0007
  ("the log is the audit record") allows it only if the exported copy is as verifiable as the local
  one. It needs a new catalog operation, a checkpoint that proves the prefix, and an export
  receipt. Deferred until live trading needs it; section 2 removes the urgency.
- **Do nothing and rotate every two weeks.** Works, but puts a routine deadline on a human and
  fails closed in the worst way: an exit refused.

## Consequences

- Section 2 changes the paper edge only. The policy, the kernel and the gateway are unchanged.
- "Decisions per minute" on the dashboards falls from 60 to about 2 when nothing is happening.
  Panels and alerts that assume one decision per second must use the heartbeat interval instead
  (`Last decision` thresholds, `VelocitaPaperEdgeStalled` already keys on the candle age, not on
  decisions).
- Tick-to-decision latency has fewer samples; the percentile panels take longer to fill.
- The 30-second heartbeat is a number to revisit: shorter costs archive, longer delays noticing a
  cluster that has stopped answering.
