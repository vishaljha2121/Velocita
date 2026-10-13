# Swap runtime services

This bundle implements production startup mapping for the existing one-cluster
arrangement. It does not install or start itself. The default manifest must set
`launch_authorized=false`; generated review units have no `[Install]` section.
The qualified C++ real-transaction admission gate remains false. No current
manifest or paper-trading approval authorizes real signing.

## Process boundary

Gateway and signer use separate fixed non-root UIDs, caller identities/keys,
private state directories and credential files. They share only the signer socket
group: signer primary group, gateway supplementary group. The signer owns its
0750 runtime directory and creates a 0660 Unix socket; the gateway cannot replace
it. The gateway unit is ordered after and bound to the signer unit, so losing the
signer stops the gateway. Both use `Restart=no`; neither initializes journals or
retries venue operations automatically.

ADR 0019's owner-approved October 13 amendment permits the signer to contact only
the three pinned voter IPv4 addresses. `IPAddressDeny=any`, three `/32` allows and
AF_UNIX/AF_INET restrictions replace the incompatible `PrivateNetwork=yes` rule.
This permits all ports on those trusted hosts and does not exclude a relay there.
The bounded October 13 native network-policy probe passed. Actual Aeron and
permanent-user communication are qualified separately by `communication_probe.py`.

## Pinned runtime userspace

The qualified Ubuntu 26.04 binaries need newer GLIBC/GLIBCXX than NUC1's Ubuntu
22.04 host supplies. Units therefore use `RootDirectory` at
`/opt/velocita/runtimes/<runtime_image_sha256>/rootfs`, preserving the pinned image
libraries without upgrading the host or running another cluster. The manifest
requires the image digest; the launcher checks the immutable image marker before
account or credential access. `MountAPIVFS=yes` exposes process/cgroup inspection.
Only the selected release, public configuration and own-role credential directory
are bound read-only. systemd binds the role's own state/runtime directories; the
gateway also gets a read-only bind of the signer socket directory and host DNS
configuration. Numeric host UIDs and host cgroup filtering remain authoritative.

`install_runtime.py` adopts a reviewed normalized content manifest into a fresh
root-owned image directory. It never extracts archives as root, follows source
symlinks while copying, overwrites/resumes a partial installation, or starts a
service. A 0700 pending directory becomes 0755 only after complete verification
and durable completion records. Normalization strips set-ID/capabilities and
makes runtime data read-only to service users. The original pinned image remains
unchanged. A failed copy is retained for inspection.

## Files and startup behavior

- `service_profile.py`: validates the bounded public manifest and renders its
  exact bytes, two `.service.review` files and two explanatory launch contracts.
  Manifest SHA256 is embedded in each unit's launcher command. The reviewed
  source commit selects `/opt/velocita/releases/<commit>/velocita-v2-gateway`
  and the colocated launcher bundle. No shell is used in ExecStart.
- `service_launcher.py`: accepts a hash-pinned, root-owned manifest. Its default
  operation checks startup without opening credentials. `--launch` additionally
  requires an explicitly operator-authorized manifest, the exact system-service
  cgroup, zero effective/bounding capabilities and NoNewPrivileges. It checks
  UID/GID/groups, file hashes, account identity/review/history links, private
  state/runtime directory permissions and the native ELF release before opening
  the role's caller key and bearer/signing-key descriptor. It derives argv from
  the validated manifest; it never executes arbitrary argv from a JSON contract.
- `fd_handoff.py`: validates every ancestor without symlink traversal, exact 0600
  role ownership and one hard link for credentials. Immutable public inputs are
  root-owned and not writable by service users. FD3 alone is inherited beyond
  standard streams; extra descriptors and ambient environment variables are
  removed. C++ rechecks the descriptor and public-key/bearer pins. Its standalone
  CLI remains a refusal stub; production uses `service_launcher.py`.
- `network_probe.py`: harmless owner-run native isolation test, described below.
- `provision_services.py`: review/create the fixed gateway 29001 and signer/socket
  29002 identities and private directories, refusing every existing mismatch.
- `communication_probe.py`: two bounded transient services using those identities,
  real native Aeron and production signer IPC, but entirely synthetic state/key.
  Local transport success does not prove authenticated Raft reads from voters.
- `owner_setup.py`: hash-check the staged artifacts, provision, adopt the runtime,
  install the separately identified synthetic helper, then run the bounded drill.
  It installs no production unit, account artifact or real credential. No retry
  follows a failed step. Keep all partial results and inspect before continuing.

The gateway may need an expired account review for read-only recovery. The
launcher preserves that existing C++ behavior; it does not pretend to decide
whether journals are idle or funds are safe. The signer refuses stale reviews,
and an expired gateway bearer always refuses. Economic validation, canonical
opening/runtime digests, authenticated Raft catalog/state and journal-dependent
admission remain in the C++ runtime. A launcher preflight pass is not live admission.

