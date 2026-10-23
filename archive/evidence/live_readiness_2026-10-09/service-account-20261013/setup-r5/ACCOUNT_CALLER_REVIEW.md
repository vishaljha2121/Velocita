# Account and caller installation review — October 13

Scope: static review of repository source and retained evidence for the requested
checkpoint, inactive production-unit installation and authenticated state reads.
This review made no host changes, opened no credential contents, and did not
refresh account state. Host/configuration observations below require the main
agent's current remote verification before use.

## Decision

Install the committed release helpers and **inactive, launch-disabled** units now.
Do not invent executable account pins or enable the production launcher to run a
read test. Its launch contract includes `--arm-live-venue` and `--allow-new-risk`;
`launch_authorized=false` must continue to refuse before credential access.
A bounded purpose-built state reader needs only a Raft caller credential, not a
TM bearer, venue signing key, live funding, or the production trading loop.

Current recorded paper voters and the qualified swap executable are incompatible
for production state decoding. This is an existing deployment dependency, not a
new optional live gate. A diagnostic may prove authenticated delivery from the
old voters, but must report the schema mismatch and leave production read
qualification false. Never reinterpret snapshot bytes to hide it.

## Four distinct identities

| Identity | Reviewed value / status | What it controls |
|---|---|---|
| Linux gateway UID / primary GID | 29001 / 29001, installed in revision 4 | Files, process isolation and Unix peer identity |
| Linux signer UID / socket GID | 29002 / 29002, installed in revision 4 | Private key/state and signing socket |
| Raft gateway CallerId / authentication key ID | Existing paper caller 22 / key 13 | Gateway requests, authenticated response routing and command replay identity |
| Raft signer CallerId / key ID | 23 / 14 are candidates, **not verified registered** | Independent signer reads under new voter support |

The Linux UID must not be substituted for CallerId, and key ID is not CallerId.
Retained node config maps key 12 to strategy CallerId 21, key 13 to gateway
CallerId 22, and the latter responses to `100.72.127.102:44413`. Existing gateway
traffic and a second reader must not share a response endpoint concurrently.
The config fixes that endpoint; changing the client alone does not reroute node
responses. Reusing caller 22 for the signer also fails to prove independent
signer identity and violates the separate-principal design.

Current code accepts signer state/authority reads when the configured
`signer.caller` matches the authenticated caller. This ingress capability is
separate from the replicated operator/strategy/gateway role table. Do not assign
the signer the gateway role merely to make reads work. Every voter needs the
matching caller/key/endpoint registration and `signer.caller` setting on the
compatible release. Preserve one cluster and coordinate voter/exporter changes.

## Verified source incompatibility

`git show` inspection establishes:

| Source | Snapshot schema | State-read admission |
|---|---|---|
| Recorded deployed `884816d` | 11 | Only configured `gateway.caller`; no signer principal; old election-read behavior |
| Qualified `86f4408` | 12 | Separate signer principal and current-term applied barrier |

The new `GatewayLink` hashes and decodes the complete snapshot before yielding
state. An old-schema response cannot be called a successful new production read.
The old deployment also lacks the new election-read barrier. Authentication of
old bytes alone does not qualify that correctness property. Do not reset or
replace the current cluster to sidestep this review; a migration/replacement
step must preserve the existing state and operational constraints.

## Account pins and adoption

The last public summary (`../account-summary.json`) records October 13 08:57 UTC:

- TM user/wallet identity matched, DeFi enabled, zero holds.
- Actual balance: 1.006989 USDC and 0.031896934 SOL.
- Proposed managed allocation: 1.006989 USDC, zero SOL; existing SOL outside the
  strategy with no invented acquisition cost, rent excluded.
- Proposal explicitly says `proposal_requires_owner_review`,
  `ledger_initialized=false`, `live_admitted=false`.

These are retained observations, not fresh balances or approved opening funding.
The existing $5 cumulative purchase cap and $0.50 loss stop do not add cash or
authorize that proposed partition. Do not reuse the virtual paper $25.

