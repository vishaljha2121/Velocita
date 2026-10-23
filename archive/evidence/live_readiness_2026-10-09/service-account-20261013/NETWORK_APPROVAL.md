# Approved signer networking — October 13, 2026

**Correction implemented:** see [revision 3](setup-r3/README.md) for the controlled
TCP endpoint test, completed launcher mapping, passing native synthetic tests and
the new owner command. Do not rerun the original command below; this page retains
the failed-run evidence and original setup history.

**Latest result:** the owner ran the test as `vishal`. Run
`1791883111161147370` completed both services but failed the external TCP
error-code assertion. See [preserved raw results](native-run-1791883111161147370/result.json).
This supersedes the prepared/not-run status below; it is not a qualification pass.

| Check | Control | Restricted |
|---|---|---|
| TCP to each of the three voter SSH ports | Connected | Connected |
| TCP to 1.1.1.1:443 | Connected | Two-second timeout; assertion failed |
| UDP to 127.0.0.1:59998 | Send accepted | EPERM |
| UDP to 1.1.1.1:59998 | Send accepted | EPERM |

There were no service startup errors. Control UID was 63510 and restricted UID
61411; both non-root. NUC1 reports kernel `6.8.0-138-generic`. The probe required
EPERM/EACCES for TCP as well as UDP; that assumption was too specific. Upstream
[Linux v6.8 tcp_connect](https://github.com/torvalds/linux/blob/v6.8/net/ipv4/tcp_output.c#L3821-L3840)
propagates ECONNREFUSED from the initial SYN transmit but otherwise arms a retry
timer, which explains why immediate permission-error propagation is not a portable
TCP contract. This is upstream context, not inspection of Ubuntu's exact patched
kernel binary. The observed timeout is consistent with filtering, but does not
independently prove it. No observed connection to a denied address succeeded in
the restricted service; UDP denial and permitted voter connectivity are positive
partial evidence.

Keep `network_policy_passed=false`. Do not change this retained run to a pass or
simply accept arbitrary timeouts. A revised TCP check should include direct
filter/drop or controlled endpoint evidence. No automatic rerun, code change,
policy relaxation, service deployment or live activation followed this diagnosis.

The owner approved the proposed voter-address-only signer network design after
reviewing the conflict between `PrivateNetwork=yes` and independent Raft authority
checks. ADR 0019 now records this amendment. The architecture-review blocker is
removed from generated launch contracts; all other admission/deployment blockers
remain. Approval does not authorize a signature, trade or live activation.

## Implemented and verified locally

- `service_profile.signer_network_properties()` now supplies the same policy to
  generated signer review units and the native probe. Three voter IPv4 `/32`
  allows, deny-all default and AF_UNIX/AF_INET only; this is not port filtering.
- `network_probe.py` implements a bounded control/restricted service comparison.
  It creates no permanent users or installed units, touches no credential or
  ledger, and sends no Raft, HTTP or venue message. No application data is sent
  over the TCP test connections. The inert UDP datagram is
  `velocita-network-probe`, sent to non-service port 59998 on loopback and
  `1.1.1.1`; TCP destinations are the fixed three voter SSH ports and
  `1.1.1.1:443`. No DNS lookup is needed.
- Temporary system services run under DynamicUser, not root. Each has a
  35-second runtime cap, 2-second stop timeout, 64 MiB memory cap, 16-task cap,
  no restart and automatic collection. The client waits at most 45 seconds per
  service. First failure stops the batch; no retry. A client timeout can leave
  the transient service until its runtime cap expires; it cannot run indefinitely.
- All **8 service/descriptor tests and 6 network-probe tests pass**. They reject
  timeout/refusal as filtering proof, missing checks, root execution, wrong host,
  output overwrite, policy drift and a failed control test. Synthetic success
  never marks Aeron or real UID/custody qualification complete.

Commands:

```zsh
python3 -B -m unittest discover -s tests -p 'test_swap_*profile.py' -v
python3 -B tests/test_swap_network_probe.py -v
```

The earlier five profile mutations and `verification.json` in this folder refer
to the pre-amendment source. They are preserved, not silently relabeled as current
mutation evidence. `verification-network.json` pins this iteration's tested files.

## Native status: prepared, not run

Read-only SSH reconfirmed `bulbazar-NUC`, deployment UID 1003 and active existing
voter/gateway user services. `sudo -n /usr/bin/true` reported **a password is
required**. No attempt was made to change sudo policy, use a different identity
or ask for the password. This is a host privilege limitation, not an automatic
approval-review rejection.

Only these two files were staged in the fresh 0700 scratch directory
`/home/velocitas/.cache/velocita-build/signer-network-20261013/`:

| File | Local and remote SHA256 |
|---|---|
| `network_probe.py` | `7911b4297f3d8ca071f8cf84d32b428424a34f38cfc82abe2508382df57e82ad` |
| `service_profile.py` | `28eccf88775ffa5f72b6cdafc20e120d73d7ea80b94c156490c6270f03eedd34` |

`python3 -B network_probe.py --output .../result` passed its plan-only invocation
on NUC1. No test service or network probe has run. Scratch fixture staging is not
a production source-tree swap, image build, deployment or reset. The running
cluster and existing service mounts are unchanged.

## Corrected account and owner command

The first owner attempt under `velocitas` was rejected by sudo policy:
`Sorry, user velocitas may not run sudo on bulbazar-NUC.` The earlier noninteractive
password-required result did not establish authorization; interpreting it as
only a password barrier was incorrect. The `&&` prevented the probe from starting.

The owner identified `vishal` as the administrator. Read-only SSH verified
`bulbazar-NUC`, UID 1002/GID 1003, and membership in group 27 (`sudo`). The same two
unchanged probe files were staged in a fresh 0700 directory beneath
`/home/vishal/.cache/velocita-build/signer-network-20261013/`, and both SHA256s
match the table above. Its plan-only invocation passed. No sudoers changes,
privileges for `velocitas`, production service changes or real test run occurred.

From the Mac, run the following once. Enter the sudo password only in the SSH
terminal prompt. The Python controller stays under `vishal`; only transient
service creation uses sudo, and the actual probe processes run without root.

```zsh
ssh -t vishal@100.72.127.102 'sudo -v && python3 -B "$HOME/.cache/velocita-build/signer-network-20261013/network_probe.py" --output "$HOME/.cache/velocita-build/signer-network-20261013/result" --run'
```

The output directory must not exist. If the run fails, preserve `result/` and do
not delete it or retry automatically. The final JSON distinguishes
`network_policy_passed`, `aeron_qualified`, `production_uids_qualified` and
`live_admitted`. Only the first can become true in this test.

Even a pass leaves complete native Aeron operation, permanent role provisioning,
filesystem/socket separation and the production descriptor-supervisor mapping to
qualify. It does not resolve the deferred sponsor-repayment/minimum-receipt proof.
Actual account review remains time limited; the earlier balance capture is not
silently refreshed or adopted by this probe.
