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
