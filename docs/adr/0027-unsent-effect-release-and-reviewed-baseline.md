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
