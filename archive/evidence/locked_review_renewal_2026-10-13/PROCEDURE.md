# Locked expiry-only review renewal

This procedure cannot start a service or submit an order. The prepared bundle renews the transaction-review expiry and associated account-review pins; it cannot change route accounts, quantities, receipt/cost limits, caller identities, signer keys, opening balances, or launch flags. A separately pinned same-account bearer refresh is supported as described below.

## Why the locked transition is needed

The protected signer's journal is bound to a transaction-policy digest that includes its expiry. Editing only the review file therefore makes the existing journal unusable. Deleting or reinitializing the journal would discard replay protection. Instead, retain every old byte and advance the protected journal ordinal exactly once, keeping its idle phase, zero effect, original committed index and original state digest. The other four journals remain byte-identical. Repeated pristine expiry renewals advance the protected ordinal again; no ordinal is reset or wrapped.

## Fixed owner procedure

1. Prepare a pinned `renewal.json` bundle with schema 1, evaluation timestamp, installed authenticated-read helper hash, and exact before/after artifacts returned by `idle_policy_renewal.propose`. Include every manifest, account/runtime/transaction document and all five journal records. The bundle is private; do not commit its account data. Default invocation validates only.
2. Execute the committed `idle_policy_apply.py --bundle … --sha256 … --apply` as the NUC1 root owner. The helper serializes migrations using a flock on the public directory. No secrets are read by this Python helper.
3. Persist `/etc/velocita-idle-renewal-active` and root-owned system/user service condition drop-ins. The marker is intentionally under the globally traversable `/etc`: the user manager cannot inspect the private public-document directory. Verify the effective non-trigger, negative path condition through systemd's typed D-Bus property (v249 `systemctl show` renders this compound property as `[unprintable]`). Reload the managers, stop and verify all known live and legacy paper producers, and keep the inhibition in place. No voter is stopped or replaced.
4. Acquire nonblocking exclusive flock locks on all five journal **directory inodes**, matching the native C++ stores. Verify owners, modes, current generation bytes and pinned executable. The native authenticated reader is then invoked directly under each protected service identity; saved JSON is never accepted as proof. Both fresh reads must retain the exact original state digest and an applied index at least as large as the original journal index. An unchanged digest inherits the original initialization checks: no pending intent/effect, no settlement, and the same funding/authority state. This assumes the trusted owner does not issue concurrent operator commands; it is not a new distributed fencing protocol.
5. Fsync a private before/after archive and exact publication plan before changing a destination. Atomically replace the protected signer journal, reviewed documents, manifest and rendered unit files, fsyncing each parent. Keep the four gateway journal bytes unchanged. The replacement keeps file ownership/modes and uses a deterministic pending filename that can be inspected on recovery.
6. Reload systemd and verify live services remain inactive. Leave the marker and condition drop-ins installed. Removing the marker and launching requires the separate reviewed start step. A renewed review is not permission to trade.

## Crash recovery

Use the **same bundle and pin** with explicit `--recover`. There is no automatic retry. Recovery reacquires all locks, keeps inhibition, executes new protected authenticated reads and accepts only the before/after bytes already pinned in the archive. It rolls forward only, including if the new review expired while the host was down; expired reviews still cannot launch. An incomplete archive can be finished only while every installed destination is still at its before generation. A ready archive with missing/corrupt bytes is refused. Completed recovery is idempotent.

A second expiry renewal may replace an old inhibition marker only after validating the prior completion hash and every currently installed after hash. Unknown bytes, changed state, in-use locks, foreign account history, expired initial proposals, pending effects and changed economic authority refuse the transition and preserve the inhibition and evidence.

## Verification

- 15 application tests and 7 proposal tests pass locally (22 total).
- Actual OS flock exclusion, symlink refusal, exact state/role/index refusal, all publication crash boundaries, incomplete archive recovery, pending rename recovery, corruption refusal, and proof-before-write behavior tested.
- Four mutation runs each fail a named assertion with zero test errors: bypass exact state, omit native directory lock, skip the fresh authenticated read, and accept a corrupted archive.
- Existing native hash-parity evidence remains in `../live_fill_wiring_2026-10-13/idle-renewal-parity-20261013/`.
- No credentials, live host writes, service activation, orders or funds movement were performed by the implementation agent.

## Real review size correction

The first host preparation refused the17780-byte installed transaction review
before writing a proposal or any journal. It reused the8KiB runtime-profile parser.
Transaction-only parsing now uses the native contract's32KiB limit with the same
strictASCII/unique-field grammar. Runtime/account profile limits stay8KiB.
A256-address lookup fixture reproduces the original failure and now passes;
32769-byte input is rejected.66relevant local tests pass and two parser mutations
failed assertions with zeroerrors. This corrects the operational helper only; the
qualified C++ gateway artifact remains69679c0.

## Expired bearer discovered during host preparation

The installed token expired while telemetry qualification and deployment were in
progress. The existing protected handoff already supports replacing the gateway
bearer. The renewal now optionally includes a pinned `session-refresh.json`:
schema1, authenticated setup/history hashes, bearer SHA256 and actual token expiry.
The proposal verifies the same owner/user/balances, derives the original canonical
session-evidence formula from the fresh capture, and updates the runtime-identity
pin coherently. Without this witness, session-field changes remain forbidden.
The token bytes are never in the bundle, archive, logs or committed evidence.

After locked document publication, use the committed
`tm-stage-swap-credential.py` to send the locally verified session over SSH stdin
to owner-only tmpfs. Then call `credential_handoff.py install --role gateway`
with the new profile hash and the exact previous bearer hash. Both services must
be inactive, and all persistent startup guards stay installed. Verify the installed
bearer hash, role ownership/mode and lifetime; record this as a separate completion.
The document archive's `complete` marker does not claim credential installation.

If interrupted between these operations, the new profile with an old bearer
refuses admission. Before an explicit recovery, compare the installed bearer hash
against the pinned old/new values. If already new, verify and record completion;
do not repeat replacement with the old hash. No credential bytes are archived.
Session evidence changes runtime identity but none of the five journal domains;
only the policy-expiry change advances the protected journal ordinal. This does
not alter kernel funding or authorize a trade.