An executable profile additionally binds the real account/user/wallet, bearer
digest and expiry, signer public key/version/instance, custody/session evidence,
catalog and policy/risk versions, incarnation, Raft authority domain, journal
bindings and exact account-history/review digests. `tm-account-review-candidate.py`
only produces an offline proposal; it does not adopt funding. The production
loader requires the capture/review window to be at most 15 minutes. Refresh and
review only when activation is being prepared; do not extend old timestamps.

Safe inactive installation can pin historical review evidence as historical or
leave executable account files absent. It must not label fabricated/nonzero test
hashes or stale evidence as reviewed production configuration. The immutable
release, runtime and actual catalog file can be pinned independently now.

## Credential delivery constraint

`tools/velocita-secrets` and `docs/M2_SECRETS_ROTATION_REVIEW.md` require Raft
secrets to remain in Mac Keychain and node tmpfs, never node disk. The push tool
checks tmpfs, directory mode 0700 and ownership. Current service paths under
`/etc/velocita-swap/{gateway,signer}` are empty disk directories; copying a caller
key there would silently change the established custody policy.

For the read drill, use an ephemeral protected tmpfs file owned by the target
service UID, bind it read-only into that process, then remove it after the bounded
run. Avoid passing a secret through argv, logs, environment or an evidence file.
Production credential paths can later retain the reviewed in-unit pathname via
a read-only bind from protected per-role tmpfs, or use an explicitly reviewed
systemd credential adaptation. Test path ownership and descriptor invariants;
do not loosen them to accommodate a staging directory.

## Exact completion claims permitted

1. Checkpoint helpers and evidence from explicit paths, excluding shared dirty work.
2. Install immutable release code and disabled units without credentials/funding.
3. Run bounded authenticated reads using the correct existing response endpoint
   while preserving the current caller's running process/session state.
4. Report authentication, snapshot completeness/hash, schema decoding, and
   signer-specific authorization separately. A schema11 read or transport-only
   success cannot mark schema12 production gateway/signer reads complete.
5. Finish independent signer registration and compatible voter deployment before
   closing production authenticated-read qualification.

No trade signature, venue order, funding initialization, journal deletion or
live admission is authorized by this batch. The deferred sponsor-return
discrepancy and exact net-receipt/rounding checks remain open; the established
$1 venue minimum is a different fact.

## Concrete follow-up: compatible cluster, inactive units and tmpfs custody

### Schema12 deployment

`StaticIdentity` pins both snapshot and command/result schemas. `884816d` sets
both to 11; `86f4408` sets both to 12. `src/v2_static_identity.cpp` validates them
against the compiled versions, and `src/v2_identity_startup.cpp` rejects an
unsupported persisted identity or one differing from the requested identity.
Therefore a stop/restart with the new image and the same schema11 data directory
is not a supported upgrade. Editing identity fields by hand is not migration:
the WAL, snapshots, replay format and bound metadata remain incompatible.

No schema11-to-12 state migrator was found in the reviewed `tools`, `deploy/v2`,
`src` or `include/velocita/v2` tree. The documented available path is a **fresh
incarnation with archived prior state**, adapted from `docs/CLEAN_RESET_RUNBOOK.md`:

1. Verify the committed release/image identities and capture actual old cluster,
   caller, risk, catalog, pending-order, ledger and gateway state before change.
2. Review retirement of the current paper incarnation and the replacement's
   opening state. This is a separate state-changing step, not implied by
   installing inactive units or reading the existing cluster.
3. Stop the existing edge/gateway and all old voters; archive, never overwrite or
   delete, their state/configurations. Keep voter/exporter pairs aligned and
   maintain the one-cluster limit.
4. Initialize the new empty data directories under a newly reviewed incarnation,
   using schema12-compatible binaries across all voters, existing SOL catalog
   where valid, caller22/key13 and independently provisioned signer23/key14 (or
   different reviewed unused IDs). Include distinct fixed response endpoints.
5. Perform only the required new-cluster operator setup from the Mac. Keep live
   trading/funding disabled and omit strategy activation when qualifying reads.
6. Verify authenticated state decoding under both production users and the
   signer-specific principal on the exact new release. Qualification includes
   the election-read barrier; a successful schema11 read cannot substitute.

If retaining active history inside the same logical state is required, stop here
and design an explicit audited offline migrator with state-equivalence and
economic-reconciliation tests. Do not add such a migrator merely to save this
paper cluster; the archived fresh-incarnation route is the existing mechanism.

