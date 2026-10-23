# The paper tooling's strategies, as it defines them (2026-09-30)

The paper-enablement work defines phase on the clock (floor(event_time_s / period_s) mod 2), rotation on the clock (exit in the first `exit-window-s` of every period) and the dip on minute closes. These differ from the research candidates A-D (phase flips per decided tick, rotation after a held time, dip on a tick SMA), so the paper run is compared with THIS file's prediction. Research period only; the holdout is untouched. Nothing here was optimised: every parameter is the paper tooling's. [BACKTESTED; nulls SIMULATED]

| Flags | Research counterpart | Difference |
|---|---|---|
| churn --period-s 60 | A churn | same 2-minute cycle at exact 30 s ticks; with a slower cadence or an extra lost tick the clock version waits for the next odd minute |
| trend_churn --value 125 --period-s 60 | B | as above, gated on trend > 0 |
| rotate --value 125 --period-s 1800 --exit-window-s 60 | C (R=30, the in-sample choice) | clock period 30 min vs 30 min after each buy (31-min cycle) |
| dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | D | 30 completed minute closes, strict < and > (the D grid had 10- and 60-minute tick windows, 25-100 bp entries); NOT in the pre-registered grid |

## Cadence 30 s (524,160 ticks)

### Operational run: measured costs, $12/week cap, $25 restored weekly

| Flags | Lots | Volume/wk | Net/wk | VPD | Week DD max | Worst week | Fills/day | Kills/day | Cap weeks, median h |
|---|---|---|---|---|---|---|---|---|---|
| --strategy churn --period-s 60 | 12 | $1,020.89 | -$11.91 | $86 | $12.00 | -$12.00 | 17.7 | 0.0 | 26/26, 2.1 h |
| --strategy churn --period-s 60 | 16 | $1,017.73 | -$11.87 | $86 | $11.99 | -$11.99 | 13.2 | 0.0 | 26/26, 1.6 h |
| --strategy trend_churn --value 125 --period-s 60 | 12 | $392.45 | -$4.58 | $86 | $12.00 | -$12.00 | 6.1 | 0.0 | 10/26, 1.9 h |
| --strategy trend_churn --value 125 --period-s 60 | 16 | $389.97 | -$4.56 | $85 | $11.99 | -$11.99 | 4.5 | 0.0 | 10/26, 1.4 h |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 12 | $356.74 | -$4.14 | $86 | $11.98 | -$11.98 | 5.5 | 0.0 | 8/26, 27.5 h |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 16 | $368.55 | -$4.29 | $86 | $12.01 | -$12.01 | 4.3 | 0.0 | 8/26, 20.5 h |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 12 | $931.40 | -$10.77 | $86 | $11.99 | -$11.99 | 15.9 | 0.0 | 17/26, 113.6 h |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 16 | $980.61 | -$11.38 | $86 | $12.02 | -$12.02 | 12.6 | 0.0 | 22/26, 88.6 h |

### Paper as configured: PaperVenue 115/120 bp, fixed $25, no top-up; one week from each research Monday

p5 / p50 / p95 over the research weeks. 'No cap' is the system as deployed (the kernel has no weekly loss limit); '$12 cap' assumes the operator halts buying at $12.

| Flags | Lots | Cap | Volume first 24 h | Volume in the week | Net in the week | Fills | First capital kill (h) | Cap reached (h) |
|---|---|---|---|---|---|---|---|---|
| --strategy churn --period-s 60 | 12 | none | $1,333.52 / $1,448.14 / $1,497.58 | $1,337.17 / $1,453.88 / $1,528.22 | -$18.04 / -$17.21 / -$15.69 | 142 / 182 / 200 | 2.4 / 3.0 / 3.3 (26/26 wk) | never (0/26 wk) |
| --strategy churn --period-s 60 | 12 | $12 | $1,003.34 / $1,011.79 / $1,020.54 | $1,003.39 / $1,013.14 / $1,023.20 | -$12.00 / -$11.92 / -$11.81 | 106 / 126 / 134 | never (0/26 wk) | 1.8 / 2.1 / 2.2 (26/26 wk) |
| --strategy trend_churn --value 125 --period-s 60 | 12 | none | $0.00 / $0.00 / $1,350.34 | $0.00 / $0.00 / $1,387.65 | -$16.29 / $0.00 / $0.00 | 0 / 0 / 156 | 2.2 / 2.4 / 112.8 (10/26 wk) | never (0/26 wk) |
| --strategy trend_churn --value 125 --period-s 60 | 12 | $12 | $0.00 / $0.00 / $1,017.63 | $0.00 / $0.00 / $1,018.29 | -$11.97 / $0.00 / $0.00 | 0 / 0 / 114 | never (0/26 wk) | 1.7 / 1.9 / 112.2 (10/26 wk) |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 12 | none | $0.00 / $0.00 / $926.83 | $0.00 / $0.00 / $1,388.28 | -$16.07 / $0.00 / $0.00 | 0 / 0 / 150 | 33.0 / 36.5 / 102.5 (8/26 wk) | never (0/26 wk) |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 12 | $12 | $0.00 / $0.00 / $926.83 | $0.00 / $0.00 / $1,034.79 | -$11.89 / $0.00 / $0.00 | 0 / 0 / 110 | never (0/26 wk) | 24.5 / 27.5 / 92.0 (8/26 wk) |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 12 | none | $92.66 / $185.61 / $285.98 | $484.34 / $1,121.70 / $1,486.48 | -$17.16 / -$12.89 / -$5.56 | 63 / 134 / 187 | 101.6 / 136.5 / 167.0 (7/26 wk) | never (0/26 wk) |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 12 | $12 | $92.66 / $185.61 / $285.98 | $484.34 / $1,001.53 / $1,050.98 | -$11.96 / -$11.85 / -$5.56 | 63 / 114 / 138 | never (0/26 wk) | 74.5 / 112.9 / 160.6 (17/26 wk) |

