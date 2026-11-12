# Verifiable tape: implementation plan

Eight hours of development time, starting from the locked design: [ADR 0030](../adr/0030-the-verifiable-tape-is-a-separate-raft-group.md),
[GUARANTEES.md](GUARANTEES.md) and [SYSTEM_DESIGN.md](SYSTEM_DESIGN.md). Written 2026-11-11.

## What "done" means at hour 8

1. The tape runs end to end on three voters and three recorders. It records, attests, builds integer books,
   appends to the Merkle log, and signs tree heads with two of three signatures.
2. The offline verifier rebuilds a recorded session on a laptop with no network and matches every hash.
3. The demo's steps 2 to 5 pass as a script: unplug a voter, rejoin, replay to a `seqnum`, and flip a byte.
4. 69 of the 92 locked test cases pass, every one against an independent oracle. The other 23 are listed
   below as deferred, so their guarantees count as not yet covered.
5. A mutation run on the core guarantees kills every mutant, using the harness that proves each mutant was
   rebuilt.
6. If the owner gates are open, recording runs live on the NUCs and the Pi. If not, a release bundle plus
   the exact owner commands to start it.

The 24-hour recording for the metrics, the one-page write-up and the video come after hour 8. They need
calendar time, not development time.

## Decisions made for the plan

| Decision | Reason |
|---|---|
| **Fork the replication layer for the tape** (`tape_replication.*`) instead of making `ReplicatedKernel` generic | `ReplicatedKernel` is about 2,100 lines that decode and apply trading `Command`s directly. Making it generic changes code the trading cluster runs and would need it requalified. The fork keeps the trading kernel untouched, as ADR 0030 requires. It drops authority proofs, the result ledger and gateway reads, which the tape doesn't need. Merging the two later is a follow-up |
| **Recorder in C++**, using the Velocita node client for submission | The design specifies the client protocol over Aeron with HMAC, and the node client already implements it |
| **WebSocket through libcurl** (`curl_ws_recv` and `curl_ws_send`) | The live-run TLS image ships libcurl 8.18 built against OpenSSL. Hour 0 checks that it includes WebSocket support. Fallback: a minimal RFC 6455 client over OpenSSL, about 300 lines. The Mac's system libcurl has no WebSocket support, so local builds use Homebrew curl |
| **Ed25519 tree-head signatures through OpenSSL 3** | Outsiders must be able to verify heads (PROOF-3). OpenSSL 3.6 is already a dependency |
| **Oracles in Python, written before the C++ they check** | The test-oracle rule. They import no tape code |

## Provisional parameters

Set from the design and the WAL's limits. Hour 0's feed checks may change them, without an amendment.

| Parameter | Value | Why |
|---|---|---|
| Entry payload limit | 1,700 bytes | A WAL record holds at most about 1.8 KB (`kMaxKernelCommandBytes` = 1,789, two records per 4 KiB block). Larger frames become `ObservationPart`s (ING-4) |
| Books | 16 (12 used) | The Arena's twelve books plus room |
| Levels per side (L) | 64 | Thin books; checked at hour 0 |
| Held messages per stream (B) and changes per held message (C) | 8 and 32 | Bounds the reorder buffer at about 98 KB in total |
| Open verdicts (V) | 256 | About 5 s of messages at the feed's batching rate, with headroom |
| Part assemblies (P) | 8 | One snapshot in flight per recorder, with headroom |
| Recorder ring | 4,096 observations | Several minutes of a Raft outage at the expected rate |
| Tick interval T, verdict wait N, head cadence K | 1 s, 5 ticks, 60 ticks | ADR 0030 |
| Snapshot refresh R | 15 min, staggered | Handling decision H-1 |

## Timeline

Each block ends with a check that must pass before the next block starts. If a block overruns, the cut line
below says what gives.

