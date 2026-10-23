# CP-058: secrets from the Mac, and rotation without an outage

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted. Owner-approved
design: `../../M2_SECRETS_ROTATION_REVIEW.md` (RAM-only 0600 tmpfs files pushed
from the Mac Keychain; three-step SIGHUP overlap). No wire, durable or economic
bytes changed.

## What exists

- Node: per-route key sets. Frames are sent under the current secret and
  accepted under current, `.next` or `.previous`. `replace_secrets` is
  all-or-nothing and preserves sessions, fences, sequencers and held frames.
- Files: `load_secret_set(FILE)` loads FILE plus optional `.next`/`.previous`;
  a present but invalid optional file refuses the whole set.
- Binary: SIGHUP raises a flag and the owner thread reloads; a refusal keeps
  the working keys and prints `RELOAD_REFUSED`. The health page (version 2)
  counts reloads and refusals.
- Client: `--key-stdin` (32 or 64 raw bytes) or `--key-file` with the same
  rotation layout.
- `tools/velocita-secrets`: Keychain (values only on stdin/stdout, never in
  argv), SSH push that refuses a non-tmpfs, non-700 or foreign-owned
  directory, and atomic rename. Rotation is three explicit steps.

## Evidence honesty

The node key-set restructure and the tool were written BEFORE their tests, so
coverage is shown by mutation, not a chronological RED. The file-layout step
was RED-first (`files-red.txt`).

| Mutant | Killed by |
|---|---|
| accept only current | node rotation: "new secrets accepted" |
| previous not accepted | node rotation: "mixed promotion" |
| send with `.next` as soon as present | node rotation: "one voter accepts the new secrets" (test strengthened: voters now learn one at a time) |
| reload ignores a missing peer | "incomplete reload threw instead of refusing" (test strengthened twice: first an empty set hid it, then an escaping exception) |
| invalid optional file skipped | files: "a torn .next did not refuse the whole set" |
| Keychain value in argv; no tmpfs check; stale `.next` kept on host; raw bytes to a terminal; rotation begun over a previous one | the named tool tests (`tool-mutants.txt`; the terminal case was strengthened from an ERROR to a real assertion) |

Live proof in `v2-node-process-tests`: both real processes rotate the link and
caller secrets through SIGHUP, voters promoted at different moments, a commit
after every step, zero authentication refusals, three applied reloads per
node, and a torn push refused while the node keeps committing.

Release 108/108; ASan+UBSan 108/108 run alone (`sanitized-tests.txt`).

## Not covered

The real macOS `security` binary and real SSH/tmpfs are faked in tests; the
first real push is an operator step on the NUC/Pi (M3). A tmpfs does not
protect against a same-user compromise of a host (book ch57). Reboots need a
manual push by design.
