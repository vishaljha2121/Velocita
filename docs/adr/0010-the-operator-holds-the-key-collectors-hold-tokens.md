# 0010. The operator holds the key; collectors hold tokens

Date: 2026-09-17
Status: Accepted. Approved by the project owner before anything was built.

No byte contract change. This is about who can sign.

## Context

Market data on this venue requires a JWT (D-16). The asset catalog does not
(D-11). Minting a JWT requires the EC P-256 API key.

THE SAME KEY SIGNS ORDER PAYLOADS. `POST /orders` "returns unsigned payloads for
client signing", so completing a DeFi order needs the key itself and not merely
a session. Authentication and order-signing are the same capability at this
venue, reached through the same secret.

`model/`'s interlock exists precisely to keep that capability out of the research
process. It bans `ecdsa`, `coincurve`, `nacl.signing`, and identifiers containing
`private_key`, `keystore`, `sign_transaction` and more. Letting the research
package authenticate would hand it the order-signing key, and no path allowlist
would take that back - the `/orders` refusal is a second line, not the first.

So "the collector needs a credential" is the wrong framing. The collector needs a
TOKEN. Something has to hold a KEY to mint one, and the whole question is what.

## Decision

THE OPERATOR HOLDS THE KEY. NO LONG-LIVED COMPONENT DOES.

A short mint step, run by a person, exchanges the key for a short-lived token and
writes the token to a file. Collectors read the token file. Nothing automated
holds key material at any point.

WHAT THIS BUYS, and it is a real structural property rather than a policy:

  * a collector holding only a token can read market data and CANNOT COMPLETE AN
    ORDER, because the DeFi order flow requires signing payloads it has no key
    for;
  * the key's exposure is bounded to the moment a human runs the mint step,
    rather than to the lifetime of a service;
  * `model/`'s interlock is untouched. It still cannot import a signing library,
    still cannot name a private key, and still never sees a credential.

WHAT IT COSTS, stated plainly: tokens expire, so an unattended collection run
stops when its token does. That is friction on purpose. A component that can
re-mint its own token is a component that holds the key.

## The collector lives outside `model/`

`model/` is a protected path and its `ReadOnlyHttp` sends no `Authorization`
header by design. Rather than teach it to carry one - which would make the
research package credential-aware even while holding only a token - the
token-consuming collector is a separate operator tool. `model/` continues to read
recorded payloads from disk and never touches an authenticated endpoint.

The collector inherits the same discipline by construction rather than by
inheritance: GET only, no signing imports, no order paths.

## Alternatives rejected

A LONG-LIVED CREDENTIALLED COLLECTOR that mints its own tokens. Less operator
friction, and it was the obvious design. Rejected because it puts signing
material in a service that runs unattended, and because it creates a SECOND key
holder once the execution adapter exists - two attack surfaces and two things to
rotate, to save a manual step.

RELAXING `model/`'s INTERLOCK to allow the auth endpoint. Rejected outright. The
interlock's own comment says it exists to catch "somebody pastes a wallet snippet
into this repo during a late-night leaderboard push", and a late-night
leaderboard push is exactly the circumstance in which this would be relaxed.

WAITING FOR THE C++ ADAPTER to be the only key holder. The cleanest boundary, and
it remains the end state. Rejected for now only because it blocks research
collection entirely, and the token split above gets most of the safety without
the wait.

## Consequences

When the execution adapter is built it WILL hold the key, because signing order
payloads is its job. At that point this ADR's mint step and the adapter are the
only two things that ever touch key material, and the collector still holds only
a token.

The market-data endpoint paths are still unknown - several plausible ones return
`401 Jwt is missing`, which may mean "exists, needs auth" or may be a catch-all.
The first use of a minted token should be discovering which paths are real, not
collecting against guesses.
