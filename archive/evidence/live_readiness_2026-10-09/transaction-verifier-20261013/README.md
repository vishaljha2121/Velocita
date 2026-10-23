# Direct-buy transaction verifier — scoped contract implementation

This implements a real-byte transaction verifier and extends Jupiter direct-route
decoding. It does **not** admit the retained current TM transaction or open live
trading. Production integration and runtime policy activation belong to the main
agent's separate review.

The result is explicitly named `verified_under_program_contract`. It depends on
trusted canonical Jupiter/Kipseli execution and independently reviewed account
configuration. It is not a proof that an arbitrary CPI cannot spend owner funds.
The Kipseli CPI receives owner signer/writable authority and the System program;
a malicious or changed implementation could violate the assumed economic
contract. No primary Kipseli implementation was found in the bounded search.
That limitation must remain visible at production-contract review. IDL field
parsing, quote documentation, observed simulation, and final settlement are
different evidence strengths; none was represented as formal deployed-code proof.

## Changes and exact supported contract

- Direct-route Borsh decoding now recognizes Obsidian (109, no payload) and
  KipseliV3 (181, one bool), from the retained program-owned IDL. Neither decoder
  grants signing authority. All previous decoder cases remain supported.
- The verifier supports only a versioned six-instruction, single-hop KipseliV3
  USDC-buy route: compute limit, compute price, sponsor-funded idempotent wrapped
  SOL ATA creation, Jupiter direct exact-input route, close to owner, and fixed
  owner-to-sponsor native repayment. Exactly 20 message accounts and 25 route
  account references are checked. Sell, multi-hop, different DEXes, additional
  instructions, owner-paid network fees, alternate close recipients, shared
  routes and Token-2022 are not supported.
- Canonical Jupiter, Kipseli, System, Token, ATA, ComputeBudget, mint, sysvar,
  event-authority and captured read-only route auxiliary identities are compiled
  into the allowlist. A caller cannot make an arbitrary program pass by supplying
  matching policy pins. Owner, sponsor, source/fee accounts and pool/vault keys
  must originate in trusted reviewed configuration, not the returned payload.
- Source and fee accounts must be independently established as classic SPL USDC
  accounts. Policy pins currently bind addresses, not a serialized account-data
  proof. A prior account read alone does not prove a fee account cannot be closed
  and recreated with a different mint; ATA derivation or equivalent lifecycle
  qualification is a production review point. The retained account read and trace
  establish the observed denomination, not this stronger invariant.
- The envelope must exactly match the committed prepared payload hash and
  organization/signer, contain no signatures, meet freshness/expiry, and resolve
  through the independently pinned lookup image. Every account privilege is
  checked. Policy and full intent digests are recomputed from canonical fields.
- Only 20-bp input fees with inputs divisible by 10,000 USDC atoms are supported.
  Fractional fee rounding is refused rather than inferred. The total debit is
  fee-inclusive; principal and fee must exactly match the committed intent.
- Gross output uses the ceiling slippage threshold. Idempotent ATA creation is
  assigned **zero guaranteed new sponsor funding**. Existing owner rent is not
  new trade revenue. The full encoded owner repayment reduces the guaranteed net
  floor, and both deduction cap and committed net minimum must pass.
- An opaque `VerifiedSwapTransaction` has a private constructor. It owns exact
  payload, intent and policy digests, numeric bounds and the earliest expiry.
  It is semantic evidence under this contract, not Raft authority or permission
  to sign. Recovery must bind the same immutable policy/context and payload;
  signing must still obtain fresh committed authorization and enforce expiry.

## Hand arithmetic and actual retained bytes

| Case | Total USDC atoms | Fee atoms | Gross SOL floor | Max deduction | Net floor | Result |
|---|---:|---:|---:|---:|---:|---|
| Supported constructed transaction | 1,000,000 | 2,000 | 8,940,092 | 550,840 | 8,389,252 | Contract verification passes |
| Retained current transaction, current cap | 1,000,000 | 2,000 | 8,940,092 | 2,039,280 | Not admitted | Deduction cap exceeded |
| Separate hypothetical larger policy | 1,000,000 | 2,000 | 8,940,092 | 2,039,280 | 6,900,812 | Arithmetic passes only at/below that minimum |