| Time | Block | Builds | Done when |
|---|---|---|---|
| 0:00–0:30 | **Setup and day-one checks** | `tape/` source tree and CMake targets with the symbol check (X-1.1, ATT-6.1, MET-1.1). The F-WS fixture server in Python. The libcurl WebSocket check. If the owner approves the subscription: a 15-minute capture of the twelve books, kept outside the repo, answering ADR 0030's four assumptions | Empty targets build, and the symbol check fails on a planted denylisted symbol. The four assumptions are answered, or recorded as still open |
| 0:30–2:00 | **Reducer core**, pure library | The oracles first: O-VERDICT, O-BOOK, O-MERKLE. Then the canonical entry codec (five entry types, parts), attestation and ticks, the book engine (integer parsing, levels, states, reorder buffer, snapshot placement), the four integrity rules, leaves and the Merkle frontier | `tape-attestation-tests`, `tape-book-tests` and `tape-proof-tests` pass against the oracles |
| 2:00–3:30 | **Tape voter** | The replication fork with tape codec, apply, digest and snapshot. Tape identity domain. Tick proposer, head signer, head publisher, state page. The `velocita-tape-voter` binary | `tape-raft-tests` pass in-process with three voters, including restart, snapshot and tick numbering across a forced election |
| 3:30–5:00 | **Recorder** | The WebSocket client, frame classifier, sequence tracker, liveness monitor, ring, submitter (parts, retry, sessions), snapshot refresh timer. The `velocita-tape-recorder` binary | `tape-recorder-tests` and `tape-ingress-tests` pass against F-WS and an in-process group |
| 5:00–6:00 | **Exporter and verifier** | `velocita-tape-exporter` (log-sourced metrics with labels), `velocita-tape-verify`, and `replay --until seq` | `tape-exporter-tests` and `tape-verifier-tests` pass, including the run with networking disabled |
| 6:00–7:00 | **End to end and mutation** | A local three-voter, three-recorder run on the Mac against F-WS replaying a captured session (or a synthetic one). The demo steps as a script. Mutation run on about 20 core mutants | The demo script passes, and every core mutant is killed by a named test |
| 7:00–8:00 | **Hardware and hand-off** | Images built natively on the NUCs and the Pi. Owner key ceremony. Deploy, start recording, Grafana board. Runbook and a results stub | Recording is live on three machines, or a release bundle and the exact owner commands are ready |

### Cut line

If a block runs over, these give way in this order:
1. The Grafana board (Prometheus queries alone are enough to start with).
2. The snapshot refresh timer (H-1). A manual resubscribe covers it at first.
3. The EBBO and trade integrity rules. Crossed or locked, and snapshot against updates, stay.
4. Hardware deployment moves past hour 8.

Never cut: reducer determinism, attestation, the Merkle proofs, the verifier, and the never-trades check.

## Owner gates

| Gate | When it's needed | What the owner does |
|---|---|---|
| G1. Approve the WebSocket subscription | Hour 0. Without it, development uses synthetic fixtures and the four assumptions stay open | Says yes |
| G2. Key ceremony | Hour 7 | Runs the key scripts with `!`, since they write the Keychain: recorder HMAC keys, peer keys, ticker keys and Ed25519 head-signing keys for the tape domain |
| G3. Deploy to the hosts | Hour 7 | Approves starting tape units on NUC1, NUC2 and the Pi. The trading cluster stays halted and untouched; the tape uses its own ports and data directories |

## Test cases

69 of the 92 cases in ADR 0030 are in scope for the eight hours. Each in-scope guarantee has its mutants run
in hour 6.

