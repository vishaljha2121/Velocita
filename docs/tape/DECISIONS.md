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

**D-47. The Pi's missing frames were my recorder's refresh racing the feed's one-request-at-a-time rule.**
Decoding the log by recorder showed the gap was almost entirely DEPTH updates (the Pi recorded 116 against
2,078 and 2,937 for the NUCs over 32 minutes; EBBO and INSTRUMENT matched), with no gap events. A fresh
60 s DEPTH-only connection from each host at the same moment received the same updates on all three (111,
111, 114), so the Pi's network was not the cause. The per-symbol timeline showed that on the Pi a refresh
was usually followed by no snapshot and no updates. The feed's CONTROL replies explain it: the server
handles one subscription request per connection at a time and answers a request that arrives while
another is in progress with `"status":"ERROR","message":"Authentication already in progress"`. The
recorder sent UNSUBSCRIBE and SUBSCRIBE back to back, so the unsubscribe often took effect and the
subscribe was rejected, leaving that symbol's DEPTH silent until a later refresh happened to get through;
NUC1 showed the same rejections less often. Fix: control requests go through a queue, one at a time,
each waiting for its reply (5 s timeout); a rejected request is retried after 500 ms; and every accepted
reply's subscription list is reconciled against the configured channels and symbols (at most every 30 s),
resubscribing anything missing, so a lost subscription heals itself. The test fixture now behaves like the
feed (replies with the subscription list, rejects requests while busy). New tests fail on back-to-back
sending and on dropping a rejected request; both mutants were checked by forced rebuilds. These live in the
Python recorder suite, which the C++ mutation harness does not build, so they are not in `mutation_tape.py`.

**D-48. The recorder tests had been skipping silently.** The CMake test passed in 0.2 s because the
configured Python lacked `websockets`; every recorder test was skipped and ctest still said "Passed".
`VELOCITA_TAPE_REQUIRE_FIXTURE` (default ON) now makes a missing fixture a failure; the image's builder,
which has no `websockets`, turns it off explicitly (the recorder tests run on the Mac instead, D-35).

**D-49. The Pi now records what the NUCs record.** After D-47's fix went live (3c56181), each recorder's
committed observations matched in every 10-second window for two minutes (for example 29/29/29, 54/49/49,
32/32/32), and the update verdicts over the next minute were 75 of 3-of-3 and none partial. The Pi's
lower total right after the rollout was a late start, not a lower rate: the first run of a new image on
the Pi spends minutes preparing its user-namespace layers on the SD card. So the earlier 60% deficit was
the refresh race (D-47).

**D-50. The watchdog fired once, and now brings the tape back by itself.** At 03:26 UTC the Pi's and NUC2's
watchdogs saw 2.3 and 1.4 MB/s for a minute and stopped their voters, so the tape stalled for about 30
minutes until I noticed. The trading Prometheus shows every host at 7 to 25 KB/s through 03:25, then a
minute (03:25 to 03:26) in which the Pi and NUC2 could not even be scraped and NUC1 sent 884 KB/s, then
quiet. I ran nothing that moves data over the network in that minute (my DEPTH probe ran at 03:36), so the
source is not identified: a Wi-Fi glitch amplified by Aeron's own retransmission (which the tape's byte
budgets do not bound) is the leading suspect. The watchdog now waits until the link has been quiet for
5 minutes after a trip, then restarts the voter and recorder; a second trip within the hour doubles the
wait (up to an hour). Six tests cover it (`tests/test_tape_watchdog.py`). Exporting Aeron's own byte and
retransmit counters is the next step, so the next burst shows whether the tape was the sender.

