# Holdout: the untouched final 4 weeks (2026-09-30)

[BACKTESTED] Run ONCE, after the recommendation was written into RESEARCH_LEDGER.md. Parameters, clip* and C0p's R are the research stage's; nothing was re-chosen here.

- Ticks: 80,640, 2026-08-31 00:00 .. 2026-09-27 23:59 UTC, 29 archives SHA-256 verified; oldest price used 0 s; >5 s old: 0.
- BTC $75,065 .. $87,392 (+8.7%).

| Candidate | Lots | Volume/wk | Fees/wk | Price P&L/wk | Net/wk | VPD | Week DD max | Worst week | Weeks loss>$12 | Fills/day | Kills/day | Cap weeks, median h | Research p5..p95 volume/wk; net/wk | Inside? |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A churn | 25 | $492.32 | $5.74 | $0.04 | -$5.70 | $86 | $6.29 | -$6.29 | 0/4 | 3.6 | 1,436.4 | 0/4 | $489.06..$887.47; -$10.34..-$5.68 | NO |
| A churn | 16 | $1,012.10 | $11.81 | $0.00 | -$11.80 | $86 | $11.91 | -$11.91 | 0/4 | 11.4 | 0.0 | 4/4, 1.3 h | $1,009.18..$1,027.59; -$11.97..-$11.72 | NO |
| A churn **(recommended)** | 12 | $1,019.70 | $11.89 | -$0.01 | -$11.91 | $86 | $11.99 | -$11.99 | 0/4 | 15.4 | 0.0 | 4/4, 1.8 h | $1,011.22..$1,030.26; -$11.98..-$11.82 | NO |
| B trend-gated churn | 25 | $492.32 | $5.74 | $0.04 | -$5.70 | $86 | $6.29 | -$6.29 | 0/4 | 3.6 | 1,436.4 | 0/4 | $0.00..$590.10; -$6.84..$0.00 | all weeks |
| B trend-gated churn | 16 | $1,012.10 | $11.81 | $0.00 | -$11.80 | $86 | $11.91 | -$11.91 | 0/4 | 11.4 | 0.0 | 4/4, 1.3 h | $0.00..$1,019.32; -$11.91..$0.00 | NO |
| B trend-gated churn | 12 | $1,019.70 | $11.89 | -$0.01 | -$11.91 | $86 | $11.99 | -$11.99 | 0/4 | 15.4 | 0.0 | 4/4, 1.8 h | $0.00..$1,026.42; -$11.95..$0.00 | NO |
| C rotate 30 min, trend-gated (R=30) | 25 | $492.60 | $5.75 | $0.13 | -$5.62 | $88 | $6.14 | -$6.14 | 0/4 | 3.6 | 2,769.3 | 0/4 | $0.00..$614.12; -$6.89..$0.00 | all weeks |
| C rotate 30 min, trend-gated (R=30) | 16 | $1,034.88 | $12.07 | $0.29 | -$11.78 | $88 | $11.90 | -$11.90 | 0/4 | 11.6 | 631.4 | 3/4, 21.2 h | $0.00..$1,026.74; -$11.86..$0.00 | NO |
| C rotate 30 min, trend-gated (R=30) | 12 | $1,037.14 | $12.10 | $0.14 | -$11.96 | $87 | $12.00 | -$12.00 | 1/4 | 15.4 | 0.0 | 4/4, 27.9 h | $0.00..$1,034.29; -$11.96..$0.00 | NO |
| C0 rotate 1440 min (R=1440) | 25 | $274.82 | $3.20 | $0.42 | -$2.78 | $99 | $4.18 | -$4.08 | 0/4 | 2.0 | 0.0 | 0/4 | $216.05..$280.71; -$4.49..-$1.16 | NO |
| C0 rotate 1440 min (R=1440) | 16 | $175.89 | $2.05 | $0.27 | -$1.78 | $99 | $2.67 | -$2.61 | 0/4 | 2.0 | 0.0 | 0/4 | $138.27..$179.65; -$2.87..-$0.74 | NO |
| C0 rotate 1440 min (R=1440) | 12 | $131.91 | $1.54 | $0.20 | -$1.34 | $99 | $2.00 | -$1.96 | 0/4 | 2.0 | 0.0 | 0/4 | $103.71..$134.74; -$2.15..-$0.55 | NO |
| C0p budget-paced rotate 213 min | 25 | $477.91 | $5.57 | $0.03 | -$5.55 | $86 | $6.30 | -$6.26 | 0/4 | 3.4 | 2,146.3 | 0/4 | $463.51..$870.28; -$9.92..-$5.63 | NO |
| C0p budget-paced rotate 213 min | 16 | $1,034.96 | $12.07 | $0.21 | -$11.86 | $87 | $12.02 | -$12.00 | 1/4 | 11.6 | 139.1 | 3/4, 149.8 h | $918.84..$1,075.69; -$12.01..-$10.13 | all weeks |
| C0p budget-paced rotate 213 min | 12 | $904.93 | $10.55 | $0.18 | -$10.37 | $87 | $10.94 | -$10.80 | 0/4 | 13.5 | 0.0 | 0/4 | $692.75..$901.13; -$10.42..-$7.52 | NO |
| D dip w=20 x=100bp y=50bp (w=20,x=100,y=50) | 25 | $19.64 | $0.23 | -$0.08 | -$0.31 | $63 | $0.98 | -$0.88 | 0/4 | 0.1 | 0.0 | 0/4 | $0.00..$58.11; -$0.54..$0.00 | NO |
| D dip w=20 x=100bp y=50bp (w=20,x=100,y=50) | 16 | $12.57 | $0.15 | -$0.05 | -$0.20 | $63 | $0.63 | -$0.56 | 0/4 | 0.1 | 0.0 | 0/4 | $0.00..$37.19; -$0.35..$0.00 | NO |
| D dip w=20 x=100bp y=50bp (w=20,x=100,y=50) | 12 | $9.43 | $0.11 | -$0.04 | -$0.15 | $63 | $0.47 | -$0.42 | 0/4 | 0.1 | 0.0 | 0/4 | $0.00..$27.89; -$0.26..$0.00 | NO |
| E trend(125) per tick | 25 | $4.86 | $0.05 | $0.42 | $0.37 | no loss | $1.12 | -$0.87 | 0/4 | 0.0 | 0.0 | 0/4 | $0.00..$495.85; -$5.81..$0.00 | NO |
| E trend(125) per tick | 16 | $3.11 | $0.03 | $0.27 | $0.24 | no loss | $0.72 | -$0.56 | 0/4 | 0.0 | 0.0 | 0/4 | $0.00..$951.94; -$11.91..$0.00 | NO |
| E trend(125) per tick | 12 | $2.33 | $0.03 | $0.20 | $0.18 | no loss | $0.54 | -$0.42 | 0/4 | 0.0 | 0.0 | 0/4 | $0.00..$951.57; -$11.77..$0.00 | NO |
| BH buy and hold | 25 | $4.86 | $0.05 | $0.42 | $0.37 | no loss | $1.12 | -$0.87 | 0/4 | 0.0 | 0.0 | 0/4 | $0.00..$0.00; -$1.24..$1.71 | NO |
| BH buy and hold | 16 | $3.11 | $0.03 | $0.27 | $0.24 | no loss | $0.72 | -$0.56 | 0/4 | 0.0 | 0.0 | 0/4 | $0.00..$0.00; -$0.79..$1.10 | NO |
| BH buy and hold | 12 | $2.33 | $0.03 | $0.20 | $0.18 | no loss | $0.54 | -$0.42 | 0/4 | 0.0 | 0.0 | 0/4 | $0.00..$0.00; -$0.59..$0.82 | NO |

