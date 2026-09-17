# Questions for True Markets

Written to be answerable without a back-and-forth: each one says what we already
checked, so nobody is asked to repeat published material.

Paste the blockquoted block; the surrounding notes are for us.

---

## Ask these (the blocking three)

> Hi - building against the Retail Gateway for DeFi spot. Three things I could
> not find in the docs, having checked the increment-sizes page, the market-data
> overview, the gateway API overview, the create-order reference, and the asset
> catalog itself:
>
> **1. Do DeFi orders have a minimum price increment and size increment, and if
> so, where are they published per asset?** The increment-sizes page is scoped
> explicitly to the CeFi Exchange and does not mention DeFi. Nothing on the 210
> `venue=defi` assets in `GET /v1/conductor/assets` carries a tick, lot,
> precision or minimum-notional field. If DeFi accepts arbitrary precision down
> to each token's `decimals`, that is a complete answer and I would like to be
> able to rely on it.
>
> **2. What is the request body for a DeFi LIMIT order on `POST /orders`?** The
> create-order page renders only the endpoint line for me, and the quickstart's
> example is a market order. Specifically: the field name for the limit price,
> and whether `qty_unit` accepts a base-denominated quantity as well as
> `"quote"`. I am deliberately not guessing, because a price field the API
> ignores would silently turn a limit order into a market order.
>
> **3. Is post-only available for DeFi limit orders?** The exchange overview
> describes price-time priority for the CeFi book and does not mention post-only
> either way. For a passive strategy this decides whether a quote that crosses
> pays taker fees.

---

## Ask these if the conversation goes well

> **4.** Is there a published maker/taker fee schedule for the DeFi path? The
> fee-schedule page appears scoped to CeFi.
>
> **5.** Roughly what does a DeFi fill cost on-chain, and who pays it? At a 1
> USDC notional this is the term most likely to decide whether passive quoting is
> viable at all.
>
> **6.** The `orderbook` WebSocket channel on `/v1/defi/market` - is there a
> schema, and does it carry sequence numbers or a checksum? Without one, a
> dropped message is invisible rather than detectable.
>
> **7.** Are DeFi assets across the six chain labels (`solana`, `base`,
> `robinhood`, `hypercore-spot`, `hypercore-perp`, `arbitrum`) subject to the
> same order rules, or do they differ by chain?

---

## Notes for us, not for them

QUESTION 1 IS THE ONE THAT MATTERS. It blocks rendering any priced order: the
adapter carries tick and lot separately, the cluster stores only their product,
and the two must reconcile exactly before anything is sent.

"ARBITRARY PRECISION" IS A REAL POSSIBLE ANSWER and is not a non-answer. If DeFi
accepts full token precision, we still need it stated, because the cluster's
`quote_micros_per_tick_lot` is an integer number of quote micros and cannot
represent a granularity finer than one micro. That would be a contract question
rather than a lookup.

QUESTION 7 EXISTS BECAUSE OF D-8. Six chain labels under one `venue=defi` means
"defi" may not be one venue with one rule set, and averaging them would be a
guess we would never notice making.

IF THEY ANSWER, record each value in `model/config/research.toml` with
`confidence = "verified"` and the answer's source, and move the matching row in
`TRUEMARKETS_DOC_DISCREPANCIES.md` to resolved. An answer in a chat is a source;
an assumption is not.