### Checked inactive installation without invented account pins (initial design alternatives)

**Implemented follow-up:**45db6a6 retains schema1 and permits explicit nulls only
for the three account/runtime hashes while launch authorization is false. The
actual installed units invoke the real launcher, which refuses before credentials.
No `/usr/bin/false` placeholder or invented account digest was used. The initial
alternatives below are retained as review history.

The original full `service_profile.py` manifest could not honestly be rendered with
complete real account pins yet. Its exact schema requires every digest, and the
runtime/account documents require a fresh reviewed funding/session/custody cut.
Two minimal safe approaches are available:

- Install the two production **unit definitions only**, with the selected real
  release and immutable runtime binds, `Restart=no`, no `[Install]` section, and
  an explicit fail-closed staging `ExecStart=/usr/bin/false`. Retain all role,
  resource, network and directory properties. Record that executable profile
  installation and activation remain incomplete. Do not call this an operating
  production service.
- Alternatively, add an explicit **installation-only manifest schema** containing
  only verified release/runtime/UID/caller candidates and
  `launch_authorized=false`, with no invented account hashes. The launcher's
  dedicated staging entry must reject this schema before any credential access
  or executable invocation. A tested `ConditionPathExists`/missing executable
  account manifest can add a guard but cannot replace the unconditional reject.

The first needs the least code. A missing file can make a service fail closed,
but a unit pinning a nonexistent full manifest needs an installation status that
clearly says *awaiting executable account configuration*, not *account reviewed*.
Never put placeholder digests into the production full-manifest renderer.

### Small custody-path amendment

Use these host paths for secrets, all verified to be tmpfs:

| Host path | Owner / mode |
|---|---|
| `/run/velocita-swap-credentials` | root:root 0755 |
| `/run/velocita-swap-credentials/gateway` | 29001:29001 0700 |
| `/run/velocita-swap-credentials/signer` | 29002:29002 0700 |
| `caller.key`, `session.bearer`, `signer.pem` where applicable | matching role UID/GID, 0600, regular single-link files |

Bind only the role's tmpfs directory read-only into its RootDirectory service.
Either use the same `/run/velocita-swap-credentials/<role>` destination and update
the launch contract/checks together, or bind it over the established
`/etc/velocita-swap/<role>` in-unit location while leaving the host `/etc` directory
empty. The latter reduces code/path changes while preserving actual volatile
custody; the manifest and documentation must state source and target explicitly.
Keep the other role's credential directory inaccessible. Do not place these
credentials in the world-readable release tree or the Aeron/IPC runtime folder.

Existing `velocita-secrets push` authenticates as a login user and requires its
UID to own the destination, so it cannot directly populate the nologin-role path
as `vishal`. Use the existing private deployment-user tmpfs staging and a bounded
root-mediated FD copy into the role-owned tmpfs destination, with no disk
intermediate, no recursive ownership changes, strict source/destination checks,
and no overwrites of mismatched existing values. Delete only the specifically
created temporary credential copy after the drill. Permanent role secrets remain
absent unless deliberately provisioned for the separately reviewed runtime.

On reboot the paths/credentials disappear. Recreate the fixed directories and
push secrets explicitly, preserving the existing fail-closed reboot model; do
not solve reboot convenience by copying Keychain secrets to persistent disk.

## Source pointers

- `deploy/v2/swap-runtime/service_profile.py`: manifest, generated argv and units.
- `deploy/v2/swap-runtime/service_launcher.py`: disabled gate, pin/freshness checks.
- `src/v2_node.cpp`, `src/v2_node_config.cpp`: caller admission and fixed routing.
- `src/v2_gateway_link.cpp`: authenticated response and state decoding.
- `include/velocita/v2/state_snapshot.hpp` at `884816d` and `86f4408`: schema pins.
- `../setup-r4/README.md`: installed UID/runtime evidence, synthetic drill limits.
- `../account-summary.json`, `tools/tm-account-review-candidate.py`: proposal status.
- `docs/evidence/stochastic_1s_2026-10-07/fresh-run/stochastic/nuc1/node-1.conf`:
  historical caller key/ID/response map, requiring remote re-verification.