**D-51. The tape has its own Prometheus and Grafana on NUC1.** Adding a scrape job to the trading
Prometheus means editing and reloading a config that lives in the trading run's directory; that was
refused as too risky, and keeping the tape's monitoring separate is cleaner anyway. `velocita-tape-
prometheus` (127.0.0.1:9191, 7 days or 512 MB) scrapes the three tape exporters and, from the node
exporters, only network, disk, load and memory. `velocita-tape-grafana` serves the dashboard on NUC1's
tailnet address, port 3301, read-only to anonymous viewers with the login form disabled; its admin
password is random, in a 0600 file. The dashboard is generated by `tools/tape-grafana-dashboard.py`:
headline tiles, agreement between the three vantage points, book state per symbol, and cluster and
network health. Machine colours are fixed (NUC1 blue, Pi orange, NUC2 aqua) and checked for colour-blind
separation; verdict outcomes use the status colours. Nothing in the trading monitoring was changed.

**D-52. Aeron's own counters are on the dashboard.** The voter appends the embedded Aeron driver's system
counters (bytes sent and received, retransmitted bytes, retransmits, NAKs, heartbeats, status messages,
back-pressure events, short sends, loss gap fills, invalid packets, retransmit pool overflows, errors) and
the link's dropped sends to its metrics file. The recorder, which runs its own driver, writes the same plus
its frame, loss, submission and backlog counts to `recorder-metrics.prom` every 5 s, and the exporter now
serves both files. A new dashboard row shows each process's bytes on the wire, retransmissions, NAKs and
transport trouble, and the tape's share of each host's Tailscale send, which answers D-50's open
question the next time a burst happens. `AeronRuntime` only gained a read-only accessor; nothing the
trading code uses changed.

**D-53. The Aeron counters named the sender: the Pi's recorder, inside Aeron.** Within an hour of
deploying D-52 the Pi's watchdog tripped again (2,062 KB/s). Its recorder's embedded driver had sent
1.22 GB in about four minutes (about 5 MB/s) while the recorder itself could offer at most about 100 KB/s
under its budget; only 37 MB of that was counted as retransmission, and the recorder had sent 882,000
NAKs. This happened while the Pi's voter, which the recorder also publishes to, was being killed and
restarted every few seconds. So the floods are Aeron multiplying traffic when a peer is unreachable or
flapping, not the tape's own logic. A likely contributor: Aeron's default 1,408-byte frames exceed the
1,280-byte Tailscale MTU, so every full frame becomes two IP fragments and any loss doubles. Not yet
proven.

**D-54. The log outgrew memory and disk; the tape is stopped.** D-34's estimate came from a two-minute run
and was wrong by about ten times: the log grows about 30 KB/s (2.5 GB a day). Every voter keeps the whole
log in memory (compaction was deferred, D-07), at about 2.5 times its size on disk, so after 14 hours the
NUC voters sat at their 1 GB container limit and the Pi's voter was killed for lack of memory every few
seconds while replaying its 357 MB log. That crash loop is what set off the Pi's recorder (D-53). Memory
limits are now per host (`TAPE_MEMORY`: 3 GB on the NUCs, 2 GB on the Pi), but that only buys hours.
At 14:36 UTC on 2026-11-12 I stopped the voters, recorders and watchdogs on all three hosts so the cycle of
out-of-memory restarts and floods could not keep hitting the shared Wi-Fi. Logs, keys and configs are kept
(417, 357 and 419 MB); exporters, the tape Prometheus and the dashboard stay up. Restarting needs two
fixes: a bounded log (rotating the incarnation on a schedule, or real compaction) and Aeron frames that
fit the tunnel.

**D-55. The tape is a chain of segments (owner decision, 2026-11-12; 4 hours each, D-58).** At every
boundary every host's `velocita-tape-rotate.timer` runs `rotate.sh`: stop the recorder, wait 75 s so a final signed
head covers every recorded observation, stop the voter, move the segment to `data/segments/<incarnation>`,
and start the next segment. The new incarnation is the boundary's Unix time, so the three hosts agree
without talking to each other. A new entry type, `segment_start`, carries the previous segment's last
signed head (incarnation, tick, tree size, root); the first leader of a segment proposes it from the head
file named by `previous_head_file`, after checking that head has at least two valid signatures, and the
reducer accepts it once per segment (later ones are `segment_refused`). Because it is an ordinary leaf, the
new segment's signed heads cover the link. `velocita-tape-verify --previous-head FILE` checks a segment's
link against the previous segment's head and its signatures. The finished log is gzipped, and each host
keeps its newest `TAPE_ROTATE_KEEP` segments (NUC2 keeps all). Amends ADR 0030's single-log design:
history is now one hash-linked log per segment. Tests: the reducer and the Python reference agree on the
new entry (mutant `segment-start-twice` killed); six rotation tests with a stand-in `systemctl`; a local
three-voter run that rotates mid-run and verifies both segments and the link, and the verifier rejects a
link to the wrong head.

**D-56. Aeron frames fit the tunnel.** Every tape publication now uses `mtu=1216` (Aeron needs a multiple
of 32): with IP and UDP headers that is 1,244 bytes over IPv4 and 1,264 over IPv6, under Tailscale's
1,280-byte MTU, so no frame is split into IP fragments.

**D-57. Recovery no longer holds the log four times over.** Recovery read the whole log into a buffer,
copied every entry out of it, read the whole file again to find its valid end, and then Raft copied every
entry once more: about four times the log in memory at once, which matches the Pi's 1.45 GB for a 357 MB
log when it was killed (D-54). It now reads the file once, refers to entries by offset, finds the valid
end in the same pass, and frees the buffer once Raft has its copies, so the peak is about twice the log
and steady state about once. (`podman stats` also counts the log's page cache, which the kernel can
reclaim.)

**D-58. Segments are 4 hours, not 12 (owner decision after measurement).** Replaying NUC2's real 419 MB log
in an isolated voter: the old image peaked at 1,556 MB and held 1,521 MB afterwards; the new one (D-57)
peaked at 1,565 MB and held 1,118 MB. So D-57 cut what a voter keeps by about 400 MB but not the peak; the
real cost is about 1.45 KB of memory per entry for an average 545-byte entry (about 2.7 times the log, 3.7
times while replaying). A 12-hour segment (about 1.3 GB of log at 30 KB/s) would need about 3.5 GB held
and 4.8 GB to replay, more than the Pi or NUC1 can give. Four-hour segments (about 430 MB) need about
1.2 GB held and 1.6 GB at a replay peak, inside the per-host limits (3 GB on the NUCs, 2 GB on the Pi).
The timer runs at 00, 04, 08, 12, 16 and 20 UTC. Moving to longer segments needs the per-entry memory cut
first (keep payloads on disk and read them back only for a lagging follower).

**D-59. The 4-hour segments run, and they fit.** First rotation on 2026-11-12 at 16:00 UTC moved the stopped
tape's log (417 MB, 52 MB gzipped) into `segments/68934737119228` on every host and started segment
1794499200; its first entry links to head 44460 of the old segment, which the verifier confirms in place
along with 45 signed heads. Live memory: each voter has a base of about 610 MB and grows 1.2 to 1.6 MB per
MB of log, so a full segment at the busiest rate seen (30 KB/s, about 430 MB) ends near 1.35 GB on the Pi,
and a replay near the end peaks around 1.6 GB, both under its 2 GB limit. Next rotation runs from the
timer at 20:00 UTC. Further memory work is out of scope for the MVP.

**D-60. First findings are written up.** `docs/tape/FINDINGS-2026-11-12.md` records what one hour of the tape
showed: 98.4% of updates identical at all three vantage points and none divergent, a 35 ms median arrival
spread, per-book activity, clean book reconstruction, and the 19:14 incident (a non-tape burst on NUC1,
likely the dashboard being opened, amplified by Aeron retransmissions and stopped by the watchdog).

**D-61. The feed-quality report is built; NUC2 failed before its first real run.** `velocita-tape-verify
--extract` and `tools/tape-report.py` are built and pass 16 tests on scripted, signed segments (ctest
`tape-report-tests`, 14 of 14 tape suites); a local two-segment run produced plausible numbers (DEPTH cadence
median exactly 1,000 ms, matching the documented batching). The first real run was to happen on NUC2, which holds
the whole archive. At about 19:51 UTC NUC2 dropped off Tailscale while still answering ping on the LAN; over the
LAN, ordinary programs failed with "Input/output error" (load average 11), and then sshd stopped accepting
connections. That points to a disk or root-filesystem fault and needs physical attention. Around the same time the
watchdogs on NUC1 and the Pi tripped again (1.2 to 1.8 MB/s), so the tape is in a 20-minute cool-down. The
archive's only complete copy is on NUC2; NUC1 and the Pi keep their newest two segments. `docs/tape/TEST_MAP.md`
maps all 92 ADR test cases to tests: 49 covered, 22 partly, 21 not covered.

**D-62. First real report: 13.8 hours, two linked segments.** NUC2 came back on Tailscale but its sshd and node
exporter still failed, so the report ran on the Mac over NUC1's archive copy (60 MB, copied at 500 KB/s,
every file checksum-matched against NUC1), using the native verifier at the same commit; Docker Desktop could
not see freshly created directories, so the containerised verifier was not used. The Pi's copy was not
suitable: it was crash-looping near the end of the pre-rotation segment, so its log stops short of the head
the next segment links to. Running it found three bugs, each fixed with a test: segments were ordered by
incarnation number, so the pre-rotation segment (a random number) sorted last and failed its link; silences
were measured across tape outages (every book's "longest silence" was the 3.8-hour stop); and a silence
test without continuous ticks was unrealistic. Results (aggregates): 289,636 update verdicts, 91.0% identical
at all three recorders, none divergent; arrival spread p50 19 ms, p90 43 ms, p99 289 ms, clock error bound
3.9 ms; DEPTH update gap median about 1.0 s on ten of twelve books (2.0 s on AMZNC and TSLAC); hundreds of
silences over 30 s on quiet books, so the docs' 30 s rule would declare healthy connections dead without
heartbeats; market-hours spreads of 0.03 to 4.4 bp with top-of-book sizes of 0.005 to 0.22 shares, against
8 to 17 bp off-hours, and the rebuilt tops agree with the exchange's own best bid and offer (at most 5
mismatches per book). The 1-of-3 count (18,800) is concentrated in the morning's instability (the Pi's crash
loop and the pre-fix refresh race); a per-period breakdown would show it.
