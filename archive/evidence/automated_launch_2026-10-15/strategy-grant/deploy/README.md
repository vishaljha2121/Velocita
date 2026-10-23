# Existing-cluster upgrade preparation

Executable source: `ba299ff6c89392177cfd4702fda11dabb7ee1719`, qualified on NUC1 and Pi.
This changes the existing `aae119b95ddcea5a206f03ab5ca12c15` cluster in place. No new
incarnation, funding ceremony, ledger reset, driver re-staging or sequence reset.

## Prepared, not installed

- NUC1, Pi and NUC2 have inactive `localhost/velocita-v2:qualified-ba299ff` images.
  [Image identities and validation](packaged-images.json). Each was packaged with the
  committed `package.py` from `ba299ff`, using its existing qualified base image.
  The node, exporter and client hashes were checked inside each image; their shared
  libraries resolve. NUC2 uses the exact NUC1-qualified x86 binaries. No C++ rebuild.
- Exporter hashes were read from the clean qualified build cache and pinned during
  packaging; the original native summary did not separately list the exporter hash.
- The owner gateway/signer payload is prepared locally and staged on NUC1 at
  `/home/vishal/.cache/velocita-build/strategy-grant-upgrade-ba299ff/payload/`.
  All four remote hashes match their pins; [staging result](owner-payload-staging.json).
  Its gateway hash is `9aa06eb11b23f30db37f8c628a2ca4efac7ea32d9b08c21f08a1a95df7942023`.
  The three launcher/profile helpers retain their existing pins. Both service roles
  use this gateway executable under their distinct protected users and configurations.
- `owner_upgrade.py` reuses the tested previous installation procedure. It pins the
  exact installed `a835deb` manifest and new executable, and additionally requires
  all three voters halted during preflight and before either service starts.
  Eight new tests and the three existing installer tests pass; three freshly compiled
  Python mutations removing halt, binary-pin and voter-count checks are killed.
- Owner approved the scoped tool/evidence checkpoint. This wrapper is ready for
  committed-source staging on the owner account. No production services or runtime
  documents changed during preparation.

At the preparation readback, the driver is inactive, gateway and signer are active
with start guards retained, and the authenticated producer reports index15, flat
$25, zero effects/pending/fees. This is not a fresh independent TM-wallet read.
Driver state is sequence2, inflight_effect0, stopped=null; limit240. Preserve its
state directory and client session. Its client image still names `qualified-54a5fcb`.
All three voter configurations retain `startup = recover` and the existing incarnation.

## Owner halt completed

Owner sequence10 committed at index16, term5, code applied, mode halted. Fresh readback
confirms all three voters up/ready and halted at commit/applied16. Driver remains inactive
at sequence2, account flat$25, no pending effects or fees. [Evidence](owner-halt.json).
**Next operator sequence is11. Do not rerun the sequence10 command below.**
Owner approved the upgrade-tool checkpoint; nothing is installed yet.
Source review confirms no peer protocol or outer snapshot changes between54a5fcb andba299ff;
the grant extension remains absent until the later owner ceremony.

## Execution order

1. Owner halt using the existing Mac client. Last documented operator command is
   sequence9; expected next is10. No operator command has been sent in this preparation.
   Run once and inspect the committed result; do not retry an ambiguous outcome:

   ```sh
   cd "$HOME/aeron-raft-pi"
   zsh docs/evidence/automated_launch_2026-10-15/operator.zsh \
     aae119b95ddcea5a206f03ab5ca12c15 mode halted 10 --run
   ```

2. Checkpoint the wrapper, tests and evidence after approval. Stage committed source
   and the qualified payload under the owner's dedicated cache directory on NUC1.
   Verify every staged file hash, run the Python tests on NUC1, and validate payload pins.
   Give the owner the read-only preflight command; only after it passes give `--run`.
   The wrapper stops both services, archives old units/manifest, installs the qualified
   executable and starts signer then gateway while halted. Journals and credentials
   must match before restart; review documents and funding are unchanged. Failure
   restores start guards and stops both services. Sudo remains owner-run.

3. Upgrade voter/exporter pairs one host at a time, followers first and current leader
   last. Refresh leadership before choosing each host. Keep the other two voters healthy.
   Before each change, verify halted mode, no pending effects, stopped driver, pinned
   image, recovery configuration, and sole existing cluster. Stop both user services;
   archive their unit files, configuration and stopped data tree without moving/deleting
   originals. Verify archive hashes before changing anything. Replace only the exact
   old image token in both unit files with the pinned new image; preserve all other bytes
   and file modes. Reload user systemd and start the voter/exporter pair. Verify running
   container image IDs, ready/up, no failure, halted mode and convergence of committed,
   applied and durable indices before proceeding to the next host. Stop on any refusal;
   do not automatically roll back or bootstrap a replacement cluster.

4. With the driver still stopped, archive its configuration and replace only its
   client-image argument. Preserve the complete state directory and session byte-for-byte.
   Verify the configuration diff contains only that image change. Do not start the driver.

5. Confirm all voters and execution services run the qualified code, account remains
   flat/no pending, journals recover, and monitoring is current. Then prepare the separate
   one-time operator strategy grant with caller21, expected floor3 and epochs1/1, using
   the original opening and fresh reviewed evidence. A committed-source Mac client that
   understands the grant is still required before that ceremony. Reverify all values.
   No grant, normal-mode transition or driver start is part of this preparation.

New and old readers coexist only while the grant is absent. Every reader must be upgraded
before committing the grant; old-reader rollback after grant is unsupported. No broad
source-directory swap is needed, so Grafana/Prometheus source mounts need no refresh here.

Review entry drain: October16 2026 13:41:34.459 UTC. Expiry:13:48:34.459 UTC.
The existing reviewed live limits and fee assumptions are unchanged. Renewal is owner-run.
