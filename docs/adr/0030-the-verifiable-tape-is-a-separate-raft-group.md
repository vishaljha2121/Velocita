# 0030. The verifiable tape is a separate Raft group that records, attests and proves the public book

Date: 2026-11-11.
Status: **ACCEPTED 2026-11-11 — guarantees locked; nothing built.** Guarantees: [`docs/tape/GUARANTEES.md`](../tape/GUARANTEES.md).
A change to a locked guarantee needs an amendment to this ADR that names the guarantee ID and why it changed.

## Problem

The showcase project records True Markets' public CeFi orderbook feed on all three machines and must be
able to prove, later and to anyone, what the public book was at any `seqnum`. It must not trade. Five
choices decide its shape: where its state machine lives, how three recorders' observations reach the log,
when an agreement verdict stops waiting, whether the book waits for that verdict, and what the book does
when recorders disagree about the bytes of one message.

## Decision

- **A separate Raft group.** The tape runs as its own process and Raft group with its own incarnation,
  identity domain, keys, ports and data directories. It reuses the replication code (willemt/raft fork,
  Aeron transport, WAL, snapshots, archive, authenticated client protocol) but not the trading kernel,
  which stays unchanged. A command for one group is refused by the other.
- **Every recorder sends full observations** (option A). Each of the three recorders proposes, for every
  message it receives, the raw bytes, their SHA-256, the stream, `seqnum`, update kind and its UTC receive
  time. No recorder has a special role. Storage is about three times the minimum; it is measured on day
  one, and a later move to "first commit carries the payload, later ones carry the hash" changes only the
  recorder's send rule.
- **A verdict closes when all three have reported, or N ticks after the first report, whichever comes
  first.** The leader commits a numbered tick entry every T. Starting values: T = 1 s, N = 5. The reducer
  never reads a clock; ticks are its only notion of time.
- **The book does not wait for the verdict.** It applies the first committed copy of each message. The
  verdict is collected separately and becomes final at close.
- **Divergence marks the book unknown until the next snapshot.** If any copy of a message differs from the
  copy the book applied, the book for that stream becomes unknown from that log index until a snapshot
  restores it. No book hash is published while it is unknown.
- **Missing messages are waited for, but only for a bounded time.** If a later `seqnum` commits before an
  earlier one, the book holds the later messages in a fixed-size buffer for up to N ticks for any
  recorder to supply the missing one. Otherwise the book becomes unknown until the next snapshot.

## Consequences

- In normal running every verdict is a complete 3/3 and closes as soon as the slowest machine reports.
  With a machine down or a message missed, verdicts close after N ticks as 2/3 or 1/3, and the book is
  never held for longer than N ticks.
- Book hashes computed before a divergence was discovered stay on the tape as computed; the verdict entry
  records the later discovery. A consumer who needs certainty waits for the verdict to close.
- The tick is a new entry type to secure. The reducer cannot check tick spacing without a clock, so the
  offline verifier checks it from the ticks' recorded UTC times.

## Assumptions to verify on day one

- `seqnum` is one counter per channel and comparable across subscriptions, so a resubscription's snapshot
  can be placed relative to the book. The spec says only "current sequence number of the channel".
- The no-login feed numbers published messages consecutively. If batching skips numbers by design, the gap
  rule changes and this ADR is amended.
- Whether a DEPTH UPDATE carries changes or full replacement levels.
- The largest DEPTH snapshot message, which sets the entry size limit and the split rule.
