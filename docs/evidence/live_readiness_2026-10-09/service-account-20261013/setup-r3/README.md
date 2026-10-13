# TCP validation correction and production startup wiring

## Result and boundary

The TCP probe is corrected and the production service launcher is implemented.
**26 synthetic tests pass on both the Mac and NUC1**: 18 service/descriptor checks
and 8 network-controller/evaluator checks. Five deliberately weakened variants
fail named assertions with zero test errors; see `mutations.json`.

The owner ran the revised privileged network probe: **network policy passed**.
The four native result files are retained in
[`native-network-run-1791885226758329135/`](native-network-run-1791885226758329135/result.json).
Source hashes match the current probe and service profile. Both control phases
reached all endpoints and delivered their nonce; the restricted phase reached
all three voters, timed out on excluded TCP with zero controlled-listener
connections, and received EPERM on both excluded UDP destinations.
Aeron, permanent production users and live admission remain unqualified.
No real credential
was read or installed, permanent user/group created, production service installed/started,
ledger initialized, C++ binary rebuilt, image shipped, signature made or venue
request sent. The changes remain uncommitted. They are software preparation, not
completion of target-host isolation or live admission.

## TCP proof

The original failure is retained in `../native-run-1791883111161147370/`. Requiring
EPERM from TCP was too specific. The revision does not accept a timeout alone:

1. An unprivileged controller listens on a loopback-only ephemeral port.
2. A transient unrestricted non-root service must connect to that listener and
   deliver a unique nonce, and connect to all three voters and 1.1.1.1:443.
3. A transient restricted service must reach voters, fail the excluded TCP
   endpoints, receive explicit EPERM/EACCES for excluded UDP, and produce no
   accepted connection at the controlled listener.
4. A second unrestricted service must again reach the same endpoints and deliver
   a fresh nonce. Only the complete bracket can pass.

Controls bracket both listener availability and external reachability. The
restricted service creates its own sockets inside its cgroup. The controller's
listener is never inherited. No Raft, HTTP or venue message is sent. External
TCP is handshake-only; the local listener receives a 32-byte inert nonce. UDP
remains the fixed inert datagram to port 59998 on loopback and 1.1.1.1.

Each service is capped at 35 seconds and stops without restart; the controller
wait is capped at 45 seconds per service. There are at most three services, with
stop-on-first-failure and no retries. Unexpected accepted connections, dead
controls, incomplete results or UDP timeouts fail. TCP evidence is a controlled
behavioral check, not proof against a relay on an allowed voter host. Actual
Aeron and permanent UID/custody qualifications remain false even on success.

## Completed service wiring

`service_launcher.py` replaces the previously missing supervisor mapping. It
requires a root-owned SHA256-pinned manifest, derives supported argv from that
manifest and checks the pinned ELF binary, public files, UID/GID/groups, private
directories and account review/history links before credential handoff. The
manifest's `launch_authorized=false` default refuses before secret access.
The launcher runs Python isolated from ambient import paths and uses a minimal
explicit environment for exec. Actual launch additionally requires its expected
system-service cgroup, no effective/bounding capabilities and NoNewPrivileges.

The signer rejects stale reviews. The gateway retains C++'s ability to perform
read-only recovery with an expired account review and a valid bearer; the launcher
does not decide journal-dependent economic admission. Canonical opening digests,
Raft authority, funding limits and real transaction validation stay in C++.

Rendered review units now invoke the complete launcher, not `/usr/bin/false`.
They still have no `[Install]` section or automatic restart and are not installed.
The gateway is ordered after and bound to the signer. A separately authorized
manifest changes the hash pinned by both units; it is not silently adopted by a
running service. No manifest with real live account/release pins has been adopted.

See [host layout and installation requirements](../../../../../deploy/v2/swap-runtime/README.md).
Provisioning fixed users, protected credential files and real fresh account pins
remains an operator/deployment step. First-use journal initialization is not part
of ordinary service startup. These are existing tracker items, not new live gates.

## Native verification and retained failure

The NUC1 scratch bundle is under the verified administrator `vishal`, UID 1002:
`~/.cache/velocita-build/swap-services-20261013-r3/`.
`source-sha256.json` records all tested files. The two C++ files in that bundle
come from qualified commit `86f4408` solely for supported-flag/closed-gate checks;
no C++ build or production executable was produced from this scratch directory.

- `service-tests.log`: 18/18 pass, including an actual harmless child exec with
  synthetic FD3 and excluded ambient environment/extra descriptors.
- `network-tests.log`: 8/8 pass; no real network/systemd calls in these unit tests.
- `systemd-verify.log`: generated units parsed by installed systemd 249, exit 0.
  It also reported two host-environment warnings: unreadable existing
  `netplan-ovs-cleanup.service` and unsupported `RestartMode` in the installed
  snapd unit. These are retained, not hidden or repaired as part of this task.
  This syntax check does not demonstrate runtime isolation or access to every
  host dependency.
- `initial-native-fixture-failure.log`: preserved initial 18-test run with two
  fixture errors. One test assumed another had created its scratch directory;
  the second created that directory as 0775 under the host umask, correctly
  rejected by the credential ancestry check. The fix makes each test create a
  fresh dedicated 0700 scratch base. Production permission checks and the home
  directory were not loosened or chmodded.

## Completed owner command — retained for audit, do not rerun

From the Mac:

```zsh
ssh -t vishal@100.72.127.102 'sudo -v && cd "$HOME/.cache/velocita-build/swap-services-20261013-r3" && python3 -B deploy/v2/swap-runtime/network_probe.py --output "$PWD/network-result" --run'
```

The new output path preserves every old run. If it fails, keep the output and do
not rerun. Only `network_policy_passed` may become true; the remaining three flags
intentionally stay false. No change to sudoers or `velocitas` permissions is needed.

## Next deployment boundary

After the network result, qualify actual Aeron/Unix IPC under the permanent role
principals and install a reviewed committed service bundle with real pinned
account/configuration files. Do not invent missing custody/key registration or
funding approval, import paper funds, or revive the old account-capture timestamps.
The deferred sponsor-repayment/exact-minimum-receipt checks and separately approved
money-moving test remain unchanged. The requested post-live networking explainer
is recorded in the main tracker and is not an additional gate.
