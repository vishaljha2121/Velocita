# Verifiable tape: guarantees

What each part of the tape promises, and how each promise is proven. Design decisions are in
[ADR 0030](../adr/0030-the-verifiable-tape-is-a-separate-raft-group.md). Status: **locked 2026-11-11**; nothing
is built. Changing a guarantee needs an ADR 0030 amendment that names its ID. Parameters marked "to be set"
and the day-one assumptions may be filled in without an amendment; a different assumption answer may not.

Every guarantee has an ID and a "Proven by" entry. Two rules apply to every proof:

- **The expected answer never comes from the code under test.** It comes from a fixture's own record of
  what it sent, an independent reference implementation written from the published spec, or arithmetic
  done by hand.
- **Every guarantee has a mutant that breaks it**, and a named test must fail when that mutant is built. A
  guarantee whose mutant survives is not covered, however green the suite is.

## Terms

| Term | Meaning |
|---|---|
| Stream | One (channel, symbol) pair, for example (DEPTH, NVDAC-USDC) |
| Recorder | The process on one machine that holds the WebSocket connection. There are three: NUC1, NUC2, Pi |
| Observation | One recorder's record of one received frame: recorder id, stream, `seqnum`, update kind, raw bytes, SHA-256 of the bytes, UTC receive time, clock error estimate, and the recorder's own observation number |
| Copy | One observation of a given (stream, `seqnum`). Each message has up to three copies |
| Tick | A numbered entry the leader commits every T. The tape's only notion of time |
| Verdict | The agreement result for one (stream, `seqnum`): 3/3, 2/3, 1/3 or divergent, with every copy's hash and receive time |
| Book state | Per stream: `valid` (has a hash), `building` (inside a snapshot), or `unknown` (no hash until the next snapshot) |

## Cross-cutting

| ID | Guarantee | Proven by |
|---|---|---|
| X-1 | **The tape never trades.** No tape binary links order-entry, request-signing or authenticated-session code, and no venue credential exists on any tape host | A build-time symbol check on every tape target, in the style of `cmake/check_no_network_symbols.cmake`; a deploy test that greps the tape units and mounts for key files |
| X-2 | **The tape is separate from the trading cluster.** It has its own incarnation, key domain, ports and data directories, and each group refuses the other's commands | A test that sends a correctly signed trading command to the tape group and a tape observation to a trading node; both are refused with a domain error |
| X-3 | **Recorded data stays private.** No tape process has a path that publishes raw frames; only aggregate metrics leave the hosts | Review checklist plus a test that the exporter's output contains no frame bytes (a marker string planted in fixture frames must never appear in scraped metrics) |

## 1. Recorder

Runs on each machine as its own process.

| ID | Guarantee | Proven by |
|---|---|---|
| REC-1 | **Byte fidelity.** An observation's bytes are exactly the bytes of the received WebSocket text frame, and its hash is the SHA-256 of those bytes | A fixture server logs every frame it sends; the test compares committed observation bytes with the fixture's log byte for byte. Mutant: re-serialise the JSON before recording |
| REC-2 | **No filtering or reordering.** Every received frame becomes exactly one observation, in receive order, and the recorder's observation numbers have no gaps | The fixture sends duplicates, out-of-order `seqnum`s and malformed frames; the count and order of observations must equal the fixture's log |
| REC-3 | **Gaps are recorded and repaired.** When a stream's `seqnum` is not the expected next value, the recorder records a gap event (stream, expected, received), resubscribes, and records the resync | The fixture drops one frame; the test expects one gap event with the exact range, one resubscribe request in the fixture's inbound log, and the new snapshot recorded |
| REC-4 | **Liveness follows the spec.** Every subscription requests heartbeats. With no frame and no heartbeat for 30 s, the recorder closes the connection, records why, and reconnects with exponential backoff | Injected clock: a fixture that goes silent causes a reconnect at 30 s; a fixture that sends only heartbeats never causes one |
| REC-5 | **Bounded memory, no blocking.** Observations wait in a fixed-size ring. If the cluster can't take them and the ring fills, later frames are counted and recorded as an overflow gap with the exact `seqnum` range once delivery resumes. The socket read never blocks and the ring never grows | Stop the cluster accepting; send more frames than the ring holds; assert no allocation after start-up and exactly one overflow gap whose range matches the fixture's log |
| REC-6 | **Read-only toward the venue.** The only frames the recorder sends are `SUBSCRIBE_NO_AUTH` and `UNSUBSCRIBE_NO_AUTH`, including the HEARTBEAT channel. Connection attempts are rate-limited | The fixture logs every client frame and the test checks it against the allowlist; a connect-storm test checks the attempt rate stays within the limit |
| REC-7 | **Times are honest.** Each observation carries UTC receive time and the host's clock error estimate at that moment. Monotonic receive times never decrease within one recorder | A fake clock source with a stepped UTC jump: the monotonic sequence stays increasing and the error estimate is recorded as given |

