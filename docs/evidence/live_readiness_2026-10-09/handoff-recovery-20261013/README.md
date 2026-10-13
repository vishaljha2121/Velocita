# Protected credential handoff and delayed settlement

The existing gateway can recover an already-submitted order without preparing,
signing or executing another order. The systemd gateway nevertheless depended on
the signer, whose short account review expires. Recovery now has an explicit
manifest Boolean `recovery_only=true`, translated to `--swap-recovery-only`.
It forbids journal initialization and signer launch, omits the signer dependency
and socket bind, and forces the C++ runtime's existing reconciliation-only guards.
An idle or merely reserved journal cannot turn this mode into a trading process.
The normal trading unit retains its signer dependency. No Raft/schema changes.

The gateway retains its original account/history/transaction identities and
journals. Expired account reviews may support reconciliation; expired bearers
still refuse. A refreshed bearer may replace only the profile's bearer digest
and expiry after account identity is rechecked. Those two fields are outside the
runtime identity digest; do not replace session evidence, custody, owner, policy,
opening/account review or transaction review to make a recovery fit. No automatic
token refresh, service restart or venue retry is introduced.

## Credential path

`tools/tm-stage-swap-credential.py` defaults to proposal-only. Explicit `--send`
validates the local profile hash and private source, checks bearer expiry or the
P-256 private/public-key pair and signer ID, then sends one credential over SSH
stdin to the fixed NUC1 owner account. No secret is placed in argv, environment,
terminal output or error output. The receiver script itself is hash-checked.
It never authenticates to TM, signs a message or sends a venue request.

`deploy/v2/swap-runtime/credential_handoff.py stage` accepts at most 4096 bytes,
checks the transfer digest and creates an owner-only 0600 file directly in
`/run/user/1002`, after checking tmpfs and directory ownership. Core dumps are
disabled. Its installation mode requires root on the fixed NUC1 host, both
services inactive, the pinned root-owned runtime profile and an exact owner
staging path. It independently checks the credential against the profile before
writing a 0600 single-role file in that role's 0700 tmpfs directory. Existing
credentials are never overwritten by default. The service launcher then passes
only FD3; the C++ executable repeats credential and identity validation.

Explicit `--previous-sha256` permits only a gateway bearer rotation, requiring
the exact old content/metadata and atomic replacement while services are stopped.
It cannot rotate the signing key. Root is trusted; service users cannot modify
the public profile. Python/OpenSSL use bounded process memory but this tooling
does not claim locked/securely-zeroed Python heap memory or protection from root.

Staging sources remain in private tmpfs after adoption to preserve evidence of a
partial failure. After verifying a successful adoption, remove only the exact
returned staging file; do not use a wildcard cleanup. No real credential has
been transferred by the implementation tests.

## Use after fresh public documents are adopted

Pass `--role gateway --private-source PATH_TO_PRIVATE_SESSION` or `--role signer
--private-source PATH_TO_PRIVATE_API_KEY`, plus `--profile`, its SHA-256, the
reviewed receiver path and its SHA-256 to `tm-stage-swap-credential.py`.
Review proposal output, then the same command with `--send`. The receiver and its
imports must come from the reviewed source bundle. Do not paste raw secrets.

For each returned exact staging path, the owner runs the reviewed receiver with
sudo: `credential_handoff.py install --role ROLE --source EXACT_STAGING_PATH
--profile-sha256 REVIEWED_PROFILE_SHA`. No service is started by installation.
The root-owned account documents must already be installed and reviewed; missing
documents intentionally refuse credential installation. This is separate from
the previously completed dormant executable installation.

For delayed settlement, stop both services, retain all journals, render/adopt the
same account pins with `initialize_swap_recovery=false` and `recovery_only=true`,
then start only the gateway once. It reads the retained operation and finalized
receipt and may commit the proven settlement to Raft. It never sends another
create/execute request or invokes the signer. If evidence is still unavailable,
it stops with exposure retained. Another read is an explicit operator recovery
step, not an automatic retry or a new trade. Terminal success requires matching
on-chain deltas, a committed settlement and released reservation.

## Validation

Local: 41 service tests, 11 credential tests, six SSH-staging tests, actual gateway
argument-refusal test, and seven three-voter runtime scenarios passed. The latter
include a lost execution response, unresolved reservation retention and eventual
finalized recovery with unchanged signing/submission counts. Nine credential and
14 service mutations fail named assertions. Initial mutation-harness errors were
corrected and are not counted as kills. Native qualification is recorded
separately; these local tests do not claim a real transfer or live settlement.
