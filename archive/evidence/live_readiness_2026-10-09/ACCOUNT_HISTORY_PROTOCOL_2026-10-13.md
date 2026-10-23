# Bounded initial SOL/USDC account cut — October 13, 2026

Status: revised collector implemented and offline-tested. The first authorized capture stopped on its fourth evidence request and is preserved unchanged at the private `account-history-20261013` location. This protocol revision does not approve an allocation or admit live trading.

## Problem and scope

TM's transactions feed excludes inbound/external deposits. Scanning only the owner's Solana address also misses an incoming USDC transfer whose instruction names the receiving token account rather than its owner. A balance snapshot alone can miss offsetting activity.

The collector combines authenticated TM account/order/balance observations with finalized Solana history for the main owner and **every currently existing legacy USDC token account** found for that owner. It prepares a bounded initial funding cut for explicit review. It does not prove lifetime wallet history: already-closed historical token accounts cannot be enumerated from a current token-account snapshot, and an RPC provider returning an empty/short history page is not independently proven archival completeness. It cannot prevent activity after collection.

## Fixed collection contract

Tool: `tools/tm-account-history-check.py`; defaults to printing the plan unless `--run` is supplied.

Inputs: existing private TM session, privately retained authenticated setup response, its explicit SHA-256, and a new private output directory. It refuses a different fresh user/main Solana owner. The retained response is an observation pin; its mere existence is not independent owner review.

- At most one existing-session refresh, with no new API-key login.
- GET only on TM `/v1/account/me/trading`, `/v1/gateway/balances`, `/v1/gateway/orders` (all statuses and venues).
- Keyless POST only to `https://api.mainnet-beta.solana.com`: `getTokenAccountsByOwner`, `getMultipleAccounts`, `getSignaturesForAddress`, `getTransaction`.
- Fixed limits: 110 evidence requests total; 300-second hard POSIX alarm around CLI collection, plus a monotonic deadline checked around requests; each evidence request timeout is the smaller of 20 seconds and remaining budget; 1 MiB/response; 20 rows/page; at most three pages per history/order sequence; at most eight current token accounts; at most 40 distinct transactions. Cursor pagination is bounded continuation, not retry. A full last page is insufficient to prove exhaustion and fails closed.
- JSON duplicate keys, malformed identities/amounts, missing transaction metadata, duplicate order IDs/cursors/signatures, unsupported transaction versions and regressing history fail closed.
- Discover both SPL Token and Token-2022 accounts. Any Token-2022 account or non-USDC legacy token account blocks this narrowly scoped cut. Wrong mint/owner/decimals, frozen/delegated token accounts and native token accounts block. A classic non-native USDC account may have an alternate, canonical 32-byte close authority: record its private identity and SHA-256 without calling it the sponsor. That authority can close an emptied account but does not authorize spending its USDC. Token-account rent is excluded from managed funding. The full account configuration, close authority, amount and rent are compared again at the end, so an authority change with unchanged USDC still blocks.
- Match native SOL and summed USDC token amounts to authenticated TM totals, require zero TM holds. Finalized native account must be a non-executable, empty-data system account.
- Fetch finalized histories for the owner and current USDC accounts; deduplicate signatures and read each transaction once. Preserve raw transactions; record native-owner and current-USDC-account deltas. A transaction matching a TM order hash is only a link, not proof of correct settlement. Unlinked records require attribution review.
- Recheck account discovery, SOL/USDC balances, identity and all-status orders, then each address's newest finalized signature. Changed history heads detect zero-net activity too. This is a bracketed observation, not an atomic multi-service snapshot or future exclusivity fence.
- `initialized`, `pending`, `active` and `cancel_pending` records are counted separately and block candidate cut emission. An old unsigned quote is not presumed expired or canceled. Terminal status alone does not establish detailed settlement economics.
- First transport/parser/truncation error stops collection; no retry, signing, order creation, submission, cancellation, simulation, funding adoption or balance mutation.

## Evidence contract