Not guaranteed: that the recorder received everything the venue sent. Gap rate and vantage agreement measure
that.

## 2. Observation ingress

The authenticated path from a recorder to the tape group's leader. Reuses Velocita's client protocol.

| ID | Guarantee | Proven by |
|---|---|---|
| ING-1 | **Authenticated origin.** An observation is applied only if its HMAC verifies under the key of a caller whose role is recorder and whose recorder id matches the id inside the observation | Tests with a forged recorder id, a wrong key, an expired session and a trading-cluster key; each is refused with its own reason |
| ING-2 | **Exactly once.** A retried observation (same caller and request sequence) commits once, including across a leader change; an old frame replayed later is refused | Resend after a forced election; replay a captured frame after a restart. The log holds one copy and the replay is refused |
| ING-3 | **Nothing is lost between the ring and the log.** Every submitted observation ends committed or explicitly refused. Refused ones stay in the ring and are retried. Per recorder, observation numbers in the log are contiguous except for recorded overflow gaps | Fault injection (not leader, queue full, timeouts) with an oracle that checks contiguity from the recorder's own submission log |
| ING-4 | **Bounded entry size.** A frame larger than the entry limit is split by the recorder into numbered parts. Ingress refuses an oversize single entry. A message with a missing part counts as missing | A fixture snapshot of three times the limit is reassembled exactly; with one part dropped, the message is treated as missing |

## 3. Raft apply and persistence

Reused from Velocita, in the tape's own group.

| ID | Guarantee | Proven by |
|---|---|---|
| RAFT-1 | **Committed means durable on two of three disks** | Inherited: `v2-durable-quorum-tests` and the pinned fork's patch check, rerun on the tape group's configuration |
| RAFT-2 | **One order, applied once.** Every voter applies the same entries in the same order exactly once, including across restarts | Inherited replication tests, plus a tape test comparing the reducer-state digest on three voters after random restarts |
| RAFT-3 | **Corrupt storage stops the node.** A WAL block, snapshot or archive segment that fails its checksum or structure check refuses recovery instead of being repaired | Inherited power-cut sweep and archive adversarial tests, plus a test that the tape snapshot validator rejects each corrupted field |
| RAFT-4 | **Two of three keep committing** | Survival drill: one voter stopped, commits continue; it rejoins and its state digest matches |
| RAFT-5 | **Snapshots are exact.** Restoring the latest snapshot and replaying the rest of the log gives the same state as replaying everything | Property test over random logs: full replay and snapshot-plus-suffix produce identical canonical bytes |

Not guaranteed: that what was recorded is true. Raft guarantees agreement on what was recorded.

## 4. Attestation

Inside the tape reducer. Decides each message's verdict; never changes the book directly.

