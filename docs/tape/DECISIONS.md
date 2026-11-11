# Verifiable tape: implementation decisions

Decisions made while building the tape, in the order they were made, each with its reason and the
alternatives rejected. Design-level decisions are in [ADR 0030](../adr/0030-the-verifiable-tape-is-a-separate-raft-group.md);
this log records how they were implemented. A decision that would change a locked guarantee is not made
here; it goes to an ADR amendment.

Started 2026-11-11, when the owner approved the WebSocket subscription (gate G1).

## Day-one feed findings

**D-01. The twelve Arena books are `<SYMBOL>-USDC`.** A 20-second probe subscribed `NVDAC-USDC` through
`SPCXC-USDC` on INSTRUMENT, EBBO, DEPTH and TRADE with `SUBSCRIBE_NO_AUTH`, and the confirmation listed
all twelve on every channel. The capture tool is `tools/tape-feed-capture.py`; recordings stay outside the
repository in `~/.cache/velocita-tape-capture/`.

**D-02. `seqnum` counts per (channel, symbol).** EBBO for NVDAC was at 75,100 while TSLAC was at 38,320 in
the same second, so each stream has its own counter. INSTRUMENT shared one value (8,904) across all
symbols. The tape's stream key stays (channel, symbol), as ADR 0030 assumed.

**D-03. DEPTH updates replace whole price levels.** Each level in an UPDATE carries the level's new total
quantity and order count; `qty` "0" with `order_count` "0" removes the level. The book engine therefore
sets levels rather than adding deltas. The live field is `order_count`, where the AsyncAPI spec says
`depth`, and each level also carries a `last_update` timestamp the spec does not list. Both go on the list
of documentation questions.

**D-04. Book integers use a fixed scale of 10⁻⁹ for prices and quantities.** Observed prices use at most
3 decimals and quantities at most 4. A fixed 10⁻⁹ scale holds every observed value exactly, fits prices up
to about 9.2 billion in an `int64`, and needs no per-instrument scale table. A value with more than nine
decimals makes the book unknown (BOOK-2). `reference_price` uses 11 decimals but never enters the book.
Rejected: a per-instrument scale, since the feed publishes no tick or lot size.

**D-13. A verdict's key is (stream, update kind, `seqnum`). Proposed as ADR 0030 amendment 2.** The feed
gives a snapshot the same `seqnum` as the update that follows it (SNDKC: SNAPSHOT 111089, UPDATE 111089,
UPDATE 111090). Keyed on (stream, `seqnum`) alone, as ATT-1 is worded, a snapshot and an update would be
compared as copies of one message and reported as divergent. Including the update kind keeps every other
part of ATT-1 to ATT-5. It is implemented because the literal key cannot be implemented correctly; it is
recorded as amendment 2 for the owner's approval rather than treated as settled.

**D-14. Agreement metrics and divergence alerts count UPDATE messages only.** Two connections captured
side by side (`frames.jsonl` and `frames-second.jsonl`, five overlapping minutes) received every one of
553 updates, byte-identical, with arrival skew p50 1.9 ms, p90 3.4 ms, p99 6.4 ms and max 11 ms. Snapshots
are replies to a subscription, not broadcasts: two EBBO snapshots taken minutes apart carried the same
`seqnum` but a different top-level `last_update`. DEPTH snapshots at the same `seqnum` were identical, and
EBBO never touches the DEPTH book, so the locked book rule (BOOK-4) stays as written. But counting snapshot
verdicts would report expected differences as disagreement, so the agreement metric and the divergence
page use UPDATE verdicts.

**D-15. Day-one answers: the design's assumptions hold.** From the 15-minute capture (1,918 frames, all
twelve books):
- A snapshot's `seqnum` is the `seqnum` of the next update: the first update after every snapshot carried
  the snapshot's own number (36 of 36). So a valid book's position is the next expected update number.
- Batching skips nothing: all 1,808 update-to-update steps were exactly +1. The gap rule is plain
  consecutive numbering.
- A DEPTH update carries only the changed levels, each with its new total (D-03).
- The largest frame was 1,601 bytes (p50 432, p99 1,132), and the deepest book had 12 levels a side.
- The rate was 2.1 frames a second. With three recorders and one tick a second that is about 7 log entries
  a second, roughly 290 MB of log a day per machine, which makes deferring compaction (D-07) safe for days,
  not weeks.
- No heartbeat arrived: the subscription as a whole is never quiet for 10 seconds while any of the twelve
  books trades. Heartbeat handling is tested with the fixture instead.

**D-16. Parameters set from the capture.** Levels per side stay at 64 (5 times the deepest book), held
messages at 8, changes per held message at 32 (the largest update changed fewer than 12 levels), entry
payload at 4,096 bytes (2.5 times the largest frame). Open verdicts stay at 256: at 2.1 messages a second a
verdict lives at most 5 ticks, so about 11 are open at a time.

## Building blocks

