# Single-buy launch preparation

Prepared from the local source and evidence on October 13. No host status was
queried, credential content read, request sent, service changed, record committed
or trade executed by this preparation. The earlier catalog-only halted cluster
snapshot is historical; inspect the actual cluster before using any sequence,
kill epoch, executor generation, applied index or caller assignment below.

This is a supervised one-shot MVP, not unattended strategy operation. The deferred
external heartbeat is relevant before unattended operation; it is not a reason to
add another system to this supervised launch. An owner/operator must observe the
entire attempt and reconciliation, with the explicit stop procedure ready.

The current cluster is reported as schema 12 at `86f4408`. These transaction
verifier/startup changes do not alter Raft, the reducer or its state schema.
Prefer replacing only the gateway/signer executable and service bundle. Confirm
actual node image identity and compare relevant committed node sources before
declaring that compatible; no voter/exporter restart is inherently required for
the gateway-only release. If voter bytes or compatibility differ, review that
change separately rather than silently restarting the cluster.

## Exact scope

One operator-triggered SOL buy, effect 1: 998,000 USDC atoms principal plus 2,000
atoms fee, exactly 1,000,000 total debit. The opening's purchase budget and per-buy
limit are both 1,000,000; maximum fee 2,000 and realized-loss allowance at most
500,000. Repayment cap 2,039,280 lamports, zero guaranteed sponsor credit. No
automatic retry, resize, second effect, sell, buyback or strategy restart.

The owner selected all actual TM SOL/USDC inventory; paper balances are excluded.
The October 13 balance snapshot of 40.006989 USDC and 0.040764166 SOL must be
refreshed at activation. The historical $1 CLI buy leaves $4 of the separate $5
session allowance; this MVP may use only $1 of it. New deposits do not expand
that authorization. The $0.50 realized-loss limit is not a market-price guarantee
for an unsold SOL position; the one-buy receipt floor bounds acquisition, and no
automatic stop-loss sale is authorized.

## Remaining concrete artifacts

| Artifact | Required contents and provenance |
|---|---|
| Release and catalog | Exact committed NUC/Pi qualification and executable SHA-256. Existing SOL account 2/instrument 2 catalog, fresh authenticated authority domain/incarnation and current global controls. No new catalog or cluster is inherently required. |
| Fresh account history | Reuse `tools/tm-account-history-check.py` with the reviewed setup-response pin. Stable authenticated TM/chain balances, attributed finalized activity, current token accounts, no holds or blocking orders; preserve current-account/provider-completeness limitations. Its small history-cut JSON becomes `account.history`. |
| Opening valuation | A fresh executable net USDC liquidation valuation of the entire managed SOL amount, with exact quoted size, timestamp and evidence hash. Do not scale the old 0.0084 SOL quote or invent historical acquisition cost. No sell is executed. The offline candidate tool intentionally leaves this null and cannot fund a ledger. |
| Canonical opening record | Use production `swap::encode` for `Initialize`, including the reviewed balances/mark, funding-evidence hash, exact one-buy limits, binding, current kill epoch/generation, and nonzero source-age/sign-lifetime/price limits. `max_sell_input` must be nonzero for reducer validity, but the real signer still refuses every sell. Do not set unused required fields to zero by guesswork. |
| Transaction account review | Fresh independent source USDC mint/owner/initialized/unfrozen/delegate/close-authority facts, fee mint/recipient, sponsor, absent or supported WSOL destination and warmed active lookup contents. Pin the 20 resolved accounts and canonical programs. Snapshot facts are not guaranteed sponsor funding. |
| `transaction.review` | Strict 12-field document consumed by `decode_swap_transaction_review`: schema, 20 account keys, lookup table/slot/evidence/count/addresses, envelope max age (at most 30 seconds), compute price/units caps, slippage cap and expiry. Hash exact bytes into `transaction_review_digest`. No wallet secrets belong here. |
| `runtime.profile` | Existing strict profile plus 2,039,280 cost cap and transaction-review digest; actual owner/signer/TM organization/user, authority, account/custody/assets/session evidence, distinct callers and service UIDs, bearer digest/expiry, sponsor payer, socket, key version and signer-instance pins. No raw bearer/private key. |
| `account.review` | Exact runtime identity and canonical opening digest, user ID, history digest, reviewed/expiry timestamps, managed/outside partition and attributed outside deltas. All inventory is managed unless subsequently reviewed otherwise. Maximum lifetime 900,000 ms. |
| Service manifest | Qualified source/binary/runtime image/catalog hashes, all three account-document hashes, existing endpoints, separate callers 22/23 and keys 13/14, UIDs 29001/29002. `launch_authorized=false` during preparation; reviewed true only for actual activation. First-use initialization is a separate explicit manifest Boolean. |
| Fresh intent record | Canonical `Intent`, operator-submitted with record role `strategy`: effect 1, buy, exact 998,000/1,000,000/2,000 amounts, fresh positive net SOL floor, matching binding/controls, timestamps and bounded transient base hold. No old payload/quote reused. |

