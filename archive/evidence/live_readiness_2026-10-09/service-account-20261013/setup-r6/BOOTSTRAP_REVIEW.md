# Read-only schema12 cluster bootstrap review — October 13

The owner approved `../setup-r5/CUTOVER_SCOPE.md`. This document reviews the
minimum replacement-cluster setup and bounded read qualification; it does not
execute it. Main owns deployment, credentials and the operator ceremony.

Source reviewed: `86f4408` production C++ and the current committed read probe.
The reviewed node configuration, static identity, session fence, kernel control
and operator-client files have no diff from `86f4408` in this working tree.

## Minimum operator ceremony

Fresh `TradingState` starts halted with kill epoch 0, executor generation 0 and
no installed catalog. `InstallExecutionCatalog` requires nonzero matching control
epochs and an otherwise empty halted state. Three commands suffice:

```zsh
# C is the verified schema12-compatible client; A contains the newly reviewed
# incarnation, existing operator CallerId7/key9, new private session path,
# actual Mac response endpoint and the three fixed voter ingress endpoints.
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 1 kill-epoch 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 2 executor-generation 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 3 install-catalog \
  3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe 1 1
```

Use `set -euo pipefail`; preserve output and stop on a failed/uncertain command.
Do not rerun a partial ceremony from sequence1. The root above is the existing
catalog's canonical root, subject to comparison against the copied catalog using
`load_catalog_file`, not its file SHA256. The retained catalog file SHA256 is
`8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f`.

Omit the old ceremony's risk-limit and role-assignment commands. Neither gateway
nor signer **state reads** require a replicated trading role. No policy, strategy
activation, live mode, swap opening, balance adoption, funding or journal
initialization is needed. Kill epoch1 forces halted, and executor generation1
preserves that state. Three successfully applied operator commands yield state
version3; Raft applied index may be higher due to the current-term commit marker.

The operator client auto-creates a missing session epoch file and otherwise
advances it. Its command sequence is distinct from its authenticated envelope
sequence/epoch. Use a new private incarnation-specific Mac session directory;
do not alter or reuse archived paper gateway or operator sessions.

## Voter configuration and caller registrations

All three configs must contain the same caller mapping and response routes:

```ini
caller.13.caller_id = 22
caller.13.key_file = /run/velocita-secrets/caller-13.key
caller.13.response_endpoint = 100.72.127.102:44413
gateway.caller = 22

caller.14.caller_id = 23
caller.14.key_file = /run/velocita-secrets/caller-14.key
caller.14.response_endpoint = 100.72.127.102:44414
signer.caller = 23
```

Retain the operator caller/key/endpoint needed for the Mac ceremony and verify
the Mac address before staging. Preserve the reviewed peer keys, peer endpoints,
ingress rate/burst limits, WAL capacity, catalog root, deployment mode and owner
budgets. Changing only the incarnation and adding signer registration must not
silently remove prior operational limits. Key14 must exist consistently in all
voters' tmpfs and the signer read service's private tmpfs; independently verify
its source without printing bytes. Key13 is the existing gateway credential.

`NodeConfig::finish` requires each configured caller to have a key and response
endpoint, rejects duplicate CallerIds and requires gateway/signer callers to be
distinct registered non-operator IDs. Caller IDs and key IDs are separate from
Linux service UIDs29001/29002. There is no replicated `signer` role assignment.
`V2Node::handle_gateway` admits the configured signer to state reads and swap
authority requests, while command ingress explicitly refuses that signer as
`signer_read_only`. This probe only sends the state-read request.

Responses go to the endpoint in the voter config, not an arbitrary client
argument. Stop the old paper gateway before handing 44413 to the gateway read
probe. Keep both production trading units inactive. The two read probes have
different endpoints, keys, epoch files, Aeron directories and locks; neither may
share a session with another active process.

Start the replacement voters with `startup = bootstrap` only against fresh
empty data. The node creates `session-epoch.bin`; a restart with `bootstrap`
refuses the existing epoch. After a successful initial startup, stage the normal
`startup = recover` setting for future starts, using the standard controlled
configuration workflow. Do not delete an epoch to force bootstrap again.

## Canonical manifest and authority domain

Use the production parser and encoder for each candidate node config:

```cpp
auto parsed = load_node_config(config_path);             // require parsed.config
auto identity = parsed.config->static_identity();
std::array<std::byte, kMaxStaticIdentityBytes> encoded{};
auto result = encode_static_identity(identity, encoded); // require error == none
VerifiedStaticIdentity verified;
auto error = VerifiedStaticIdentity::decode(
    std::span(encoded).first(result.bytes_written), verified); // require none
// Record verified.manifest_digest() and verified.authority_domain().
```

This stage reads configuration only, not caller keys. Require all three encoded
identities to decode and all three manifest digests/authority domains to match.
The local node ID differs, so each complete identity-file hash may differ.

