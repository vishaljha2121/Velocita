# Invariant layout and supported route selection — October 14

## Implementation update

After the owner authorized the next steps, exact Invariant/schema5 support was
implemented locally, with two agents handling disjoint tests and review/renewal
files. The retained transaction now passes the **updated C++ verifier offline**,
using a historical clock and synthetic intent. The installed executable has not
changed and still refuses this route. No new quote or money movement occurred.

Validation: 10 decoder groups, nine verifier groups, eight review groups and66
Python tests pass. 66 production-source mutations fail their named assertions
(37 decoder,19 verifier,4 review,6 Python); decoder/verifier baselines and mutations
run with ASan/UBSan. The actual private capture passes at6,910,173lamports and
fails at6,910,174. C++/Python signing, protected-signing and runtime digests agree.
All four existing route-policy digests match the1a46fd8 baseline. Independent
review found no blocking defect. See `IMPLEMENTATION.json` and the retained test
reports for exact scope; none of this substitutes for native NUC/Pi qualification.

Next: scoped committed checkpoint, native qualification, then locked adoption
and fresh account/quote checks within the already-authorized single-$1 scope.
There are no kernel, Raft, snapshot, funding or catalog changes in this batch;
the existing qualified voters can remain on1a46fd8. Do not apply the price
amendment when a fresh quote already clears the original committed cap.

## Investigation record (before implementation)

The retained unsigned TM buy matches Invariant's published direct-swap account
layout. **TM's current public API does not document a DEX-selection parameter.**
The installed/qualified verifier still rejects this route. No adoption, price
amendment, signature, execution request, service start or funds movement occurred
during this investigation.

## What was established

Two keyless, finalized Solana account reads resolved the lookup table and all 19
transaction accounts at slot 456859959. An independent offline v0 wire decoder
matches the retained decoded transaction, tied to response SHA-256
`d13657c1eb5c651545aea1cca0e01c85d4d89c7498a5e8432319e77c105d0f17`
and payload SHA-256
`15bfdf29bcd880167ff8715a58ad7ccda512a312a8120960f828e6e855951760`.
Raw transaction/account records remain private.

| Property | Captured layout |
|---|---|
| Solana header | v0; 2 signatures, 0 readonly signed, 6 readonly unsigned |
| Accounts | 11 static + 4 writable lookup + 4 readonly lookup = 19 |
| Outer instructions | Compute limit; sponsored ATA creation; Jupiter route; WSOL close to owner; owner-to-sponsor repayment |
| Jupiter route | One step; enum18 `Invariant`; `x_to_y=true`; 100%; input0 → output1 |
| Invariant program | `HyaB3W9q6XdA5xwpU4XnSZV94htfmbmqJXZcEbRaJutt` |
| Direction | Pool token X = USDC, token Y = wrapped SOL |
| Pool relationships | Source/destination, both vaults, vault mints, state authority and tickmap match |
| Address seeds | Pool, state, program authority, destination ATA and Jupiter event authority match |
| Extra CPI accounts | No tick/referral accounts in this capture; no generalized acceptance inferred |

