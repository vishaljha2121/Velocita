# 0029. The signer authenticates the gateway for a day

Date: 2026-10-15.
Status: **PROPOSED 2026-10-15 — implemented and tested locally; ships with the new incarnation.**
Amends [0010](0010-the-operator-holds-the-key-collectors-hold-tokens.md) for the swap runtime only.

## Problem

The owner approved an unattended run of the paper stochastic rule, bounded by the $10 drawdown and
240 lifetime orders. Two rules written for one supervised buy stop that run within minutes:
- the account review and the history cut were valid for 15 minutes, and the gateway and signer close
  when they lapse;
- the gateway held one bearer token, minted by a person under ADR 0010 and expiring on TM's schedule.

ADR 0010 forbids automated minting because "a component that can re-mint its own token is a
component that holds the key". At TM the API key that mints tokens is the same key that signs order
payloads. Giving it to the gateway would let a compromised gateway sign any transaction and bypass
the verifier. The isolated signer already holds that key, under its own UID.

## Decision

- **Review lifetime.** The account review and the history cut are valid for up to 24 hours
  (`kSwapReviewLifetimeMs`, `REVIEW_LIFETIME_MS`). Every other review check is unchanged.
- **The key stays in the signer.** The signer protocol gains one operation, `authenticate`. It
  signs only a message of exactly `<uuid>.<10 decimal digits>`, whose timestamp is within 60 seconds
  of the signer's clock, and only before the review window ends. The request carries no effect,
  sequence, handle or lookup table, and the message is at most 64 bytes. A transaction payload can
  never have this shape. The reply has its own code, `authenticated`. The gateway's client checks
  the signature against the pinned signer public key, as it does for transaction stamps.
- **The gateway holds only the key id.** Its pinned credential file holds the API key's public
  identifier, and the staging tool extracts it from TM's key download. A file holding a bearer
  works as before.
- **Minting.** The gateway signs `{key_id}.{seconds}` through the signer. It posts
  `{"key_id","timestamp","signature"}` to `POST /v1/auth/api-key/token` with a raw r‖s base64url
  signature, as `tools/mint-token.py` does. It accepts only an HTTP 200 reply whose `token_type` is
  `Bearer`, whose `access_token` has three non-empty base64url parts, and whose `expires_in` is a
  strict UTC ISO-8601 time. It caches the token and mints again once 2 minutes or less remain. It
  refuses a token that arrives with 2 minutes or less left, and returns no token after the review
  window ends or after the clock moves backwards.
- **Renewal.** No automatic daily renewal. The review expiry remains a hard authorization cutoff.
  Correction from the October 15 review: a cutoff alone does not prove that inventory is flat or
  that no effect is in flight. The focused driver correction inhibits entries and requests an
  eligible exit 420 seconds before expiry, stops initiating exits 210 seconds before expiry, and
  records a durable pending/exposure/unknown stop if it cannot finish. These margins do not
  guarantee settlement during a feed or venue failure. A person reviews any unfinished exposure
  and renews authority before further activity; expiry is never silently extended.

## Consequences

The signer is again the only holder of the key, the end state ADR 0010 named. The gateway's power
is unchanged: it can obtain bearer tokens and cannot sign a transaction. A compromised gateway could
ask for authentication signatures until the review window ends, which is no more than the bearer it
already held. Each mint is one signer request and one TM POST, and happens about once per TM token
lifetime.


## Implementation correction — 2026-10-15, local qualification

The [focused correction protocol](../../archive/evidence/automated_launch_2026-10-15/focused-mvp/PROTOCOL.md)
closes a separate implementation gap: widening the signing envelope did not make the gateway
and signer journals support consecutive effects. The correction advances each durable fence only
after an authenticated Raft settlement matches its predecessor. The signer performs its own read
and a fresh authority barrier for the next effect. Existing journal formats, economic verification,
funding, caller identities and per-effect no-resubmit protections remain in force. This is a lifecycle
implementation correction, not broader signing authority. Native qualification and installation
remain separate from local tests.