| Suite | In scope | Deferred |
|---|---|---|
| Cross-cutting | X-1.1, X-2.1 | X-1.2, X-2.2, X-2.3, X-3.1 |
| Recorder | REC-1.1, 1.2, 2.1, 2.2, 2.3, 2.4, 3.1, 3.2, 4.1, 4.2, 4.3, 5.1, 6.1, 7.1 | REC-1.3, 3.3, 4.4, 5.2, 6.2, 7.2 |
| Ingress | ING-1.1, 1.2, 2.1, 4.1, 4.3 | ING-1.3, 1.4, 2.2, 3.1 (with the amendment's session case), 3.2, 4.2 |
| Raft and persistence | RAFT-1.1, 2.1, 4.1, 5.1 | RAFT-1.2, 3.1 |
| Attestation and ticks | ATT-1.1 to 6.1 (all 11), TICK-1.1 to 3.2 (all 5) | none |
| Book | BOOK-1.1, 2.1, 2.2, 2.3, 2.4, 3.1, 4.1, 4.2, 5.1, 5.2, 5.3, 6.1, 6.2, 7.1, 8.1, 9.1 | BOOK-1.2 (hardware drill), 8.2, 9.2 |
| Proof | PROOF-1.1 to 4.2 (all 6) | none |
| Exporter | MET-1.1, 2.1, 2.2 | MET-3.1, 4.1 |
| Verifier | VER-1.1, 2.1, 3.1 | none |

Deferred cases are the first work after hour 8, starting with ING-3.1, RAFT-3.1 and BOOK-1.2, which cover
loss, corruption and cross-architecture determinism.

## Risks

| Risk | Effect | Response |
|---|---|---|
| The replication fork takes longer than 1.5 hours | Pushes everything after it | Strip it to what the tape needs, in the time box; push hardware past hour 8 first |
| The image's libcurl lacks WebSocket support | Recorder blocked | Fall back to the minimal RFC 6455 client over OpenSSL |
| The feed skips `seqnum`s by design, or updates carry full levels | Gap rule or book rule changes | Amend ADR 0030 under the guarantee IDs it names, then adjust BOOK-5 and REC-3 |
| The Pi can't keep up as a full recorder | Fewer copies per message | It votes and archives while the NUCs record (ADR 0030 day-one risk) |
| The Mac sleeps during long process tests | False timeouts | Check the `pmset` log before calling it a regression |
| Raw data leaks into the repo | Breaks the rules of engagement | Captures live outside the repo, referred to by hash; X-3.1 follows in the deferred list |

## Status at gate G2 (2026-11-11)

Built and verified on the Mac: scanner, codec, Merkle log, reducer, WAL, replica, voter, recorder (own RFC 6455
client), exporter, offline verifier, key tool. Results:

| Check | Result |
|---|---|
| Tape test suites (`ctest -R '^tape-'`) | 10 of 10, including the reducer-purity and never-trades symbol checks |
| Reducer against the Python reference | 29 tests, including the real two-connection capture replayed at every index |
| Recorder against the scripted WebSocket fixture | 8 of 8 |
| End-to-end, three voters and three recorders on the live public feed | 4 of 4; 120 s with machine 2 unplugged for 20 s: 12 books valid, identical roots, every signed head verified |
| Mutation run (`tests/mutation_tape.py`) | 27 of 27 killed (DECISIONS.md D-27) |
| Tape image on Linux arm64 (local Docker) | builds; 10 of 10 tests inside the builder; live 10 s recorder smoke run, 155 frames, 0 lost (D-35) |

Cut line used: the replica is new and compact (D-05), compaction is deferred (D-07, which also defers
RAFT-5.1's snapshot half), and hardware is a release bundle plus owner commands (D-33), because NUC1's disk
is full and the Pi is under the build floor. Amendment 2 to ADR 0030 (verdict keys, D-13/D-25/D-26) is
implemented and was approved by the owner on 2026-11-12.

## Status: live (2026-11-12)

The tape runs on NUC1, the Pi and NUC2 (incarnation 68934737119228, image de7bac0). Going live surfaced
three defects no local run could: the Pi's head signature was never published (D-40), and the tape's
resends amplified Wi-Fi loss into floods, first through log replication (D-42) and then through every
resend path under partition (D-43). Each is fixed with a test and a mutant; the mutation suite now
kills 33 of 33. Paper trading and the market-data recorder are paused by the owner (D-44).