### Fixed-parameter battery (30 s; signal run, 25 lots, unlimited cash; 100 permutations, 100 random walks; no optimisation, so no walk-forward)

| Flags | Round trips | Score | Perm p (null median) | Random-walk p (null median) |
|---|---|---|---|---|
| --strategy churn --period-s 60 | 131,039 | -116.7 bp | 0.980 (-116.6) | 0.911 (-116.6) |
| --strategy trend_churn --value 125 --period-s 60 | 35,496 | -116.6 bp | 0.386 (-116.6) | 0.436 (-116.6) |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 2,513 | -116.7 bp | 0.574 (-116.6) | 0.604 (-116.6) |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 1,840 | -115.8 bp | 0.030 (-116.5) | 0.020 (-116.6) |

## Cadence 36 s (436,800 ticks; live cadence: 30 s throttle + ~12 s refresh)

### Operational run: measured costs, $12/week cap, $25 restored weekly

| Flags | Lots | Volume/wk | Net/wk | VPD | Week DD max | Worst week | Fills/day | Kills/day | Cap weeks, median h |
|---|---|---|---|---|---|---|---|---|---|
| --strategy churn --period-s 60 | 12 | $1,022.52 | -$11.92 | $86 | $12.02 | -$12.02 | 17.7 | 0.0 | 26/26, 3.2 h |
| --strategy churn --period-s 60 | 16 | $1,018.76 | -$11.87 | $86 | $11.99 | -$11.99 | 13.2 | 0.0 | 26/26, 2.4 h |
| --strategy trend_churn --value 125 --period-s 60 | 12 | $391.95 | -$4.58 | $86 | $11.96 | -$11.96 | 6.1 | 0.0 | 10/26, 2.8 h |
| --strategy trend_churn --value 125 --period-s 60 | 16 | $390.18 | -$4.56 | $86 | $11.96 | -$11.96 | 4.5 | 0.0 | 10/26, 2.1 h |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 12 | $356.11 | -$4.14 | $86 | $12.00 | -$12.00 | 5.5 | 0.0 | 8/26, 27.5 h |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 16 | $367.59 | -$4.28 | $86 | $12.02 | -$12.02 | 4.3 | 0.0 | 8/26, 20.5 h |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 12 | $922.67 | -$10.71 | $86 | $12.04 | -$12.03 | 15.8 | 0.0 | 15/26, 114.5 h |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 16 | $978.25 | -$11.36 | $86 | $12.03 | -$12.03 | 12.6 | 0.0 | 21/26, 91.1 h |

### Paper as configured: PaperVenue 115/120 bp, fixed $25, no top-up; one week from each research Monday

p5 / p50 / p95 over the research weeks. 'No cap' is the system as deployed (the kernel has no weekly loss limit); '$12 cap' assumes the operator halts buying at $12.

| Flags | Lots | Cap | Volume first 24 h | Volume in the week | Net in the week | Fills | First capital kill (h) | Cap reached (h) |
|---|---|---|---|---|---|---|---|---|
| --strategy churn --period-s 60 | 12 | none | $1,330.33 / $1,455.48 / $1,507.94 | $1,330.33 / $1,467.73 / $1,536.38 | -$18.06 / -$17.23 / -$15.71 | 140 / 182 / 200 | 3.5 / 4.5 / 4.9 (26/26 wk) | never (0/26 wk) |
| --strategy churn --period-s 60 | 12 | $12 | $1,000.12 / $1,014.54 / $1,022.88 | $1,000.12 / $1,014.66 / $1,023.46 | -$11.99 / -$11.91 / -$11.81 | 106 / 128 / 134 | never (0/26 wk) | 2.7 / 3.2 / 3.4 (26/26 wk) |
| --strategy trend_churn --value 125 --period-s 60 | 12 | none | $0.00 / $0.00 / $1,349.36 | $0.00 / $0.00 / $1,380.14 | -$16.18 / $0.00 / $0.00 | 0 / 0 / 156 | 3.3 / 3.6 / 116.2 (10/26 wk) | never (0/26 wk) |
| --strategy trend_churn --value 125 --period-s 60 | 12 | $12 | $0.00 / $0.00 / $1,017.20 | $0.00 / $0.00 / $1,017.20 | -$11.93 / $0.00 / $0.00 | 0 / 0 / 114 | never (0/26 wk) | 2.6 / 2.7 / 113.1 (10/26 wk) |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 12 | none | $0.00 / $0.00 / $926.85 | $0.00 / $0.00 / $1,388.28 | -$16.14 / $0.00 / $0.00 | 0 / 0 / 150 | 33.0 / 36.0 / 103.0 (8/26 wk) | never (0/26 wk) |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | 12 | $12 | $0.00 / $0.00 / $926.85 | $0.00 / $0.00 / $1,034.79 | -$11.94 / $0.00 / $0.00 | 0 / 0 / 110 | never (0/26 wk) | 25.0 / 27.5 / 92.5 (8/26 wk) |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 12 | none | $92.67 / $186.36 / $285.97 | $468.71 / $1,036.42 / $1,481.47 | -$17.17 / -$12.12 / -$5.38 | 61 / 128 / 181 | 104.2 / 117.5 / 163.7 (6/26 wk) | never (0/26 wk) |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | 12 | $12 | $92.67 / $186.36 / $285.97 | $468.71 / $995.17 / $1,045.04 | -$11.97 / -$11.84 / -$5.38 | 61 / 112 / 138 | never (0/26 wk) | 78.0 / 114.3 / 167.1 (15/26 wk) |

Runtime 184 s.
