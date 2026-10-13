# Production identities, pinned runtime and native communication — October 13

## Outcome

**Permanent host-user setup and the bounded Aeron/signer-IPC check passed on NUC1.**
Run `21a47de2dd7626ee` used real host UID29001 (gateway) and UID29002 (signer).
The original three-voter cluster was not changed or restarted. No real venue
credential, account funding, order, signature or transaction was used. Signatures
in the drill cover an explicitly synthetic fixture with a generated test key.

| Item | Verified result |
|---|---|
| Fixed nologin users and groups | Created; gateway primary GID29001, signer/socket GID29002 |
| Socket group membership | Added only to gateway service, not permanent login groups |
| Private directories | Exact owners/modes created; existing mismatches would refuse |
| Runtime | Root-owned pinned userspace installed; 22,392 manifest entries verified |
| Actual native Aeron UDP | Bidirectional nonce delivery/acknowledgment passed between host-role processes |
| Native signer IPC | Sign once, identical retrieval, wrong peer UID/server UID/public key rejection passed |
| Cleanup | Both transient services exited and collected; no remaining test units |
| Tests | 64/64 pass on Mac and NUC1 |
| Production unit installation | **Open**; no production gateway/signer unit installed or enabled |
| Actual venue custody/account pins | **Open**; directories are empty, no credentials or ledger adopted |
| Authenticated state reads from deployed Raft voters | **Not covered** by this local Aeron drill |
| Live admission | **False** |

The new code is uncommitted. This is qualification tooling and host provisioning,
not a new production release. The next release installation must obey the existing
build-from-commit rule and use reviewed real configuration/caller/account pins.
The deferred TM sponsor-return and minimum-receipt checks remain unchanged.

## Why a pinned runtime was necessary

`ldd` on the qualified binary exposed missing GLIBC2.38 and GLIBCXX3.4.31/3.4.32
on NUC1 (host GLIBC2.35). The binaries were qualified in Ubuntu26.04 userspace.
Direct host execution from the previous proposed service mapping would fail.

Systemd `RootDirectory` now selects a content-verified copy of the already pinned
image `076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f`.
This is a filesystem/library boundary, not a second cluster. Host UIDs, kernel,
network interfaces and cgroup filtering still apply. Role-specific binds expose
only required release/configuration/state/socket paths. No host library upgrade
or new toolchain/image build was performed.

The image was copied non-root from a stopped rootless Podman container, which was
never started and was removed afterwards. Modes were normalized: directories0755,
regular files0644/0755, no copied xattrs/capabilities/set-ID privileges. Root adoption
never extracts an archive and refuses overwrite or partial resumption. The content
manifest SHA is `9df2a0625de9fd4e79751610b2c721f85ee3b008d502cdaef0de59996566d4f1`.
The source staging tree is about1.9GiB; an installed copy occupies similar disk.

## Native helper and limits

The helper uses production `AeronRuntime` and `swap_signer::Server/Client`, linked
against unchanged qualified `86f4408` objects. Only its new test translation unit
was compiled, within one CPU/1GiB/120s. Production binaries were untouched.
The helper's synthetic fixture macro is confined to the test executable and its
already-qualified fixture objects. No fixture support was added to production.

Helper SHA256: `96c4e8c474222b76cea9182297bcd2f0f69974f70bdbfaea7b81f69cb574e7bc`.
A whitespace-only warning fix changed source SHA but left executable SHA unchanged.
Both compile records are retained. `container-smoke-r2.json` is the preliminary
namespace-user check, **not** the permanent-host-user result. The first smoke
attempt failed due to its private parent-directory mount; the fixture was fixed
with an individual read-only executable bind, without loosening staging permissions.

The owner-run drill used two transient system services with separate UIDs, no
capabilities, NoNewPrivileges, read-only runtime, voter-only address filtering,
CPU3, at most one CPU quota each, 512MiB and45s per service. Controller deadline60s.
The synthetic Aeron traffic uses NUC1 IP ports45491/45492 and fresh run nonces.
The gateway sees the signer socket directory read-only and cannot replace it.
The signer uses a synthetic in-memory state provider; **no extra Raft cluster was
created and no authenticated requests were sent to the production voters**.

The test exchanged its synthetic public key over its nonce-bound Aeron channel;
this is test rendezvous, not production key distribution. Live keys remain
independently pinned in the production runtime profile.

## Evidence and commands

- `owner-result/result.json`: exact three privileged setup commands and outcome.
- `owner-result/step-*.log`: provisioning, runtime adoption and helper installation.
- `owner-result/communication/`: exact transient-unit properties and both native outputs.
- `host-postconditions.txt`: user/group records, directory modes, installation record, zero test units.
- `source-pins.json`, `bundle.json`: reviewed code/test/binary/content-manifest pins.
- `native-tests.json` and `.log`:64 checks on NUC1.
- `build*-result.json`, `build*.log`: qualified link-input hashes and both compilations.
- Mutation records:4 provisioning,5 runtime and4 communication mutations killed
  by named assertions. An initial communication mutation survived because its
  fixture also changed another user's group; that masking fixture was corrected
  and both mutation runs were preserved. These do not stand in for native checks.

Executed once under the owner-authorized administrator session (password omitted):

```zsh
ssh -t vishal@100.72.127.102 'sudo -v && cd "$HOME/.cache/velocita-build/swap-services-20261013-r4" && python3 -B owner_setup.py --run --output "$PWD/owner-result"'
```

**Audit command only: do not rerun.** Users/runtime are installed and existing
runtime/helper destinations deliberately refuse replacement. All test processes
have stopped. Do not delete or recreate state to make this one-shot command pass.

Installed paths:

- `/etc/velocita-swap/{public,gateway,signer}` — empty prepared configuration/credential directories.
- `/var/lib/velocita-swap-{gateway,signer}/{recovery,metrics}` — empty prepared state directories, no journals.
- `/opt/velocita/runtimes/076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f/` — immutable-to-roles runtime and completion records.
- `/opt/velocita/qualification/96c4e8c474222b76cea9182297bcd2f0f69974f70bdbfaea7b81f69cb574e7bc/communication-probe` — synthetic qualification executable only.

No production service unit, real credential, account opening, catalog replacement,
new live image, cluster incarnation or live-enabling flag was installed.
