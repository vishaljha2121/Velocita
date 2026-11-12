# Feed-quality report: findings, 2026-11-12

First run of `tools/tape-report.py`, over NUC1's copy of two linked segments: the pre-rotation segment
(incarnation 68934737119228, 00:07 to 14:36 UTC) and segment 1794499200 (18:17 to 19:48 UTC). 13.8 hours were
recorded; 18 gaps when the tape was stopped are excluded from every rate. Both segments verified (741 and 75
signed heads), and the second links to head 44460 of the first. Aggregates only.

## Headline

| Measure | Value |
|---|---|
| Update verdicts | 289,636 |
| Identical at all three recorders | 91.0% overall; **98.7 to 100% in every hour without an incident** |
| Divergent (different bytes for one message) | **0** |
| Arrival spread across recorders | p50 19 ms, p90 43 ms, p99 289 ms (clock error bound 3.9 ms) |
| DEPTH update cadence | median about 1.0 s on ten books, 2.0 s on AMZNC and TSLAC |
| Silences over 30 s | hundreds per quiet book (up to 31 min off-hours); 4 to 21 on the busiest books |

## By hour

| Hour (UTC) | Recorded min | Updates | 3 of 3 | 1 of 3 | What happened |
|---|---|---|---|---|---|
| 00:00 | 19.9 | 8,089 | 44.0% | 4,421 | First night: resend floods (D-42) and the recorder refresh race (D-47) |
| 01:00 | 30.6 | 8,730 | 65.6% | 1,969 | Same |
| 02:00 | 60.0 | 15,573 | 57.1% | 3,885 | Same |
| 03:00 | 26.3 | 7,414 | 45.2% | 2,188 | Same; watchdog stop at 03:26; recorder fix live about 03:56 |
| 04:00 | 60.0 | 13,676 | 98.7% | 164 | Clean |
| 05:00 | 60.0 | 16,898 | 100.0% | 0 | Clean |
| 06:00 | 54.7 | 14,154 | 86.8% | 1,458 | Watchdog stop at 06:45 |
| 07:00 | 59.7 | 15,489 | 98.6% | 146 | Clean |
| 08:00 | 53.6 | 14,387 | 83.3% | 1,959 | Watchdog stop at 08:23 |
| 09:00 to 13:00 | 60.0 each | 15,629 to 39,879 | 100.0% | 0 | Clean; 13:00 is the busiest hour (US open at 14:30 UTC approaching) |
| 14:00 | 25.7 | 17,767 | 88.5% | 2,040 | Flood at 14:19, tape stopped at 14:36 |
| 18:00 | 43.0 | 21,846 | 99.0% | 11 | New 4-hour segment |
| 19:00 | 32.7 | 17,030 | 94.6% | 559 | Burst at 19:14 (FINDINGS-2026-11-12.md) |

Conclusion: when the tape itself is healthy, the three vantage points see the same updates; every shortfall lines
up with a known problem on our side.

## Book shape

Market-hours spreads are very tight (0.03 to 4.4 bp mean) but sit on very small sizes (median top of book 0.005
to 0.22 shares); off-hours spreads are 8 to 17 bp. Several books (GOOGLC, NVDAC) show a top one price tick wide
that does not change for long stretches. The rebuilt tops agree with the exchange's own best bid and offer
(at most 5 mismatches per book over the whole run), so these quotes are on the exchange's book, not an error in
the rebuild.

## Trades outside the book

**What it means.** When a trade print arrives, the reducer checks it against the book it has rebuilt from the
DEPTH channel at that moment: a trade price below the best bid or above the best ask is flagged. A trade normally
executes against resting orders, so its price should sit within or at the edge of the visible book. A flag means
the trade and the visible book disagree at that instant. Integrity checks only observe; they never change a book.

**What happened.** 23 of 221 trades (10%) were flagged.

| Period | Flagged | Distance outside | Reading |
|---|---|---|---|
| 02:30 to 02:47 UTC (AMZNC, MUC, SNDKC) | 7 | 1.5 to 17 bp | Before the recorder fix (D-47). AMZNC's top had not changed for about 18 minutes, and the bug could drop a book's DEPTH subscription while TRADE kept flowing. Most likely our stale book, not the exchange's |
| 13:49 to 19:43 UTC | 16 | mostly 0.01 to 0.2 bp (one to three price ticks); five at 1 to 5 bp | Fits TRADE and DEPTH being published in separate one-second batches: a trade can print against a level whose change only appears in the next DEPTH batch |

**Open questions for True Markets** (to ask, not claim): are TRADE and DEPTH batched on the same clock, and can a
trade be published before the DEPTH change it caused? What explains the larger cases, such as an NVDAC buy at
164.701 at 19:01:29 UTC while the book showed 164.783 / 164.784, below even the best bid?

**Limits of this check.** The comparison uses the book as of the trade's position in the tape's log, which
depends on arrival order across two channels batched separately, so sub-second disagreements are expected. The
report does not yet measure how long a disagreement lasts; the analysis above was done by hand from the extracted
rows and the logs.

## Caveats

All three recorders sit on one home network, so this measures delivery to one location, and arrival spread
includes our Wi-Fi. Only the no-login tier (1 s batching) is measured. The 1-of-3 counts mostly reflect our own
incidents, which is why the hourly view matters more than the overall share.
