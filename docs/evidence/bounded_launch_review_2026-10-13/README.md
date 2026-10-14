# Bounded $1 launch review — October 13

**Decision: the newly quoted transaction is not launchable on the qualified
signer.** TM selected a different route. Do not relax the current account/route
checks or execute an expired capture. No signing, execution or service changes
occurred during this review.

Latest follow-up: the separate Byreal policy now passes focused local checks;
see [local qualification](BYREAL_RESULT.md). The installed build remains blocked.

## Scope reviewed

One SOL buy on Solana, account 2/instrument 2, effect 1. Exactly 1,000,000 USDC atoms
maximum debit including 2,000 fee atoms; 998,000 principal. Repayment allowance
at most 2,039,280 lamports, zero guaranteed sponsor credit. Preserve the existing
funding and all journals; no new Initialize, second buy, resize, automatic retry,
sell or buyback. The $0.50 realized-loss control does not guarantee a maximum
mark-to-market loss on an unsold SOL position or authorize a stop-loss sale.

## Fresh evidence

- Host read at 23:50:34UTC: gateway and signer inactive/PID 0;
  RefuseManualStart=yes and both renewal/telemetry inhibitors loaded. The account
  review expired at 23:50:24.625UTC. No inhibition was removed.
- One authenticated unsigned $1 buy request returned 201/initialized, no issues,
  displayed output 0.00934546 SOL and explicit fee 0.002 USDC. This can create an
  initialized TM record; it cannot sign or execute. Raw response remains private.
- Production bounded parser diagnostic found 16 static accounts, different lookup
  selections and a different route instruction. The qualified transaction verifier
  requires 12 static/20 resolved accounts and the reviewed direct KipseliV3 route.
- The direct route's variant byte is 156. The committed 69679c0 decoder supports
  variants 26/109/177/181 and returns `unsupported` for these exact new bytes.
  A standalone C++ diagnostic compiled from the exact committed decoder/header
  confirmed that refusal. It is not a full protected-signer invocation.
- The decoded final System Program transfer still specifies 2,039,280 lamports
  from owner to sponsor. This does not establish the new route's whole-transaction
  net minimum. No numerical guaranteed receipt is claimed for this capture.
- A subsequent bounded 20-request account/history check found no blocking orders,
  unchanged 40.006989 USDC and 0.040764166 SOL, and four historical transactions.
  Current-account/provider-history limitations remain as in the existing collector.

`review.json` contains sanitized quote/account results and response hashes.
`decoder-result.json` and `route-check.cpp` retain the exact offline refusal check.
No raw tokens, signing payloads or private account keys are committed here.

## Problem and next step

The venue chooses its liquidity route dynamically. Our signer deliberately proves
only one reviewed transaction shape; a new route cannot inherit that proof merely
because the displayed fee is the same. This is an execution-contract limitation,
not a Raft consensus failure.

Identify variant 156 from authoritative program sources, verify its complete
account/instruction effects, and qualify a narrowly scoped verifier extension
under the same $1/debit/repayment limits. Alternatively, use a documented venue
route-selection capability if one exists; none was established in this review.
Do not repeatedly request quotes hoping to obtain the old route.

After route qualification, prepare a fresh positive net receipt floor and current
account review. The exact on-chain threshold/rounding must be checked for this
route; the earlier KipseliV3 boundary simulation does not prove its behavior.
Only then review the start/unhalt/one-intent sequence, observe confirmed settlement
and balance deltas, and halt/stop afterward. Continuous stochastic trading is
outside this one-buy scope. No new economic approval is inferred from this review.