| ID | Guarantee | Proven by |
|---|---|---|
| ATT-1 | **One verdict per message.** Every (stream, `seqnum`) reported by any recorder gets exactly one final verdict, 3/3, 2/3, 1/3 or divergent, listing each distinct hash and which recorders sent it | Property test over random arrival orders. The oracle is an independent verdict calculator written from this table, never the reducer |
| ATT-2 | **Closing rule.** A verdict closes at the first of: all three recorders have reported, or the Nth tick after the first copy committed. Closing depends only on committed entries | The oracle computes each close index from the log alone; mutants that close one tick early or late, or on two reports, are killed |
| ATT-3 | **A closed verdict never changes.** A copy committed after close is recorded as late. If its bytes differ, it is recorded as a late divergence and the book rule BOOK-4 applies, but the closed verdict is not rewritten | A late matching copy and a late divergent copy, each checked against the oracle |
| ATT-4 | **Divergent bytes are kept.** Every distinct version of a message stays in the log and is named by the verdict | Three recorders with three different byte versions: the verdict lists three hashes, and the log holds all three payloads |
| ATT-5 | **Bounded.** Open verdicts live in a fixed-size table. If it is full, the oldest open verdict is closed at that index and marked "closed by capacity" | A flood test past capacity: the oracle predicts exactly which verdicts close by capacity |
| ATT-6 | **No clocks.** The reducer reads no clock and makes no system calls | A link-time symbol check that the reducer library references no clock, time, file or socket functions |
| TICK-1 | **One clock for everyone.** Ticks are numbered; the reducer applies tick k only if k is the last tick plus one. Any other tick is refused and recorded, so two leaders can't create two clocks | Two leaders proposing overlapping ticks across a forced election: the applied tick sequence has no gaps or repeats |
| TICK-2 | **Only voters tick.** A tick must be signed with a voter's ticker key; a recorder or outside caller can't tick | Tick signed with a recorder key is refused |
| TICK-3 | **Tick spacing is recorded, not trusted.** Each tick carries the proposer's UTC time. The reducer stores it but never uses it; the offline verifier flags spacing below T/2 or above a stated maximum | Verifier test on a log with a burst of fast ticks and an election gap: both are flagged |

## 5. Book reducer

Inside the tape reducer, alongside attestation.

| ID | Guarantee | Proven by |
|---|---|---|
| BOOK-1 | **Deterministic everywhere.** Book state and hash are a pure function of the committed log, and identical on x86 and ARM | Cross-architecture replay of the same archive on a NUC, the Pi and a Mac compares the hash at every index. The oracle is a separate reference book builder written from the AsyncAPI spec, not from the C++ code |
| BOOK-2 | **Integers, never rounded.** Prices and sizes are parsed from decimal strings into integers at the instrument's scale. A value with more decimals than the scale, or one that overflows, makes the book unknown with a recorded reason | Boundary tests on scale, maximum value and malformed strings, with hand-computed expectations |
| BOOK-3 | **First copy applies.** The book applies the first committed copy of each message. Later identical copies change nothing | Three identical copies: the hash changes once, at the first copy's index |
| BOOK-4 | **Divergence makes the book unknown.** A copy whose bytes differ from the applied copy makes that stream's book unknown from that index until a complete snapshot that starts after it | Divergent copy injected mid-stream: no hash is published from that index until the next complete snapshot, and the reference builder agrees on the first valid hash after it |
| BOOK-5 | **Missing messages are waited for, but only for a bounded time.** If a later `seqnum` commits before an earlier one, later messages are held in a fixed-size buffer for at most N ticks for any recorder to supply the missing one. If it arrives, messages apply in `seqnum` order; if not, or the buffer fills, the book becomes unknown until the next snapshot. Messages never apply out of order | Arrival-order property tests: the oracle predicts which case applies and the resulting state |
| BOOK-6 | **Snapshots are placed correctly.** A complete snapshot at or after the book's position replaces the book. An older one is recorded but not applied. Between SNAPSHOT and SNAPSHOT_COMPLETE the book is `building` and has no hash | A lagging recorder's old snapshot is ignored; an interleaved snapshot from a resyncing recorder is applied only when complete |
| BOOK-7 | **No false hashes.** Only a `valid` book has a hash. `building` and `unknown` publish their state and reason instead, which can't be mistaken for a hash | Every state transition checked for hash presence; a mutant that hashes an unknown book is killed |
| BOOK-8 | **Bounded state.** Fixed maximum levels per side, books and buffered messages. Exceeding one makes that book unknown with reason "capacity"; the reducer never allocates and never crashes | Capacity tests with a 10× level flood; a no-allocation check after start-up |
| BOOK-9 | **Integrity checks only observe.** The four integrity rules (not crossed or locked, EBBO matches DEPTH, trades inside the book and band, snapshots agree with updates) are recorded after each event and never change the book | The same log replayed with the checks on and off gives identical book hashes; each rule has fixture cases with hand-computed expected flags |