The official [Invariant protocol swap source](https://github.com/invariant-labs/protocol/blob/a30e3b6d86a34400fae202299dba969daff36359/programs/invariant/src/instructions/swap.rs)
and [Jupiter adapter account builder](https://github.com/invariant-labs/jupiter-invariant/blob/687df8f18cf76d33412b95e168b7b70ce500cfb1/src/accounts.rs)
agree with the captured account order. Pool/state discriminators, sizes and fields
match these sources. This is **not a reproducible-build proof that deployed
Invariant or Jupiter bytecode equals the public source**. Program identities and
the existing reviewed Jupiter execution contract remain trust assumptions.

The source USDC account has the sponsor as close authority, as in prior account
evidence; it has no delegate. The evidence checker initially assumed no close
authority and refused that assertion. Inspection identified the existing sponsor
role, which is now checked explicitly; this is recorded account state, not a new
permission granted by this investigation. The captured transaction contains no
instruction closing the source USDC account.

## Economics, with the existing contract assumptions

| Quantity | Amount |
|---|---:|
| Total USDC input | 1,000,000 atoms = $1 |
| Explicit fee, inside input | 2,000 atoms = $0.002 |
| Swap principal | 998,000 atoms |
| Quoted SOL output | 9,420,476 lamports |
| Slippage field | 500 bp |
| Conditional Jupiter floor | ceil(9,420,476 × 9,500 / 10,000) = 8,949,453 lamports |
| Conservative repayment deduction | 2,039,280 lamports |
| Sponsor funding credited | 0 |
| Conditional net receipt floor | **6,910,173 lamports = 0.006910173 SOL** |
| Original Raft floor for $1 | 6,819,647 lamports |

This capture clears the original price cap by 90,526 lamports. A price amendment
is **unnecessary for this capture**; do not apply it automatically. A fresh payload
must be checked again against committed state. The exact deployed Jupiter minimum
output/rounding implementation remains the previously disclosed deferred item;
these figures apply the existing reviewed contract, not a new on-chain proof.
Quoted output, conditional minimum and actual settled receipt are different values.

## What TM lets us select

Downloaded the complete current [Trading OpenAPI](https://docs.truemarkets.co/openapi/gateway.yaml)
(153,402 bytes), checked the full `CreateOrderRequest`, the live
[create-order reference](https://docs.truemarkets.co/api/gateway/create-order),
[CLI documentation](https://docs.truemarkets.co/cli/commands) and installed v0.0.11
`tm buy --help`. Source hashes and the request-schema excerpt are retained here.

- `venue=defi` selects the DeFi listing. It does not select a Solana DEX.
- There is no documented `dexes`, `excludeDexes`, pool, aggregator or DEX route
  selector in TM's create-order request or checked CLI flags.
- `route_reason` describes the smart router's CeFi/DeFi choice, not a DEX selector.
- Jupiter's own routing parameters do not establish support through TM. Do not
  pass speculative fields, modify TM's payload, or trade directly through Jupiter
  as a substitute for this TM end-to-end test.

This establishes **no documented TM route pinning**, not proof that a private or
future capability cannot exist. No undocumented API parameter was probed and no
new quote was created.

## Support decision and minimal next change

Keep TM's supported SOL/DeFi market-buy flow. Treat route handling as **local
acceptance after quote**, not selection of the venue's DEX. The signer must accept
only a reviewed route whose full bytes, account roles and economic bounds pass.
Unknown or changed routes stop unsigned. There is no guarantee the next quote
will return Invariant; repeated quotes until one passes are not part of this plan.

The minimal candidate implementation is a distinct Invariant route/schema5,
with the exact 19-account layout, true direction flag, five outer instructions,
classic SPL Token accounts and unchanged fee/repayment caps. It needs matching
native/Python policy digest handling and an explicit locked installed-schema2 →
schema5 transition. Neither pretending this is Bison/schema4 nor adding enum18
to the decoder alone is sufficient.

In C++, use the existing bounded arrays/spans and immutable verified result;
compare every account index, signer/writable flag, program and instruction. The
work stays outside Raft's hot path. It requires no new kernel record, funding
reset, network worker or mutable shared cache. Complexity stays linear in the
already-bounded transaction size. It improves compatibility for this exact route,
while deliberately sacrificing availability when TM returns another layout.

Production implementation, mutation tests, scoped checkpoint and native
qualification are **not done by this evidence review**. After qualification,
adopt the matching route/release under the existing lock, refresh account state
and quote checks, and execute only if that fresh transaction passes the current
price cap and one-$1 authorization. Preserve one cluster and every journal.
The prior authorization for that bounded buy remains recorded; this review
does not execute it or widen its scope.

## Validation and commands

`CAPTURE_REVIEW.json`: 20 offline structural/account checks pass.
`NEGATIVE_CHECKS.json`: 14 independently changed inputs fail their named checks
(direction, fee, repayment, recipients, mint/authority, permissions and others).
These are evidence-tool negative checks, **not production-verifier tests or
production source-mutation qualification**. The qualified native verifier's
retained verdict is still `REFUSED: direct route decode refused`.

```sh
cd "$HOME/aeron-raft-pi"
# Offline only; private retained inputs required. Safe to rerun:
python3 -B docs/evidence/bounded_launch_review_2026-10-13/invariant/review_capture.py
python3 -B docs/evidence/bounded_launch_review_2026-10-13/invariant/check_review.py
```

`read_accounts.py` was run once under `PROTOCOL.md`; its exclusive output directory
prevents accidental repeat. Public-source files are cached under
`~/.cache/velocita-build/invariant-review-20261014`; `SOURCES.json` records pinned
URLs and hashes. Nothing was committed or deployed in this review.
