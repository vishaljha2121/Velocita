# Operator tools

Two scripts, and the split between them is the point. See ADR 0010.

```
  you (hold the key)
     |
     |  ./tools/mint-token.py  ~/secure/tm-key.json  /run/velocita/tm.token
     v
  a short-lived token in a 0600 file
     |
     |  ./tools/probe-market-data.py  /run/velocita/tm.token  out/
     v
  responses on disk, which model/ can read
```

NOTHING AUTOMATED HOLDS KEY MATERIAL. The mint step is run by a person, the key
exists in a temporary 0600 file for as long as `openssl` needs it, and the token
is written with 0600 created before the first byte so it is never briefly
world-readable.

A HOLDER OF THE TOKEN CANNOT PLACE AN ORDER. Completing a DeFi order needs the
key to sign payloads that `POST /orders` hands back, and the token is not the
key. That is a structural property rather than a rule these scripts follow.

Both scripts refuse state-changing paths anyway, using the same over-broad
fragment list `model/`'s `ReadOnlyHttp` uses - a false refusal costs one line, a
false permit costs an order.

## When the token expires

Re-run the mint step. That friction is deliberate: a component that can re-mint
its own token is a component that holds the key.

## Why a probe before a collector

Several plausible market-data paths answer `401 Jwt is missing` without a token,
which may mean "exists, needs auth" or may be a catch-all for anything under an
authenticated router. Collecting against guessed paths would record silence as
data. Establish which paths are real first; `_probe.json` in the output
directory is that record.

## Two things that will bite otherwise

CLOUDFLARE REJECTS NON-BROWSER USER-AGENTS with `403 error code: 1010`, before
the request reaches the API - no API error appears, so a default HTTP client
looks like an authentication failure when nothing was ever delivered. Both
scripts set a browser user-agent. See D-10.

ES256 HERE MEANS RAW `r||s`, NOT DER. `openssl dgst -sign` emits DER and the
venue rejects it. `mint-token.py` converts; anything reimplementing this must
too. See D-13.
