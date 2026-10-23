# Live run of 2026-10-15/16: collected evidence

Incarnation `aae119b95ddcea5a206f03ab5ca12c15`, release `ba299ff`, $25 managed USDC, 0.12 SOL clips.
Owner wallet `DYCV956p6xfScqV9Y53zJ3xrZUGHskrQACrMgZ8n6FPr` on Solana mainnet.

| File | What it is | How it was collected |
|---|---|---|
| `chain-transactions.json` | Every finalized transaction of the owner wallet: slot, block time, error, network fee, fee payer, and the owner's SOL and USDC change | `getSignaturesForAddress` and `getTransaction` (jsonParsed, finalized) against the public `api.mainnet-beta.solana.com` on 2026-10-23 |
| `trades.json` | The 16 settled swaps joined across sources: driver intent time, on-chain block time, Raft settlement, Binance reference at intent and at fill, round trips and totals | Joined from the two files here, the Raft observation read by the effect 25 preflight on 2026-10-16, and Binance `SOLUSDT` 1 s klines from `data-api.binance.vision` |
| `driver-events.txt` | The strategy driver's journal reduced to starts, stops, intents, commit results and feed restarts (times are EDT, UTC-4) | `journalctl --user -u velocita-strategy-driver.service` on NUC1 |

Checks made when the files were assembled:

- For each of the 16 settlements, Raft `base_atoms` equals the owner's lamport change and Raft `quote_atoms`
  equals the owner's USDC atom change in the matching transaction.
- The eight round trips sum to -952,140 USDC atoms, equal to Raft `realized_pnl_atoms`.
- The fee payer of every swap is the venue's `A93NGHwpaa5xB7kzuiWqhCzZjtdhNFa8g4gduk3wvdXy`; the owner paid no
  network fee.
- Effect 25's transaction `3gPjgFxhmeoP2MornCnoMSjymr7b5XaEJMdje4LAMNWV1Cu8EVXGjxmq81J6qLYnCauxE2GaGqbXutkGUBasqiqk`
  landed in slot 457481187 and failed with `InstructionError [2, Custom 5]`; the owner's balances did not change.

Not available: Prometheus keeps one week, and by 2026-10-23 its series for the trading hours had expired. The
gateway's system journal, which names the reason each released effect was refused, was not read for this record.
