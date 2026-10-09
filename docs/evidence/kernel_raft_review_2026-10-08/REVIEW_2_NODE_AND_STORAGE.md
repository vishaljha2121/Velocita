# Node loop, authentication and storage: line-by-line review

Reviewed 2026-10-09 at commit `9f96cf3`. This is the second pass of the review in `REVIEW.md` and
covers part of what that one listed as not read (live promotion plan item 20).

## What was read

| File | Lines | Read |
|---|---|---|
| `src/v2_node.cpp` | 473 | All |
| `src/v2_auth_envelope.cpp` | 199 | All |
| `src/v2_session_fence.cpp` | 157 | All |
| `src/v2_bound_frame.cpp` | 136 | All |
| `src/v2_wal_arena.cpp` | 669 | All |
| `src/v2_archive_store.cpp` | 1,136 | All |
| `src/v2_node_protocol.cpp` | 350 | All |
| willemt/raft `raft_node.c` | 192 | All |

Still not read: `v2_kernel_codec.cpp`, `v2_state_snapshot.cpp` beyond the parts changed this week,
`v2_archive_file.cpp`, the archive segment and catalog codecs, the Aeron transport, and the three unread ranges of `v2_kernel.cpp`.

## Summary

No safety defect found. Authentication and the storage formats are careful and fail closed. The
findings are about staying up: two places where a crash or a misbehaving key stops a voter or fills
the log, and one constant cost.

---

## Findings

### 1. Medium: a key with the wrong role can still fill the log

`receive_ingress` proposes every well-formed command whose caller id matches its key. Since ADR 0020
the kernel refuses a command the sender's role does not allow, but only after it has been
replicated. The refusal writes no receipt, yet the command still takes a log entry and, later,
archive space. The archive is never trimmed (ADR 0023), and when it fills, every ordinary command is
refused, the strategy's exits included.

So a leaked or faulty strategy key cannot change state, but it can spend the cluster's life. The
new `VelocitaUnauthorizedCommand` alert reports it; nothing stops it.

Fix: the node refuses such a command before proposing it, reading the roles from its own applied
state. That check is advisory (a voter that is behind may let one through) and the kernel's check
stays the rule. A per-caller rate limit at ingress would bound a key that stays inside its role.

### 2. Medium: a crash during archive publication stops the voter from starting

`ArchiveStore::publish` writes a segment to `v2-archive-segment.bin.tmp`, renames it to its final
name, then writes and renames the catalog. `ArchiveStore::open` refuses a directory that holds any
name the catalog does not list (`unexpected_entry`). A crash between those steps leaves exactly such
a name: the temporary file, or a segment the catalog does not yet reference.

This is deliberate (the design notes say never adopt an orphan), and it is safe: the write-ahead
log still holds those entries, because reclamation happens only after the catalog is published. But
the voter then fails at start with "verified archive open refused", which does not name the file,
and no runbook covers it. The window is short (the compaction pause, 0.1 to 0.6 s every 2,048
entries) and the cluster runs on the other two voters meanwhile.

Fix, in order of size: name the unexpected entry in the error; add the repair to the runbook; then
decide whether start-up may delete the two fixed temporary names, which by construction were never
published.

### 3. Medium (cost): the write-ahead-log writer thread never sleeps

`WalWriter::start` loops on `poll()` and calls `yield()` when there is nothing to do. With no other
runnable thread that is one core at 100% per voter, all day. On the Pi it is a quarter of the
machine and heat. It also hides real load in CPU graphs. A condition variable or an eventfd wake
would remove it; the latency cost is one wake-up.

### 4. Low: a torn final block stops the voter

`visit_segment_records` treats a block that does not decode and is not all zeros as corruption and
throws. A power loss during a 4,096-byte block write can leave that. The block was never reported
durable, so dropping it would be safe; the code chooses not to guess
(`test_a_torn_or_corrupt_block_is_detected_not_guessed`). Same consequence as finding 2: a voter
that needs a human after a power cut. Worth the same runbook entry.

### 5. Low: the receive fence forgets across a restart

`SessionFence` keeps the highest session epoch and sequence it has accepted in memory. After a node
restarts it accepts any sequence of the sender's current session again, so a recorded frame can be
replayed to it until the sender's next frame arrives. What bounds the damage is above it: Raft
messages are checked against term and log, commands against the caller's sequence and receipts, and
an authority request yields a proof only the gateway's own nonce accepts. It needs an attacker
inside the tailnet holding recorded traffic.

### 6. Low: smaller points

- `WalSegment::write_block` and the rewrite writer treat an interrupted `pwrite` (`EINTR`) as a
  failure. Regular-file writes are rarely interrupted, and the result is a stop, not corruption.
- "Preallocation" is `ftruncate`, which reserves no space. A full disk shows up at write time as a
  stop. `kWalPrealloctionBlocks` is misspelled.
- After a failed synchronous `flush`, the file is truncated back but `reserved_through_` keeps its
  old value. Harmless: later writes extend the file anyway.
- Block generation and sequence numbers are written and checksummed but never checked on read.
  Ordering relies on file offset alone.
- `frame_block` and `decode_block` allocate a vector per block.
- `V2Node::send_peers` uses the receive buffer as scratch for outgoing frames. Correct today,
  because receiving has finished by then.
- `serve_state` encodes and hashes the whole state for every gateway read, several times a second.
- The HMAC helper wipes its pads but not the two SHA-256 states built from them.

## What is solid

- **Authentication.** HMAC-SHA-256 over the whole header and body, compared in constant time. The
  key is derived per route (role, sender, recipient) and per incarnation, so a frame cannot be
  replayed on another route or cluster. Lengths are bounded before any arithmetic.
- **Caller binding.** A command's caller id must equal the id configured for the key that signed
  it; gateway requests are accepted only from the configured gateway caller.
- **Bound frames.** Every stored or peer frame carries the manifest digest, its kind and its owner,
  and is refused on any mismatch.
- **Session epoch file.** Written to a temporary name, synced, renamed, directory synced; refuses
  symlinks.
- **Archive.** Every publication is re-read and compared after each step; file type, owner, mode and
  link count are checked before and after each read; the directory is locked; a failed publication
  marks the store failed until restart.
- **Node loop.** Any exception marks the node failed and it refuses further turns.
- **Node protocol.** Every frame has one exact length per kind, checked before any field is read; a
  gateway request cannot be mistaken for a command.
- **`raft_node.c`.** Accessors only.
