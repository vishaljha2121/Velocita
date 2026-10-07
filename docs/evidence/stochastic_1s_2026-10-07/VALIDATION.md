# Validation before build

- Clean worktree based on fa7ebd5; shared working tree untouched.
- Python model suite: 368 tests passed. Tool suite: 130 passed.
- New one-second suite: 10 tests; hand EMA oracle: N=3 inputs 1,2,3,6,2
  give warmup,warmup,2,4,3. Hand stochastic small-window oracle: raw K3=500/7,
  K4=(500/7+75)/2=1025/14, K5=75, D5=2075/28. Flat range gives 50.
- Six named mutations killed: EMA alpha, confirmation length, execution delay,
  future-candle acceptance, cooldown, cost-epoch history. See mutations.json.
- Local C++ paper-state suite passed including new durable fee-epoch oracle:
  two $0.10 fills at 30 bp cost 300 micros each; after restore at 20 bp a new
  $0.10 buy costs 200 micros; historical orders/cash are unchanged.
- Public feed smoke: three fixed keyless 1000-row 1s GETs returned 3000 bars.
  WebSocket delivered closed 1s bars; bootstrap backlog ages were 3517,2517,
  1517,517 ms. The first two exceed the 2s decision-age bound and are never
  forwarded as orders. No venue credentials, quotes or orders in this check.
- Existing compatibility tests retain the read-only observer contract.
- Not a completed economic backtest/null/holdout study. No profitability claim.
