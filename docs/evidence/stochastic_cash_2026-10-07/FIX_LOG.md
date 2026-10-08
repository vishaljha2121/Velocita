# Fix log after the checkpoint review

Follows `CHECKPOINT_REVIEW.md` (finding numbers below are that file's). Written
2026-10-08 00:30 UTC. Nothing here restarted a trading service.

| Finding | Commit | What changed | Verified by | On the hosts |
|---|---|---|---|---|
| A1 edge freezes after a late retry | `0063c86` | The retry is cleared on every path and the socket re-presents a message once | The isolated reproduction now ends after one re-presentation; 3 new tests; 3 new mutants killed | Not deployed |
| A2 one bad frame blinds the edge | `0063c86` | A rejected frame requests a fresh bootstrap; a bar ahead of the local clock is recorded but not traded; a latched stop is sent during warm-up | 3 new tests; 4 new mutants killed | Not deployed |
| A3 allowlist bypass | `0063c86` | The one-second socket goes through `ReadOnlyMarketSocket.__init__`; the endpoint is in `ALLOWED_ENDPOINTS` | 1 new test; 1 mutant killed | Not deployed |
| A4 Role panel always empty | `3d49cbe` | Query aggregates by voter instead of adding mismatched series | Live: returns 1, 3, 1 for the three voters | Installed |
| A5 dead and stale panels | `3d49cbe` | Elections no longer extrapolates; soak panels go blank when the bot is silent; the strategy page shows the running edge | All 176 queries run against live Prometheus | Installed |
| B7 loaded dashboards had no source | `3d49cbe` | The generator reproduces them; `--paper-run` adds the run label and writes `prometheus.yml` | Generated `prometheus.yml` is byte-identical to the loaded one; dashboards differ only by the fixes | Installed |
| B1 archive limit unmeasured | `9dcca2c` | Voter publishes archive bytes, segments and limits; two alerts; dashboard row; ADR 0023 proposes retention | 140 of 140 C++ tests; 6 mutants killed; promtool rule tests pass | Not deployed (needs new voter and exporter images) |

Also in `0063c86`: the committed mutation harness had a stale mutant
(`fee_epoch`) that no longer matched the source, so the script aborted before
running anything. It is repaired and runs without colour codes; 14 of 14 mutants
are killed.

## Installed on nuc1

The five dashboard files in `~/stochastic-cash-20261007/observability/` were
replaced with the generator's output for run
`c8a6a0a78fbf49a099b222124a5d557f-cash-v1`. The previous files are in
`~/stochastic-cash-20261007/observability-before-3d49cbe/`. Grafana re-reads the
directory every 30 seconds; no service was restarted. The files Grafana sees
inside its container match the generated ones by checksum. I could not confirm
through Grafana's API because it requires a login.

The raft dashboard installed on nuc1 is the `3d49cbe` version. The archive row
added in `9dcca2c` is not installed, because its metrics do not exist until the
voters are rebuilt.

## Correction to the review

The review said 56 alert rules were loaded. The correct number is 47. The rest
of that paragraph stands.

## Side effect to record

I ran `promtool test rules` in a throwaway container on nuc1. It used a full
core for about three minutes before I stopped it; the test file takes about a
minute even on the Mac. No election occurred, the candle age stayed under one
second and no unit restarted. `VelocitaCompactionPauseLong` was firing for the
Pi at the time, but it had started five minutes earlier (a 0.61 s compaction
pause on the Pi at 00:09 UTC) and is unrelated. Rule tests now run on the Mac in
Docker.

## Still open from the review

A1 to A3 need the edge image rebuilt and the edge restarted while flat. B1 needs
new voter and exporter images (health page version 6) and the rule file copied
to the host. B2 to B6, B8 and section C are unchanged.

## 2026-10-08 03:03 UTC: edge deployed

Owner approved deploying the edge only while the account was flat. It was
(venue report: 0 lots, 0 pending reports; kernel position 0).

- Image `localhost/velocita-v2-shadow:paper-cash-d542601`: the running image
  `paper-cash-9a71762` plus four Python files from commit `d542601`
  (`adapters/websocket.py`, `shadow/edge.py`, `shadow/run_stochastic_second.py`,
  `shadow/stochastic_second.py`). File checksums inside the image match the
  commit. The submitter binary is byte-identical to the previous image. The 25
  strategy tests pass inside the new image. This is a layer, not a build from
  the shipped tree; the owner accepted that to avoid a compile on the trading
  host.
- Only `velocita-paper-stochastic-edge.service` changed (image tag). The
  previous unit is `~/edge-d542601/velocita-paper-stochastic-edge.service.before`.
  The observer, gateway, voters and exporters were not touched.
- Restart took 15.6 s by the wall clock; the longest gap between recorded
  candles was 8.8 s. No trade was in flight.
- After: 0 errors, rule agreement 2 of 2, reconciled, `quiet` bars counted, the
  metrics file refreshed on quiet bars. Committed entries fell from 60 to 2 per
  30 seconds, so the archive now grows about thirty times more slowly.
- The strategy dashboard on nuc1 was replaced with the `d542601` version (new
  "Last decision" thresholds).

Deployed with this: A1, A2, A3 and ADR 0023 section 2.

## Observation: an election at 00:36:51 UTC

The term went from 2 to 3 and leadership moved from nuc2 to nuc1. No voter
restarted. nuc2's exporter could not be scraped for about 20 seconds at that
moment, so nuc2 was unreachable or stalled briefly. The cluster recovered by
itself, and the old edge (which still had the freeze bug) happened not to have
a trade in flight. Cause not established.

## 2026-10-08: kernel work built, not deployed

| Item | Commit | Verified by |
|---|---|---|
| Policy table 16 to 8; slots-used metric; `VelocitaPolicySlotsLow` | `d8ad46d` | 140 of 140 C++ tests |
| Kernel money ledger, fill price and fee checks, loss limit (ADR 0021) | `4c04f75` | 140 of 140 C++ tests; hand-computed ledger sequences; promtool rule tests |
| Promotion plan | `c34e414` | n/a |

These change the stored format (both schema versions 9 to 10). A voter built from them refuses the
running cluster's data, so they need a new incarnation, at a flat moment, with the owner's go-ahead.

Mutation testing: an early run reported one mutant as surviving that was in fact killed. The cause
was the harness, which could leave a stale binary between mutants. The harness now builds and
passes the clean tree before each mutant and confirms the mutant was compiled. Every mutant from
this log was rerun with it: 65 killed, 2 shown to be equivalent (they cannot change behaviour), none
unexplained. That includes the 15 from the archive and policy metric work.

## 2026-10-08: review fixes and the next incarnation prepared

| Item | Commit |
|---|---|
| Indicator lines on the paper dashboard (K, D, EMA 300 s, EMA 1200 s) | `8a15b98` |
| Archive usage in status captured only while the writer is idle (review A1) | `3264351` |
| Refused fills kept as evidence while their order is in conflict (review A2) | `f55209e` |
| Kernel and Raft review | `51a42bc`, `docs/evidence/kernel_raft_review_2026-10-08/REVIEW.md` |

New incarnation `abd1e149a26d4062ba5bbba2d90b3305` is prepared and not started:

- Source `f55209e` shipped to `~/src/ledger-f55209e` on all three hosts, whole-tree checksum
  `1622acd89b180fbc` matching the Mac export. The directory the monitoring mounts read was not
  touched.
- Images `localhost/velocita-v2:ledger-f55209e` (three hosts) and
  `localhost/velocita-v2-shadow:paper-ledger-f55209e` (nuc1), built at low priority with the full
  test suite inside the build.
- `~/ledger-abd1e149/` on each host holds `stage-host.sh`; on nuc1 also `prepare-profile.py`,
  `activate.py`, `install-monitoring.py`, the dashboards and scrape config for run label
  `abd1e149a26d4062ba5bbba2d90b3305-ledger-v1`, and the alert rules. The substitutions were
  dry-run on copies and change only the incarnation, `startup`, and image tags.
- The owner's ceremony is `docs/evidence/ledger_incarnation_2026-10-08/owner-ceremony.zsh`.

Not done: the cutover. It needs the account flat and the owner at the Mac for the ceremony.

Consequences the owner must accept before cutover: the paper account restarts at $25.00 with no
order history (the edge and the paper venue both fix funding at exactly $25), the 256-order
lifetime limit of the paper venue restarts, and the retired incarnation's files move to
`~/velocita-indicator-paper/retired/stochastic-c8a6a0a7-20261008`.

An image build was started twice on the Pi by mistake and the second was stopped after about
20 seconds. An election burst at 05:48 to 06:05 UTC (term 3 to 16) predates all of this work.