Worked integers: `1,000,000 * 20 / 10,000 = 2,000`; principal is `998,000`.
`9,410,623 * 9,500 / 10,000 = 8,940,091.85`, ceiling `8,940,092`.
The ordinary supported test subtracts `550,840`. The actual transaction encodes
`2,039,280`; subtracting the historically observed `1,488,440` funding would be
invalid as a guaranteed execution bound. The hypothetical larger cap test is
**not owner approval** and changes no runtime setting.

The original October 13 unsigned envelope, SHA-256
`630a1e232cfb4759e6273c4aeb079f006c0749025cdd8a5d2df03df64b2ee233`, was
checked offline with the full retained lookup image. Metadata response SHA-256:
`12829262fab81d416762106cbbc45079bc3567c57052c431b32f0d17047adb68`.
Result: `deduction_exceeded`, gross floor `8940092`, max deduction `2039280`,
no opaque proof. This diagnostic used a historically anchored clock and a
constructed pending intent; it does not claim actual committed Raft authority,
a fresh quote, or live execution validity. Private driver/source/binary remain
under `~/.cache/velocita-build/transaction-verifier-20261013/private/`.

## Validation

Tests were written before implementation: the new direct variants first failed
`current_variants_decode`; the verifier with a stub first failed
`supported_buy_verified`. The decoder then passed eight groups and 28 named
mutation kills. The verifier passed five groups with ASan/UBSan and
`-Wall -Wextra -Werror`; twelve mutations were killed by named assertions.
Mutation compilation errors were not counted: an initial unused-local mutant
failed compilation, then the mutation was made warning-clean and rerun. Fractional
fee tests cover inputs immediately below/above divisibility. Positive/negative
one-lamport net floors, changed policy/lookup/intent identity, stale/already-signed
payloads, arbitrary DEX pins and unauthorized instruction/account changes are
covered. No network, credentials or signing occur in these tests.

Reproduce:

```sh
python3 -B tests/mutation_jupiter_route_decode.py \
  --output-dir "$HOME/.cache/velocita-build/transaction-verifier-20261013/route-mutations-review"
python3 -B docs/evidence/live_readiness_2026-10-09/transaction-verifier-20261013/mutation_check.py \
  --output-dir "$HOME/.cache/velocita-build/transaction-verifier-20261013/verifier-mutations-review"
```

`results.json` records source hashes and named outcomes. These are local macOS
checks, not qualification of a new committed native build. No commit, deployment,
credential load, signature, order request or fund movement occurred in this task.

## Primary sources and scope

- Retained program-owned IDL: `../jupiter-public-idl-20261012.json`, SHA-256
  `ac409554b289a63e035717a516240c93adc4b8d894d4668fde254e73c7cfdb47`.
  Defines variants 109/181 and route field/account layout. It describes ABI,
  not full economic semantics.
- [Jupiter ExactIn quote contract](https://developers.jup.ag/docs/api-reference/swap/v1/quote),
  read October 13, 2026: fixed input, output slippage, quoted output after fees.
  This is the API contract, not source for every current CPI implementation.
- [Jupiter platform fees](https://developers.jup.ag/docs/swap/v1/add-fees-to-swap),
  read October 13, 2026: 20 bp means 0.2%; ExactIn fee account may use input or
  output mint. Our USDC fee interpretation also relies on the retained account
  metadata and execution trace.
- [Official program-label endpoint](https://lite-api.jup.ag/swap/v1/program-id-to-label),
  read October 13, 2026: maps Kipseli to
  `3TK9D8aoBFYjYZtKCjciPrVrRStsnvo7KmpcJqDavpaU` and Obsidian to
  `HBVw6bZtcCaezhcBrmfyXBSBRWCdv72271xQ4GPvms2z`. Identity is not a semantic audit.
- [Official public aggregator client](https://github.com/jup-ag/jupiter-amm-implementation/blob/main/jupiter-aggregator-v6/src/lib.rs),
  reviewed October 13, 2026: client interface, not the full deployed execution
  implementation. It does not independently establish Kipseli native debit bounds.
- `../minimum-output-20261013/README.md`: prior source-backed integer threshold
  and adjacent one-lamport simulation pair; observed supported boundary, not
  universal execution proof. `../owner-buy-settlement-20261013/README.md` records
  actual fee-inclusive USDC accounting and net receipt.
