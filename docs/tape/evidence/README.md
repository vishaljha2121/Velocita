# Velocita Tape: evidence

Every number in the [project README](../../../README.md) is backed by a file in this directory. Each file is real
output from the commit it names, run on the archived segments, and each section shows the command that recreates it.
The evidence was regenerated on 2026-11-20 at commit `8577e42` and compared with the outputs saved when the results
were first written (2026-11-12 and 13). Where the two can be compared byte for byte, they are identical.

The raw log (every frame the recorders received) is not here: recorded feed data stays on the recording machines
and the owner's Mac (see [Data and scope](#data-and-scope)). What is here is enough to check the claims: the
aggregates, the signed roots, the public keys, one real book with its proof, and the test and mutation runs.

## Contents

| Claim | Evidence | Section |
|---|---|---|
| Both 2026-11-12 segments verify offline, and the second links to the first | `verify/verify-*.json`, `verify/nuc1-archive-sha256.txt` | [Offline verification](#offline-verification) |
| 289,636 verdicts, 91.0% identical at 3 of 3, 0 divergent; spread p50 19 ms, p99 289 ms | `report-2026-11-12/summary.json` | [Feed-quality report](#feed-quality-report-2026-11-12) |
| 221 trades: 187 consistent, 4 late, 30 unexplained | `report-2026-11-12/summary.json` (`trades`) | [Feed-quality report](#feed-quality-report-2026-11-12) |
| One NVDAC book is on the tape under a head signed by all three voters; one changed size fails | `proof/` | [Proof page](#proof-page) |
| The reference builder matches 23,723 real books; four of five broken clients are caught | `book-kit/` | [Book-builder kit](#book-builder-kit-on-the-real-stream) |
| 16 test suites pass; 34 of 34 planted bugs are caught | `tests/` | [Tests and mutation](#tests-and-mutation) |
| The two machines still running recorded 7.2 more hours on 2026-11-13 | `report-2026-11-13-two-recorders/`, `verify/` | [Two-recorder run](#two-recorder-run-2026-11-13) |

## Environment

| | |
|---|---|
| Commit | `8577e42` (branch `orderbook/baseline`) |
| Machine | Mac, arm64, macOS 26.6.2, Apple clang 21.0.0, OpenSSL 3.6.3, CMake/CTest 4.4.2, Python 3.14.6, Node 26.7.0 |
| Build | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVELOCITA_BUILD_TAPE=ON -DVELOCITA_TAPE_FIXTURE_PYTHON=<python with websockets>` then `cmake --build build` |
| Recording hosts | NUC1 (recorder and voter 1), Raspberry Pi 5 (2), NUC2 (3); home network, Tailscale between machines |
| Archive | NUC1's copy of each segment, copied to the Mac; every file's SHA-256 matches the value taken on NUC1 |

## Public keys

`keys/head-{1,2,3}.pub.pem` are the voters' Ed25519 head-signing public keys. Each was generated on its own
host during the key ceremony, and the private half never left that host. `keys/fingerprints.txt` holds the SHA-256
of each key's DER encoding, as `tools/tape-key-ceremony.sh fingerprints` prints them:

```text
voter 1  d8486f20c11a37ec2de1fc94ebef6c7e8a0f15533b6a7a9751cf654d8b0b1807
voter 2  7b886f91f0a3faf7518a50fe997f48c4ed12432a8762a98c14ba2d51bdf60d45
voter 3  5fb8b28617327d21903d272c3c28b56a75f8d840b5359412f1fcb104ca2780f6
```

These fingerprints are built into the proof page; they are the only thing a reader of a proof has to trust.

## Offline verification

The archive copy was checked first. `verify/nuc1-archive-sha256.txt` lists 824 files (both segments' logs, heads,
state and metadata), hashed on NUC1, and all 824 match on the Mac (`shasum -a 256 -c`).

```sh
velocita-tape-verify --wal 68934737119228/wal.log --heads 68934737119228/heads --incarnation 68934737119228 \
    --head-every 60 --key keys/head-1.pub.pem --key keys/head-2.pub.pem --key keys/head-3.pub.pem
velocita-tape-verify --wal 1794499200/wal.log --heads 1794499200/heads --incarnation 1794499200 \
    --head-every 60 --previous-head 68934737119228/heads/head-44460.json \
    --key keys/head-1.pub.pem --key keys/head-2.pub.pem --key keys/head-3.pub.pem
```

| Segment | UTC | Entries | Signed heads verified | Root of the full log | Link |
|---|---|---|---|---|---|
| `68934737119228` (before rotation) | 2026-11-12 00:07 to 14:36 | 769,565 | 741 | `e75c8935…0abbd` | first segment |
| `1794499200` (4-hour segment) | 2026-11-12 18:17 to 19:48 | 121,569 | 75 | `62200354…bd64` | to head 44460 of the first: `linked: true` |

Each run replays every entry through the voters' reducer, rebuilds the Merkle tree and checks every signed head
(two valid signatures each, and consistency between consecutive heads). `tick_spacing_flags` (14 and 2) counts the
places where the tape was stopped. The verifier reports these stops rather than hiding them; `coverage.gaps` in
the report lists each one with its length.

## Feed-quality report, 2026-11-12

```sh
python3 tools/tape-report.py --segments ARCHIVE --verify build/velocita-tape-verify \
    --key keys/head-1.pub.pem --key keys/head-2.pub.pem --key keys/head-3.pub.pem --out report-2026-11-12/
```

The rerun's `summary.json` and `report.html` are byte-identical to the ones produced on 2026-11-12 (`cmp`), so the
report is deterministic and nothing changed in the numbers since they were published. `report.html` is the
one-page view (open it in a browser); `summary.json` is the source for the figures below.

| Figure | JSON path | Value |
|---|---|---|
| Recorded hours, segments used | `coverage.recorded_hours`, `coverage.segments_used` | 13.772, 2 (0 excluded) |
| Update verdicts | `completeness.updates.closed` | 289,636 |
| Identical at 3 of 3 / 2 of 3 / 1 of 3 | `completeness.updates` | 263,497 / 7,339 / 18,800 |
| Divergent | `completeness.updates.divergent` | 0 |
| Arrival spread p50 / p90 / p99 / max | `timeliness.arrival_spread_ms` | 19 / 43 / 289 / 6,709 ms |
| Clock error bound | `timeliness.clock_error_bound_ms` | 3.935 ms |
| Trades: consistent / late / unexplained | `trades.class` | 187 / 4 / 30 of 221 |
| Trades inside / at the touch / outside the book | `trades.position` | 91 / 107 / 23 |
| Crossed books | `books.*.flags.crossed` | 0 on every book |
| Rebuilt top versus the exchange's EBBO, mismatches | `books.*.flags.ebbo_mismatch` | 0 to 5 per book |
| DEPTH cadence median | `books.*.cadence_ms.p50` | 1,000 to 1,020 ms on ten books, 1,998 to 2,000 ms on AMZNC and TSLAC |
| Silences over 30 s | `books.*.silences_over_30s` | 4 (SNDKC) to 388 (METAC) |
| Hour by hour | `periods[]` | see [FINDINGS-2026-11-12-report.md](../FINDINGS-2026-11-12-report.md) |

The interpretation of each figure, including every hour that falls short of 100% and why, is in
[FINDINGS-2026-11-12-report.md](../FINDINGS-2026-11-12-report.md).

## Proof page

`proof/proof.html` is the browser check, built with the published fingerprints and the real NVDAC-USDC bundle as
its sample. It is one 21 KB file with no network requests: open it in any browser, or host it as a static page.

```sh
velocita-tape-verify ... --book NVDAC-USDC --until-seq 173921 --bundle proof/nvdac.json > proof/nvdac-book.json
python3 tools/tape-proof/build.py --out proof/proof.html --fingerprints keys/fingerprints.txt --sample proof/nvdac.json
```

| File | What |
|---|---|
| `nvdac.json` | The 2.8 KB proof bundle: NVDAC-USDC's levels just after update 173921, the leaf's fields, 17 proof hashes, signed head 4500 and the three public keys. The verifier writes it only after the whole segment verifies |
| `nvdac-book.json` | The verifier's own output for that book: hash, levels, `log_index` 111428, and the inclusion check |
| `check-node.json` | `tools/tape-proof/check.js` run under Node on the bundle, pinned to the published fingerprints: all six steps pass, signed by voters 1, 2 and 3 |
| `nvdac-tampered.json`, `check-node-tampered.json` | The same bundle with the best bid's size raised by 0.001; the check fails at `inclusion` ("the proof does not lead to the head's root") |
| `proof.html` | The page. Its "Change one bid size" button makes the same change live |

The regenerated bundle and page are byte-identical to the ones built on 2026-11-13. The six steps, as the page
shows them:

```text
Verified: this book is on the tape, under a head signed by voters 1, 2, 3.
  ✓ bundle      well formed
  ✓ book hash   8d67705e95a279cef0f1260099144bdb9e48ec2f9995b68cf20aefeee41bdaf8
  ✓ leaf hash   5d38f2e382fc7e62c513e324568f1203fd9b548fbd65c33023cbcaa86cd3af0f
  ✓ inclusion   entry 111428 is in the tree of 120615
  ✓ keys        all keys match the published fingerprints
  ✓ signatures  3 of 3 voters signed this head
```

## Book-builder kit on the real stream

Each client was run on recorder 1's own DEPTH stream from segment `1794499200` (23,818 messages, twelve books).
The harness verifies the segment before it uses the stream or the answer key.

```sh
python3 tools/book-kit/conform.py --client "python3 -B tools/book-kit/reference_book.py" \
    --tape 1794499200 --recorder 1 --incarnation 1794499200 \
    --key-file keys/head-1.pub.pem --key-file keys/head-2.pub.pem --key-file keys/head-3.pub.pem \
    --verify build/velocita-tape-verify --scenario clean --report book-kit/reference.json
# and the same with --client "python3 -B tests/book_kit_traps.py <trap>" for each trap
```

| Client | Report | Result |
|---|---|---|
| Reference builder | `reference.json` | Passed: 23,723 valid books, every one checked, all equal to the tape |
| Adds update sizes instead of replacing them | `add-deltas.json` | Caught: NVDAC bid 165.516 shows 0.266, the tape has 0.133 |
| Treats a snapshot's number as the last update applied | `snapshot-is-last.json` | Caught: TSLAC on the second check |
| Parses decimals as floating point | `floats.json` | Caught: 2.142999999 where the tape has 2.143 |
| Reads the spec's `depth` field instead of `order_count` | `depth-field.json` | Caught: order count 0 where the tape has 2 |
| Uses the book before SNAPSHOT_COMPLETE | `serve-before-complete.json` | Not caught on this stream: every real snapshot arrived as one message. The kit's tests catch it on a two-part synthetic snapshot |

The reports contain counts and the first differing level only, never message text.

## Tests and mutation

```sh
ctest --test-dir build -R '^(tape-|book-kit)' --output-on-failure   # tests/ctest-tape.log
python3 tests/mutation_tape.py . build tests/mutation-tape.json      # tests/mutation-tape.log
```

- `ctest-tape.log`: 16 of 16 suites pass in 110 s. The recorder suite really runs against the scripted feed
  (11 tests in 48 s, not a silent skip; see D-48).
- `mutation-tape.json`: each of 34 mutants is applied to a scratch copy of the sources, compiled directly, and run
  against the test named for it. Verdict `KILLED` for all 34. The mutants cover the reducer's verdicts, the Merkle
  tree, the write-ahead log, HMAC checks, byte budgets, resend back-off and segment rotation.

[TEST_MAP.md](../TEST_MAP.md) maps the ADR's 92 test cases to these tests: 49 covered, 22 partly, 21 not covered.

## Two-recorder run, 2026-11-13

After the 2026-11-12 report, NUC2 failed (D-61: input/output errors, then no sshd) and has not come back. The other
two machines kept recording, and NUC1 still holds two complete segments from 2026-11-13. Both were copied to the Mac
on 2026-11-20; all 436 files match the SHA-256 values taken on NUC1 (`verify/nuc1-2026-11-13-sha256.txt`).
Recorder and voter 3 are missing from these segments, so every head carries the signatures of voters 1 and 2, which
is the quorum the design requires.

| Segment | UTC | Entries | Signed heads verified | Stops | Link |
|---|---|---|---|---|---|
| `1794571200` | 2026-11-13 12:01 to 16:01 | 298,843 | 238 | 0 | names head 14280 of `1794556800`, which NUC1 has pruned, so this link cannot be checked here |
| `1794585600` | 2026-11-13 16:01 to 19:13 | 281,127 | 191 of 192 | 0 | to head 14280 of `1794571200`: `linked: true` |

**One head file is empty, and it shows how the tape stopped.** In `1794585600`, `head-11520.json` is 0 bytes (its
SHA-256 in the checksum list is that of empty input, `e3b0c442…b855`), written at 19:13:23 UTC, the tape's last
moment. NUC1's root filesystem is at 100% today. The voter's `atomic_write` (`src/tape_voter.cpp`) writes to a
temporary file and renames it into place, but it never checks that the write succeeded, so on a full disk it
published an empty head. The verifier refuses the segment as it stands ("unreadable head"), which is the correct
result. To produce the figures below, that one file was left out and the other 191 heads were verified. Nothing
else was changed. The findings for a follow-up are to check the write and fsync before renaming, and to alert on
disk space before it runs out. A head that already exists is safe as long as the write is checked, because an
empty temporary file would then never replace it.

```sh
python3 tools/tape-report.py --segments SEGMENTS-2026-11-13 --verify build/velocita-tape-verify \
    --key keys/head-1.pub.pem --key keys/head-2.pub.pem --key keys/head-3.pub.pem \
    --out report-2026-11-13-two-recorders/
```

| Figure | JSON path | Value |
|---|---|---|
| Recorded hours | `coverage.recorded_hours` | 7.203, with two gaps of 5.6 s and 8.2 s at the segment boundaries |
| Update verdicts | `completeness.updates.closed` | 274,683 |
| Identical at both live recorders (closed 2 of 3) | `completeness.updates.2/3` | 274,532 (99.95%) |
| Seen by one recorder only | `completeness.updates.1/3` | 151 (all in 12:00, 16:00 and 17:00 UTC) |
| Divergent | `completeness.updates.divergent` | 0 |
| Arrival spread NUC1 to Pi, p50 / p90 / p99 / max | `timeliness.arrival_spread_ms` | 10 / 24 / 105 / 6,525 ms |
| Clock error bound | `timeliness.clock_error_bound_ms` | 4.821 ms |
| Sequence gaps, crossed books, EBBO mismatches | `books.*.events.gap`, `books.*.flags` | 0, 0 and 0 on every book |
| DEPTH cadence median | `books.*.cadence_ms.p50` | 1,000 to 1,002 ms on all twelve books |
| Trades: consistent / late / unexplained | `trades.class` | 609 / 26 / 46 of 681 (89.4% consistent) |
| Trades inside / at the touch / outside the book | `trades.position` | 267 / 385 / 29 |

`share_3_of_3` is 0.0 by construction, since no update can reach three of three with two recorders. The number to
compare is how often the recorders that are running agree: on 2026-11-12 it was 98.7 to 100% in every clean hour,
and here it is 99.95% over 7.2 hours with no stops at all. Every hour from 12:00 to 19:00 UTC closes at least 99.7%
of its updates at both recorders. The trade checker has three times as many prints to work with as on 2026-11-12,
and the share it explains rose from 85% to 89%.

## Data and scope

Recorded frames stay on the recording machines and the owner's Mac and are never committed. This directory holds
aggregates, signed roots and public keys, plus one deliberate exception: the proof bundle publishes one NVDAC-USDC
book (its price levels at one instant) so that anyone can check a proof end to end. All three recorders sat on one
home network, so the results describe delivery to one location and only the no-login feed tier.
