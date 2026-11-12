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

**D-21. Clock error comes from the kernel's NTP state.** On Linux the recorder reads `ntp_adjtime`'s
estimated error and reports "unknown" when the clock is unsynchronised; elsewhere it reports "unknown".
Shelling out to `chronyc` per frame was rejected as too slow. Skew metrics treat unknown as "below
resolution" (MET-3).

**D-22. The recorder reads on its own thread into a fixed ring.** The reader thread owns the WebSocket,
tracks each stream's expected `seqnum`, applies the 30-second silence rule and the staggered snapshot
refresh, and never waits for the cluster. When the ring is full it keeps reading, counts what it drops
and, once the ring has room again, records one overflow event per stream with the exact `seqnum` range
and resubscribes those streams (REC-5, H-2). Request numbers are assigned by the submitter when it takes
an item from the ring, so dropped frames never consume a number and ING-3's contiguity holds.

**D-23. The recorder has a sink mode for tests.** `--sink FILE` writes entries to a file and acknowledges
them at once, with an optional stall, so recorder behaviour is tested without a cluster.

**Finding: a framing bug caught by the fixture.** The first WebSocket reader restored a stale buffer
position after a read timeout that had already compacted the buffer, so the next frame was read from the
wrong offset. Against the live feed it looked like the venue closing the connection every few seconds,
which caused a resubscription storm (132 snapshots per channel in 20 seconds). The scripted fixture
(`tests/tape_oracles/fixture_ws.py`) reproduced it over plain `ws://` with a frame trace, the restore was
removed, and both the fixture (400 frames from 10 bytes to 70 KB) and a 30-second live run are now clean.

**D-24. The exporter serves a metrics file the voter writes.** Every second the voter writes `metrics.prom`
beside `state.json`, each metric labelled `source="log"` (recomputable from the log) or `source="local"`
(MET-2). `velocita-tape-exporter` is a small HTTP server that serves that file and links no tape code, so it
cannot write to the cluster (MET-1).

**D-25. A snapshot is a reply to one subscription, so its verdict is keyed per recorder. Extends proposed
amendment 2.** The first live three-recorder run showed two DEPTH snapshots of SNDKC, taken 49 ms apart by
different recorders, with the same `seqnum` (112266) but different levels: the later one already included
changes that the next published update (also 112266) then restated. So D-15 needs a correction: a snapshot is
the engine's live state at subscription time, labelled with the next update's number, not a fixed version.
Because update levels are absolute, applying that update to either snapshot gives the same book. Comparing
snapshots across recorders reported false divergence and made 2 of 12 books unknown within 90 seconds.
Fix, keeping BOOK-4 and BOOK-6 exactly as locked: snapshot verdicts are keyed (stream, kind, `seqnum`,
recorder), so two subscriptions' replies are never copies of each other; updates, which are broadcast, are
still compared across all three. The "snapshot agrees with updates" integrity rule now compares the old
book and the new snapshot after the next update has been applied to both, so it fires only on a real
reconstruction error. Both behaviours have tests (`test_snapshots_from_different_subscriptions_are_not_copies`,
`test_snapshot_still_wrong_after_the_next_update_is_flagged`), and the reference implements the same key.

**D-26. Every snapshot message is its own verdict.** The unplug drill showed D-25 was not enough: recorder 2
restarted (new session, same recorder number), and its new subscription returned an MSTRC snapshot with the
same `seqnum` as its first one, because MSTRC had published no update for a minute, but different levels.
Keyed by recorder, the two replies counted as copies and the book went unknown. A staggered snapshot refresh
on a quiet book would do the same. So a snapshot's verdict key now includes a tag derived from the
recorder, session and request number (`reply_owner`), which makes every snapshot reply its own verdict:
snapshots are never compared across copies, only placed by the book engine (BOOK-6) and checked against the
next update (D-25). Updates are still compared across recorders. Covered by
`test_a_recorders_own_resubscription_snapshot_is_not_a_copy`; amendment 2's proposed wording is updated to
match.

## Verification and hand-off

**D-27. Mutation bar: 27 targeted mutants, all killed.** `tests/mutation_tape.py` compiles each mutant
directly with `c++` in a scratch copy (no CMake, so no stale binary can answer) and checks that one named
test fails, after first checking that the same test passes unmutated. The first run killed 20 of 27; each
survivor was a missing or vacuous test (a capacity test whose frame never reached capacity, a duplicate-copy
path no test sent, a Merkle prefix only the reference checked) or an equivalent mutant, which was replaced
by one that changes behaviour. The rerun killed 27 of 27.

**D-28. The tape is opt-in at build time.** `VELOCITA_BUILD_TAPE` defaults to OFF so the pinned trading
image builds, which have no OpenSSL headers, are byte-for-byte unaffected. The tape image turns it on.

