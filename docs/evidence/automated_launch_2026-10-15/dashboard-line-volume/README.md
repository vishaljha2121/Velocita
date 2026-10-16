# Live chart, executed volume and decision-price comparison

Owner approved dashboard-only scope: observer and dashboard installation, with no driver update or restart. Checkpoint/staging proceeds under that approval. Trading processes remain untouched.
The owner's latest selection is decision-price difference in basis points.

## Exact behavior

- Price-chart markers use the same sampled Binance price as the line. Each retained confirmed effect appears at the first chart sample on or after its persisted first-confirmation observation time. This is a visual anchor, not a claim about the execution price or exact chain time. Zooming changes chart sampling.
- Executed volume sums successful retained buys and sells once: buy quote debit minus explicit fee; sell quote receipt plus explicit fee. It is fee-excluded USDC notional, not a claim about TM leaderboard credit. Missing/stale source does not become zero.
- The confirmed-fill table adds `Decision difference (bp, all-in)`. Buy = 10,000 × (effective settled price / decision close − 1). Sell = 10,000 × (1 − effective settled price / decision close). Positive is worse. This includes fees, routing and price movement during execution; it is not pure venue slippage.
- Old fills have N/A. No historical reference is guessed. The deferred driver change would append the actual decision candle close for future accepted intents with run/effect/side/sequence/source/decision times to `strategy-decision-references.jsonl` beside the published settlement snapshot. This contains no credentials. Only the driver writes it; the observer reads it.
- A missing reference or write failure affects telemetry only. It cannot retry an order or clear its pending state. Crash between acceptance and reference persistence leaves N/A. The file has a 512 KiB cap and rejects symlink/non-regular/multilink targets. The observer rejects conflicting or invalid references; matching requires run, effect, side and decision time preceding first confirmation.

## Validation

29 observer and 21 layout tests pass for this checkpoint. The separately prepared driver tests passed but that change is excluded. Hand-computed examples: $1.01 for .01 SOL against $100/SOL = +100 bp for a buy; $.99 for .01 SOL = +100 bp for a sell; $1.01 sale = −100 bp. Missing reference emits N/A and no numerical difference metric. Four freshly compiled observer-reference mutants were killed in this checkpoint; the three driver mutants are deferred with that work. Earlier timestamp/volume mutants were also killed. Promtool marker fixtures pass, including exact bucket boundaries and no point before/after the single marker bucket.

`before.json` is the installed dashboard from checkpoint 5abd5ad. `after.json` modifies only the price chart and fill table, then appends the volume row/panel. All other panels, IDs and dashboard settings are preserved. `prepared-hashes.json` identifies the reviewed local source.

## Approved dashboard-only deployment

1. Commit only the owned observer, generator/live JSON, observer tests and this evidence folder. Driver source/tests are excluded. Do not include unrelated dirty work or handoff files without their own review.
2. Stage hash-checked committed files. Archive the mounted dashboard and observer source; atomically replace them. The observer at `/opt/velocita/live-observer/live_observer.py` is root-owned, so owner runs that installation. It is invoked by a timer; no trading service or timer restart is needed. Grafana reloads the dashboard file.
3. DEFERRED by owner: future reference capture and any driver restart. Do not perform this step under dashboard-only approval. A future activation would require explicit approval. Preserve existing driver.json, state/driver-state.json, client session, opening record and all journals. Use the existing deployment root; never re-stage a fresh driver. Recheck flat/no pending state immediately before any restart; stop if it changed. Gateway, signer and voters stay running.
4. Verify loaded Grafana resource via its read-only database and Prometheus queries from the terminal. Confirm all unaffected service PIDs are unchanged. Owner checks visual rendering in Safari. Until step 3, all decision-price comparisons remain N/A.

## Latest read-only runtime observation

2026-10-16 00:59:32 UTC: gateway PID511436, signer511419, driver516074 active and unchanged. Raft telemetry index84, base0, quote24498113 atoms, pending0, fees202395 atoms, realized P&L−501887 atoms, eight successful fills, last admitted effect16. Driver sequence18, no stopped reason. These are ledger figures, not an independent TM-wallet read. Do not use this time-sensitive snapshot as a later restart preflight.

The previously documented cache-size and old 16-lifetime-effects wording issues remain review items; this patch does not change trading capacity, budgets, review expiry, authorizations or C++ behavior.

## Owner installation

`owner_install.py --run` installs the observer and mounted dashboard together from the committed source tree. Both previous and candidate hashes are checked before writing. Originals are archived under `/var/lib/velocita-live-observer/dashboard-update-backups/`; replacements preserve owners/modes and use atomic rename. It sends no service-control commands, and compares service PIDs/start times before and after. An isolated fixture verified dry-run preservation, refusal before writes on a stale hash, successful replacement and intact backups. Owner sudo remains required; this agent only stages the committed payload.
