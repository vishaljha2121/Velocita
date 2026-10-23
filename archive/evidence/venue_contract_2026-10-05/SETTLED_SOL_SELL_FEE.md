# Executed SOL sell: approximately 20 bp fee verified

The owner submitted the approved 0.0084 SOL sale through the pinned official
CLI v0.0.11 at 2026-10-05 19:21:53 UTC. The assistant did not sign or submit it.
At 19:24:02 UTC the public Solana `getTransaction` read, requested at `finalized`
commitment, returned the matching signature with `meta.err = null` at slot
453671564. This is chain settlement evidence, not just a CLI success message.

Transaction:
[Solscan](https://solscan.io/tx/4QmcU9go1f5eLoQmw9qhg4BjCeHb9igjGgbKBgjzpEAiJFnqgmnpLELaRP4khxf2P7s4cAK5UFeSQ55uiEPpDXea).

| Item | Verified amount |
| --- | --- |
| SOL debited | 0.008400000 SOL, exactly 8,400,000 lamports |
| Gross USDC transferred into owner's token account | 1.009007 USDC |
| USDC transferred out as fee | 0.002018 USDC |
| Net USDC retained | 1.006989 USDC |
| Fee / gross output | 0.1999986125%, approximately 20 bp |
| Remaining native SOL | 0.031896934 SOL |

The chain shows the owner's USDC account receiving 1,009,007 atoms and then
transferring 2,018 atoms to the separate fee-recipient account, leaving 1,006,989
atoms. This closely matches the CLI's quoted fee of 2,019 atoms. Classifying
that outgoing transfer as the fee is supported by its position and amount and
the displayed quote; the legal identity of the recipient was not independently
verified. The exact money movements do not depend on identifying that recipient.

Both native SOL pre/post balances and the USDC credit match the authenticated
TM balances. The net SOL change is exactly the requested trade input. The
38,000-lamport network fee was charged to a different payer account. That payer
also funded the newly retained USDC token account's 1,488,440-lamport balance;
its net native debit was 1,526,440 lamports. These were not additional SOL
deductions from the owner's wallet in this transaction. No statement about
every future transaction's gas/rent policy is inferred.

## Preview versus settlement

The owner saw 1.007579 USDC net and 0.002019 USDC fee before confirming.
Settlement delivered 1.006989 net and transferred 0.002018 as fee.

- Net shortfall from preview: **0.000590 USDC**.
- Gross swap output shortfall: **0.000591 USDC**, approximately 5.85 bp of
  quoted gross output (1.009598 USDC).
- Fee difference: **0.000001 USDC lower** than the preview.

This discrepancy is not evidence of another 1% service fee. The gross execution
output differs from the preview; market movement and routing effects were not
separately isolated. Without a synchronous external reference price or cost
basis, this is not a complete all-in spread estimate or realized trading P&L.

## Scope and research implication

This validates an approximately **0.20% sell fee for this executed CLI route**,
asset, size, account and time. It supersedes a blanket claim that this account
must always pay 1%, but does not invalidate the earlier 1% quote observations.
The new trade used an API-key-derived token and the released CLI's legacy
`/v1/defi/core/quote` and `/trade` flow. Endpoint, credential type, User-Agent,
size and time differ from the earlier MCP/Gateway checks; the cause of the
fee difference has not been isolated.

Do not claim a verified buy fee, 0.40% round trip, other-asset cost or maker fee.
Do not overwrite historical cost evidence or promote a rejected strategy based
on one sale. Research can register this measured sell-side cost for this route;
buy-side validation and a new approved cost-specific study remain separate.
At 20 bp alone, $50,000 of executed notional would incur about $100 in fees;
the $20/month loss objective still needs positive gross edge or lower costs.

## A reconciliation gap exposed

At 19:22:55 UTC, authenticated Gateway `GET /orders` and
`GET /transactions?type=order&limit=100` both returned empty lists, although
balances had updated and the transaction was subsequently found finalized on
chain. We cannot yet tell whether this is legacy-route coverage, indexing delay
or another service limitation. Absence from these lists is not proof of absence
of execution. The production adapter must reconcile the specific route's history
and chain evidence; this single trade does not close the live gateway contract.

The public RPC uses HTTP POST with the read-only `getTransaction` method and no
credentials. No `sendTransaction`, signing, order creation or new trade occurred
during reconciliation. The owner-attempt marker remains present; no repeat trade
or buyback was prepared or executed in this reconciliation.

## Evidence

- [Computed result and evidence fingerprints](settled_sell_fee_result.json)
- [Authenticated Gateway reads](settled_sell_api_reconciliation.json)
- [Finalized chain balance changes](settled_sell_chain_reconciliation.json)
- [Parsed transfer amounts with account indexes](settled_sell_parsed_transfers.json)
- The full public-chain receipt is retained privately outside the repo. Evidence
  uses account indexes rather than wallet addresses and contains no credentials.
- [Solana getTransaction contract](https://solana.com/docs/rpc/http/gettransaction)
- [TM transaction-list semantics](https://docs.truemarkets.co/trading-api/transactions/)

Exact integer assertions checked the matching signature, successful finalized
status, unique owner debit, token-account ownership and conservation:
1,009,007 - 2,018 = 1,006,989 USDC atoms. No C++ runtime state was modified.