**D-29. The tape image builds from the recorded `velocita-dev` toolchain plus `libssl-dev` at the toolchain's
own OpenSSL version.** The readiness-TLS image has OpenSSL headers but exists only on the Pi; the dev image
exists on all three hosts with recorded IDs (`deploy/v2/TOOLCHAIN.env`) and ships `libssl3t64`
3.5.5-1ubuntu3.7. The builder installs `libssl-dev` at exactly that version and the runtime installs
`libssl3t64` at the same version, so the build fails loudly instead of drifting if the archive moves on.
`build-image.sh` refuses an unrecorded toolchain, as the trading build does. The runtime adds
`ca-certificates`, which the recorder needs for TLS peer verification.

**D-30. Units mirror the trading voter unit, with their own names, ports, directories and tmpfs.** Same
hardening (`--read-only`, `--cap-drop all`, `no-new-privileges`, journald, 1 GiB, 1 MiB Aeron terms so
shared memory stays far under the 256 MiB limit). Names are `velocita-tape-*`, data lives under
`~/velocita-tape`, secrets under `%t/velocita-tape-secrets`, and ports 45101–45103 (voters), 45201–45203
(recorders) and 45301 (exporter) are absent from every trading config. No one-cluster guard: the tape is
not a trading cluster, holds no trading secret and links no trading code (X-1 symbol check).

**D-31. The master secret goes through `velocita-secrets` (Keychain account `tape-master`); head keys are
born on their own hosts.** The HMAC master secret follows the trading secret path exactly: Keychain on the
Mac, pushed into a 0700 tmpfs, never on host disk. A head-signing key is the evidence that a voter vouched
for a tree head, so its private half is generated on that voter's host and never leaves it; only public
keys are collected (`~/.config/velocita-tape/public`) and copied to the other hosts, with SHA-256
fingerprints printed for the record. The private key sits in `~/velocita-tape/config` (mode 0700 directory,
0600 file) because the voter must sign after a reboot without a push.

**D-32. Key files are created 0600 atomically.** `save_private` opened the file under the process umask and
chmodded it afterwards, so another local user could open it in between and read the key once written. It
now uses `open(O_CREAT | O_EXCL, 0600)` and fsyncs, and never overwrites an existing key.

**D-33. Images are not built on the hosts yet.** Read-only checks on 2026-11-11 found: NUC1's root
filesystem is 100% full (0 bytes free; `~/velocita-md` holds 60 GB) while its Prometheus, Grafana and
Alertmanager are running; the Pi has 3.1 GB free, below the 4,000,000 KiB floor the earlier release builds
used; NUC2 runs `velocita-paper-stochastic-node`, and its user cgroup does not enforce CPU quotas. The
2026-10-09 build incident (lost leadership and scrape timeouts during host builds) argues against building
next to running services without the owner present. Freeing disk is the owner's call, so the hand-off is a
release bundle plus exact commands, which the plan allows. `run-build-guarded.sh` refuses to start below
4,000,000 KiB and stops the build below 3,000,000 KiB.

**D-34. A demo run is capped at 72 hours.** The 120-second live run wrote 481 KB of WAL per voter, about
3.5 KB/s or 300 MB a day. With compaction deferred (D-07), 72 hours is about 0.9 GB, which fits the Pi with
margin once the build floor is met, and is long enough to show an unplug drill and a few thousand signed
tree heads.

**D-35. The tape image was proven on Linux before any host sees it.** Docker on the Mac rebuilt the dev
toolchain from `Containerfile.dev` (same base digest, Aeron and Raft commits; arm64, OpenSSL
3.5.5-1ubuntu3.7, the version recorded on the hosts) and then the tape image from a clean `git archive`
export. That caught two Linux-only compile errors that macOS headers had hidden (`<cstdint>` in the
exporter, `<algorithm>` in the voter tests), and a full `ninja -k 0` build found no others. Inside the
builder all 10 `tape-` tests pass, the symbol checks included; the recorder fixture tests skip there because
the toolchain has no `websockets` module (they run on the Mac). The runtime image has no missing shared
libraries, writes keys 0600, and its recorder, run read-only in sink mode against the live public feed for
10 s, received 155 frames with none lost over verified TLS. The x86_64 build on NUC2 and NUC1 is still
unproven until step 1 of the runbook.

## Deployment (owner approved hosts, ceremony and deploy, 2026-11-11)

