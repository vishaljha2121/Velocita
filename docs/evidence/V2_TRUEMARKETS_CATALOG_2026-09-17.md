# True Markets asset catalog — read 2026-09-17

Answers the instrument-type question left open by the 2026-09-17 re-check, from
the venue's own catalog rather than from its prose.

Read-only. `GET /v1/conductor/assets` only. No order endpoint was called.

## The headline: the surface is SPOT, from the venue itself

| | |
|---|---|
| `type` on all 210 DeFi assets | **`spot`** |
| `type` on all 3 CeFi assets | **`spot`** |
| `asset_class` on all 213 | `crypto` |

This is the positive evidence the re-check could not supply. The re-check
established only that the word "perpetual" had disappeared from the prose, and
was careful to record instrument type as UNVERIFIED because absence of a word is
not evidence. The catalog states the type as a field, on every asset.

ADR 0004's decision stands for perpetuals and does not block this surface.

## Chains, and the operator report confirmed

| chain | entries | tradeable |
|---|---:|---:|
| solana | 112 | 110 |
| base | 73 | 72 |
| robinhood | 20 | 19 |
| hypercore-spot | 3 | 2 |
| hypercore-perp | 1 | 1 |
| arbitrum | 1 | 0 |

The project owner's report (D-8) is CONFIRMED on all three counts: Base is
present and is the second-largest chain, Hyperliquid is present as `hypercore-*`,
and Robinhood tokens are a first-class chain label with 20 entries.

`hypercore-perp` HOLDS EXACTLY ONE ASSET, AND IT IS USDC - the settlement token,
not a perpetual contract, and its own `type` is `spot`. So the perp venue appears
in the catalog only through its collateral asset. Nothing here is a perpetual
instrument.

204 of 210 DeFi assets are `tradeable: true`; the six that are not are stablecoin
entries on chains where they serve as quote rather than as a tradable leg.

## What the catalog does NOT carry

NO TICK SIZE, LOT SIZE, MINIMUM NOTIONAL, FEE, OR PRICE PRECISION. A search of
every field on every entry for `tick`, `lot`, `fee`, `min_`, `increment` or
`precision` returns NOTHING. `market_data` holds supply figures only;
`networks` is an empty array on the entries checked.

These four GET paths are 404: `/v1/conductor/assets/{id}`, `/v1/conductor/pairs`,
`/v1/conductor/markets`, `/v1/conductor/instruments`. So does
`/v1/market-data/instruments`.

CONSEQUENCE FOR THE ADAPTER. `VenueInstrumentSpec` still has no source. Its whole
purpose is to carry tick and lot separately - which the replicated profile cannot
supply, because it stores only their product - and the venue does not publish
them at any endpoint found. `spec_agrees_with_profile` therefore cannot be
satisfied by anything yet, and the adapter cannot render a priced order.

## CeFi is three assets

BTC, ETH, PYUSD. If the quickstart's "limit orders are supported only on the CeFi
assets" were the operative rule, the entire tradable universe for a passive
strategy would be three instruments. That is a further reason the endpoint
reference's DeFi resting-order evidence matters, and it is recorded here because
the scale of the difference was not obvious before the catalog was read.

## Authentication, now verified

The envelope was INFERRED before today and is now confirmed by a 200:

```
POST /v1/auth/api-key/token
{"key_id": "<uuid>", "timestamp": <integer>, "signature": "<base64url>"}
```

The signature is ES256 over the ASCII string `{key_id}.{timestamp}`, as RAW
r||s - sixty-four bytes, base64url, unpadded. A DER-encoded signature is the
wrong shape.

A STRING TIMESTAMP IS REJECTED with `invalid request body`; it must be a JSON
number. `keyId` is rejected in favour of `key_id`. The response field is
`access_token`.

## Two infrastructure facts the adapter must handle

CLOUDFLARE BLOCKS NON-BROWSER CLIENTS. Python's `urllib` receives `HTTP 403`
with the body `error code: 1010` - a Cloudflare browser-signature block - on
every request including plain public GETs. `curl` with a browser user-agent
receives 200 for the same URL. This is not an API behaviour and no API error
appears; anything built on a default HTTP client will see a uniform 403 and may
conclude its credentials or envelope are wrong when nothing has reached the API
at all.

THE ASSET CATALOG NEEDS NO AUTHENTICATION. `GET /v1/conductor/assets` returns 200
with no `Authorization` header, and the authenticated response is identical.

## Credential handling

The key used was supplied as a JWK, converted to PEM outside the repository,
used for one token exchange, and deleted along with the token. No key material,
token, or `Authorization` header is recorded in this file or anywhere in the
tree. The key should be rotated, since it passed through a session transcript.