Signal score on the holdout (25 lots, unlimited cash, no cap; fee-only = -116.6 bp): A -116.6 bp, B -116.6 bp, C -116.2 bp, C0 -101.2 bp, C0p -114.6 bp, D -158.6 bp, E +762.5 bp, BH +762.5 bp.

## The paper tooling's definitions on the holdout, 12 lots

| Flags | Run | Volume/wk | Net/wk | VPD | Fills/day | Kills/day | First capital kill (h) | Cap reached (h) |
|---|---|---|---|---|---|---|---|---|
| --strategy churn --period-s 60 | measured costs, $12 cap, top-up | $1,019.70 | -$11.88 | $86 | 15.4 | 0.0 | - | 4/4 wk, median 1.8 |
| --strategy churn --period-s 60 | PaperVenue 115/120, fixed $25, no cap | $1,336.43 | -$15.67 | $85 | 20.1 | 1,419.9 | 2.4, 2.3, 2.5, 2.2 | never |
| --strategy churn --period-s 60 | PaperVenue 115/120, fixed $25, $12 cap | $1,015.03 | -$11.92 | $85 | 15.3 | 0.0 | never | 1.8, 1.8, 1.9, 1.8 |
| --strategy trend_churn --value 125 --period-s 60 | measured costs, $12 cap, top-up | $1,019.70 | -$11.88 | $86 | 15.4 | 0.0 | - | 4/4 wk, median 1.8 |
| --strategy trend_churn --value 125 --period-s 60 | PaperVenue 115/120, fixed $25, no cap | $1,336.43 | -$15.67 | $85 | 20.1 | 1,419.9 | 2.4, 2.3, 2.5, 2.2 | never |
| --strategy trend_churn --value 125 --period-s 60 | PaperVenue 115/120, fixed $25, $12 cap | $1,015.03 | -$11.92 | $85 | 15.3 | 0.0 | never | 1.8, 1.8, 1.9, 1.8 |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | measured costs, $12 cap, top-up | $1,037.00 | -$11.94 | $87 | 15.4 | 0.0 | - | 4/4 wk, median 27.0 |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | PaperVenue 115/120, fixed $25, no cap | $1,341.90 | -$15.66 | $86 | 20.0 | 2,208.8 | 35.5, 34.5, 36.0, 32.0 | never |
| --strategy rotate --value 125 --period-s 1800 --exit-window-s 60 | PaperVenue 115/120, fixed $25, $12 cap | $1,027.16 | -$11.92 | $86 | 15.3 | 0.0 | never | 27.0, 26.5, 27.5, 26.0 |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | measured costs, $12 cap, top-up | $1,013.76 | -$11.69 | $87 | 15.1 | 0.0 | - | 3/4 wk, median 109.9 |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | PaperVenue 115/120, fixed $25, no cap | $1,131.86 | -$13.10 | $86 | 16.9 | 4.3 | 120.5 | never |
| --strategy dip --window-min 30 --enter-below-bps 20 --exit-above-bps 10 | PaperVenue 115/120, fixed $25, $12 cap | $1,003.86 | -$11.65 | $86 | 15.0 | 0.0 | never | 109.9, 158.3, 94.0 |