Root is the trusted configuration/deployment authority. Stop both services before
changing public inputs, authorization, keys or the immutable release. No hot config
replacement or automatic credential refresh is implemented. The trusted bundle
and its ancestors must be root-owned and not writable by either service user;
Python runs with `-I -B` and imports only the explicitly located bundle directory.
Do not run a production service from a user-writable source checkout.

## Required host layout

The reviewed fixed UID/GIDs are 29001 for gateway and 29002 for signer/socket.
Provision these identities before installation; do not reuse
`velocitas`, `vishal`, root, or the temporary DynamicUser IDs from the network test.
The manifest's gateway UID/GID, signer UID and socket GID must agree with this
layout and the separately pinned runtime profile. The gateway receives the socket
group through its unit, not broad membership in the administrator's groups.

| Path | Owner/group | Mode / purpose |
|---|---|---|
| `/opt/velocita/runtimes/<image>/rootfs/` | root/root | verified normalized image userspace, immutable to roles |
| `/opt/velocita/releases/<commit>/` and launcher helpers | root/root | 0755 dirs, 0644 Python; pinned gateway executable 0755 |
| `/etc/velocita-swap/public/` | root/socket group | 0750; manifest, profile, review, history, catalog root-owned 0640 |
| `/etc/velocita-swap/gateway/` | gateway UID/gateway GID | 0700; caller.key and raw session.bearer 0600, singly linked |
| `/etc/velocita-swap/signer/` | signer UID/socket GID | 0700; caller.key and signer.pem 0600, singly linked |
| `/var/lib/velocita-swap-<role>/`, `recovery/`, `metrics/` | role UID/primary GID | 0700; never paper state |
| `/run/velocita-swap-gateway/` | gateway UID/gateway GID | 0700, created by systemd |
| `/run/velocita-swap-signer/` | signer UID/socket GID | 0750, created by systemd |

Precreate private state subdirectories during reviewed provisioning. Merely
creating `recovery/` does not initialize a journal; first-use journal initialization
remains a reviewed one-shot step, absent from normal service arguments. Do not
replace existing state or import the paper wallet. A service principal must not
own or write the release/public configuration directory. Preserve existing caller
key provisioning through the repo's protected secret workflow; never put secret
contents in this manifest, command arguments, environment or repository.

Prepare a manifest containing exactly `FIELDS` in `service_profile.py`, including
`launch_authorized=false`, real reviewed public hashes and identity values. Then:

```zsh
python3 -B deploy/v2/swap-runtime/service_profile.py \
  --manifest /path/to/reviewed-public-manifest.json \
  --output /path/to/new-review-directory
python3 -B -m unittest discover -s tests -p 'test_swap_service*.py' -v
python3 -B tests/test_swap_network_probe.py -v
```

After release/account review, a privileged operator installs the exact committed
bundle and root-owned public files, verifies hashes and permissions, and installs
the reviewed units as system services. Do not enable/start them now. Changing
`launch_authorized` changes manifest bytes and therefore requires regenerating and
reviewing both units' manifest pins. It is the existing separate live-authorization
boundary, not an automatic promotion flag. No account artifacts have been adopted
and no real signer/session credentials have been installed by this work.

## Revised TCP isolation proof

The original probe incorrectly required TCP to return EPERM; NUC1 returned a
timeout while UDP returned EPERM. That failed run remains preserved.

The revised probe creates a loopback-only listener on an ephemeral port and runs
three sequential transient non-root services: control-before, restricted, then
control-after. Each uses at most 35 seconds, no restart and automatic collection.
Controls must reach all three voter SSH ports, `1.1.1.1:443` and the controlled
listener, delivering its unique inert nonce. The restricted service must still
reach voters, fail both excluded TCP destinations, explicitly get EPERM/EACCES
for excluded UDP, and produce zero accepted connections at the controlled listener.
Only the complete before/restricted/after evidence can classify TCP timeouts as
consistent with enforced isolation. A timeout alone, dead listener, unexpected
connection, wrong nonce, failed control or missing final phase never passes.

No HTTP, venue or Raft message is sent. External/voter TCP is handshake-only; the
controlled loopback connection sends a 32-byte nonce. Inert UDP goes to port 59998
on loopback and 1.1.1.1. The listener stays in the unprivileged controller, and no
socket is inherited by the restricted service. This qualifies tested network
behavior, not cryptographic absence of a relay, real Aeron transport, permanent
role users or key custody. Those flags stay false even on a network pass.

The administrator account on NUC1 is `vishal`; `velocitas` is not authorized for
sudo. The revised probe and synthetic test bundle are staged in
`~/.cache/velocita-build/swap-services-20261013-r3/` under `vishal`. Run only the
recorded owner command in the current evidence; do not overwrite the old result
or silently rerun a failed capture. No sudoers changes are needed.
