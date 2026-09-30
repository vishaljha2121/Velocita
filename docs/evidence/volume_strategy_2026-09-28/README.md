# Volume strategy — band rebalancer on SOL, verdict 2026-09-28

Report: `SOL_BAND_REBALANCER.md` (`model` `research.volume_report`, Binance hourly
archive, checksum-verified, 2023-01 .. 2026-08).

1. VOLUME IS BOUNDED BY STAKE x TURNOVER. A 50/50 SOL/USDC rebalancer turns over
   3x (5 % band) to 24x (0.5 % band) its equity per year: $66 .. $471 a year on
   a $20 stake. Rebalancing alone cannot produce leaderboard-scale volume.
2. NO EDGE TO PAY FOR IT. The rebalancing premium is not distinguishable from
   shuffled history (p = 0.28 .. 0.71): no exploitable hourly mean reversion in
   SOL. In a trending period it LAGS holding (-20 % vs the un-rebalanced 50/50).
3. SO VOLUME IS BOUGHT, and its price is the markup: volume ~= loss budget /
   cost per unit volume. At 1 / 5 / 10 / 30 bp per side a $20 loss buys
   $200k / $40k / $20k / $6.7k of volume. The markup measurement decides this.
4. Round trips with no market view to hit a volume target are wash-like; the
   project's rule is that volume must come from trades with a stated reason.
   A band rebalancer has one (restore a target weight), but at a very narrow
   band it degenerates into round-tripping. The owner must decide where that
   line sits before any volume strategy runs.

UPDATE, same day: the markup is MEASURED (`../truemarkets_markup_2026-09-28/`): a
flat 1 % fee per side, route at market. A $20 loss budget buys about $2,000 of
volume. Leaderboard volume farming at the retail fee is NOT VIABLE unless the
prize is large or a lower fee tier exists.
