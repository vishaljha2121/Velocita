# Dormant production units and independent Raft-read preparation

## Result

On October 13, installed the production gateway and signer units on NUC1 from
committed source **45db6a6**, retaining the earlier immutable `06df068` release.
Both units are loaded, **inactive/dead/static**, with `launch_authorized=false`.
No credentials, venue sessions, live account opening, journals or funds were
installed/adopted. No trading process was started. The original one-cluster
paper deployment remains on `884816d`; all three voters/exporters and the paper
gateway are active. The edge was already inactive and remains so.

**Authenticated deployed-Raft reads are not qualified.** The old voters use
snapshot/command schema11 and replication epoch1 and admit only gateway caller22.
The new production decoder requires schema12/epoch2 and a separate signer caller23.
No supported persisted-state migration exists. See [cutover scope](CUTOVER_SCOPE.md)
and [identity review](ACCOUNT_CALLER_REVIEW.md). No old cluster was retired/reset.

## What is installed

- `/etc/systemd/system/velocita-swap-{gateway,signer}.service`, root-owned0444,
  no Install section or automatic start; launcher manifest SHA256 is pinned.
- `/opt/velocita/releases/45db6a64a3dd14b0c4f3a94dd2e9706309c445d1/`:
  gateway executable0555 plus immutable launcher/profile/FD helpers0444.
- Existing root-owned image userspace is reused at runtime image `076701392104...`.
  This installer verifies its marker/protected ancestry; the prior R4 content
  inventory established full runtime integrity. It does not claim a new full rehash.
- `/etc/velocita-swap/public/{manifest.json,catalog.bin}`. Manifest pins actual
  existing incarnation/catalog, Linux identities and proposed signer identity.
  Account review/history/runtime profile digests are explicitly **null** because
  the fresh live configuration is unapproved. Nulls are permitted only with
  launch authorization false. The manifest must be replaced for a new incarnation.
- Empty role-owned0700 `/run/velocita-swap-credentials/{gateway,signer}` under a
  root0755 parent; `/run` verified tmpfs. Each service receives only its own role
  directory as a read-only bind over its in-unit `/etc` credential path.
  Host `/etc` credential directories remain empty. Reboot does not auto-provision secrets.

Reviewed identities: gatewayUID/GID29001, caller22/key13/port44413;
signerUID/socketGID29002, proposed caller23/key14/port44414. **Signer registration
is not present in current voters.** No keys were copied or borrowed for this task.

The qualified gateway ELF hash is unchanged:
`d9db106f51382e7bf3bd5f0d51a8d982ae439cf9fe45c2c626dcbab12f088447`.
The source diff from qualified86f4408 through this release contains no production
C++/CMake changes. We reused that exact ELF instead of rerunning native builds.
New Python helpers are from their named committed source; this is not a claim
that a different compiler invocation recreated the ELF.

## Validation and corrected failures

**38 focused tests pass on Mac and NUC1**:12 profile/descriptor,11 launcher,
7 installer and8 read-probe tests. Final native output is `fd3-native-tests.log`.
Retained `native-tests.log`, `native-tests-r2.log` and `native-tests-r3.log` show
all earlier failures; none are relabeled as passes.

The first tests used a group-writable build-cache ancestor; the second attempt
still inherited the host's group-write umask for an intermediate directory.
Private single-level test scratch fixed the fixture without weakening any check.
That exposed a real FD handoff bug: `dup2(3,3)` leaves close-on-exec set. The fix
explicitly marks existingFD3 inheritable. Real fork/exec tests deterministically
exercise sourceFD3 andFD7 and prove unrelatedFD100 is closed. Removing the fix
fails the named assertion (`fd3-mutation.json`), with zero test errors.

The bounded native read helper compiled one translation unit against qualified86
libraries with `-Werror`; `build.log` is empty. `build-result.json` pins every
link input and helper source06df068. No production C++ executable changed.
The native loader/parser smoke ran with network disabled and proved missing-pin
refusal. Eight parser/filesystem checks and six mutation checks validate its
local boundaries; **none substitutes for authenticated production reads**.

Installation verifies exact public bundle hashes, fixed identities, generated
unit equality, safe ancestry and fresh release paths. The FD correction replaces
only exact hash-matched inactive configuration, preserving old release bytes.
Each file replacement is atomic; the set is not transactional. A partial failure
must be preserved and reviewed, never automatically retried. Both manifests
remain false-admission and credentials empty throughout.

## Checkpoints and evidence

- `92abb5f`: priorR4 native service-user setup/communication evidence.
- `06df068`: dormant installer/profile and independent bounded state reader.
- `446a651`, `9f93677`: test-scratch corrections; failed native attempts retained.
- `45db6a6`: FD3 correctness fix and constrained dormant release replacement.
- `host-postconditions.txt`: final unit paths/status, root ownership and hashes.
- `fd3-install-result.json`: final install result; `install-result.json`: first install.
- `manifest.json`, `install-bundle.json`, `source-pins.json`: final public pins.
- `initial-*`: first installed public bundle retained for provenance.
- `cluster-*.txt`: one original voter/exporter pair per host after initial install.
- [Commands](COMMANDS.md) records the exact privileged installation commands.

The reviewed account configuration remains **pending**, rather than invented or
adopted from paper cash. Live admission stays false. Deferred transaction economic
proof and explicit trade approval are unchanged.
