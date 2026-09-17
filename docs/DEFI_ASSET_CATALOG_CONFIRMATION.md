# Confirming the DeFi asset catalog

Read this in one sitting before running anything. It is short on purpose.

WHY THIS EXISTS. `MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md` refuses to let the
adapter connect on unverified fields, and after the 2026-09-17 re-check the
single most important field - what instrument the DeFi surface actually trades -
is unverified. The documentation no longer says "perpetual" and no longer names
Hyperliquid, but absence of a word is not evidence of spot. This procedure
replaces that absence with an answer from the venue itself.

## The one rule

NEVER CALL `POST /orders`. It is the only endpoint that can create an external
effect. Everything here is a GET. If a step seems to need a POST other than the
auth token exchange, stop and re-read.

`POST /v1/auth/api-key/token` is the one permitted POST, because it creates a
session and not an order.

## Before you start

Your API key is an EC P-256 PRIVATE KEY that signs on-chain actions. G-066 says
it never enters replicated policy, commands, snapshots, logs, test fixtures or
evidence bundles.

Practically, for this procedure:

- do not paste the key, the JWT, or any `Authorization:` header into a file you
  intend to commit;
- when you save a response, strip the header first;
- `set +o history` in the shell before exporting anything, or put the key in a
  file and read it rather than passing it on a command line where it lands in
  shell history and in `ps`.

## Step 1 - get a token

```sh
# The key lives in a file, never in an argument.
KEY_ID="<your key id>"
TS=$(date +%s)
SIG=$(printf '%s.%s' "$KEY_ID" "$TS" \
  | openssl dgst -sha256 -sign ~/.config/velocita/truemarkets.pem \
  | openssl base64 -A | tr '+/' '-_' | tr -d '=')

TOKEN=$(curl -sS -X POST https://api.truemarkets.co/v1/auth/api-key/token \
  -H 'content-type: application/json' \
  -d "{\"key_id\":\"$KEY_ID\",\"timestamp\":$TS,\"signature\":\"$SIG\"}" \
  | jq -r '.token // .access_token')
```

If the signature shape is wrong the venue will say so. The exact envelope is
documented at `docs.truemarkets.co/trading/quickstart`; the point above is only
that the signed string is `{key_id}.{timestamp}`.

DO NOT ECHO `$TOKEN`.

## Step 2 - read the DeFi catalog

```sh
curl -sS 'https://api.truemarkets.co/v1/conductor/assets?venue=defi' \
  -H "authorization: Bearer $TOKEN" > /tmp/defi-assets.json

jq 'if type=="array" then .[0] else (.assets//.data//.)[0] end' /tmp/defi-assets.json
```

That first object is the thing to read carefully. Then:

```sh
# every distinct key, so nothing is skimmed past
jq -r '[paths(scalars)|join(".")]|unique[]' /tmp/defi-assets.json | sort -u

# anything naming a chain, venue, or contract type
jq -r '..|objects|to_entries[]|select(.key|test("chain|network|venue|type|kind|contract|instrument|settle";"i"))|"\(.key)=\(.value)"' \
  /tmp/defi-assets.json | sort -u | head -40
```

## Step 3 - what you are looking for

| Question | What would answer it | Why it decides something |
|---|---|---|
| Instrument type | a field saying `spot` / `perp` / `perpetual` / `swap`, or a contract type | ADR 0004 blocks perpetuals from V1. Spot needs no new accounting; perps need a new observation kind and a leverage bound. |
| Chain | a field naming `base`, `hyperliquid`, `arbitrum`, a chain id | decides gas cost per fill, which at 1 USDC notional can exceed the whole spread |
| Which assets are DeFi | the catalog itself, filtered `venue=defi` | an ineligible pair wastes the whole competition window |
| Tick / lot / min notional | `tick_size`, `lot_size`, `min_notional_quote` or similar | quotes round to invalid prices without them |
| Fees | `maker_fee_bps`, `taker_fee_bps` | wrong sign on expected edge |

IF THE CATALOG DOES NOT SAY. That is itself an answer, and the honest one is
"still unverified" - not a guess. Record it as unverified with the timestamp, and
the next place to look is an authenticated `GET /positions` response shape on an
account with a position, or the `create-order` request schema, which did not
render in the public docs and may sit behind an expandable section.

## Step 4 - record it

In `model/config/research.toml`, per field:

```toml
tick_size  = "0.0001"
confidence = "verified"       # verified | inferred | unverified
source_url = "https://api.truemarkets.co/v1/conductor/assets?venue=defi"
```

`confidence` is stored per field alongside its source and collection timestamp,
because a value we inferred must never become indistinguishable from one the
venue documented. That is how an assumption becomes a fact nobody remembers
making.

## Operator report, recorded as UNVERIFIED

The project owner believes, as of 2026-09-17, that:

- the DeFi surface settles on **Base** and **Hyperliquid**;
- the catalog includes **Robinhood tokens**.

NONE OF THIS IS IN THE DOCUMENTATION as read on 2026-09-17, and it is written
here so it is not lost - not so it can be relied on. If the catalog confirms any
of it, promote that row to `verified` with the response as its source. If the
catalog contradicts it, that contradiction is the more interesting finding and
belongs in the evidence file.

A Base + Hyperliquid split would matter more than it looks: two chains means two
gas regimes and possibly two instrument types under one `venue=defi` label, and
the adapter would need to keep them apart rather than average them.

## When you are done

Append the answers to `docs/evidence/V2_TRUEMARKETS_RECHECK_2026-09-17.md` under
a new "Step 3 answers" heading, with the collection timestamp. Strip credentials
first.
