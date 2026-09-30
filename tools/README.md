# Operator tools

## Review correction and quote capture — 2026-09-21

The historical token-safety claim below is conditional on an inert prepare step;
it is NOT established for MCP prepare, which advertises direct submission.
Local method/path allowlists remain mandatory. Do not infer token permissions
from the absence of a signing key.

`python3 tools/measure-markup.py owner@example.com 25 100 500` is now an
owner-operated raw quote capture, NOT a markup measurement. It never prints
login responses or guesses quote units. Exit 2 means capture succeeded but the
measurement contract is unverified; exit 1 is failure. Review output for privacy
before sharing. No signing or trading call is allowlisted.

`mcp-tools.py` retains complete paginated schema result pages. `mcp-read.py`
has a local read-only allowlist. `roundtrip-cost.py` produces indicative Jupiter
comparisons, not a True Markets lower bound; it refuses invalid sizes and will
not overwrite an existing roundtrip.json. The unverified HYPE mint was removed.

Offline checks: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*tools.py'`.

### Token handling after F2 remediation

Mint only to a directory owned by the current user with no group/other mode
permissions (normally 0700). Existing directories are checked, not silently
chmodded. Missing components are created private. Relative paths are allowed;
parent traversal and symlink components/destinations are refused. The only alias
exceptions are verified root-owned macOS `/var` and `/tmp` system links.

The token is validated before replacement, written through a private 0600
exclusive temporary file, synced and atomically renamed through an open parent
directory descriptor. An invalid response or write/sync/rename failure leaves
the previous token in place. This is not a guarantee against power loss of the
directory rename, privileged/same-user attackers, or copies of an older token
that was previously readable. Rotate previously exposed tokens separately.

Auth uses only `https://api.truemarkets.co`; arbitrary `TM_BASE` origins are
refused before key access. Curl user config and redirects are disabled. The
probe's exact path allowlist is read-only, and bearer credentials go through
stdin, never argv. Raw probe response files can still contain private venue
data: review before sharing. JWT shape/claims checks do not establish token
permissions or cryptographically verify venue signatures. These tests use only
dummy secrets; no auth or venue call is part of verification.

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

The earlier claim that a token holder cannot place an order is withdrawn:
the MCP direct-prepare surface invalidated that inference. Token scope is still
unverified. Local read-only allowlists, not possession of a token rather than a
key, enforce these tools' request boundary.

The mint tool can only POST to the fixed auth endpoint. The probe can only GET
its declared candidate paths. `model/` remains untouched and structurally passive.

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