Not guaranteed: that the book equals the exchange's internal book. It is the public batched feed's book.

## 6. Proof log

A Certificate Transparency style Merkle log (RFC 9162) over committed tape events.

| ID | Guarantee | Proven by |
|---|---|---|
| PROOF-1 | **Append-only.** Each new signed tree head is consistent with the previous one, and the consistency proof between them is generated and checked | RFC 9162 test vectors for leaf and node hashing; a consistency check across every pair of consecutive heads in a long run |
| PROOF-2 | **Identical heads everywhere.** The tree head at a log index is computed deterministically from committed entries, so every voter signs the same bytes | Three voters' heads compared at every publication index |
| PROOF-3 | **A head needs two of three signatures.** A tree head is published only with valid public-key signatures from at least two voters' tape keys. HMAC is not used here, because outsiders must be able to verify | A head with one signature, a wrong key or a signature over different bytes is refused by the verifier |
| PROOF-4 | **Every event can be proven.** Each committed event has an inclusion proof against the first published head that covers it | For random events, the inclusion proof verifies against that head; a flipped byte in the event or the proof fails |

Not guaranteed: that the recording was complete or correct. Only that it was not changed after signing.

## 7. Metrics exporter

Runs on each machine as its own process.

| ID | Guarantee | Proven by |
|---|---|---|
| MET-1 | **Read-only.** The exporter has no path to propose or write to the tape group | A link-time check that the exporter target links no proposal or client-write code |
| MET-2 | **Recomputable.** Every metric labelled `source="log"` can be recomputed by the verifier from the archive and must match for closed periods. Everything else is labelled `source="local"` | The verifier recomputes each log-sourced metric for a fixture day and compares it with the exporter's values |
| MET-3 | **Uncertainty is shown.** Every time-based metric carries the clock error bound, and values below it are reported as below resolution | Fixture with a known error bound: the skew metric reports "below resolution" under it and a number above it |
| MET-4 | **Isolated.** An exporter crash or hang does not affect recording | Kill and pause the exporter during a recording run; the committed log is identical to a run without the exporter |

## 8. Offline verifier

Runs anywhere, including a laptop with no access to the cluster.

| ID | Guarantee | Proven by |
|---|---|---|
| VER-1 | **Trusts nothing in the cluster.** It reads only archived log segments, published tree heads and the voters' public keys | Run with networking disabled; it must complete |
| VER-2 | **Checks everything.** It rebuilds every book, recomputes every verdict, tick sequence, book hash, inclusion and consistency proof, and every log-sourced metric | Mutation of each artifact type (one entry, one hash, one proof, one metric) is detected |
| VER-3 | **Fails at the first problem, specifically.** It stops at the first mismatch and reports the log index, entry type, expected and actual values, with a non-zero exit code | Tests assert the exact reported index and values for each injected fault, not only that it failed |

## Parameters

Starting values. Each is reviewed after the day-one measurements and recorded in the ADR when it changes.

| Parameter | Start | Set by |
|---|---|---|
| Tick interval T | 1 s | Matches the feed's typical 1 s batching |
| Verdict close after N ticks | 5 | Must exceed the slowest recorder's usual lag; check the Pi's lag on day one |
| Missing-message wait | N ticks | Same bound as verdicts, so the book is never held longer than a verdict waits |
| Dead connection | 30 s without frames or heartbeats | True Markets' spec |
| Books recorded | 12, capacity 16 | The Orderbook Arena books |
| Levels per side, recorder ring, reorder buffer, open verdicts, entry size | To be set | Day-one measurements of depth, message size and rate |

## Assumptions checked on day one

These come from the [ADR](../adr/0030-the-verifiable-tape-is-a-separate-raft-group.md); a different
answer changes the guarantee named.

| Assumption | Affects |
|---|---|
| `seqnum` is one counter per channel, comparable across subscriptions | REC-3, BOOK-5, BOOK-6 |
| The no-login feed numbers published messages consecutively | REC-3, BOOK-5 and the gap-rate metric |
| A DEPTH UPDATE carries changes, not full replacement levels | BOOK-1 to BOOK-6 |
| The largest snapshot message fits a known bound | ING-4, BOOK-8 |