A new mode-0700 directory contains mode-0600 raw responses, a raw SHA-256 manifest, private request trace (no bearer), private detailed observations, and a redacted summary. Errors preserve partial raw evidence and a stopped manifest. Console output contains no wallet/user identity, token, URL secret or raw response.

When the bracket succeeds and no blocking orders exist, `baseline-cut.json` contains exactly:

```
schema, scope, owner_sha256, user_id_sha256, setup_sha256, balances_sha256,
evidence_manifest_sha256, capture_started_ms, capture_finished_ms,
start_slot, end_slot, base_atoms, quote_atoms, stable_window,
pagination_exhausted, blocking_orders, unsupported_accounts
```

`scope` is `sol-usdc-current-accounts`; owner hash uses decoded 32-byte key; user hash uses UUID UTF-8 bytes. Start/end slots come from finalized native-account reads. `stable_window` means the described bracket comparisons matched; `pagination_exhausted` means the bounded queried pages reached their endpoint's exhaustion marker. Neither field asserts complete lifetime history. The cut does not contain approval, and `result.private.json`/`summary.json` retain `live_admitted=false` and `requires_reviewed_initial_cut=true`.

Next: review the actual private observations and explicitly adopt the starting managed/outside partition. Existing SOL cost basis must not be invented. The candidate builder/startup review must hash-pin these exact bytes, retain the scope limitation, reject blocking records and require a fresh checked observation before authorization. Later cuts should be anchored to the reviewed initial cut; this collector currently recollects bounded history and does not claim ongoing monitoring.

## Offline validation

`python3 -m unittest discover -s tests -p test_tm_account_history.py`

22 tests cover incoming USDC without an owner signature, initialized-order blocking, native-balance mismatch, zero-net head changes, duplicate JSON/amounts, order cursor encoding/cycle/exhaustion and hard bounds, signature truncation, unsupported Token-2022/mints, malformed transaction identity/metadata, restricted transport and no retry, response cap, private evidence and redacted summaries. Additional cases cover a legitimate System Program account key, exact uint64 decimal boundaries, boolean-version rejection, required error metadata, monotonic timeout gating and independent per-program slot regression.

Ten isolated source mutations were killed by their designated unittest assertion failures, with zero test errors; results and runner are in `~/.cache/velocita-build/account-history-20261013/mutations-v2.json` and `mutations.py`. Mutations weaken initialized-order handling, token-account history coverage, head-change detection, pagination exhaustion, transaction metadata/version validation, native balance matching, individual slot comparisons, duplicate JSON handling, and full close-authority configuration comparison. New tests separately preserve refusal of delegation, native token accounts, frozen state and malformed close authorities; they verify rent exclusion and hard-alarm handler restoration. Working source was not mutated.

Sources: cached official TM account/gateway OpenAPI under `~/.cache/velocita-build/account-recovery-20261013/`; especially gateway `GET /orders`, pagination, `OrderStatus`, and `/transactions` scope. Solana results are treated as trusted-provider observations, not cryptographically independent finalized-state proofs.

## Protocol revision before the second capture

The first capture revealed a correct legacy USDC account with an alternate close authority. Blanket refusal conflated authority to reclaim empty-account rent with authority to transfer tokens. The revised collector accepts this narrowly scoped configuration, records it, compares it across the observation window, and leaves rent outside the proposed allocation. It still refuses a transfer delegate. An emptied USDC account can later be closed by this authority, which would invalidate that account's continued existence and require a new account observation; this evidence is not a future availability guarantee.

This revision was made after the failed observation and before another capture; the first capture is neither rewritten nor retried automatically. A separate new private directory is required. No money-moving operation was added.

Primary contract: [Solana close-account documentation](https://solana.com/docs/tokens/basics/close-account) states the non-native zero-token-balance prerequisite; classic SPL Token `CloseAccount` and `Transfer` use distinct authority checks. The main agent supplied this verified contract finding for this revision. The collector itself performs no network lookup when imported or tested.