**D-36. NUC1's disk is freed through its own archive lifecycle, not by deleting data.** NUC1 filled on
Nov 5: the market-data recorder had grown `stream.db` to 49 GB, the Mac's nightly pull had been failing
since Nov 6 (ssh timeouts), so no day after Oct 28 was acknowledged and the nightly archive job could not
prune; once the disk was full it could not even start its container, and the recorder crash-looped. The
only file deleted is the Oct 28 archive (999 MB), which was acknowledged, past the 7-day archive retention
the job enforces, and whose Mac copy was re-hashed and matched NUC1's sidecar. Then `md-backup-pull` was run
by hand (to the IP) so the Mac fetches, verifies and acknowledges Oct 29 to Nov 3; the nightly job then
prunes and removes acknowledged archives as designed. No row of `stream.db` was touched by hand.

**D-37. The Pi's image is built on the Mac from the Pi's own toolchain image; NUC1 gets NUC2's image.**
Deleting old Pi images would free about 100 MB because they share layers, and a native Pi build needs a
4 GB floor it does not have. Instead the Pi's recorded `velocita-dev:current` was streamed into Docker on
the Mac (its MANIFEST hashes identically on both sides; Docker reports a different ID for the same image),
the arm64 tape image was built there with `TOOLCHAIN_ID` set to the recorded Pi ID, and loaded onto the Pi.
NUC2 built the x86_64 image natively, bounded (CPU 7, build slice, 1 h), 10 of 10 tests in the builder, and
that image was copied to NUC1, so no build ran beside NUC1's full disk or the Pi's small one.

**D-38. Binary streams use `/usr/bin/ssh`.** The interactive shell wraps `ssh` in a function that writes
terminal colour codes into stdout, which corrupted the first image stream. The tape scripts run under
`sh` and never see the function.

**D-39. Prometheus on NUC1 is left untouched for now.** Its config is a file inside `~/live-aae119b9`, the
trading live run's directory, so adding the tape scrape job would edit trading evidence. The tape's
exporters already serve `/metrics` on port 45301 on every host; a Grafana board can follow from a copy of
that config once the owner says where tape monitoring should live.

**D-40. A published head gains signatures as they land.** Live, the Pi's signature reached the log with the
matching root on every head, but always third, and the voter wrote a head's file once, at the second
signature, so no published head ever showed the Pi. A head's file is now rewritten whenever its count of
valid signatures grows, so it only ever gains signatures; any earlier copy stays valid. Covered by a new
voter test and mutant `head-never-gains-signatures`.

**D-41. The tape went live on 2026-11-12 00:07 UTC, incarnation 68934737119228, image b783f6a.** First minute
on the three hosts: one leader (voter 1, term 1), 12 of 12 books valid on every voter, 968 updates agreed 3
of 3, none divergent (the 6 seen by one recorder only are the seconds before recorders 2 and 3 started),
identical signed heads at ticks 60, 120 and 180, and an offline verify of NUC2's log from the Mac using the
public keys alone. Head-key fingerprints: voter 1 d8486f20c11a37ec2de1fc94ebef6c7e8a0f15533b6a7a9751cf654d8b0b1807,
voter 2 7b886f91f0a3faf7518a50fe997f48c4ed12432a8762a98c14ba2d51bdf60d45, voter 3
5fb8b28617327d21903d272c3c28b56a75f8d840b5359412f1fcb104ca2780f6.

**D-42. Each entry crosses the network once; the tape was stopped until that held.** About 25 minutes after
go-live the hosts, all on Wi-Fi, were each sending 1 to 2.5 MB/s over Tailscale, host-to-host pings lost 90
to 100%, Raft had moved through 36 terms and ticks had fallen to a quarter of real time. Books stayed
correct (12 valid, none divergent), but the tape was congesting the network it shares. Stopping the
recorders did not stop it; stopping the voters dropped every host to a few KB/s, and pings recovered to 0%
loss and ~10 ms. Cause: on every proposal and every 200 ms heartbeat the leader resent a follower every
entry it had not yet acknowledged, so traffic grows with the square of the round-trip time. On localhost
that was invisible; on a relayed Wi-Fi link it fed on itself. Recorders had the same shape: every
unacknowledged observation resent every 2 s to all voters, with no backoff. Fixes, both relying on Aeron
already delivering reliably and in order: the replica sends a follower each entry once, and resends only
after the follower reports a gap or 2 s pass without progress; the recorder backs off each resend (2, 4, 8,
16, 32 s) and resends at most 64 per interval. New tests run the replica over a 400 ms link (before: 5,314
entries sent for 303; now at most 1.25 per entry per follower) and over 10% message loss, and the submitter
through a 60 s outage; mutants `replica-resends-in-flight` and `recorder-resends-without-backoff` are killed.
The runbook now checks each host's send rate after start.