The account review and history must still be current when both processes start;
the history cut expires 15 minutes after capture. Capture late, after release
installation and offline rendering have been rehearsed. A manually requested
unsigned quote can create an initialized TM order record: take the final history
cut afterward, and do not hide it if the collector reports a blocking order.
The gateway obtains the actual fresh unsigned buy during its prepared-intent
flow. It must refuse if that route differs from the reviewed account policy.

For an actual quoted gross output `Q` and encoded slippage `S` basis points, this
narrow decoder's gross floor is `ceil(Q*(10000-S)/10000)`. Conservative net floor
subtracts the encoded repayment (no more than 2,039,280); no sponsor credit is
added. Verify the exact route's rounding and compare the result to the committed
intent floor. The retained diagnostic 6,900,812 lamports is not a fresh limit.
Kernel `max_buy_price_per_sol` must agree with that net floor: its admission
requires `min_base_receive >= ceil(1,000,000*1,000,000,000/max_buy_price_per_sol)`.
Use current prices and the owner's loss allowance to choose the economic limit;
do not copy the synthetic test's $145/SOL value.

## Reuse commands and startup order

These are command shapes, not a ready-to-run ceremony. Capitalized shell
variables must come from a reviewed private preparation bundle and fresh
authenticated cluster state. Do not fill them from old logs, print credentials,
or run the write commands before the concrete launch review.

Read-only local preparation tools:

```zsh
python3 -B tools/tm-account-history-check.py \
  --session "$SESSION_FILE" --setup-pin "$SETUP_RESPONSE" \
  --setup-sha256 "$SETUP_SHA256" --output "$FRESH_CAPTURE" --run
python3 -B tools/tm-account-review-candidate.py \
  --capture "$FRESH_CAPTURE" --output "$FRESH_PROPOSAL" \
  --owner-buy-evidence "$OWNER_BUY_EVIDENCE" \
  --owner-buy-sha256 "$OWNER_BUY_SHA256" --all-current-inventory
python3 -B deploy/v2/swap-runtime/service_profile.py \
  --manifest "$REVIEWED_MANIFEST" --output "$NEW_SERVICE_REVIEW"
```

The history collector may renew the session under existing POST authorization;
it cannot sign or execute. Candidate generation is offline and never adopts
balances. The existing route probe has a fixed $1 buy or 0.0084 SOL sell; it is
not an arbitrary-size opening-valuation tool.

1. Confirm one cluster only, same catalog, authenticated state and no pending
   legacy order/exposure. Keep the strategy/edge stopped. Retain prior runtime
   and state artifacts; no clean/reset/reinitialize of an existing ledger.
2. Install the new immutable release dormant and with actual verified hashes.
   `install_disabled.py --bundle "$BUNDLE" --bundle-sha256 "$BUNDLE_SHA256"`
   validates without applying; `--apply` is the reviewed privileged installation
   step. Replacement additionally requires `--previous-bundle` and its SHA-256,
   exact prior bytes, inactive/static services and empty credential staging.
   This tool does not install account documents, activate units or move funds.
3. Prepare fresh final documents and exact protected credentials through the
   existing secret workflow. Role-specific raw bearer and signer PEM must remain
   in private tmpfs directories with exact owner/mode; never in repository,
   command line, environment, public profile or deployment logs. Root-owned
   public documents are immutable to both service users.
