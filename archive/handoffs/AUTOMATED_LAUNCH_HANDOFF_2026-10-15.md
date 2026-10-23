# Handoff — automated stochastic swap run, 2026-10-15 ~14:05 UTC

**Latest continuation:** read [Codex's completed-work and feed-blocker handoff](CODEX_TO_CLAUDE_MVP_2026-10-15.md).
The corrected runtime is installed, started and sealed. Driver is now stopped and cluster halted
at index9 after a zero-order feed-bootstrap failure. Earlier startup and sequence commands below
are historical and must not be rerun. Next is a Python catch-up fix, not initial deployment.

**Superseded continuation, October 15 (Codex):** the missing caller keys were confirmed, and
review found additional consecutive-effect, freshness, order-cap and expiry defects. Start with
[the focused correction report](../evidence/automated_launch_2026-10-15/focused-mvp/README.md)
and the current tracker before using the historical commands below. The tested restoration helper
creates missing active keys while retaining the retired originals; do not run the old `mv` advice.
The corrected executable must be qualified and installed before the supervised run. Do not repeat
arm, funding or retirement. This handoff's original observations remain historical evidence.

Read this first. Runbook: `docs/evidence/automated_launch_2026-10-15/RUNBOOK.md`.
Branch `v2/phase-5-corrections`, head `cd36dcd`. The shared working tree has other sessions' uncommitted
edits; commit only explicit owned paths, never whole shared dirty files (see `velocita-shared-tree-commit-method`).

## Goal and owner decisions

Run the paper stochastic one-second rule unattended on TM swaps: 0.12 SOL clips, $25 managed cash,
$10 realized-loss stop, 240 lifetime orders. Supervised 3-round-trip drill first. The SOL in the wallet
stays unsold outside the managed account. The TM API key lives on NUC1, in the signer only (ADR 0029).

## State now

- Release `54a5fcb` qualified natively on NUC1 and the Pi (58 tests and 8 process drills each).
  Images `localhost/velocita-v2:qualified-54a5fcb` on all three hosts.
- Old incarnation `b79e65be` retired at index 24, halted. Voter data archived on each host under
  `~/velocita-indicator-paper/retired/stochastic-b79e65be-20261015/`.
- New incarnation `aae119b95ddcea5a206f03ab5ca12c15`, authority domain `ce15af7a…`, three voters
  healthy, startup = recover. Operator sequences used: 1–5 ceremony, 6 funding (index 7). **Next operator
  sequence is 7.** Mode is halted (5).
- Funding committed: $25.000000 managed, outside 0.049972532 SOL and $14.006989 USDC.
  Review window ends **2026-10-16 13:48:34 UTC**. After that, a fresh history cut and documents are needed.
- NUC1 swap install: old one archived in `/var/lib/velocita-retired-b79e65be-20261015`. New dormant
  release installed, documents adopted (manifest `73bec284…`), gateway credential (API key id) and signer
  PEM installed and verified. **Arm transition applied** (manifest now `f139936f…`, launch authorized,
  first-use journals allowed).
- **Blocked:** `systemctl start velocita-swap-signer` exited with status 64 within 5 s. Gateway never
  started, nothing signed, nothing traded, not sealed.

## Cause (almost certain) and fix

The launcher opens `/etc/velocita-swap/<role>/caller.key`, a bind of `/run/velocita-swap-credentials/<role>`.
`retire_installation.py` renamed the whole old credential folder, taking the Raft caller keys (gateway
key 13, signer key 14) with it. They are still in RAM at
`/run/velocita-retired-credentials-b79e65be-20261015/{gateway,signer}/caller.key`, and they are unchanged
for the new incarnation (the voter configs keep callers 22/23 with keys 13/14). `verify_installed.py` did
not check caller keys.

1. Owner sudo: confirm the reason with
   `sudo journalctl -u velocita-swap-signer -n 30 --no-pager -o cat`. If it is not the caller key, stop
   and reassess.
2. Owner sudo: move both keys back (same tmpfs, keeps owner, mode 0600 and single link):
   `sudo mv /run/velocita-retired-credentials-b79e65be-20261015/gateway/caller.key /run/velocita-swap-credentials/gateway/caller.key`
   and the same for `signer`. Check `sudo ls -ln` shows 29001/29002, mode 0600.
3. Confirm the recovery folders are still empty (`sudo ls /var/lib/velocita-swap-{gateway,signer}/recovery`).
   If the signer created a journal before refusing, stop: seal requires exactly the expected set.
4. Rerun `docs/evidence/automated_launch_2026-10-15/owner-start.zsh` **without its first command**
   (arm is already applied; rerunning it would be refused). Make a copy without the arm line, or run the
   remaining lines over `ssh -t vishal@100.72.127.102`. They are: start signer, start gateway, show logs,
   check both active, seal (bundle sha `44d071f8…`), `upgrade-observer.sh`.
5. Fix the tooling gap: add a caller-key check to `verify_installed.py`, and have `retire_installation.py`
   leave `caller.key` in place, or move it back. Commit with a test.

The gateway's first start is also the first live check of ADR 0029. The gateway asks the signer to sign
`<key_id>.<seconds>` and posts to `https://api.truemarkets.co/v1/auth/api-key/token`. If the gateway exits
on token minting, read its log. Likely suspects, in order:
- TM's response shape (`expires_in` ISO 8601, `token_type` `Bearer`);
- the user agent `velocita-swap-fixture/1` (`tools/mint-token.py` sends a browser UA);
- clock skew over 60 s.

Do not fall back to a static bearer without the owner's say-so.

## After the start (runbook steps 17–21)

- Step 17, me/Codex as `velocitas` on NUC1: `deploy/v2/strategy/stage_driver.py --max-orders 6` with
  `--archive` = `git archive cd36dcd model/src/velocita_model` (tar), `--opening` = the opening record from
  `~/.velocita-credentials/truemarkets/automated-launch-aae119b9/documents/opening/opening.record`
  (sha `2f329e44…`, copy it to NUC1 first), `--image localhost/velocita-v2:qualified-54a5fcb`,
  `--incarnation aae119b95ddcea5a206f03ab5ca12c15`. Check `strategy-settlements.json` appears and is fresh.
- Step 18, owner: `operator.zsh aae119b95ddcea5a206f03ab5ca12c15 mode normal 7 --run`.
- Step 19: `systemctl --user start velocita-strategy-driver` as `velocitas`. Watch each effect from intent
  to settlement and compare Raft with the wallet after every settlement. The driver stops itself flat after
  6 effects.
- Step 20, owner: `mode halted 8 --run`; at close-down `restore-guards.py`
  (`docs/evidence/rerun_launch_2026-10-14/`) once both services are stopped.
- Step 21: on owner approval, `python3 -m velocita_model.shadow.live_swap_driver --config <driver.json>
  --extend 240`, then `mode normal 9 --run`, then start the driver. Never re-stage a fresh driver state:
  Raft sequences are per caller, so restarting at 1 is rejected as a duplicate.

## Rules that still apply

- Owner-only actions: sudo, the Keychain (`tools/velocita-secrets`) and the TM session. The owner runs
  them with `!`. Never put secrets in the repo, argv or logs. Never bypass a failed verification and
  never retry a partial ceremony; reconcile first.
- One cluster only. Nothing is deleted; archives stay.
- No code comments. Strict MVP. Mutation-test new logic (see `velocita-mutation-harness-must-verify-rebuild`).

## Open items

- Owner asked for an artifact after deployment explaining the kill epoch, executor generation, strategy
  modes, start guards, review window and how to stop the run.
- No automatic daily renewal (ADR 0029). The run must finish, or be halted, before 2026-10-16 13:48 UTC.
  At measured costs the 30-day replay hits the $10 drawdown in about 0.9 days.
- `docs/CURRENT_HANDOFF.md` is a shared dirty file; this file stands in for its update.