The schema12 release also requires **replication semantics epoch2**, not the old
epoch1. `NodeConfig::static_identity()` uses the compiled defaults: snapshot12,
command/result12, authority-marker1, catalog1, replication2, storage1 and quorum
mode1. A three-voter deployment uses its existing laboratory mode, not the
two-host-pilot mode. The sorted voter set must be exactly1,2,3 and contain each
local node ID.

For understanding only, the manifest digest hashes the domain prefix
`velocita.v2.cluster-manifest.v1` followed by canonical identity bytes covering
incarnation through voter IDs, excluding local node ID and frame checksum.
The authority domain hashes `velocita.v2.authority-domain.v1` followed by the
manifest digest bytes. Prefixes have no NUL terminator. Use the C++ APIs above,
not a separately reimplemented serializer, JSON hash or config-file SHA.

After startup, decode the persisted `v2-identity.bin` through the same verifier
and compare to the precomputed per-node identity and shared authority domain.
Pin the new incarnation/domain in both read invocations. The dormant production
manifest installed during setup-r5 still names the old incarnation; it remains
inactive and is not evidence of new-cluster configuration until explicitly
updated with matching unit manifest digest. Do not silently adopt new account
pins during that metadata update.

## Explicit durable read-session preparation

`tests/v2_authenticated_read_probe.cpp` deliberately refuses a missing session
file. It checks exact0600, matching production UID, regular/single-link file,
safe directory ancestry and no preexisting `.tmp`; takes a `.read-probe.lock`;
and calls `advance_session_epoch_file` before connecting. It never creates or
resets an epoch itself.

Prepare a separate fresh session file for each role using the production API:

```cpp
auto result = create_session_epoch_file(fresh_role_session_path);
// Require result.error == SessionEpochError::none and result.epoch == 1.
```

Run creation under that role UID in a role-owned0700 durable directory, or
create exclusively and then verify exact ownership/mode before handing it off.
The function creates with `O_EXCL | O_NOFOLLOW`, mode0600, and fsyncs both file
and directory. It refuses an existing file. A small initialization helper linked
against the same committed `session_fence` implementation can expose this API;
it must contain no credentials or network operation. The normal read probe then
advances epoch1 to2 and persists it before sending. A failed run still consumes
its epoch; preserve it and its `.tmp`/lock evidence, do not restore epoch1.

The exact file is16 bytes: ASCII `VLS1` (4 bytes), uint64 epoch little-endian
(8 bytes), IEEE CRC32 little-endian over the first12 bytes (4 bytes). Initial
epoch is1; zero is invalid. This describes the format for review, not a reason
to create it with `echo`, unvalidated Python bytes or an empty file. Prefer the
existing C++ API and its tests. Keep this durable session outside credential
tmpfs; wiping it while retaining an incarnation can violate replay fencing.

Create neither the read probe's Aeron leaf directory nor its media-driver files
in advance: the helper insists the leaf is absent and creates it mode0700.
Its parent must be root-owned or role-owned and not group/world writable.

## Invocation and acceptance evidence

Each helper runs under its production UID/GID with a separate service sandbox,
read-only catalog, private caller-key tmpfs bind, writable private session path,
fresh Aeron directory and pinned qualified runtime. Fixed arguments:

```text
--run --role gateway|signer
--incarnation <new-32hex>
--catalog-root <verified-existing-root>
--authority-domain <new-production-derived-domain>
--response-port 44413|44414
--key-file <role-private-tmpfs-caller-file>
--session-file <role-private-durable-session-file>
--catalog <root-owned-pinned-catalog>
--aeron-dir <fresh-role-private-leaf>
```

The committed probe checks the actual UID/GID, derives incarnation-bound
authentication keys, verifies full snapshot integrity/decoding, requires the
leader response, positive term/applied index, matching authority domain/catalog,
and zero unauthentic/fenced/uncorrelated/proof-refused/dropped counters. It sends
one correlated read, permitting only normal transport retransmission/redirect
inside its12-second deadline. Outer bounds remain60-second service and75-second
controller with no outer retry.

Do not demand every raw service counter is zero: a follower may legitimately
answer not-leader before the link redirects. Do demand the specific bad-auth,
fenced, correlation/proof/drop counters defined above stay zero. The production
leader may append/apply its internal current-term marker while serving a read;
this is not a trading command or funding action.

The probe's current output does not print the entire state or prove a general
authorization-negative test. Preserve the three operator results and stopped
edge/gateway postconditions alongside successful read outputs. Those explain
why this cluster has only halted/catalog setup, with no funded strategy or
trading processes. Do not infer live admission from `authenticated_voter_read`.

## Historical script pitfalls

`release-884816d/stage-host.sh` is an audit example, not executable as-is here:
its old/new incarnation checks, source images, retired destination and schema
are obsolete. Its companion ceremony also installs paper risk limits and roles,
which are unnecessary for this cutover. The full clean-reset runbook starts a
funded paper run; this approved scope stops at the three voters, their exporters
and bounded reads. Do not run the old activation/profile helper or copy its
$25 paper cash into new state. Archive old data/config/unit/monitoring identities
and verify them before replacement, as specified in the approved scope.