4. Use the existing Mac operator client/session and pipe its caller key via
   `tools/velocita-secrets client-key caller-9`. Preserve its next unused request
   sequence. If caller 22 has no role, commit `assign-role 22 gateway`; do not
   borrow gateway identity for the signer. Read-only signer caller 23 does not
   need gateway command authority. Confirm existing controls rather than
   advancing their epochs blindly.
5. While globally halted, submit exactly one reviewed opening record with
   `"$CLIENT" "${OPERATOR_ARGS[@]}" --sequence "$SEQUENCE" spot-swap-record "$OPENING_RECORD"`.
   Its hash and decoded content must match the account review. The existing
   client accepts only canonical Initialize or Intent records; it does not
   itself generate them. Funding initialization refuses a non-exclusive or
   already-funded account.
6. First start uses a hash-pinned manifest with explicit
   `initialize_swap_recovery=true`. The renderer adds the existing C++
   `--initialize-swap-recovery` flag for each role. Start signer first, then
   gateway, under their fixed service users and reviewed units. The C++ path
   requires an unused funded opening with no pending intent/effect/settlement;
   each store create refuses existing files. Preserve partial initialization
   on any failure. There is no missing-file fallback or retry.
7. Verify initialized journals, authenticated state/account checks and signer
   IPC before admitting risk. Keep only the single explicit intent source;
   do not start the stochastic edge. Commit `strategy-mode normal "$KILL_EPOCH"
   "$EXECUTOR_GENERATION"`, then submit the fresh canonical buy record via
   `spot-swap-record "$INTENT_RECORD"` once. The operator key is supplied by
   pipe, not inline. Fresh request sequences and committed acknowledgments are
   required after every command. Stop on any failure; no automatic repetition.
8. Observe finalized transaction reconciliation and committed settlement, not
   just a CLI/HTTP success. Confirm actual wallet deltas, one effect, exact fee,
   released reservation and correct balance/P&L. Lost responses retain exposure
   and use the existing same-operation reconciliation path; never create a new
   quote/order as a retry. After settlement, halt and stop the live services.
9. Before any later restart, render/review a manifest with initialization absent
   or false and regenerate the unit's manifest SHA. Recover existing journals;
   never restart the first-use command. Both units remain `Restart=no`.

## Minimal remaining tooling and implemented fixes

The existing release/account tooling covers most of this procedure. The concrete
launch bundle still needs an offline canonical record/document builder (reuse
`swap::encode`, `swap_opening_review_digest`, runtime-identity digest and document
decoders), the exact-size unsigned opening-valuation capture, and a reviewed
root-owned adoption step for public account documents/activation units. No
general strategy engine or new Raft command is required. Reject incomplete
values rather than emitting an executable-looking placeholder bundle.

This preparation found and fixed three launch-path issues locally:

- Dormant release replacement now permits the executable SHA to change with its
  immutable source release, while rejecting account/authorization/caller/catalog
  or initialization-policy changes and still validating both manifests.
- Optional Boolean `initialize_swap_recovery` selects explicit first use. Old
  manifests and false continue to recover only; the flag changes the manifest
  hash embedded in the unit. C++ retains the actual empty-opening and create-only
  checks. No automatic initialization or recovery retry was added.
- `launch()` requires a nonzero, valid, already pinned transaction-review SHA
  before opening either caller or venue credentials. Legacy profile previews can
  remain read-only; real launch refuses them.

Validation: 39 service/profile/launcher/provision tests and 7 dormant-install
tests passed; 8 isolated named mutations killed. Red logs showed the missing
release-hash/initialization/legacy-before-secret behavior before implementation.
An intermediate missing Python import and assertion-order mismatch were corrected
and are not counted as mutation kills. Logs and isolated mutant sources:
`~/.cache/velocita-build/bounded-launch-20261013/`. Reproduce mutations with
`python3 -B tests/mutation_swap_service_launch.py --output "$FRESH_MUTATION_DIR"`.

These local fixes are not native qualification or deployment evidence. Main
integration owns C++ admission, owner CLI wording, checkpoint/build and eventual
bounded activation. No financial limit was enlarged by this preparation.
