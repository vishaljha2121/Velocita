# 0027. Unsent effect release and a reviewed baseline for renewal

Date: 2026-10-14.
Status: **PROPOSED 2026-10-14 — implemented and tested locally; not yet committed, qualified or deployed.**
Context: the first cluster buy (effect 1, intent at Raft index 13) stopped before signing. No funds
moved. Effect 1 stays reserved in Raft and the gateway prepare journal is at `create_intent`.

## What failed

The gateway checked the live wallet against the startup committed read just before it created the TM
order. `reconcile_swap_balance` requires that read to be at most 30 s old. The intent arrived about
two minutes after startup. The check failed locally, so the order was never sent to TM. The gateway
correctly stopped as `blocked / prepare_unknown`, but it kept no record of why.

## Decision

1. **Fresh create-time check.** The prepare session hands the port its own committed read: index,
   digest, state, and wall-clock request and receive times. The wallet is compared against that
   state with the not-yet-sent reservation removed (`swap_unsent_funding_state`). A reserved effect
   has touched nothing on chain, so its pre-intent balances are the right expectation. Gateway startup
   uses the same projection.
2. **Create trace.** The gateway records the step at which `create` stopped: latched, journal,
   admission, balance, bearer or exchanged. It also records transport status, exposure, HTTP status,
   body size and digest. On a non-2xx answer it keeps the first 1 KiB of TM's error body in
   `create-error.txt`. All of this goes to the gateway's root-only metrics folder at stop.
3. **Release of an expired unsent effect.** The prepare journal gains a fourth phase, `released`,
   reachable only from `create_intent`. The released record keeps the `create_intent` record's
   checksum as evidence. When Raft shows the effect still `reserved`, the journal holds
   `create_intent` for it, and wall time is at or past its `sign_by`, the gateway proceeds in order:
   - it writes `released` to the journal;
   - it commits the existing gateway-role `ReleaseUnsent` with that evidence;
   - it then accepts a newer effect in the same run.

   Past `sign_by`, the reducer refuses `Prepare`, and the signer signs only a prepared effect. So no
   signature for the released effect can ever exist, whatever TM did with an unsigned order.
4. **One buy, any effect number.** `swap_mvp_buy_scope` no longer requires effect 1. A pending buy is
   in scope only when nothing has settled, and only when it is effect 1 or directly follows a
   released effect. The opening's $1 purchase cap is unchanged and still enforced by the reducer.
5. **Reviewed baseline.** Renewal and start no longer require the pristine original state. They take
   `baseline.json`, a fresh authenticated read's state digest and index, and apply re-reads live state
   against it. The prepare journal may be at `create_intent` or `released` for a non-zero effect with
   its exact binding. A release-only upgrade needs no route review when the transaction review bytes
   are unchanged. The protected journal is re-bound only when its policy domain changes.

## Not changed

No kernel or Raft change; the voters stay on `1a46fd8`. No new venue, program or money authority.
Funding, the opening, the purchase cap, the fee and loss caps, and the signer's verification policy
are unchanged.

## Amendment 2026-10-16: a venue-failed order that can never land

**Problem.** Effect 25 was signed and executed, then True Markets answered `failed`, "Venue
temporarily unavailable", 0 executed, 0 fee, no `tx_hash`. The effect is `unknown` in Raft.
`ReleaseUnsent` is valid only from `reserved` (a signature exists), and `Settlement` needs a
finalized transaction identity that does not exist. The gateway blocked with
`settlement_unproven`, and a restart repeated the block. The account could never trade again.

**Decision.** One new settlement outcome, `expired_unexecuted`, authored by the gateway:

- Valid only from `attempted` or `unknown`; all five amounts zero; `transaction` zero (and zero
  only for this outcome); `finalized` true; evidence non-zero. It releases the reservation and
  enters the rolling history and `settled_total` like any settlement. A different later settlement
  for the same effect is a conflict.
- The gateway proposes it only with three facts:
  1. **The venue says nothing executed.** The authenticated order read for the exact handle is
     terminal `failed` with executed 0, fee 0, leaves present and no `tx_hash`. A known
     `tx_hash` always takes the existing finalized-settlement path instead.
  2. **The signed message can never land.** The verifier only admits transactions whose first
     instructions are compute budget, so no durable nonce is possible. A Solana transaction is
     valid only while its recent blockhash is. `isBlockhashValid` at `finalized` commitment
     returns false at context slot S, and `getBlockTime(S)` is at least 60 s after the envelope
     timestamp (so the node had seen the blockhash). Expiry is monotone in block height, so
     nothing can include this message after S.
  3. **It did not land before expiry.** `getSignaturesForAddress(owner, finalized,
     minContextSlot = S)` is paged (64 per page, at most 8 pages) back to 60 s before the
     envelope timestamp. Every entry at or before S is fetched with `getTransaction` and its
     message bytes compared with the retained unsigned payload; none may match. More than 32
     candidates, a missing block time, an RPC error or a provider change keeps the effect unresolved.
- Evidence is SHA-256 over a domain label, the TM response digest and the digest of every RPC
  response in order. The kernel cannot check chain facts; it trusts gateway-role evidence exactly
  as it already does for finalized settlements.
- Consumers that required a non-zero transaction (predecessor fence for gateway and signer,
  submission resolution, prepare session and store predecessor, startup recovery-only check)
  accept a zero transaction exactly when the outcome is `expired_unexecuted`.
- Reconciliation stops now log `WARNING settlement unresolved stage=S code=C` once per change.

**Cost.** Old readers decode the outcome byte but refuse it in `valid_state` and in the reducer,
so every voter, the gateway, the signer and the client must run the new build before the first
such settlement commits; never roll back after. No format size, budget or authority changes.
Four new read-only RPC request templates are admitted by the HTTPS allowlist.