**D-05. The tape gets a new, compact replica instead of a fork of `ReplicatedKernel`.** The plan assumed a
fork. Measured, the fork would carry about 4,500 lines tied to trading: `ReplicatedKernel` (2,104 lines,
162 references to trading types), the archive store (90), identity startup with the execution catalog
(31), the snapshot store typed to `TradingState`, and the node process (84). The tape's replica instead
uses the same pinned willemt/raft fork, so it keeps the durable-quorum patch, pre-vote and the persistence
callbacks, and reuses only Velocita's generic pieces. The trading code is untouched. Rejected: making
`ReplicatedKernel` generic, which changes code the trading cluster runs.

**D-06. Durability: the tape's own write-ahead log with group commit.** Append-only records (index, term,
type, length, CRC-32, bytes) are written as Raft offers entries. On the leader, one fsync per loop pass,
after which `durable_idx` reports the synced index, so the leader counts its own vote only for entries on
disk (the patch's contract). On a follower, the fsync happens after `raft_recv_appendentries` returns and
before its response is sent, so a success response always means the entries are durable. A suffix
truncation writes a truncation record, so recovery cannot resurrect popped entries. Term and vote are
written atomically (temporary file, fsync, rename, fsync of the directory). Rejected: one fsync per entry,
which V1.1 measured as too slow on the Pi's SD card.

**D-07. Snapshot install and log compaction are deferred.** For the MVP the write-ahead log is the archive:
a restart replays the whole log through the reducer, and the verifier reads the same files. The canonical
snapshot encoding of the tape state is still built and tested (RAFT-5.1), so compaction can be added
without a format change. Cost: memory and restart time grow with the log; a day of recording fits.

**D-08. Entry payload limit: 4,096 bytes.** With its own log the tape is not bound by the trading WAL's
1.8 KB record limit. The largest frame in the probe was 1,601 bytes (an NVDAC snapshot), so most frames fit
in one entry and deeper books split into `ObservationPart`s (ING-4). This replaces the plan's provisional
1,700 bytes.

**D-09. HMAC is built on Velocita's pure `Sha256`.** The existing `hmac_sha256` lives in the node library,
which links the trading kernel. Building HMAC on the `Sha256` class (in the contracts library) keeps the
tape free of trading code and keeps the reducer free of OpenSSL. Keys are derived per direction under the
label `velocita-tape/v1/<incarnation>/<from>-><to>`, so tape keys can never equal trading keys (X-2).

**D-10. Recorders send each observation to all three voters; the leader proposes.** The leader proposes a
recorder's requests only in order, holding any that arrive early until the gap is retried, and never
proposes a request already in its log. Every voter acknowledges the highest request it has applied for
each recorder session, and the recorder frees its ring up to the highest acknowledgement and resends
anything older than two seconds. This gives exactly-once commit (ING-2) and contiguity per session
(ING-3) without a forwarding path between voters. The reducer also refuses any request that is not the
next in its session, as a second check.

**D-11. The recorder uses its own minimal WebSocket client over OpenSSL.** The Mac's system libcurl has no
WebSocket support, and adding a library to the pinned build image is a toolchain change. RFC 6455 for a
client is small: the upgrade handshake, unmasked server frames, masked client frames, ping and pong,
close, and continuation frames. It supports `ws://` for the test fixture and `wss://` with certificate
verification for the venue.

**D-12. The reducer parses frames with its own allocation-free JSON scanner.** It reads only the fields
it needs (`seqnum`, `channel`, `update`, `item`, the symbol, levels, best bid and offer, trade price,
`price_bands_percent`) from a `string_view`, so the reducer stays pure and bounded (ATT-6, BOOK-8). It
re-checks the recorder's classification against the payload; a mismatch is treated as a malformed frame.

**D-17. One tape master secret; a key per direction derived from it.** Every tape process holds one 32-byte
master secret, and each direction gets its own HMAC key, derived under D-09's label. Cost: a host that leaks
the secret can impersonate any tape process. Rejected for now: a separate secret per pair, which multiplies
the owner's key ceremony. The secret never leaves RAM-only tmpfs on the hosts.

**D-18. Replay protection by boot nonce and counter.** Each process picks a random boot nonce at start and
numbers its frames. A receiver refuses a frame whose counter is not above the last one seen for that sender
and nonce. After the receiver restarts, an old session's frames could be replayed once; Raft and the
reducer already refuse stale terms, duplicate requests and out-of-order ticks, so this is accepted.
ING-2.2 (replay after restart) stays deferred.

**D-19. An append message carries at most 64 entries and 256 KB.** willemt/raft sends everything a lagging
follower is missing in one message. Sending fewer entries is valid Raft: the follower reports how far it got
and the leader continues from there.

**D-20. The tape has its own peer message codec.** The V1 peer encoding (`peer_wire`) has no pre-vote flag,
so a granted pre-vote arrived as a real vote and no election ever completed. The voter test caught it before
any hardware ran. Velocita's V2 node carries the flag in its own bound-peer encoding, which builds on
`peer_wire`, so `peer_wire` is left unchanged and the tape encodes peer messages itself
(`src/tape_peer_codec.cpp`).
