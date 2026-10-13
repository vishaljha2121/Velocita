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

## Exact release and dormant upgrade

Implementation checkpoint: 1c582e6942673804e776bb757040a3a106a513cb.
A two-line renderer/test correction in 13cc98b7d8d24a21ce5f9549d989cf88e4ea822e
preserves normal unit mount order, allowing the installer to verify the old
installed unit byte-for-byte. No production C++ changed in that correction.
The original successful qualification is retained under initial-qualified.

The new public bundle is staged on NUC1 under the owner account; all 29 public
file hashes and the dormant bundle validator passed. Both services remain
inactive. After final qualification, install the update from the Mac:

    ssh -t vishal@100.72.127.102 'sh "$HOME/.cache/velocita-build/handoff-recovery-13cc98b-20261013/owner-install.sh"'

This replaces only the dormant executable/launcher units and runs daemon-reload.
It does not transfer credentials, initialize an account, start services or trade.
The previous 84c353a release remains retained. The source tree mounted by
observability is not swapped, and no voter/exporter restart is required.
Exact bundle pins and commands: dormant-upgrade.json and owner-install.sh.

Fresh public account documents still precede real credential adoption.
Implementation/fixture success is not a real host credential-adoption result.

## Final qualification result

Exact commit 13cc98b passed on both NUC x86_64 and Pi aarch64:
52 selected C++ tests; 50 account Python tests; 62 service/installer tests;
17 credential/SSH tests; the actual gateway refusal test, signer isolation and
schema golden checks; and eight validated fixture process drills per host.
Native credential tests used generated synthetic keys, never the owner's key.
Host status/logs and fixture results are in nuc1/ and pi/, with checked artifact
manifests. New compiler diagnostics are retained in warning-inventory.json;
cached builds are not claims of a warning-free clean build.

The final NUC gateway SHA-256 is
09f7c3ff0ea68db58cf55c0045e8fbf35c0dcffe1f01632a21cf77b2107b9d7c,
identical to the initial qualified C++ build before the renderer correction.
The dormant upgrade is ready for the owner command above. It has not yet run.
No real credential adoption, live service start, signature or trade occurred.