**D-43. Every tape process has a hard bandwidth ceiling, and a watchdog stops the tape if the host's
Tailscale traffic stays high.** D-42 was not enough. At 01:00 UTC a 1 GB upload from the Mac saturated
the shared Wi-Fi; host-to-host pings lost 40 to 100%, and the tape then kept the network down after the
upload stopped. The Pi, with no leader, sent 900 KB/s (recorder about 400, voter about 500) and NUC2
1.4 MB/s; stopping the tape brought every link back to 0% loss within a minute. The amplifiers were
everything the tape resends when the other side is silent: each new leader resending 256 KB batches to
followers that had fallen behind, resends that never backed off while new entries kept arriving, recorder
resends to all three voters, and Aeron's own retransmission of large messages. The fixes give every
loop a ceiling that does not depend on the network behaving:
- the replica spends a per-follower byte budget for log entries (64 KB/s, 64 KB burst), sends at most 32
  entries or 32 KB per message, and after 2 s without progress sends one batch as a probe, doubling the
  wait up to 16 s, instead of re-sending the backlog;
- the recorder spends a 32 KB/s send budget (unsent observations wait in the queue), and resends at most
  32 per interval with per-observation backoff;
- the election timeout is 3 s (the Wi-Fi links spike past 500 ms), and Aeron's receiver window is 64 KB;
- `velocita-tape-watchdog` stops the tape's voter and recorder if `tailscale0` sends more than 600 KB/s
  for 60 s, and leaves `watchdog-tripped` in the data directory. The tape's own worst case is about
  200 KB/s, so the watchdog fires only when something has gone wrong; it counts all Tailscale traffic, so
  stop it during deliberate bulk transfers.
Tests: a silent follower now costs at most 192 resent entries in 60 s (960 without backoff); catch-up
of a 3,000-entry backlog never exceeds the budget plus one burst in any second (142 KB without the
budget); the submitter never exceeds its budget (92 KB in one second without it). Each has a mutant.

**D-44. Paper trading and the market-data recorder are paused; only the tape runs.** Owner instruction
2026-11-12: pause the trading cluster and run only the tape on the three machines. The v2 trading voters
were already stopped. Stopped now: `velocita-paper-stochastic-node` and its exporter on NUC1 and NUC2
(the Pi's had already failed), and on NUC1 `velocita-md-stream` and the `velocita-md-archive` timer. Paper
trading uses a paper venue, and its ledger stays on disk and is reconciled at restart. The market-data
recorder was refilling NUC1's disk (300 MB/h) and downloading about 500 KB/s over the shared Wi-Fi. Left
running: monitoring (Prometheus, Grafana, Alertmanager, ntfy, node exporters) and the trading secrets
check timers. Units stay enabled, so a reboot would start them again.

**D-45. My deletion of six unpruned market-data archives blocked their prune; repair is staged.** D-36
deleted the Oct 29 to Nov 3 archives on NUC1 after the Mac had verified copies, assuming the nightly job
removes acknowledged archives. It removes only *pruned* ones, and pruning requires the archive on host
to verify, so those six days' rows (about 3.65 million) can no longer be pruned, and the 00:30 UTC
archive run failed for lack of temporary space. The repair restores each archive from the Mac's verified
copy (checked against NUC1's ack), restores its manifest (rebuilt from the archive: rows, id range and
both hashes match the originals' rules; `archived_at_ns` is the original file time to the minute), and
then runs only the job's own `prune()` and `remove_old_archives()` under its lock. The tooling is in
`~/.cache/velocita-md-repair`. It is paused with the market-data recorder (D-44); uploads must be
rate-limited (`scp -l`) so they cannot flood the Wi-Fi.

**D-46. First 100 minutes under the ceilings, and two findings.** From 01:35 to 03:15 UTC the tape held
one leader except for a single election (term 54 to 55), kept ticks in real time, published 119 signed
heads that verify on every host with all three signatures, and kept 12 of 12 books valid with none
divergent; hosts sent 11 to 25 KB/s and no watchdog tripped. The election coincided with my offline
verification copy: pulling the logs to the Mac drove the Pi's send to 630 KB/s even at a 1 MB/s scp
limit, and that copy then stalled for 81 minutes. Verification now runs in place on each host with the
verifier inside the image, and the head files are compared across hosts, so nothing large crosses the
Wi-Fi. Second finding: the Pi's recorder receives about 60% of the frames the NUCs receive (16,686 against
23,289 over the same period) with no sequence gaps, no ring loss and nothing unacknowledged, so roughly a
third of updates close 2 of 3 and some Pi copies arrive after the verdict closes (585 late copies). The
tape records this as designed; why the Pi's feed connection carries fewer frames is open (next: compare
per-channel frame counts between the Pi and a NUC).
