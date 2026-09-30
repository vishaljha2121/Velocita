# Secrets from the owner's Mac, and rotation without an outage

2026-09-24, CP-058. **Status: OWNER APPROVED** both choices: RAM-only secrets
pushed from the Mac Keychain (reboot waits for a manual push, fail-closed), and
three-step overlap rotation via SIGHUP. No wire,
durable or economic bytes change. Aligns with the book, ch57 "Injection
patterns" and "The simplest acceptable first milestone".

## 1. Problem and why it matters

Today each voter reads its link and caller secrets from 0600 files on its own
disk. Anyone who takes a disk, a backup or a stolen Pi SD card gets every
secret, forever. And rotating a secret means stopping both voters together,
which with two voters is a full outage.

## 2. Proposal

### A. The Mac is the source of truth; hosts hold secrets only in RAM

- Secrets live in the **macOS Keychain** on the owner's Mac (hex text items,
  service `velocita`, one account per secret).
- `tools/velocita-secrets` (run on the Mac) reads the Keychain and PUSHES
  over SSH into a private **tmpfs** directory on each host
  (`/run/velocita/secrets`, owned by the voter's UID, mode 0700). Each file is
  written 0600 under a temporary name and renamed into place (atomic).
- Nothing is written to NUC or Pi disk. The node's existing loader already
  refuses anything not private, owned, unlinked and exactly 32 bytes.
- The Mac-side client reads its caller secret from the Keychain through
  stdin (`--key-stdin`), so it is not stored as a file on the Mac either.

Why push rather than pull: pulling needs a bootstrap credential already on the
host (the same problem again) and a Mac server that must always be up. Pushing
needs neither.

**The cost, and the decision it needs:** tmpfs empties on reboot. After a power
cut or reboot a voter cannot start until you push again from the Mac. With two
voters, trading stays stopped until then. This is fail-closed and fits a $20
canary, but it means **no unattended recovery**.

### B. Rotation by overlap, with no restart

Each link/caller secret file may have a sibling `<file>.next`. On SIGHUP the
node re-reads its secret files on the owner thread and:
- sends with the current secret;
- accepts a frame whose tag verifies under the current secret OR `.next`
  (and `.previous` during the tail of a rotation);
- keeps sessions, fences and sequencers unchanged: only keys change.

A reload that finds a missing or invalid file keeps the old keys, logs the
reason and counts it; it never crashes the node or drops to no keys.

Rotation, driven by the tool from the Mac:
1. Push `.next` to both voters, then SIGHUP both. Both now accept old and new.
2. Promote: `.next` becomes current and old current becomes `.previous`, then
   SIGHUP both. Both now send with the new secret and still accept the old.
3. Remove `.previous`, then SIGHUP. The old secret is dead everywhere.
4. Verify: a harmless authenticated command succeeds; health shows zero
   authentication refusals across the steps.

Each step is safe to stop after, and step 1 is safe to roll back. The cost is
that verification tries up to three keys, so a forged frame costs up to three
HMACs. That is bounded and cheap.

### C. C++ lens

Reload runs on the owner thread only; the signal handler sets an atomic flag,
nothing more. New `AuthKey`s are derived into a new key set, then swapped in
by move; the old keys are wiped by their destructors. There are no locks,
because no other thread touches keys.

## 3. Implementation (RED-first, mutation-checked)

1. Node key sets: current plus optional next and previous per route; verify
   under any, send with current. Tests: a frame under each key accepted, a
   foreign key refused, the send key always current.
2. SIGHUP reload: a pure `reload_secrets` over paths; failure keeps old keys;
   sessions unchanged. Tests include a missing `.next`, a corrupt file and a
   mid-rotation restart.
3. Client `--key-stdin`.
4. `tools/velocita-secrets` (push, rotate, verify), with an offline test using
   a fake Keychain and fake SSH, and never secrets in argv or logs.
5. Process test: a full rotation across two live nodes with no failed commit
   and no authentication refusals.

## 4. Deferred by owner decision (next iteration)

Asymmetric caller signatures, WireGuard confidentiality, key-generation
tooling beyond this tool, persistent receiver epoch watermarks.

## 5. Operator runbook (implemented in CP-058)

One-time, on the Mac:

    tools/velocita-secrets init link-1-2       # voter link secret
    tools/velocita-secrets init caller-9       # your operator caller secret

On each host, once per boot, a private tmpfs directory owned by the voter user
(M3 will make this a systemd `RuntimeDirectory`):

    mkdir -p /run/user/$(id -u)/velocita && chmod 700 /run/user/$(id -u)/velocita

Push, then start the node (config points `key_file` into that directory):

    tools/velocita-secrets push link-1-2 velocita@nuc /run/user/1000/velocita/link.key
    tools/velocita-secrets push caller-9 velocita@nuc /run/user/1000/velocita/caller.key
    # repeat for the Pi

Run a command from the Mac without the key touching disk:

    tools/velocita-secrets client-key caller-9 | velocita-v2-client --key-stdin ... kill-epoch N

Rotate (verify with a harmless command after every step):

    tools/velocita-secrets rotate-begin link-1-2
    push to NUC; reload NUC; push to Pi; reload Pi        # both now accept new
    tools/velocita-secrets rotate-promote link-1-2
    push to NUC; reload NUC; push to Pi; reload Pi        # both send new, accept old
    tools/velocita-secrets rotate-finish link-1-2
    push to NUC; reload NUC; push to Pi; reload Pi        # old secret dead

`reload` is `tools/velocita-secrets reload velocita@nuc`. Check each node's
health: `secret_reloads` rises and `auth_refusals` stays flat. A
`RELOAD_REFUSED` line means the node kept its old keys; fix the push, retry.
