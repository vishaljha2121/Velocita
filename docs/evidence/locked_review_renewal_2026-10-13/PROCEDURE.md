# Locked expiry-only review renewal

Implementation status: tested locally; host installation and source checkpoint are separate main-agent steps. This procedure cannot start a service or submit an order. The prepared bundle renews the transaction-review expiry and associated account-review pins; it cannot change route accounts, quantities, receipt/cost limits, caller identities, credentials, opening balances, or launch flags.

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
