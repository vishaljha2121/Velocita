# Qualified release and locked route adoption

Status: locally tested; not committed or installed. Native executable remains
the qualified `fe54001ce7bbd36c25947cc29ecf70833d161e37` artifact. This helper-only
change does not rebuild or change that executable.

## Problem and fix

The installed completed renewal archive pins the exact current manifest, units,
review documents and protected journal. Updating the manifest to a new executable
first would break the next renewal's check that the previous generation is still
installed. The older executable installer also expects to create guards that
already exist on this host.

`upgrade_byreal.py` therefore only stages four pinned release artifacts. It
requires both existing loaded start guards, locks all five native journal
directories, and leaves manifest, units, reviews, credentials and journals
unchanged. A partial staging failure remains stopped and requires reconciliation;
it does not automatically retry or remove the partial release.

The extended `idle_policy_renewal.py` accepts an explicit release witness together
with the existing route witness. Beyond the existing account-review pins it allows
only source commit, executable path and executable hash to change. Runtime image,
callers, economic limits and all other authority remain fixed.
`idle_policy_apply.py` checks all four staged artifact hashes under the locks,
performs fresh authenticated reads using both protected roles, then publishes the
release and route as one archived generation. Publication is not a single atomic
multi-file rename: start inhibition and explicit archived roll-forward recovery
protect interrupted updates. Four gateway journals remain unchanged; the pristine
protected signing journal advances its ordinal and policy domain.

The release witness's qualification hash is an owner-reviewed evidence reference,
not a substitute for native qualification. Bind it to the retained qualification
evidence. The executable's SHA-256 is
`e7fb8da295b0abbb7a8decf5c761068e2a732499c1a5e16c7b3433d6b5c7a590`.

## Validation

From the repository:

```sh
python3 -B -m unittest discover -s tests -p 'test_idle_policy_*.py'
python3 -B -m unittest discover -s docs/evidence/bounded_launch_review_2026-10-13/deploy -p 'test_*.py'
```

38 renewal/application tests and 12 staging tests passed. Tests exercise modified
release witnesses, all four artifact checks before publication, publication crash
boundaries with the combined generation, retained archives, real filesystem locks,
staging failures after each of the four artifact writes, and existing start guards.
Privileged service operations and host identities are injected in local tests;
these results do not claim host installation qualification.

Four targeted mutations produced assertion failures, with no test errors: removing
artifact checks, prior-release binding, immutable-authority checks, and existing
guard checks. See `mutation-results.json`. Independent read-only review found no
new correctness blocker in the staging/combined-adoption design.

## Current host evidence and remaining sequence

A fresh read-only SSH check found both NUC1 swap services `inactive`, PID 0,
`RefuseManualStart=yes`, and no directory for the qualified release under
`/opt/velocita/releases/`. No installation, route adoption, fresh account/quote
check or live order occurred in this continuation.

1. Checkpoint these Python helpers/tests and their evidence under the owner's
   committed-source rule. Keep unrelated working-tree changes out of the commit.
2. Transfer that committed helper source and verify it on NUC1; stage the already
   qualified native executable and its three pinned Python runtime files.
3. Obtain fresh account history/session evidence. Construct the actual route and
   release witnesses, verify native/Python digest parity for those exact bytes,
   and validate the short-lived transition bundle.
4. Apply the locked combined transition. Preserve all start guards and archives;
   verify journal identities, authenticated role reads and stopped services.
5. Refresh current account/unsigned-quote checks for the separate bounded $1
   launch review. Signing, broadcasting and starting trading are outside this
   installation operation. A fresh quote may select a different route; it must
   pass the actual installed policy rather than inherit the old quote's verdict.

Fresh account history is required before transition as well as near launch;
therefore the five-step educational outline is not a strictly serial checklist
with every account check deferred until after installation.
