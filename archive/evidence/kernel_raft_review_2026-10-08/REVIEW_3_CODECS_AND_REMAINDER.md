# Codecs, Aeron transport and the kernel ranges left over: line-by-line review

Reviewed 2026-10-09 at commit `765185b`. Third pass of the review in `REVIEW.md`; it closes the
list of unread files in `REVIEW_2_NODE_AND_STORAGE.md` (live promotion plan item 20).

## What was read

| File | Lines | Read |
|---|---|---|
| `src/v2_kernel_codec.cpp` | 1,420 | Reader, writer, every decoder, command validation, both entry points. The encoders were checked against the decoders field by field only for the commands added this week |
| `src/v2_state_snapshot.cpp` | 1,730 | State validation (368-790), payload decoder (1433-1632), both entry points. The writers and the nested artifact readers mirror the command codec and were skimmed |
| `src/v2_archive_file.cpp` | 124 | All |
| `src/v2_archive_segment.cpp` | 339 | All |
| `src/v2_archive_catalog.cpp` | 238 | All |
| `src/v2_aeron_node_transport.cpp` | 138 | All |
| `src/v2_kernel.cpp` | 3,664 | The three ranges the first pass skipped: ledger and digest helpers (206-350), the tail of venue order-event handling with mass cancel and replace (2740-2992), effect materialization (3456-3520) |

## Result

No defect found. Nothing here needs a code change before the next incarnation.

## What makes these files safe

1. **Every decoder checks the frame before it reads the content.** Magic, schema, declared length
   against actual length, then the SHA-256 over the whole frame, and only then the fields. A frame
   with one flipped bit is refused at the digest.
2. **Reads cannot run off the end.** The reader returns zero and latches invalid when bytes run
   out; every decoder ends with `finished()`, which also refuses trailing bytes.
3. **Counts are bounded before they index.** Rule, feature, template, cursor, receipt, decision,
   reservation, order and effect counts are each compared with the array size before the loop.
   `reservation_index` in an order is checked against the reservation count in state validation
   before it is used.
4. **Enumerations are checked after decoding.** Decoders cast raw bytes, then `valid_command`,
   `valid_result` or `valid_state` refuses any value that is not a named enumerator. The three
   range checks (`ContractError`, `DecisionCode`, `ActionResolutionCode`) name the current last
   enumerator.
5. **A snapshot is checked as a whole.** State validation ties orders to reservations, effects and
   decisions, and requires each account's reserved totals to equal the sum over its orders. A
   snapshot that is well formed but inconsistent is refused.
6. **Decoding writes to a scratch value and copies only on success**, so a refused frame leaves the
   destination untouched.
7. **Archive files are opened without following links, must be regular single-link files, and are
   compared before and after the read.** Records are validated twice; the visitor runs only on the
   second pass, so it never sees part of a bad segment.
8. **The Aeron inbox is bounded** and counts what it drops.

## Things worth knowing, none of them defects

- The writers do not bounds-check. They rely on the size function agreeing with what the writer
  emits. A mismatch would show as a decode failure in the round-trip tests, which cover every
  command kind (twelve in one test, the rest in their own tests and in the risk tests).
- An archive segment decodes its commands with the current command schema. Archives written under
  schema 10 cannot be read by a schema 11 binary. This is consistent with the rule that a schema
  change starts a new incarnation; it also means an old incarnation's archive needs the old binary
  to replay (the replay tool in `docs/evidence/gateway_stop_2026-10-09/` was built that way).
- The archive has a lifetime budget per incarnation: 1 GiB and 4,096 segments. Measured on nuc1 on
  2026-10-09: 7.4 MB of node data after 27.5 hours, so the byte budget lasts months.
  `VelocitaArchiveFilling` already watches it.
- Snapshot state validation compares every receipt with every earlier receipt. This is the known
  cost behind the compaction pause (ADR 0022), not a new one.
- Mass cancel refuses as a whole when the outbox cannot hold the new cancel effects. New orders
  are admitted only while a cancel slot stays free for them, so this refusal should not be
  reachable. This pass did not look for a test of it.
